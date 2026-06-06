/*
 * Loader.c - Pure C dynamic library and module loader
 *
 * Python-style module loading via dlopen/dlsym:
 * havel_loader_load(l, "foo") -> finds libfoo.so / foo.so
 * havel_loader_sym(h, "bar") -> get symbol from loaded lib
 * havel_loader_import(l, "x", a) -> find, dlopen, call init
 * havel_loader_attr(h, "method") -> python-style: resolve symbol
 *
 * Script resolution via priority search:
 * havel_loader_resolve(l, "mymod", "/scripts", &out) ->
 *   1. cache  2. scriptDir/*.hv  3. __cache__/*.hvc
 *   4. stdlib/*.hv  5. ~/.havel/packages/  6. searchPaths
 *   7. *.so native  8. host builtin
 *
 * Module plugins: havel_mod_<name>.so with ABI validation.
 * Toolkit plugins: havel_toolkit_<name>.so with ABI validation.
 *
 * No C++ dependencies. No STL. Pure POSIX.
 */

#include "Loader.h"
#include "c/ModulePlugin.h"
#include "c/ToolkitPlugin.h"
#include "c/LoggerC.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <limits.h>
#include <errno.h>

#define MAX_SEARCH_PATHS 64
#define MAX_LOADED 128
#define MAX_SCRIPT_PATHS 64
#define MAX_CACHE_ENTRIES 256
#define MAX_HOST_MODULES 128
#define MAX_LOADED_FLAGS 128

struct HavelLoader {
 char *search_paths[MAX_SEARCH_PATHS];
 int search_path_count;
 HavelLoadedModule loaded[MAX_LOADED];
 int loaded_count;
 char *script_paths[MAX_SCRIPT_PATHS];
 int script_path_count;
 char stdlib_path[1024];
 struct { char key[256]; uint64_t value; int used; } cache[MAX_CACHE_ENTRIES];
 int cache_count;
 char *host_modules[MAX_HOST_MODULES];
 int host_module_count;
 char loaded_flags[MAX_LOADED_FLAGS][256];
 int loaded_flag_count;
};

static const char *platform_suffix(void) {
#if defined(_WIN32)
 return ".dll";
#elif defined(__APPLE__)
 return ".dylib";
#else
 return ".so";
#endif
}

const char *havel_loader_suffix(void) {
 return platform_suffix();
}

HavelLoader *havel_loader_create(void) {
 HavelLoader *l = (HavelLoader *)calloc(1, sizeof(HavelLoader));
 if (!l) return NULL;
 havel_loader_add_standard_paths(l);
 return l;
}

void havel_loader_destroy(HavelLoader *loader) {
 if (!loader) return;
 for (int i = 0; i < loader->search_path_count; i++)
  free(loader->search_paths[i]);
 for (int i = 0; i < loader->script_path_count; i++)
  free(loader->script_paths[i]);
 for (int i = 0; i < loader->host_module_count; i++)
  free(loader->host_modules[i]);
 free(loader);
}

const char *havel_loader_error(void) {
 return dlerror();
}

void havel_loader_add_search_path(HavelLoader *loader, const char *path) {
 if (!loader || !path) return;
 if (loader->search_path_count >= MAX_SEARCH_PATHS) return;
 loader->search_paths[loader->search_path_count++] = strdup(path);
}

void havel_loader_add_standard_paths(HavelLoader *loader) {
 if (!loader) return;
 havel_loader_add_search_path(loader, ".");
 havel_loader_add_search_path(loader, "/usr/lib/havel/extensions");
 havel_loader_add_search_path(loader, "/usr/local/lib/havel/extensions");

 const char *home = getenv("HOME");
 if (home) {
  char buf[512];
  snprintf(buf, sizeof(buf), "%s/.havel/extensions", home);
  havel_loader_add_search_path(loader, buf);
 }

 const char *ext_dir = getenv("HAVEL_EXTENSION_DIR");
 if (ext_dir) havel_loader_add_search_path(loader, ext_dir);
}

