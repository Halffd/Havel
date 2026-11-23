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
 * demo_hooked_add, both defined below */
__attribute__((visibility("default"))) int demo_add(int a, int b);
__attribute__((visibility("default"))) int demo_hooked_add(int a, int b);

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

__attribute__((visibility("default"))) int demo_add(int a, int b) {
    demo_note(a);
    demo_note(b);
    int c = a + b;
    return c;
}

__attribute__((visibility("default"))) int demo_hooked_add(int a, int b) {
    return a + b + 1000;
}