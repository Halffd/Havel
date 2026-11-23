#if !defined(_POSIX_C_SOURCE) || _POSIX_C_SOURCE < 200809L
#define _POSIX_C_SOURCE 200809L
#endif

#include "hook.h"

#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

#if defined(__x86_64__) && !defined(_WIN32) && !defined(__APPLE__)

#define HOOK_MAX 128
#define HOOK_STOLEN_MAX 64
#define HOOK_MIN_PATCH 5 /* jmp rel32 */
#define HOOK_GATE_OFFSET 0
#define HOOK_ORIG_OFFSET 16
#define HOOK_REL32_MAX ((int64_t)INT32_MAX)
#define HOOK_REL32_MIN ((int64_t)INT32_MIN)

typedef struct HookRecord {
	const void *target;
	void *page;
	size_t page_size;
	uint8_t stolen[HOOK_STOLEN_MAX];
	size_t stolen_len;
	void *replacement;
	int active;
} HookRecord;

static HookRecord g_hooks[HOOK_MAX];
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static long g_page_size = 4096;

static void hook_err(char *err, size_t errsz, const char *fmt, ...)
{
	if (!err || errsz == 0) return;
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(err, errsz, fmt, ap);
	va_end(ap);
}

static long hook_pagesize(void)
{
	long ps = sysconf(_SC_PAGESIZE);
	return ps > 0 ? ps : 4096;
}

static uintptr_t page_align(uintptr_t addr, long ps)
{
	return addr & ~((uintptr_t)ps - 1);
}

/* rel32 distance from `at` (address of the jmp) to dst, or -1 out of range. */
static int rel32_fits(const void *at, const void *dst)
{
	int64_t d = (int64_t)((uintptr_t)dst - ((uintptr_t)at + HOOK_MIN_PATCH));
	return d >= HOOK_REL32_MIN && d <= HOOK_REL32_MAX;
}

/*
 * Build `jmp rel32` landing at dst, for a jump that will live at address
 * jmp_at. buf is where the bytes are written (buf == jmp_at for a jump
 * emplaced directly; a stack buffer for the 5 bytes later written onto the
 * target). The displacement is ALWAYS relative to jmp_at, the address the
 * jmp instruction will occupy once it lands - computing it from buf instead
 * turns it into a stack address and the truncated int32 points nowhere near
 * the trampoline.
 */
static void write_jmp_rel32(uint8_t *buf, const void *jmp_at, const void *dst)
{
	int32_t d = (int32_t)((uintptr_t)dst - ((uintptr_t)jmp_at + HOOK_MIN_PATCH));
	buf[0] = 0xE9;
	memcpy(buf + 1, &d, sizeof(d));
}

static void write_gate(uint8_t *at, const void *dst)
{
	/* mov rax, imm64 ; jmp rax */
	at[0] = 0x48;
	at[1] = 0xB8;
	memcpy(at + 2, &dst, sizeof(dst));
	at[10] = 0xFF;
	at[11] = 0xE0;
}

static int mprotect_range(uintptr_t addr, size_t len, int prot)
{
	long ps = g_page_size;
	uintptr_t start = page_align(addr, ps);
	uintptr_t end = page_align(addr + len - 1, ps) + (uintptr_t)ps;
	return mprotect((void *)start, end - start, prot);
}

/*
 * Write len bytes into a possibly read-only executable page.
 *
 * Primary path: /proc/self/mem. A write through that fd bypasses the VMA's
 * protections entirely, so exec text is patched WITHOUT a W^X window and
 * without the mprotect-to-RW dance. That matters on hardened hosts where
 * mprotect(PROT_WRITE) on file-backed executable mapping is refused with a
 * synchronous SIGSEGV (observed on the development host: the exact call
 * `mprotect(text_page, PAGE, PROT_READ|PROT_WRITE)` killed the process with
 * RIP inside the syscall - the register dump showed args addr/page/3, i.e. a
 * mprotect that never returned). An errno return could be handled; a SIGSEGV
 * cannot be caught, so the mprotect dance must not be the primary path.
 *
 * Fallback: mprotect the page RW, memcpy, restore RX. Needed where procfs is
 * absent or /proc/self/mem writes are blocked. A hardened host that fails
 * the mem write AND refuses the mprotect cannot be patched at all - hook
 * install then fails with a clear message where it can, and hard-faults
 * where it cannot (that is the platform's answer to patching, documented).
 */