void havel_loader_add_module_paths(HavelLoader *loader) {
 if (!loader) return;
 havel_loader_add_search_path(loader, "modules");

 {
  char exe_path[512];
  ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
  if (len > 0) {
   exe_path[len] = '\0';
   char *last_slash = strrchr(exe_path, '/');
   if (last_slash) {
    *last_slash = '\0';
    char buf[768];
    snprintf(buf, sizeof(buf), "%s/modules", exe_path);
    havel_loader_add_search_path(loader, buf);
   }
  }
 }

 havel_loader_add_search_path(loader, "/usr/lib/havel/modules");
 havel_loader_add_search_path(loader, "/usr/local/lib/havel/modules");

 const char *home = getenv("HOME");
 if (home) {
  char buf[512];
  snprintf(buf, sizeof(buf), "%s/.havel/modules", home);
  havel_loader_add_search_path(loader, buf);
 }

  const char *mod_dir = getenv("HAVEL_MODULE_DIR");
  if (mod_dir) havel_loader_add_search_path(loader, mod_dir);
}



void havel_loader_add_toolkit_paths(HavelLoader *loader) {
 if (!loader) return;
 havel_loader_add_search_path(loader, "toolkits");
 havel_loader_add_search_path(loader, "/usr/lib/havel/toolkits");
 havel_loader_add_search_path(loader, "/usr/local/lib/havel/toolkits");

 const char *home = getenv("HOME");
 if (home) {
  char buf[512];
  snprintf(buf, sizeof(buf), "%s/.havel/toolkits", home);
  havel_loader_add_search_path(loader, buf);
 }

 const char *tk_dir = getenv("HAVEL_TOOLKIT_DIR");
 if (tk_dir) havel_loader_add_search_path(loader, tk_dir);
}

static int file_exists(const char *path) {
 struct stat st;
 return stat(path, &st) == 0;
}

static int file_mtime(const char *path, time_t *out) {
 struct stat st;
 if (stat(path, &st) != 0) return -1;
 *out = st.st_mtime;
 return 0;
}

static char *find_library(HavelLoader *loader, const char *name) {
 const char *suffix = platform_suffix();

 for (int i = 0; i < loader->search_path_count; i++) {
  const char *dir = loader->search_paths[i];
  char buf[768];

  snprintf(buf, sizeof(buf), "%s/%s%s", dir, name, suffix);
  if (file_exists(buf)) return strdup(buf);

  snprintf(buf, sizeof(buf), "%s/lib%s%s", dir, name, suffix);
  if (file_exists(buf)) return strdup(buf);
 }

 return NULL;
}

static void register_loaded(HavelLoader *loader, const char *name,
       const char *path, void *handle) {
 if (loader->loaded_count >= MAX_LOADED) return;
 HavelLoadedModule *mod = &loader->loaded[loader->loaded_count++];
 snprintf(mod->name, sizeof(mod->name), "%s", name);
 snprintf(mod->path, sizeof(mod->path), "%s", path);
 mod->handle = handle;
 mod->is_loaded = 1;
}

void *havel_loader_open(HavelLoader *loader, const char *path) {
 if (!path) return NULL;
 (void)loader;

 void *handle = dlopen(path, RTLD_LAZY | RTLD_LOCAL);
 if (!handle) {
  HAVEL_LOGF_ERROR("dlopen failed for %s: %s", path,
     dlerror() ? dlerror() : "unknown");
 }
 return handle;
}

void *havel_loader_load(HavelLoader *loader, const char *name) {
 if (!loader || !name) return NULL;

 for (int i = 0; i < loader->loaded_count; i++) {
  if (strcmp(loader->loaded[i].name, name) == 0 && loader->loaded[i].is_loaded)
   return loader->loaded[i].handle;
 }

 char *path = find_library(loader, name);
 if (!path) return NULL;

 void *handle = havel_loader_open(loader, path);
 if (!handle) { free(path); return NULL; }

 register_loaded(loader, name, path, handle);
 free(path);
 return handle;
}

