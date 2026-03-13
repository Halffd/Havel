// ModuleMacros.hpp
// Convenience macros for module registration
// Makes module registration clean and consistent

#pragma once
#include "ModuleRegistry.hpp"

// ============================================================================
// Standard Library Module Registration
// ============================================================================

/**
 * Register a standard library module (no host dependencies)
 * 
 * Example:
 *   STD_MODULE(array) {
 *       env.Define("push", BuiltinFunction(...));
 *       env.Define("pop", BuiltinFunction(...));
 *   }
 */
#define STD_MODULE(name) \
    static void register##name##Module(havel::Environment& env); \
    REGISTER_MODULE(name, #name " standard library", register##name##Module); \
    static void register##name##Module(havel::Environment& env)

/**
 * Register a standard library module with description
 */
#define STD_MODULE_DESC(name, desc) \
    static void register##name##Module(havel::Environment& env); \
    REGISTER_MODULE(name, desc, register##name##Module); \
    static void register##name##Module(havel::Environment& env)

// ============================================================================
// Host Module Registration
// ============================================================================

/**
 * Register a host module (requires host APIs)
 * 
 * Example:
 *   HOST_MODULE(window) {
 *       env.Define("focus", BuiltinFunction([&hostAPI](...) {
 *           hostAPI->FocusWindow(...);
 *       }));
 *   }
 */
#define HOST_MODULE(name) \
    static void register##name##Module(havel::Environment& env, havel::IHostAPI* hostAPI); \
    REGISTER_HOST_MODULE(name, #name " host module", register##name##Module); \
    static void register##name##Module(havel::Environment& env, havel::IHostAPI* hostAPI)

/**
 * Register a host module with description
 */
#define HOST_MODULE_DESC(name, desc) \
    static void register##name##Module(havel::Environment& env, havel::IHostAPI* hostAPI); \
    REGISTER_HOST_MODULE(name, desc, register##name##Module); \
    static void register##name##Module(havel::Environment& env, havel::IHostAPI* hostAPI)

// ============================================================================
// Module with Dependencies
// ============================================================================

/**
 * Register a module with dependencies
 * 
 * Example:
 *   STD_MODULE_DEPS(json, (array, string)) {
 *       // Can use array and string functions
 *   }
 */
#define STD_MODULE_DEPS(name, deps) \
    static void register##name##Module(havel::Environment& env); \
    REGISTER_MODULE_WITH_DEPS(name, #name " standard library", register##name##Module, deps); \
    static void register##name##Module(havel::Environment& env)

/**
 * Register a host module with dependencies
 */
#define HOST_MODULE_DEPS(name, deps) \
    static void register##name##Module(havel::Environment& env, havel::IHostAPI* hostAPI); \
    namespace { \
    struct name##ModuleRegistrar { \
        name##ModuleRegistrar() { \
            havel::ModuleRegistry::RegisterHost(#name, #name " host module", register##name##Module, true, deps); \
        } \
    }; \
    static name##ModuleRegistrar name##Registrar; \
    } \
    static void register##name##Module(havel::Environment& env, havel::IHostAPI* hostAPI)

// ============================================================================
// Module Loading Helpers
// ============================================================================

/**
 * Load a module and check result
 * 
 * Example:
 *   LOAD_MODULE(env, "array");
 *   LOAD_HOST_MODULE(env, hostAPI, "window");
 */
#define LOAD_MODULE(env, name) \
    if (!havel::ModuleRegistry::Load(env, #name)) { \
        havel::error("Failed to load module: " #name); \
    }

#define LOAD_HOST_MODULE(env, hostAPI, name) \
    if (!havel::ModuleRegistry::Load(env, #name, hostAPI)) { \
        havel::error("Failed to load host module: " #name); \
    }

/**
 * Load all standard library modules
 */
#define LOAD_ALL_STD_MODULES(env, hostAPI) \
    havel::ModuleRegistry::LoadAll(env, hostAPI)

/**
 * Load all host modules
 */
#define LOAD_ALL_HOST_MODULES(env, hostAPI) \
    havel::ModuleRegistry::LoadAllHost(env, hostAPI)

// ============================================================================
// Module Entry Points
// ============================================================================

/**
 * Declare module initialization function
 * Use in header files
 */
#define DECLARE_STD_MODULE(name) \
    void register##name##Module(havel::Environment& env)

#define DECLARE_HOST_MODULE(name) \
    void register##name##Module(havel::Environment& env, havel::IHostAPI* hostAPI)
