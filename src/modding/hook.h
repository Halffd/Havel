#ifndef HAVEL_MODDING_HOOK_H
#define HAVEL_MODDING_HOOK_H

/*
 * hook.h - x86-64 inline detour engine (POSIX).
 *
 * Patching model for a target T and replacement R:
 *
 *   - a private page is mmap'd within +-2GB of T (required for `jmp rel32`)
 *   - page offset 0: the GATE - `mov rax, <R>; jmp rax` (12 bytes, no range
 *     limit because the address is absolute)
 *   - page offset 16: the ORIGINAL - the stolen prologue bytes, followed by
 *     `jmp rel32` back to T + stolen_len. This pointer is handed to the
 *     caller and calls the original function.
 *   - T's first 5 bytes are overwritten with `jmp rel32 -> GATE`
 *
 * The prologue length decoder accepts only a conservative subset of x86-64
 * instructions that clang/gcc emit at function entry (push/pop, reg-reg
 * moves, sub/add rsp imm, lea from rsp/rbp, xor, nops, endbr64, mov-imm).
 * Anything RIP-relative, absolute-disp32, or a control-flow instruction is
 * REFUSED with an error rather than relocated - a wrong length silently
 * corrupts the caller's stack, so refusal is the only safe answer.
 *
 * W^X: the trampoline page is written while PROT_READ|PROT_WRITE and then
 * flipped to PROT_READ|PROT_EXEC; the target page is patched through
 * /proc/self/mem, which bypasses the VMA protections and therefore needs no
 * writable window at all. The mprotect-to-RW dance remains only as a fallback
 * for hosts without procfs, because on hardened hosts (observed on the
 * development machine) mprotect(PROT_WRITE) on file-backed executable text is
 * refused with an uncatchable SIGSEGV - which is why the mem path comes
 * first. install still requires that no worker thread is inside the target
 * while the 5-byte patch lands; that race is not solved by the write path.
 *
 * x86-64 has coherent instruction/data caches: no icache flush is needed
 * after patching. On ARM this would be required (cacheflush/__clear_cache).
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HAVEL_HOOK_ERR_SIZE 256

/* Install a detour. target/replacement must use the same calling
 * convention. On success returns 0 and sets *trampoline to a callable
 * pointer to the original function. On failure returns -1 and fills err. */
int havel_hook_install(void *target, void *replacement, void **trampoline,
                       char *err, size_t errsz);

/* Remove a detour previously installed on target, restoring the stolen
 * bytes. Returns 0 on success, -1 (with err) when target is not hooked or
 * the restore fails. */
int havel_hook_remove(void *target, char *err, size_t errsz);

/* Non-zero when target currently has a detour installed. */
int havel_hook_is_hooked(const void *target);

#ifdef __cplusplus
}
#endif

#endif /* HAVEL_MODDING_HOOK_H */