void *havel_loader_sym(void *handle, const char *symbol) {
 if (!handle || !symbol) return NULL;

 dlerror();
 void *sym = dlsym(handle, symbol);
 if (!sym) {
  const char *err = dlerror();
  if (err) HAVEL_LOGF_ERROR("dlsym '%s': %s", symbol, err);
 }
 return sym;
}

void *havel_loader_attr(void *handle, const char *name) {
 return havel_loader_sym(handle, name);
}

void *havel_loader_import(HavelLoader *loader, const char *name, void *api) {
 if (!loader || !name) return NULL;

 for (int i = 0; i < loader->loaded_count; i++) {
  if (strcmp(loader->loaded[i].name, name) == 0 && loader->loaded[i].is_loaded)
   return loader->loaded[i].handle;
 }

 char *path = find_library(loader, name);
 if (!path) return NULL;

 void *handle = havel_loader_open(loader, path);
 if (!handle) { free(path); return NULL; }

 HavelModuleInitFn init_fn = (HavelModuleInitFn)havel_loader_sym(handle, "havel_extension_init");
 if (init_fn && api) init_fn(api);

 register_loaded(loader, name, path, handle);
 free(path);
 return handle;
}

int havel_loader_is_loaded(HavelLoader *loader, const char *name) {
 if (!loader || !name) return 0;
 for (int i = 0; i < loader->loaded_count; i++) {
  if (strcmp(loader->loaded[i].name, name) == 0 && loader->loaded[i].is_loaded)
   return 1;
 }
 return 0;
}

void *havel_loader_get_handle(HavelLoader *loader, const char *name) {
 if (!loader || !name) return NULL;
 for (int i = 0; i < loader->loaded_count; i++) {
  if (strcmp(loader->loaded[i].name, name) == 0 && loader->loaded[i].is_loaded)
   return loader->loaded[i].handle;
 }
 return NULL;
}

char **havel_loader_list_loaded(HavelLoader *loader, int *count) {
 if (!loader || !count) return NULL;
 *count = loader->loaded_count;

 char **names = (char **)malloc(sizeof(char *) * (loader->loaded_count > 0 ? loader->loaded_count : 1));
 if (!names) { *count = 0; return NULL; }

 for (int i = 0; i < loader->loaded_count; i++)
  names[i] = strdup(loader->loaded[i].name);
 return names;
}

void havel_loader_free_names(char **names, int count) {
 if (!names) return;
 for (int i = 0; i < count; i++) free(names[i]);
 free(names);
}

void havel_loader_close(void *handle) {
 if (handle) dlclose(handle);
}

const HavelModuleABI *havel_loader_load_module(HavelLoader *loader, const char *name) {
 if (!loader || !name) return NULL;

 char lib_name[300];
 snprintf(lib_name, sizeof(lib_name), "havel_mod_%s", name);

 for (int i = 0; i < loader->loaded_count; i++) {
  if (strcmp(loader->loaded[i].name, lib_name) == 0 && loader->loaded[i].is_loaded) {
   HavelModuleInfoFn info_fn = (HavelModuleInfoFn)havel_loader_sym(loader->loaded[i].handle, "havel_module_info");
   return info_fn ? info_fn() : NULL;
  }
 }

 char *path = find_library(loader, lib_name);
 if (!path) return NULL;

 void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
 if (!handle) {
  HAVEL_LOGF_ERROR("dlopen failed for module '%s': %s", name,
     dlerror() ? dlerror() : "unknown");
  free(path);
  return NULL;
 }

 HavelModuleInfoFn info_fn = (HavelModuleInfoFn)havel_loader_sym(handle, "havel_module_info");
 if (!info_fn) {
  HAVEL_LOGF_ERROR("module '%s': missing havel_module_info", name);
  free(path);
  return NULL;
 }

 const HavelModuleABI *abi = info_fn();
 if (!abi) {
  HAVEL_LOGF_ERROR("module '%s': havel_module_info returned NULL", name);
  free(path);
  return NULL;
 }

 if (abi->abi_version < 1 || abi->abi_version > HAVEL_MODULE_ABI_VERSION) {
 HAVEL_LOGF_ERROR("module '%s': ABI mismatch (got %d, need 1-%d)", name, abi->abi_version, HAVEL_MODULE_ABI_VERSION);
 free(path);
 return NULL;
 }

 register_loaded(loader, lib_name, path, handle);
 free(path);
 return abi;
}

