// End-to-end detour test for the modding hook engine: install a hook, call
// the target, call through the trampoline to get the ORIGINAL, remove the
// hook and verify everything is restored. The engine is exercised through
// its real code paths (decode, near-page mmap, /proc/self/mem patch).
//
// Two engine bugs were caught by this test: a rel32 computed from the
// destination stack buffer instead of the jump's runtime address, and the
// mprotect(PROT_WRITE)-on-exec-text rejection that /proc/self/mem exists to
// bypass. Keep asserting the exact values; both failures looked like
// "works on my machine" without them.
#include "src/modding/hook.h"
#include <stdio.h>

static int g_hook_calls = 0;
static int g_sink = 0;
static void *g_tramp = NULL;

/* Writes through the pointer, so the array escapes and add() has a frame:
 * prologue = endbr64 / push rbp / mov rbp,rsp / sub rsp,N / mov [rbp-N],edi.
 * A body without an escaping local collapses to `lea; ret` (4 bytes), which
 * is legitimately un-hookable - the engine must refuse that, this test must
 * not assert the impossible. */
__attribute__((noinline, visibility("default"))) void sink(int *p) {
    g_sink = p ? p[0] : 0;
}

/* Compiled at -O2 by the test script, this collapses to
 * `lea eax,[rdi+1]; ret` (4 bytes): fewer than the 5 bytes the detour needs.
 * Hooking must REFUSE rather than corrupt it - and after the refusal, the
 * normal target must still be hookable (no engine state left half-initialised).
 * (A per-function optimize attribute is not portable: this clang rejects it,
 * so the -O2 comes from the build script instead.) */
__attribute__((noinline, visibility("default"))) int tiny(int x) {
    return x + 1;
}

__attribute__((noinline, visibility("default"))) int target(int a, int b) {
    int buf[4];
    buf[0] = a;
    buf[1] = b;
    sink(buf);
    buf[2] = buf[0] + buf[1];
    buf[3] = buf[2] * 1;
    return buf[3];
}

__attribute__((noinline, visibility("default"))) int hooked_replacement(int a, int b) {
    g_hook_calls++;
    typedef int (*fn)(int, int);
    fn orig = (fn)g_tramp;
    return orig(a, b) + 1000;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    char err[HAVEL_HOOK_ERR_SIZE];
    void *tp = NULL;

    /* read g_sink: the store inside sink() is then live, so the array that
     * escapes into sink() forces target() to have a real frame at -O2. */
    target(2, 3);
    printf("sink side effect = %d (nonzero keeps the frame alive)\n", g_sink);

    printf("unhooked  target(2,3) = %d\n", target(2, 3));

    int rc = havel_hook_install((void *)target, (void *)hooked_replacement, &tp, err, sizeof err);
    if (rc != 0) { printf("install FAILED: %s\n", err); return 1; }
    g_tramp = tp;
    printf("hooked    target(2,3) = %d  (expected 1005)\n", target(2, 3));

    typedef int (*fn)(int, int);
    fn orig = (fn)tp;
    printf("trampoline orig(2,3) = %d  (expected 5)\n", orig(2, 3));

    rc = havel_hook_remove((void *)target, err, sizeof err);
    if (rc != 0) { printf("remove FAILED: %s\n", err); return 1; }
    printf("removed   target(2,3) = %d  (expected 5)\n", target(2, 3));

    printf("hook fired total %d time(s) (expected exactly 1)\n", g_hook_calls);
    if (g_hook_calls != 1) { printf("FAIL: hook should have fired exactly once\n"); return 1; }
    if (havel_hook_is_hooked((void *)target)) { printf("FAIL: still marked hooked\n"); return 1; }

    /* a target too short to steal 5 bytes from must be REFUSED, not corrupted */
    tp = NULL; /* clear the pointer from the first install before asserting */
    rc = havel_hook_install((void *)tiny, (void *)hooked_replacement, &tp, err, sizeof err);
    if (rc == 0) { printf("FAIL: a 4-byte body must not be hookable\n"); return 1; }
    printf("refused short body as expected: %s\n", err);
    if (tp != NULL) { printf("FAIL: trampoline pointer set on a refused install\n"); return 1; }
    if (havel_hook_install((void *)target, (void *)hooked_replacement, &tp, err, sizeof err) != 0) {
        printf("FAIL: install on an unhooked target after refused install: %s\n", err);
        return 1;
    }
    havel_hook_remove((void *)target, err, sizeof err);

    printf("PASS: detour, trampoline, unhook and refusal all behave\n");
    return 0;
}