static int write_exec_memory(void *target, const void *data, size_t len,
                             char *err, size_t errsz)
{
	int fd = open("/proc/self/mem", O_RDWR | O_CLOEXEC);
	if (fd >= 0) {
		ssize_t w = pwrite(fd, data, len, (off_t)(uintptr_t)target);
		int saved = errno;
		close(fd);
		if (w == (ssize_t)len) return 0;
		hook_err(err, errsz, "/proc/self/mem write of %zu bytes at %p failed (wrote %zd of %zu: %s)",
		         len, target, w, len, len > 0 ? strerror(saved) : "");
		return -1;
	}

	/* fallback: the W^X dance */
	if (mprotect_range((uintptr_t)target, len, PROT_READ | PROT_WRITE) != 0) {
		hook_err(err, errsz, "mprotect(target, RW) failed: %s", strerror(errno));
		return -1;
	}
	memcpy(target, data, len);
	if (mprotect_range((uintptr_t)target, len, PROT_READ | PROT_EXEC) != 0) {
		hook_err(err, errsz, "mprotect(target, RX) failed: %s", strerror(errno));
		return -1;
	}
	return 0;
}

/* ---------------------------------------------------------------------------
 * Prologue instruction length decoder (conservative x86-64 subset).
 * Returns the instruction length in bytes, or -1 with err filled in.
 * ------------------------------------------------------------------------- */

struct DecoderCtx {
	const uint8_t *p;
	size_t avail;
	char *err;
	size_t errsz;
};

static int fail(struct DecoderCtx *c, size_t at, const char *why)
{
	if (at < c->avail)
		hook_err(c->err, c->errsz, "unsupported prologue instruction at offset %zu (opcode 0x%02x): %s",
		         at, c->p[at], why);
	else
		hook_err(c->err, c->errsz, "truncated prologue at offset %zu: %s", at, why);
	return -1;
}

static int is_rex(uint8_t b) { return b >= 0x40 && b <= 0x4F; }

/* Length of the ModRM/SIB/displacement tail starting at c->p[pos].
 * On success returns >= 1; refuses RIP-relative, absolute disp32 and SIB
 * base=5/mod=0 (all position-dependent or relocating host addresses). */
static ssize_t modrm_tail_len(struct DecoderCtx *c, size_t pos, int lea)
{
	if (pos >= c->avail) return fail(c, pos, "truncated ModRM");
	uint8_t modrm = c->p[pos];
	int mod = modrm >> 6;
	int rm = modrm & 7;
	size_t len = 1;

	if (mod == 3) {
		/* register-register: fully position-independent */
		if (lea) return fail(c, pos, "lea with register operand");
		return (ssize_t)len;
	}
	if (rm == 5 && mod == 0)
		return fail(c, pos, "RIP-relative / absolute disp32 addressing cannot be relocated");
	if (rm == 4) {
		/* SIB byte */
		if (pos + 1 >= c->avail) return fail(c, pos, "truncated SIB");
		uint8_t sib = c->p[pos + 1];
		len++;
		int base = sib & 7;
		if (base == 5 && mod == 0)
			return fail(c, pos, "absolute disp32 SIB base cannot be relocated");
	}
	if (mod == 1) len += 1;
	else if (mod == 2) len += 4;
	return (ssize_t)len;
}