const HavelToolkitABI *havel_loader_load_toolkit(HavelLoader *loader, const char *name) {
 if (!loader || !name) return NULL;

 char lib_name[300];
 snprintf(lib_name, sizeof(lib_name), "havel_toolkit_%s", name);

 for (int i = 0; i < loader->loaded_count; i++) {
  if (strcmp(loader->loaded[i].name, lib_name) == 0 && loader->loaded[i].is_loaded) {
   havel_toolkit_info_fn info_fn = (havel_toolkit_info_fn)havel_loader_sym(loader->loaded[i].handle, "havel_toolkit_info");
   return info_fn ? info_fn() : NULL;
  }
 }

 char *path = find_library(loader, lib_name);
 if (!path) return NULL;

 void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
 if (!handle) {
  HAVEL_LOGF_ERROR("dlopen failed for toolkit '%s': %s", name,
     dlerror() ? dlerror() : "unknown");
  free(path);
  return NULL;
 }

 havel_toolkit_info_fn info_fn = (havel_toolkit_info_fn)havel_loader_sym(handle, "havel_toolkit_info");
 if (!info_fn) {
  HAVEL_LOGF_ERROR("toolkit '%s': missing havel_toolkit_info", name);
  free(path);
  return NULL;
 }

 const HavelToolkitABI *abi = info_fn();
 if (!abi) {
  HAVEL_LOGF_ERROR("toolkit '%s': havel_toolkit_info returned NULL", name);
  free(path);
  return NULL;
 }

 if (abi->abi_version != HAVEL_TOOLKIT_ABI_VERSION) {
  HAVEL_LOGF_ERROR("toolkit '%s': ABI mismatch (got %d, need %d)",
     name, abi->abi_version, HAVEL_TOOLKIT_ABI_VERSION);
  free(path);
  return NULL;
 }

 register_loaded(loader, lib_name, path, handle);
 free(path);
 return abi;
}

void havel_loader_set_stdlib_path(HavelLoader *loader, const char *path) {
 if (!loader || !path) return;
 snprintf(loader->stdlib_path, sizeof(loader->stdlib_path), "%s", path);
}

const char *havel_loader_get_stdlib_path(const HavelLoader *loader) {
 if (!loader) return NULL;
 return loader->stdlib_path[0] ? loader->stdlib_path : NULL;
}

void havel_loader_add_script_path(HavelLoader *loader, const char *path) {
 if (!loader || !path) return;
 if (loader->script_path_count >= MAX_SCRIPT_PATHS) return;
 loader->script_paths[loader->script_path_count++] = strdup(path);
}

int havel_loader_get_script_path_count(const HavelLoader *loader) {
 if (!loader) return 0;
 return loader->script_path_count;
}

const char *havel_loader_get_script_path(const HavelLoader *loader, int index) {
 if (!loader || index < 0 || index >= loader->script_path_count) return NULL;
 return loader->script_paths[index];
}

int havel_loader_is_cached(HavelLoader *loader, const char *key) {
 if (!loader || !key) return 0;
 for (int i = 0; i < loader->cache_count; i++) {
  if (loader->cache[i].used && strcmp(loader->cache[i].key, key) == 0)
   return 1;
 }
 return 0;
}

void havel_loader_put_cache(HavelLoader *loader, const char *key, uint64_t value) {
 if (!loader || !key) return;
 for (int i = 0; i < loader->cache_count; i++) {
  if (loader->cache[i].used && strcmp(loader->cache[i].key, key) == 0) {
   loader->cache[i].value = value;
   return;
  }
 }
 if (loader->cache_count >= MAX_CACHE_ENTRIES) return;
 int idx = loader->cache_count++;
 snprintf(loader->cache[idx].key, sizeof(loader->cache[idx].key), "%s", key);
 loader->cache[idx].value = value;
 loader->cache[idx].used = 1;
}

