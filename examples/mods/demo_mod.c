/*
 * demo_mod.c - example mod for the Havel modding API (include/havel/mod.h).
 *
 * It demonstrates the whole protocol surface: init that installs a hook,
 * state that survives a hot reload, and unload that removes the hook before
 * the generation's mapping is torn down. Used by the host lifecycle test and
 * by the Havel-level test scripts/tests/modding/test_mod_hot_reload.hv.
 *
 * Build: cc -shared -fPIC -I<tree/include> demo_mod.c -o libdemo_mod.so
 *
 * Exported demo functions (called through Havel's ffi module, or through
 * ModManager::resolve from C++):
 *   demo_bump()      -> increments the preserved counter, returns it
 *   demo_counter()   -> current preserved counter
 *   demo_instances() -> how many times this .so has been init'd (never
 *                       preserved; a fresh reload therefore returns 1)
 *   demo_add(a, b)   -> a + b (hooked by this mod at init)
 *   demo_hooked_add  -> the replacement: a + b + 1000
 */

#include "havel/mod.h"

#include <string.h>

static HavelModApi *g_api;

/* State round-tripped through state_save/state_reload on a hot reload. */
static int g_counter;

/* NOT round-tripped: its value proves a reload opened a fresh instance. */
static int g_instances;

/* Set from the trampoline handed to hook_install during init. */
static void *g_demo_add_trampoline;

/* prototypes: init() installs a detour on demo_add that redirects to
 * demo_hooked_add, both defined below. demo_triple/demo_hooked_triple are
 * the pair the SCRIPT-level hook test uses (a symbol the mod itself leaves
 * unhooked, since one detour per target is supported). */
__attribute__((visibility("default"))) int demo_add(int a, int b);
__attribute__((visibility("default"))) int demo_hooked_add(int a, int b);
__attribute__((visibility("default"))) int demo_triple(int x);
__attribute__((visibility("default"))) int demo_hooked_triple(int x);

static size_t state_size(void) { return sizeof(g_counter); }
static void state_save(void *buf) { memcpy(buf, &g_counter, sizeof(g_counter)); }
static int state_reload(const void *st, size_t sz) {
    if (!st || sz != sizeof(g_counter)) return -1;
    memcpy(&g_counter, st, sizeof(g_counter));
    return 0;
}

int havel_mod_init(HavelModApi *api) {
    char err[HAVEL_MOD_ERR_SIZE];

    if (!api) return -1;
    if (api->abi_version != HAVEL_MOD_ABI_VERSION) {
        if (api->log) api->log("demo_mod: ABI version mismatch");
        return -1;
    }
    g_api = api;
    g_instances++;

    /* Hook our own exported function. The target resolves through the
     * global scope because the manager dlopens mods RTLD_GLOBAL. */
    if (api->hook_install("demo_add", (void *)demo_hooked_add,
                          &g_demo_add_trampoline, err, sizeof err) != 0) {
        api->log(err[0] ? err : "demo_mod: hook_install(demo_add) failed");
        return -1;
    }
    api->log("demo_mod: init complete (demo_add hooked)");
    return 0;
}

void havel_mod_unload(void) {
    /* Removes the detour BEFORE the mapping goes away: a trampoline left
     * installed into a dlclose'd page would execute unmapped code. The
     * manager also removes mod-owned hooks, so this is belt-and-braces. */
    if (g_api) {
        char err[HAVEL_MOD_ERR_SIZE];
        g_api->hook_remove("demo_add", err, sizeof err);
    }
    g_api = NULL;
    g_demo_add_trampoline = NULL;
}

size_t havel_mod_state_size(void) { return state_size(); }
void havel_mod_state_save(void *buf) { state_save(buf); }
int havel_mod_reload(const void *state, size_t size) {
    return state_reload(state, size);
}

__attribute__((visibility("default"))) int demo_bump(void) {
    return ++g_counter;
}

__attribute__((visibility("default"))) int demo_counter(void) {
    return g_counter;
}

__attribute__((visibility("default"))) int demo_instances(void) {
    return g_instances;
}

static int g_demo_scratch;

/* noinline + writes a global that is read, so -O2 cannot dead-code it: that
 * forces demo_add to have a real frame (a callsite in the prologue), which
 * in turn makes it long enough for the engine's 5-byte patch. A body like
 * `lea eax,[rdi+rsi]; ret` is only 4 bytes and is legitimately un-hookable. */
__attribute__((noinline, visibility("hidden"))) static void demo_note(int v) {
	g_demo_scratch += v;
}

/* The pointer form: the caller's array escapes through it, which is what
 * gives the caller a frame when a single noinline call would not (a
 * register-only body schedules the call first, and relocating a call's
 * rel32 is exactly what the hook decoder must refuse). */
__attribute__((noinline, visibility("hidden"))) static void demo_note_arr(const int *p) {
	g_demo_scratch += p ? p[0] : 0;
}

/* optnone: the demo mod is built with the release flags (-O3 -flto), and at
 * that level IPA eliminates any escaping-store trick, leaving a body too
 * short to hook. -O0 codegen gives the function the full, deterministic
 * prologue the detour engine needs. This is a test-fixture concern, not a
 * modding-API rule: real mods are compiled by their author's own build. */
__attribute__((visibility("default"), optnone)) int demo_add(int a, int b) {
    demo_note(a);
    demo_note(b);
    int c = a + b;
    return c;
}

__attribute__((visibility("default"))) int demo_hooked_add(int a, int b) {
    return a + b + 1000;
}

/* Same shape as demo_add: a frame (via demo_note) so the hook engine has
 * prologue bytes to steal, and a value a test can distinguish from the ho
 * oked replacement. Left un-hooked by init so a script can take it.
 * A single noinline call leaves no frame (a register-only body schedules the
 * call first, and a relocated call's rel32 cannot be stolen), so the
 * escaping array is what forces the prologue. */
__attribute__((visibility("default"), optnone)) int demo_triple(int x) {
    int buf[8];
    /* the whole array escapes into demo_note_arr, so none of these stores can
     * be eliminated: they are the >5 bytes of position-independent prologue
     * the detour needs before the (unrelocatable) call. */
    buf[0] = x;
    buf[1] = x + 1;
    buf[2] = x + 2;
    demo_note_arr(buf);
    buf[3] = buf[0] * 3;
    return buf[3];
}

__attribute__((visibility("default"))) int demo_hooked_triple(int x) {
    return x * 3 + 1000;
}