/*
 * Loader.c - Pure C dynamic library loader
 *
 * Python-style module loading via dlopen/dlsym.
 * No C++ dependencies. No STL. Pure POSIX.
 */

#include "Loader.h"
#include "ModulePlugin.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define HAVEL_MAX_SEARCH_PATHS 64
#define HAVEL_MAX_LOADED 128

struct HavelLoader {
    char *search_paths[HAVEL_MAX_SEARCH_PATHS];
    int search_path_count;
    HavelLoadedModule loaded[HAVEL_MAX_LOADED];
    int loaded_count;
};

static const char *suffix_platform(void) {
#if defined(_WIN32)
    return ".dll";
#elif defined(__APPLE__)
    return ".dylib";
#else
    return ".so";
#endif
}

const char *havel_loader_suffix(void) {
    return suffix_platform();
}

HavelLoader *havel_loader_create(void) {
    HavelLoader *l = (HavelLoader *)calloc(1, sizeof(HavelLoader));
    if (!l) return NULL;
    havel_loader_add_standard_paths(l);
    return l;
}

void havel_loader_destroy(HavelLoader *loader) {
    if (!loader) return;
    for (int i = 0; i < loader->search_path_count; i++) {
        free(loader->search_paths[i]);
    }
    /* Do NOT dlclose handles - extensions stay loaded until process exit.
     * dlclose + dangling function pointers = crash.
     * Python does the same. */
    free(loader);
}

const char *havel_loader_error(void) {
    return dlerror();
}

void havel_loader_add_search_path(HavelLoader *loader, const char *path) {
    if (!loader || !path) return;
    if (loader->search_path_count >= HAVEL_MAX_SEARCH_PATHS) return;
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
    if (ext_dir) {
        havel_loader_add_search_path(loader, ext_dir);
    }
}

void havel_loader_add_module_paths(HavelLoader *loader) {
    if (!loader) return;

    havel_loader_add_search_path(loader, "modules");

    havel_loader_add_search_path(loader, "/usr/lib/havel/modules");
    havel_loader_add_search_path(loader, "/usr/local/lib/havel/modules");

    const char *home = getenv("HOME");
    if (home) {
        char buf[512];
        snprintf(buf, sizeof(buf), "%s/.havel/modules", home);
        havel_loader_add_search_path(loader, buf);
    }

    const char *mod_dir = getenv("HAVEL_MODULE_DIR");
    if (mod_dir) {
        havel_loader_add_search_path(loader, mod_dir);
    }
}

static int file_exists(const char *path) {
    return access(path, F_OK) == 0;
}

static char *find_library(HavelLoader *loader, const char *name) {
    const char *suffix = suffix_platform();

    for (int i = 0; i < loader->search_path_count; i++) {
        const char *dir = loader->search_paths[i];
        char buf[768];

        /* Try: dir/name.so */
        snprintf(buf, sizeof(buf), "%s/%s%s", dir, name, suffix);
        if (file_exists(buf)) return strdup(buf);

        /* Try: dir/libname.so */
        snprintf(buf, sizeof(buf), "%s/lib%s%s", dir, name, suffix);
        if (file_exists(buf)) return strdup(buf);
    }

    return NULL;
}

void *havel_loader_open(HavelLoader *loader, const char *path) {
    if (!path) return NULL;
    (void)loader;

    void *handle = dlopen(path, RTLD_LAZY | RTLD_LOCAL);
    if (!handle) {
        fprintf(stderr, "[Loader] dlopen failed for %s: %s\n",
                path, dlerror() ? dlerror() : "unknown");
    }
    return handle;
}

void *havel_loader_load(HavelLoader *loader, const char *name) {
    if (!loader || !name) return NULL;

    /* Already loaded? */
    for (int i = 0; i < loader->loaded_count; i++) {
        if (strcmp(loader->loaded[i].name, name) == 0 && loader->loaded[i].is_loaded) {
            return loader->loaded[i].handle;
        }
    }

    char *path = find_library(loader, name);
    if (!path) {
        fprintf(stderr, "[Loader] library '%s' not found in search paths\n", name);
        return NULL;
    }

    void *handle = havel_loader_open(loader, path);
    if (!handle) {
        free(path);
        return NULL;
    }

    /* Register loaded module */
    if (loader->loaded_count < HAVEL_MAX_LOADED) {
        HavelLoadedModule *mod = &loader->loaded[loader->loaded_count++];
        snprintf(mod->name, sizeof(mod->name), "%s", name);
        snprintf(mod->path, sizeof(mod->path), "%s", path);
        mod->handle = handle;
        mod->is_loaded = 1;
    }

    free(path);
    return handle;
}