int havel_loader_get_cache(HavelLoader *loader, const char *key, uint64_t *out_value) {
 if (!loader || !key || !out_value) return 0;
 for (int i = 0; i < loader->cache_count; i++) {
  if (loader->cache[i].used && strcmp(loader->cache[i].key, key) == 0) {
   *out_value = loader->cache[i].value;
   return 1;
  }
 }
 return 0;
}

void havel_loader_clear_cache(HavelLoader *loader) {
 if (!loader) return;
 for (int i = 0; i < loader->cache_count; i++)
  loader->cache[i].used = 0;
 loader->cache_count = 0;
}

int havel_loader_has_host_module(HavelLoader *loader, const char *name) {
 if (!loader || !name) return 0;
 for (int i = 0; i < loader->host_module_count; i++) {
  if (strcmp(loader->host_modules[i], name) == 0) return 1;
 }
 return 0;
}

void havel_loader_register_host_module(HavelLoader *loader, const char *name) {
 if (!loader || !name) return;
 if (havel_loader_has_host_module(loader, name)) return;
 if (loader->host_module_count >= MAX_HOST_MODULES) return;
 loader->host_modules[loader->host_module_count++] = strdup(name);
}

void havel_loader_mark_loaded(HavelLoader *loader, const char *name) {
 if (!loader || !name) return;
 if (havel_loader_was_loaded(loader, name)) return;
 if (loader->loaded_flag_count >= MAX_LOADED_FLAGS) return;
 snprintf(loader->loaded_flags[loader->loaded_flag_count], 256, "%s", name);
 loader->loaded_flag_count++;
}

int havel_loader_was_loaded(HavelLoader *loader, const char *name) {
 if (!loader || !name) return 0;
 for (int i = 0; i < loader->loaded_flag_count; i++) {
  if (strcmp(loader->loaded_flags[i], name) == 0) return 1;
 }
 return 0;
}

static int is_absolute_path(const char *path) {
 if (!path) return 0;
 return path[0] == '/';
}

static int is_relative_import(const char *path) {
 if (!path) return 0;
 return (path[0] == '.' && path[1] == '/') ||
        (path[0] == '.' && path[1] == '.' && path[2] == '/');
}

static int string_ends_with(const char *s, const char *suffix) {
 size_t slen = strlen(s);
 size_t suflen = strlen(suffix);
 if (suflen > slen) return 0;
 return strcmp(s + slen - suflen, suffix) == 0;
}

static int string_starts_with(const char *s, const char *prefix) {
 return strncmp(s, prefix, strlen(prefix)) == 0;
}

static char *extract_module_name(const char *filename, const char *prefix, const char *suffix) {
 size_t plen = strlen(prefix);
 size_t slen = strlen(suffix);
 size_t flen = strlen(filename);
 if (flen <= plen + slen) return NULL;
 if (!string_starts_with(filename, prefix)) return NULL;
 if (!string_ends_with(filename, suffix)) return NULL;
 size_t namelen = flen - plen - slen;
 char *name = (char *)malloc(namelen + 1);
 if (!name) return NULL;
 memcpy(name, filename + plen, namelen);
 name[namelen] = '\0';
 return name;
}

static void make_path(char *buf, size_t bufsz, const char *dir, const char *name, const char *ext) {
 if (dir && dir[0]) {
  if (name && name[0])
   snprintf(buf, bufsz, "%s/%s%s", dir, name, ext ? ext : "");
  else
   snprintf(buf, bufsz, "%s%s", dir, ext ? ext : "");
 } else {
  snprintf(buf, bufsz, "%s%s", name ? name : "", ext ? ext : "");
 }
}

static void replace_extension(char *buf, size_t bufsz, const char *path,
          const char *old_ext, const char *new_ext) {
 size_t len = strlen(path);
 size_t old_len = strlen(old_ext);
 size_t new_len = strlen(new_ext);
 if (len + new_len - old_len + 1 > bufsz) { buf[0] = 0; return; }
 if (string_ends_with(path, old_ext)) {
  size_t base_len = len - old_len;
  memcpy(buf, path, base_len);
  memcpy(buf + base_len, new_ext, new_len + 1);
 } else {
  snprintf(buf, bufsz, "%s%s", path, new_ext);
 }
}

