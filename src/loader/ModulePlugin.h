#pragma once

/*
 * ModulePlugin.h - C ABI contract for Havel module plugins
 *
 * Every module .so must export:
 * 1. havel_module_info() - returns static metadata + ABI version
 * 2. havel_module_register() - called once on first use, registers host functions
 *
 * The register_fn receives an opaque void* which the C++ Loader.hpp
 * casts to VMApi*. This keeps the C linkage clean for dlopen/dlsym
 * while allowing C++ modules to use the full VMApi interface.
 *
 * Loader validates ABI version before calling register.
 * If ABI mismatches, the module is silently skipped.
 *
 * Naming convention: havel_mod_<name>.so
 * e.g. havel_mod_log.so, havel_mod_fs.so, havel_mod_config.so
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HAVEL_MODULE_ABI_VERSION 1

typedef struct HavelModuleABI {
    int abi_version;
    const char *name;
    const char *version;
    const char *description;
    void (*register_fn)(void *vmapi);
    void (*cleanup_fn)(void);
} HavelModuleABI;

typedef const HavelModuleABI *(*HavelModuleInfoFn)(void);
typedef void (*HavelModuleRegisterFn)(void *vmapi);

#ifdef __cplusplus
}
#endif

#ifdef HAVEL_MODULE_PLUGIN
#ifdef __cplusplus

#include "havel-lang/compiler/vm/VMApi.hpp"

#define HAVEL_MODULE_PLUGIN_IMPL(name, version_str, description_str, ...) \
extern "C" void havel_module_register(void *vmapi_ptr); \
static const HavelModuleABI havel_mod_abi_##name = { \
    HAVEL_MODULE_ABI_VERSION, \
    #name, \
    version_str, \
    description_str, \
    havel_module_register, \
    nullptr \
}; \
extern "C" const HavelModuleABI *havel_module_info(void) { \
    return &havel_mod_abi_##name; \
} \
extern "C" void havel_module_register(void *vmapi_ptr) { \
    auto *api = static_cast<havel::compiler::VMApi*>(vmapi_ptr); \
    if (api) { \
        __VA_ARGS__ \
    } \
}

#endif
#endif
