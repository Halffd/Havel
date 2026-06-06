#pragma once

/*
 * Loader.h - Pure C dynamic library and module loader
 *
 * Python-style: loadByName("foo") -> finds libfoo.so, dlopens.
 * import("foo", api) -> finds, dlopens, calls havel_extension_init(api).
 * attr(handle, "method") -> python-style: resolve symbol.
 *
 * Script resolution: resolve("mymod", "/scripts") -> finds .hv/.hvc files
 * via priority search (cache, scriptDir, __cache__, stdlib, packages, paths).
 *
 * Module plugins: havel_mod_<name>.so with ABI validation.
 * Toolkit plugins: havel_toolkit_<name>.so with ABI validation.
 *
 * C ABI: no C++ types, no STL, no exceptions. Pure POSIX dlopen/dlsym.
 */

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct HavelModuleABI HavelModuleABI;
typedef struct HavelToolkitABI HavelToolkitABI;

typedef struct HavelLoader HavelLoader;

typedef struct HavelLoadedModule {
 char name[256];
 char path[512];
 void *handle;
 int is_loaded;
} HavelLoadedModule;

typedef void (*HavelModuleInitFn)(void *api);

typedef enum HavelSourceType {
 HAVEL_SOURCE_USER,
 HAVEL_SOURCE_STDLIB,
 HAVEL_SOURCE_PACKAGE,
 HAVEL_SOURCE_BYTECODE_CACHE,
 HAVEL_SOURCE_CACHED,
 HAVEL_SOURCE_NATIVE_EXTENSION,
 HAVEL_SOURCE_HOST_BUILTIN,
} HavelSourceType;

typedef struct HavelResolvedModule {
 HavelSourceType type;
 char resolved_path[1024];
 char original_name[256];
} HavelResolvedModule;

HavelLoader *havel_loader_create(void);
void havel_loader_destroy(HavelLoader *loader);

void havel_loader_add_search_path(HavelLoader *loader, const char *path);

const char *havel_loader_suffix(void);

void *havel_loader_open(HavelLoader *loader, const char *path);

void *havel_loader_load(HavelLoader *loader, const char *name);

void *havel_loader_sym(void *handle, const char *symbol);

void *havel_loader_attr(void *handle, const char *name);

void *havel_loader_import(HavelLoader *loader, const char *name, void *api);

int havel_loader_is_loaded(HavelLoader *loader, const char *name);
void *havel_loader_get_handle(HavelLoader *loader, const char *name);

char **havel_loader_list_loaded(HavelLoader *loader, int *count);
void havel_loader_free_names(char **names, int count);

const char *havel_loader_error(void);

void havel_loader_close(void *handle);

void havel_loader_add_standard_paths(HavelLoader *loader);

void havel_loader_add_module_paths(HavelLoader *loader);

const HavelModuleABI *havel_loader_load_module(HavelLoader *loader, const char *name);

void havel_loader_add_toolkit_paths(HavelLoader *loader);

const HavelToolkitABI *havel_loader_load_toolkit(HavelLoader *loader, const char *name);

void havel_loader_set_stdlib_path(HavelLoader *loader, const char *path);
const char *havel_loader_get_stdlib_path(const HavelLoader *loader);

void havel_loader_add_script_path(HavelLoader *loader, const char *path);
int havel_loader_get_script_path_count(const HavelLoader *loader);
const char *havel_loader_get_script_path(const HavelLoader *loader, int index);

int havel_loader_resolve(HavelLoader *loader, const char *module_path,
 const char *script_dir, HavelResolvedModule *out);

int havel_loader_is_cached(HavelLoader *loader, const char *key);
void havel_loader_put_cache(HavelLoader *loader, const char *key, uint64_t value);
int havel_loader_get_cache(HavelLoader *loader, const char *key, uint64_t *out_value);
void havel_loader_clear_cache(HavelLoader *loader);

int havel_loader_has_host_module(HavelLoader *loader, const char *name);
void havel_loader_register_host_module(HavelLoader *loader, const char *name);
void havel_loader_mark_loaded(HavelLoader *loader, const char *name);
int havel_loader_was_loaded(HavelLoader *loader, const char *name);

typedef struct HavelModuleInfo {
 char name[256];
 char aliases[8][256];
 int alias_count;
 int eager;
} HavelModuleInfo;

int havel_loader_scan_modules(HavelLoader *loader, HavelModuleInfo *out, int max_out);
const HavelModuleABI *havel_loader_probe_module(HavelLoader *loader, const char *name,
 HavelModuleInfo *out_info);

#ifdef __cplusplus
}
#endif