static int pick_hv_or_hvc(const char *dir, const char *name,
       HavelResolvedModule *out, HavelSourceType hv_type) {
 char hv_path[1024];
 char hvc_path[1024];

 make_path(hv_path, sizeof(hv_path), dir, name, ".hv");
 make_path(hvc_path, sizeof(hvc_path), dir, name, ".hvc");

 int hv_exists = file_exists(hv_path);
 int hvc_exists = file_exists(hvc_path);

 if (hvc_exists && hv_exists) {
  time_t hvc_time, hv_time;
  int have_hvc_time = (file_mtime(hvc_path, &hvc_time) == 0);
  int have_hv_time = (file_mtime(hv_path, &hv_time) == 0);
  if (have_hvc_time && have_hv_time && hvc_time >= hv_time) {
   out->type = HAVEL_SOURCE_BYTECODE_CACHE;
   snprintf(out->resolved_path, sizeof(out->resolved_path), "%s", hvc_path);
   snprintf(out->original_name, sizeof(out->original_name), "%s", name);
   return 1;
  }
  out->type = hv_type;
  snprintf(out->resolved_path, sizeof(out->resolved_path), "%s", hv_path);
  snprintf(out->original_name, sizeof(out->original_name), "%s", name);
  return 1;
 }

 if (hvc_exists) {
  out->type = HAVEL_SOURCE_BYTECODE_CACHE;
  snprintf(out->resolved_path, sizeof(out->resolved_path), "%s", hvc_path);
  snprintf(out->original_name, sizeof(out->original_name), "%s", name);
  return 1;
 }

 if (hv_exists) {
  out->type = hv_type;
  snprintf(out->resolved_path, sizeof(out->resolved_path), "%s", hv_path);
  snprintf(out->original_name, sizeof(out->original_name), "%s", name);
  return 1;
 }

 return 0;
}