/* Decode the instruction starting at c->p[0]. Returns its length, or -1. */
static ssize_t insn_len(struct DecoderCtx *c)
{
	const uint8_t *p = c->p;
	size_t n = c->avail;
	size_t i = 0;
	int rex = 0;
	int opsize66 = 0;

	/* prefixes: 66 operand-size, 67 address-size, F0 lock, F2/F3 rep,
	 * 26/2E/36/3E segment overrides, 64/65 FS/GS. Each is spelled out
	 * explicitly - a range test here would swallow opcodes (0x30-0x35
	 * are ALU ops, not prefixes) and silently mis-decode the rest. */
	while (i < n) {
		uint8_t b = p[i];
		if (b == 0x66) { opsize66 = 1; i++; continue; }
		if (b == 0x67 || b == 0xF0 || b == 0xF2 || b == 0x26 || b == 0x2E ||
		    b == 0x36 || b == 0x3E || b == 0x64 || b == 0x65) {
			i++;
			continue;
		}
		if (is_rex(b)) {
			if (rex) return fail(c, i, "multiple REX prefixes");
			rex = b;
			i++;
			continue;
		}
		break;
	}
	if (i >= n) return fail(c, i, "truncated instruction");

	/* endbr64: F3 0F 1E FA (CET) */
	if (p[i] == 0xF3 && i + 3 < n && p[i + 1] == 0x0F &&
	    p[i + 2] == 0x1E && p[i + 3] == 0xFA)
		return (ssize_t)(i + 4);

	/* one-byte opcodes we accept in a prologue */
	uint8_t op = p[i];
	size_t op_pos = i;

	switch (op) {
	case 0x90: /* nop */
		return (ssize_t)(i + 1);
	case 0x50: case 0x51: case 0x52: case 0x53:
	case 0x54: case 0x55: case 0x56: case 0x57: /* push r64 */
	case 0x58: case 0x59: case 0x5A: case 0x5B:
	case 0x5C: case 0x5D: case 0x5E: case 0x5F: /* pop r64 */
		return (ssize_t)(i + 1);
	case 0x6A: /* push imm8 */
		return (ssize_t)(i + 2);
	case 0x68: /* push imm32 */
		return (ssize_t)(i + 5);
	case 0xB0: case 0xB1: case 0xB2: case 0xB3:
	case 0xB4: case 0xB5: case 0xB6: case 0xB7: /* mov r8, imm8 */
		return (ssize_t)(i + 2);
	case 0xB8: case 0xB9: case 0xBA: case 0xBB:
	case 0xBC: case 0xBD: case 0xBE: case 0xBF: /* mov r32/r64, imm.
	   i already counts any 66/REX prefix bytes, so the immediate sizes add
	   only 1 (opcode) + 4 or + 8: `48 B8 <imm64>` is 1+1+8 = i+9, and
	   `B9 <imm32>` is i+5. A previous `i+10` over-counted by the prefix
	   length and rejected real prologues carrying a REX prefix. */
		return (ssize_t)(i + ((rex & 0x08) ? 9 : 5));
	case 0x83: { /* group1, imm8 */
		struct DecoderCtx tmp = *c;
		ssize_t tail = modrm_tail_len(&tmp, i + 1, 0);
		if (tail < 0) return tail;
		return (ssize_t)(i + 1 + (size_t)tail + 1);
	}
	case 0x81: { /* group1, imm32 (imm16 with 66 prefix) - REX.W does NOT
	              change the immediate encoding, only the operand size */
		struct DecoderCtx tmp = *c;
		ssize_t tail = modrm_tail_len(&tmp, i + 1, 0);
		if (tail < 0) return tail;
		return (ssize_t)(i + 1 + (size_t)tail + (opsize66 ? 2 : 4));
	}
	case 0x88: case 0x89: case 0x8A: case 0x8B: /* mov r/m,r ; r,r/m */
	case 0x30: case 0x31: case 0x32: case 0x33: /* xor */
	case 0x28: case 0x29: case 0x2A: case 0x2B: /* sub */
	case 0x00: case 0x01: case 0x02: case 0x03: /* add */
	case 0x08: case 0x09: case 0x0A: case 0x0B: /* or */
	case 0x20: case 0x21: case 0x22: case 0x23: /* and */
	case 0x38: case 0x39: case 0x3A: case 0x3B: /* cmp */
	case 0x84: case 0x85:                        /* test */
	case 0x63:                                   /* movsxd */
	case 0xC6: case 0xC7: {                      /* mov r/m, imm */
		int lea = 0;
		struct DecoderCtx tmp = *c;
		ssize_t tail = modrm_tail_len(&tmp, i + 1, lea);
		if (tail < 0) return tail;
		size_t len = i + 1 + (size_t)tail;
		if (op == 0xC6) len += 1;                        /* imm8 */
		else if (op == 0xC7) len += (opsize66 ? 2 : 4);   /* imm16/32 */
		return (ssize_t)len;
	}
	case 0x8D: { /* lea r64, m */
		struct DecoderCtx tmp = *c;
		ssize_t tail = modrm_tail_len(&tmp, i + 1, 1);
		if (tail < 0) return tail;
		return (ssize_t)(i + 1 + (size_t)tail);
	}
	default:
		break;
	}

	/* two-byte opcodes */
	if (op == 0x0F && i + 1 < n) {
		uint8_t op2 = p[i + 1];
		if (op2 == 0x1F) { /* multi-byte nop */
			struct DecoderCtx tmp = *c;
			ssize_t tail = modrm_tail_len(&tmp, i + 2, 0);
			if (tail < 0) return tail;
			return (ssize_t)(i + 2 + (size_t)tail);
		}
		/* The only other 0F-form that appears at a function entry is
		 * endbr64, and that always arrives with the F3 prefix (handled
		 * above). A bare 0F 1E /7 has a ModRM byte whose disposition
		 * decides its length, so a fixed +4 here would be wrong; refuse. */
	}

	(void)op_pos;
	return fail(c, i, "opcode not in the safe prologue subset");
}

