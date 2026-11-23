#ifndef HAVEL_MOD_H
#define HAVEL_MOD_H

/*
 * mod.h - Public SDK contract for Havel .so mods.
 *
 * A mod is a shared library loaded by the `mod` Havel module
 * (havel_mod_mod.so). It must export:
 *
 *   int havel_mod_init(HavelModApi *api);   // required, return 0 on success
 *
 * and may export:
 *
 *   void havel_mod_unload(void);                          // cleanup on unload
 *   int havel_mod_reload(const void *state, size_t size); // restore state
 *   size_t havel_mod_state_size(void);                    // state blob size
 *   void havel_mod_state_save(void *buf);                 // serialize state
 *
 * Hot-reload protocol (driven by mod.reload(name) from Havel):
 *   1. havel_mod_state_size() on the CURRENT instance -> byte count
 *   2. havel_mod_state_save(buf) on the CURRENT instance
 *   3. havel_mod_unload() on the CURRENT instance (remove your hooks here)
 *   4. dlclose old generation, dlopen the rebuilt .so
 *   5. havel_mod_init(api) on the NEW instance
 *   6. havel_mod_reload(buf, size) on the NEW instance, if it exported
 *      state_size and the saved size is non-zero
 *
 * Static/global variables do NOT survive the generation switch unless they
 * are round-tripped through state_save/state_reload.
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HAVEL_MOD_ABI_VERSION 1
#define HAVEL_MOD_ERR_SIZE 256

typedef struct HavelModApi {
	/* Set by the host. Mods should refuse to run when this differs from
	 * HAVEL_MOD_ABI_VERSION. */
	int abi_version;

	/* Reserved for future host context. NULL today. */
	void *ctx;

	/* Host-side logging. Thread-safe. */
	void (*log)(const char *message);

	/*
	 * Install an inline detour. `symbol` is resolved through the global
	 * symbol namespace (RTLD_DEFAULT) and then through every loaded mod,
	 * so functions exported by mods with RTLD_GLOBAL are hookable by
	 * name. `replacement` must match the target's calling convention.
	 * On success *trampoline receives a callable pointer to the ORIGINAL
	 * function and 0 is returned; on failure -1 is returned and err is
	 * filled with a NUL-terminated message.
	 */
	int (*hook_install)(const char *symbol, void *replacement,
	                    void **trampoline, char *err, size_t errsz);

	/* Remove a previously installed detour. Returns 0, or -1 with err. */
	int (*hook_remove)(const char *symbol, char *err, size_t errsz);

	/* Resolve a symbol: RTLD_DEFAULT first, then loaded mods. */
	void *(*resolve)(const char *symbol);
} HavelModApi;

#ifdef __cplusplus
}
#endif

#endif /* HAVEL_MOD_H */