int havel_loader_resolve(HavelLoader *loader, const char *module_path,
    const char *script_dir, HavelResolvedModule *out) {
 if (!loader || !module_path || !out) return 0;
 memset(out, 0, sizeof(*out));

 const char *name = module_path;

 if (is_absolute_path(module_path)) {
  if (file_exists(module_path)) {
   out->type = HAVEL_SOURCE_USER;
   snprintf(out->resolved_path, sizeof(out->resolved_path), "%s", module_path);
   snprintf(out->original_name, sizeof(out->original_name), "%s", module_path);
   return 1;
  }
  char hvc_path[1024];
  replace_extension(hvc_path, sizeof(hvc_path), module_path, ".hv", ".hvc");
  if (file_exists(hvc_path)) {
   out->type = HAVEL_SOURCE_BYTECODE_CACHE;
   snprintf(out->resolved_path, sizeof(out->resolved_path), "%s", hvc_path);
   snprintf(out->original_name, sizeof(out->original_name), "%s", module_path);
   return 1;
  }
  return 0;
 }

 if (is_relative_import(module_path)) {
  char resolved[1024];
  make_path(resolved, sizeof(resolved), script_dir, module_path, NULL);
  if (file_exists(resolved)) {
   out->type = HAVEL_SOURCE_USER;
   snprintf(out->resolved_path, sizeof(out->resolved_path), "%s", resolved);
   snprintf(out->original_name, sizeof(out->original_name), "%s", module_path);
   return 1;
  }
  char hvc_path[1024];
  if (string_ends_with(module_path, ".hv")) {
   size_t len = strlen(module_path);
   memcpy(hvc_path, module_path, len - 3);
   memcpy(hvc_path + len - 3, ".hvc", 5);
   char full_hvc[1024];
   make_path(full_hvc, sizeof(full_hvc), script_dir, hvc_path, NULL);
   if (file_exists(full_hvc)) {
    out->type = HAVEL_SOURCE_BYTECODE_CACHE;
    snprintf(out->resolved_path, sizeof(out->resolved_path), "%s", full_hvc);
    snprintf(out->original_name, sizeof(out->original_name), "%s", module_path);
    return 1;
   }
  } else {
   char full_hvc[1024];
   make_path(full_hvc, sizeof(full_hvc), script_dir, module_path, ".hvc");
   if (file_exists(full_hvc)) {
    out->type = HAVEL_SOURCE_BYTECODE_CACHE;
    snprintf(out->resolved_path, sizeof(out->resolved_path), "%s", full_hvc);
    snprintf(out->original_name, sizeof(out->original_name), "%s", module_path);
    return 1;
   }
  }
  return 0;
 }

 if (havel_loader_is_cached(loader, name)) {
  out->type = HAVEL_SOURCE_CACHED;
  out->resolved_path[0] = 0;
  snprintf(out->original_name, sizeof(out->original_name), "%s", name);
  return 1;
 }

 if (script_dir && script_dir[0]) {
  if (pick_hv_or_hvc(script_dir, name, out, HAVEL_SOURCE_USER))
   return 1;

  char pkg_dir[1024];
  make_path(pkg_dir, sizeof(pkg_dir), script_dir, name, NULL);
  if (pick_hv_or_hvc(pkg_dir, name, out, HAVEL_SOURCE_USER))
   return 1;
 }

 if (script_dir && script_dir[0]) {
  char cache_path[1024];
  make_path(cache_path, sizeof(cache_path), script_dir, "__cache__", NULL);
  char hvc_file[1024];
  make_path(hvc_file, sizeof(hvc_file), cache_path, name, ".hvc");
  if (file_exists(hvc_file)) {
   out->type = HAVEL_SOURCE_BYTECODE_CACHE;
   snprintf(out->resolved_path, sizeof(out->resolved_path), "%s", hvc_file);
   snprintf(out->original_name, sizeof(out->original_name), "%s", name);
   return 1;
  }
  char hbc_file[1024];
  make_path(hbc_file, sizeof(hbc_file), cache_path, name, ".hbc");
  if (file_exists(hbc_file)) {
   out->type = HAVEL_SOURCE_BYTECODE_CACHE;
   snprintf(out->resolved_path, sizeof(out->resolved_path), "%s", hbc_file);
   snprintf(out->original_name, sizeof(out->original_name), "%s", name);
   return 1;
  }
 }

 if (loader->stdlib_path[0]) {
  if (pick_hv_or_hvc(loader->stdlib_path, name, out, HAVEL_SOURCE_STDLIB))
   return 1;
 }

 {
  const char *home = getenv("HOME");
  if (home) {
   char pkg_base[1024];
   snprintf(pkg_base, sizeof(pkg_base), "%s/.havel/packages", home);
   char pkg_dir[1024];
   make_path(pkg_dir, sizeof(pkg_dir), pkg_base, name, NULL);
   if (pick_hv_or_hvc(pkg_dir, name, out, HAVEL_SOURCE_PACKAGE))
    return 1;
  }
 }

 for (int i = 0; i < loader->script_path_count; i++) {
  const char *sp = loader->script_paths[i];
  if (pick_hv_or_hvc(sp, name, out, HAVEL_SOURCE_USER))
   return 1;

  char pkg_dir[1024];
  make_path(pkg_dir, sizeof(pkg_dir), sp, name, NULL);
  if (pick_hv_or_hvc(pkg_dir, name, out, HAVEL_SOURCE_USER))
   return 1;
 }

 const char *suffix = platform_suffix();
 for (int i = 0; i < loader->script_path_count; i++) {
  const char *sp = loader->script_paths[i];
  char so_path[1024];
  snprintf(so_path, sizeof(so_path), "%s/%s%s", sp, name, suffix);
  if (file_exists(so_path)) {
   out->type = HAVEL_SOURCE_NATIVE_EXTENSION;
   snprintf(out->resolved_path, sizeof(out->resolved_path), "%s", so_path);
   snprintf(out->original_name, sizeof(out->original_name), "%s", name);
   return 1;
  }
  snprintf(so_path, sizeof(so_path), "%s/libhavel_%s%s", sp, name, suffix);
  if (file_exists(so_path)) {
   out->type = HAVEL_SOURCE_NATIVE_EXTENSION;
   snprintf(out->resolved_path, sizeof(out->resolved_path), "%s", so_path);
   snprintf(out->original_name, sizeof(out->original_name), "%s", name);
   return 1;
  }
 }

 if (havel_loader_has_host_module(loader, name)) {
 out->type = HAVEL_SOURCE_HOST_BUILTIN;
 out->resolved_path[0] = 0;
 snprintf(out->original_name, sizeof(out->original_name), "%s", name);
 return 1;
 }

 return 0;
}