void *havel_loader_sym(void *handle, const char *symbol) {
    if (!handle || !symbol) return NULL;

    dlerror(); /* clear */
    void *sym = dlsym(handle, symbol);
    if (!sym) {
        const char *err = dlerror();
        if (err) {
            fprintf(stderr, "[Loader] dlsym failed for '%s': %s\n", symbol, err);
        }
    }
    return sym;
}

void *havel_loader_import(HavelLoader *loader, const char *name, void *api) {
    if (!loader || !name) return NULL;

    /* Already loaded? */
    for (int i = 0; i < loader->loaded_count; i++) {
        if (strcmp(loader->loaded[i].name, name) == 0 && loader->loaded[i].is_loaded) {
            return loader->loaded[i].handle;
        }
    }

    char *path = find_library(loader, name);
    if (!path) {
        fprintf(stderr, "[Loader] module '%s' not found in search paths\n", name);
        return NULL;
    }

    void *handle = havel_loader_open(loader, path);
    if (!handle) {
        free(path);
        return NULL;
    }

    /* Try to call havel_extension_init if present */
    HavelModuleInitFn init_fn = (HavelModuleInitFn)havel_loader_sym(handle, "havel_extension_init");
    if (init_fn && api) {
        init_fn(api);
        fprintf(stderr, "[Loader] module '%s' initialized\n", name);
    }

    /* Register loaded module */
    if (loader->loaded_count < HAVEL_MAX_LOADED) {
        HavelLoadedModule *mod = &loader->loaded[loader->loaded_count++];
        snprintf(mod->name, sizeof(mod->name), "%s", name);
        snprintf(mod->path, sizeof(mod->path), "%s", path);
        mod->handle = handle;
        mod->is_loaded = 1;
    }

    free(path);
    return handle;
}

int havel_loader_is_loaded(HavelLoader *loader, const char *name) {
    if (!loader || !name) return 0;
    for (int i = 0; i < loader->loaded_count; i++) {
        if (strcmp(loader->loaded[i].name, name) == 0 && loader->loaded[i].is_loaded) {
            return 1;
        }
    }
    return 0;
}

void *havel_loader_get_handle(HavelLoader *loader, const char *name) {
    if (!loader || !name) return NULL;
    for (int i = 0; i < loader->loaded_count; i++) {
        if (strcmp(loader->loaded[i].name, name) == 0 && loader->loaded[i].is_loaded) {
            return loader->loaded[i].handle;
        }
    }
    return NULL;
}

char **havel_loader_list_loaded(HavelLoader *loader, int *count) {
    if (!loader || !count) return NULL;
    *count = loader->loaded_count;

    char **names = (char **)malloc(sizeof(char *) * (loader->loaded_count > 0 ? loader->loaded_count : 1));
    if (!names) { *count = 0; return NULL; }

    for (int i = 0; i < loader->loaded_count; i++) {
        names[i] = strdup(loader->loaded[i].name);
    }
    return names;
}

void havel_loader_free_names(char **names, int count) {
    if (!names) return;
    for (int i = 0; i < count; i++) {
        free(names[i]);
    }
    free(names);
}

void havel_loader_close(void *handle) {
    if (handle) {
        dlclose(handle);
    }
}

const HavelModuleABI *havel_loader_load_module(HavelLoader *loader, const char *name) {
    if (!loader || !name) return NULL;

    char lib_name[300];
    snprintf(lib_name, sizeof(lib_name), "havel_mod_%s", name);

    void *handle = havel_loader_load(loader, lib_name);
    if (!handle) return NULL;

    HavelModuleInfoFn info_fn = (HavelModuleInfoFn)havel_loader_sym(handle, "havel_module_info");
    if (!info_fn) {
        fprintf(stderr, "[Loader] module '%s': missing havel_module_info symbol\n", name);
        return NULL;
    }

    const HavelModuleABI *abi = info_fn();
    if (!abi) {
        fprintf(stderr, "[Loader] module '%s': havel_module_info returned NULL\n", name);
        return NULL;
    }

    if (abi->abi_version != HAVEL_MODULE_ABI_VERSION) {
        fprintf(stderr, "[Loader] module '%s': ABI version mismatch (got %d, need %d)\n",
                name, abi->abi_version, HAVEL_MODULE_ABI_VERSION);
        return NULL;
    }

    return abi;
}