/* Decode instructions until at least HOOK_MIN_PATCH bytes are covered. */
static ssize_t decode_prologue(const uint8_t *target, size_t max,
                               uint8_t *out, size_t *out_len,
                               char *err, size_t errsz)
{
	size_t total = 0;
	while (total < HOOK_MIN_PATCH) {
		struct DecoderCtx c = { target + total, max - total, err, errsz };
		ssize_t len = insn_len(&c);
		if (len < 0) return -1;
		if (total + (size_t)len > HOOK_STOLEN_MAX) {
			hook_err(err, errsz, "prologue exceeds %d bytes without reaching the %d-byte patch size",
			         HOOK_STOLEN_MAX, HOOK_MIN_PATCH);
			return -1;
		}
		memcpy(out + total, target + total, (size_t)len);
		total += (size_t)len;
	}
	*out_len = total;
	return (ssize_t)total;
}

/* mmap a page within +-2GB of target so a rel32 jump reaches it. */
static void *alloc_near(uintptr_t target, char *err, size_t errsz)
{
	static const intptr_t offsets[] = { 64, -64, 128, -128, 256, -256,
		                                512, -512, 1024, -1024, 1536, -1536 };
	const size_t noffsets = sizeof(offsets) / sizeof(offsets[0]);
	intptr_t mb = 1024 * 1024;

	for (size_t k = 0; k < noffsets; k++) {
		uintptr_t hint = (uintptr_t)((int64_t)target + offsets[k] * mb);
		hint = page_align(hint, g_page_size);
		void *p = mmap((void *)hint, (size_t)g_page_size,
		               PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (p == MAP_FAILED) continue;
		int64_t d = (int64_t)((uintptr_t)p - target);
		if (d > HOOK_REL32_MIN + 65536 && d < HOOK_REL32_MAX - 65536)
			return p;
		munmap(p, (size_t)g_page_size);
	}
	hook_err(err, errsz, "could not mmap a trampoline page within +-2GB of target %p", (void *)target);
	return NULL;
}

int havel_hook_install(void *target, void *replacement, void **trampoline,
                       char *err, size_t errsz)
{
	if (err && errsz) err[0] = '\0';
	if (!target || !replacement) {
		hook_err(err, errsz, "target and replacement are required");
		return -1;
	}
	g_page_size = hook_pagesize();

	pthread_mutex_lock(&g_lock);

	for (int i = 0; i < HOOK_MAX; i++) {
		if (g_hooks[i].active && g_hooks[i].target == target) {
			hook_err(err, errsz, "target %p is already hooked", target);
			pthread_mutex_unlock(&g_lock);
			return -1;
		}
	}
	int slot = -1;
	for (int i = 0; i < HOOK_MAX; i++) {
		if (!g_hooks[i].active) { slot = i; break; }
	}
	if (slot < 0) {
		hook_err(err, errsz, "hook table full (%d hooks)", HOOK_MAX);
		pthread_mutex_unlock(&g_lock);
		return -1;
	}

	HookRecord *rec = &g_hooks[slot];
	memset(rec, 0, sizeof(*rec));

	if (decode_prologue((const uint8_t *)target, HOOK_STOLEN_MAX,
	                    rec->stolen, &rec->stolen_len, err, errsz) < 0) {
		pthread_mutex_unlock(&g_lock);
		return -1;
	}

	void *page = alloc_near((uintptr_t)target, err, errsz);
	if (!page) {
		pthread_mutex_unlock(&g_lock);
		return -1;
	}
	rec->page = page;
	rec->page_size = (size_t)g_page_size;
	rec->target = target;
	rec->replacement = replacement;
	rec->active = 1;

	uint8_t *page_bytes = (uint8_t *)page;

	/* gate: absolute jump to the replacement */
	write_gate(page_bytes + HOOK_GATE_OFFSET, replacement);

	/* original: stolen prologue + jump back to target+len */
	uint8_t *orig = page_bytes + HOOK_ORIG_OFFSET;
	memcpy(orig, rec->stolen, rec->stolen_len);
	uint8_t *back_at = orig + rec->stolen_len;
	if (!rel32_fits(back_at, (const uint8_t *)target + rec->stolen_len)) {
		hook_err(err, errsz, "trampoline page out of rel32 range of target");
		munmap(page, rec->page_size);
		memset(rec, 0, sizeof(*rec));
		pthread_mutex_unlock(&g_lock);
		return -1;
	}
	write_jmp_rel32(back_at, back_at, (const uint8_t *)target + rec->stolen_len);

	/* gate must be reachable from the target too */
	if (!rel32_fits(target, page_bytes + HOOK_GATE_OFFSET)) {
		hook_err(err, errsz, "trampoline page out of rel32 range of target (gate)");
		munmap(page, rec->page_size);
		memset(rec, 0, sizeof(*rec));
		pthread_mutex_unlock(&g_lock);
		return -1;
	}

	/* flip trampoline page to RX */
	if (mprotect(page, rec->page_size, PROT_READ | PROT_EXEC) != 0) {
		hook_err(err, errsz, "mprotect(trampoline, RX) failed: %s", strerror(errno));
		munmap(page, rec->page_size);
		memset(rec, 0, sizeof(*rec));
		pthread_mutex_unlock(&g_lock);
		return -1;
	}

	/* patch the target with `jmp rel32 -> gate` (via /proc/self/mem when
	 * available; see write_exec_memory for why not mprotect-first) */
	uint8_t patch[HOOK_MIN_PATCH];
	/* the jmp will live on the target; its rel32 is relative to the target,
	 * not to this stack buffer (see write_jmp_rel32) */
	write_jmp_rel32(patch, target, page_bytes + HOOK_GATE_OFFSET);
	if (write_exec_memory(target, patch, HOOK_MIN_PATCH, err, errsz) != 0) {
		munmap(page, rec->page_size);
		memset(rec, 0, sizeof(*rec));
		pthread_mutex_unlock(&g_lock);
		return -1;
	}

	/* NOTE: a thread already executing inside [target, target+5) while the
	 * patch lands can fault - the documented install-time discipline (no
	 * worker threads inside the target) is what makes this safe. */

	if (trampoline) *trampoline = orig;
	pthread_mutex_unlock(&g_lock);
	return 0;
}

int havel_hook_remove(void *target, char *err, size_t errsz)
{
	if (err && errsz) err[0] = '\0';
	if (!target) {
		hook_err(err, errsz, "target is required");
		return -1;
	}
	g_page_size = hook_pagesize();

	pthread_mutex_lock(&g_lock);
	for (int i = 0; i < HOOK_MAX; i++) {
		HookRecord *rec = &g_hooks[i];
		if (!rec->active || rec->target != target) continue;

		if (write_exec_memory((void *)target, rec->stolen, rec->stolen_len, err, errsz) != 0) {
			pthread_mutex_unlock(&g_lock);
			return -1;
		}
		munmap(rec->page, rec->page_size);
		memset(rec, 0, sizeof(*rec));
		pthread_mutex_unlock(&g_lock);
		return 0;
	}
	hook_err(err, errsz, "target %p is not hooked", target);
	pthread_mutex_unlock(&g_lock);
	return -1;
}

int havel_hook_is_hooked(const void *target)
{
	pthread_mutex_lock(&g_lock);
	int found = 0;
	for (int i = 0; i < HOOK_MAX; i++) {
		if (g_hooks[i].active && g_hooks[i].target == target) {
			found = 1;
			break;
		}
	}
	pthread_mutex_unlock(&g_lock);
	return found;
}

#else /* !x86-64 POSIX */

int havel_hook_install(void *target, void *replacement, void **trampoline,
                       char *err, size_t errsz)
{
	(void)target; (void)replacement; (void)trampoline;
	if (err && errsz) err[0] = '\0';
	hook_err(err, errsz, "inline hooking requires x86-64 POSIX; this platform is not supported");
	return -1;
}

int havel_hook_remove(void *target, char *err, size_t errsz)
{
	(void)target;
	if (err && errsz) err[0] = '\0';
	hook_err(err, errsz, "inline hooking requires x86-64 POSIX; this platform is not supported");
	return -1;
}

int havel_hook_is_hooked(const void *target)
{
	(void)target;
	return 0;
}

#endif