const HavelModuleABI *havel_loader_probe_module(HavelLoader *loader, const char *name,
 HavelModuleInfo *out_info) {
 if (!loader || !name) return NULL;

 char lib_name[300];
 snprintf(lib_name, sizeof(lib_name), "havel_mod_%s", name);

 for (int i = 0; i < loader->loaded_count; i++) {
 if (strcmp(loader->loaded[i].name, lib_name) == 0 && loader->loaded[i].is_loaded) {
 HavelModuleInfoFn info_fn = (HavelModuleInfoFn)havel_loader_sym(loader->loaded[i].handle, "havel_module_info");
 if (!info_fn) return NULL;
 const HavelModuleABI *abi = info_fn();
 if (!abi || abi->abi_version < 2) return abi;
 if (out_info) {
 snprintf(out_info->name, sizeof(out_info->name), "%s", abi->name ? abi->name : name);
 out_info->alias_count = 0;
 for (int j = 0; j < 8 && abi->aliases[j]; j++) {
 snprintf(out_info->aliases[j], sizeof(out_info->aliases[j]), "%s", abi->aliases[j]);
 out_info->alias_count++;
 }
 out_info->eager = abi->eager;
 }
 return abi;
 }
 }

 char *path = find_library(loader, lib_name);
 if (!path) return NULL;

 void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
 if (!handle) {
 free(path);
 return NULL;
 }

 HavelModuleInfoFn info_fn = (HavelModuleInfoFn)havel_loader_sym(handle, "havel_module_info");
 if (!info_fn) {
 dlclose(handle);
 free(path);
 return NULL;
 }

 const HavelModuleABI *abi = info_fn();
 if (!abi) {
 dlclose(handle);
 free(path);
 return NULL;
 }

 if (abi->abi_version != HAVEL_MODULE_ABI_VERSION) {
 dlclose(handle);
 free(path);
 return NULL;
 }

 register_loaded(loader, lib_name, path, handle);
 free(path);

 if (out_info) {
 snprintf(out_info->name, sizeof(out_info->name), "%s", abi->name ? abi->name : name);
 out_info->alias_count = 0;
 for (int j = 0; j < 8 && abi->aliases[j]; j++) {
 snprintf(out_info->aliases[j], sizeof(out_info->aliases[j]), "%s", abi->aliases[j]);
 out_info->alias_count++;
 }
 out_info->eager = abi->eager;
 }

 return abi;
}

int havel_loader_scan_modules(HavelLoader *loader, HavelModuleInfo *out, int max_out) {
 if (!loader || !out || max_out <= 0) return 0;

 const char *suffix = platform_suffix();
 const char *prefix = "havel_mod_";
 int count = 0;

 for (int si = 0; si < loader->search_path_count && count < max_out; si++) {
 const char *dir = loader->search_paths[si];
 DIR *d = opendir(dir);
 if (!d) continue;

 struct dirent *entry;
 while ((entry = readdir(d)) != NULL && count < max_out) {
 char *mod_name = extract_module_name(entry->d_name, prefix, suffix);
 if (!mod_name) continue;

 int already = 0;
 for (int k = 0; k < count; k++) {
 if (strcmp(out[k].name, mod_name) == 0) { already = 1; break; }
 }
 if (already) { free(mod_name); continue; }

 HavelModuleInfo info;
 const HavelModuleABI *abi = havel_loader_probe_module(loader, mod_name, &info);
 if (abi) {
 out[count] = info;
 count++;
 }
 free(mod_name);
 }
 closedir(d);
 }

 return count;
}
