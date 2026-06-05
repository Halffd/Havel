/*
 * Loader.c - Pure C dynamic library loader
 *
 * Python-style module loading via dlopen/dlsym:
 *   havel_loader_load(l, "foo")    -> finds libfoo.so / foo.so
 *   havel_loader_sym(h, "bar")     -> get symbol from loaded lib
 *   havel_loader_import(l, "x", a) -> find, dlopen, call init
 *   havel_loader_attr(h, "method") -> python-style: resolve symbol
 *
 * Module plugins: havel_mod_<name>.so with ABI validation.
 * Toolkit plugins: havel_toolkit_<name>.so with ABI validation.
 *
 * No C++ dependencies. No STL. Pure POSIX.
 */

#include "Loader.h"
#include "ModulePlugin.h"
#include "ToolkitPlugin.h"
#include "LoggerC.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_SEARCH_PATHS 64
#define MAX_LOADED 128

struct HavelLoader {
 char *search_paths[MAX_SEARCH_PATHS];
 int search_path_count;
 HavelLoadedModule loaded[MAX_LOADED];
 int loaded_count;
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
 // do NOT dlclose — dangling function pointers from unloaded libs
 // cause crashes. python does the same.
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

static int file_exists(const char *path) {
 return access(path, F_OK) == 0;
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

 void *handle = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
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

 if (abi->abi_version != HAVEL_MODULE_ABI_VERSION) {
  HAVEL_LOGF_ERROR("module '%s': ABI mismatch (got %d, need %d)",
                    name, abi->abi_version, HAVEL_MODULE_ABI_VERSION);
  free(path);
  return NULL;
 }

 register_loaded(loader, lib_name, path, handle);
 free(path);
 return abi;
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

 void *handle = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
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
