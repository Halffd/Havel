#pragma once

/*
 * Loader.h - Pure C dynamic library loader
 *
 * Python-style module loading:
 *   - loader_load("mylib")       -> finds libmylib.so / mylib.so / mylib.dylib
 *   - loader_sym(handle, "foo")  -> get symbol from loaded lib
 *   - loader_import("requests")  -> python-style: finds & inits module .so
 *
 * C ABI: no C++ types, no STL, no exceptions. Pure POSIX dlopen/dlsym.
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct HavelModuleABI HavelModuleABI;
typedef struct HavelToolkitABI HavelToolkitABI;

/* Opaque handle */
typedef struct HavelLoader HavelLoader;

/* Loaded module info */
typedef struct HavelLoadedModule {
    char name[256];
    char path[512];
    void *handle;
    int is_loaded;
} HavelLoadedModule;

/* Init function signature (matches HavelCAPI.h HavelExtensionInit) */
typedef void (*HavelModuleInitFn)(void *api);

/* Create/destroy loader */
HavelLoader *havel_loader_create(void);
void havel_loader_destroy(HavelLoader *loader);

/* Search paths */
void havel_loader_add_search_path(HavelLoader *loader, const char *path);

/* Get platform suffix (".so", ".dylib", ".dll") */
const char *havel_loader_suffix(void);

/* Load a shared library by path. Returns dlopen handle or NULL. */
void *havel_loader_open(HavelLoader *loader, const char *path);

/* Load a shared library by name (python-style).
 * Tries: name.so, libname.so, name.dylib, libname.dylib
 * Searches all registered paths.
 * Returns dlopen handle or NULL. */
void *havel_loader_load(HavelLoader *loader, const char *name);

/* Get symbol from a loaded handle. Returns pointer or NULL. */
void *havel_loader_sym(void *handle, const char *symbol);

/* Import a module by name (python-style).
 * Like loader_load, but also calls havel_extension_init() if present.
 * Returns the dlopen handle or NULL. */
void *havel_loader_import(HavelLoader *loader, const char *name, void *api);

/* Query loaded state */
int havel_loader_is_loaded(HavelLoader *loader, const char *name);
void *havel_loader_get_handle(HavelLoader *loader, const char *name);

/* Get list of loaded module names.
 * Caller frees the returned array with havel_loader_free_names(). */
char **havel_loader_list_loaded(HavelLoader *loader, int *count);
void havel_loader_free_names(char **names, int count);

/* Get last error string (from dlerror) */
const char *havel_loader_error(void);

/* Close a handle (normally you don't need this - libs stay loaded) */
void havel_loader_close(void *handle);

/* Platform: add standard search paths */
void havel_loader_add_standard_paths(HavelLoader *loader);

/* Platform: add module plugin search paths */
void havel_loader_add_module_paths(HavelLoader *loader);

/* Load a module plugin by name.
 * Searches for havel_mod_<name>.so in module paths.
 * Validates ABI version via havel_module_info().
 * Returns HavelModuleABI* if valid, NULL if not found or incompatible. */
const HavelModuleABI *havel_loader_load_module(HavelLoader *loader, const char *name);

/* Platform: add toolkit plugin search paths */
void havel_loader_add_toolkit_paths(HavelLoader *loader);

/* Load a toolkit plugin by name.
 * Searches for havel_toolkit_<name>.so in toolkit paths.
 * Validates ABI version via havel_toolkit_info().
 * Returns HavelToolkitABI* if valid, NULL if not found or incompatible. */
const HavelToolkitABI *havel_loader_load_toolkit(HavelLoader *loader, const char *name);

#ifdef __cplusplus
}
#endif
