// ModuleRegistry.hpp
// Clean module registration and loading system
// Separates language core from host modules

#pragma once
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <unordered_map>
#include <algorithm>

namespace havel {

// Forward declarations
class Environment;
class IHostAPI;

/**
 * Module initialization function type
 */
using ModuleInitFn = std::function<void(Environment&)>;

/**
 * Host module initialization function type (has access to host APIs)
 */
using HostModuleInitFn = std::function<void(Environment&, IHostAPI*)>;

/**
 * Module metadata
 */
struct ModuleInfo {
    std::string name;
    std::string description;
    bool isHostModule;  // true if requires host APIs
    bool autoLoad;      // true if should be loaded by default
    std::vector<std::string> dependencies;
    
    ModuleInitFn initFn;         // For standard library modules
    HostModuleInitFn hostInitFn; // For host modules
};

/**
 * Module Registry - Central registration and loading system
 * 
 * Usage:
 *   // Register standard library module
 *   ModuleRegistry::Register("array", "Array operations", registerArrayModule);
 *   
 *   // Register host module
 *   ModuleRegistry::RegisterHost("window", "Window management", registerWindowModule);
 *   
 *   // Load modules
 *   ModuleRegistry::LoadAll(env);                    // Load stdlib
 *   ModuleRegistry::LoadAllHost(env, hostAPI);       // Load host modules
 *   ModuleRegistry::Load(env, "array");             // Load specific module
 */
class ModuleRegistry {
public:
    /**
     * Register a standard library module (no host dependencies)
     */
    static void Register(
        const std::string& name,
        const std::string& description,
        ModuleInitFn initFn,
        bool autoLoad = true,
        std::vector<std::string> dependencies = {}
    ) {
        auto& registry = getInstance();
        
        ModuleInfo info;
        info.name = name;
        info.description = description;
        info.isHostModule = false;
        info.autoLoad = autoLoad;
        info.dependencies = std::move(dependencies);
        info.initFn = std::move(initFn);
        
        registry.modules[name] = std::move(info);
    }
    
    /**
     * Register a host module (requires host APIs)
     */
    static void RegisterHost(
        const std::string& name,
        const std::string& description,
        HostModuleInitFn initFn,
        bool autoLoad = true,
        std::vector<std::string> dependencies = {}
    ) {
        auto& registry = getInstance();
        
        ModuleInfo info;
        info.name = name;
        info.description = description;
        info.isHostModule = true;
        info.autoLoad = autoLoad;
        info.dependencies = std::move(dependencies);
        info.hostInitFn = std::move(initFn);
        
        registry.modules[name] = std::move(info);
    }
    
    /**
     * Load a specific module
     */
    static bool Load(Environment& env, const std::string& name, IHostAPI* hostAPI = nullptr) {
        auto& registry = getInstance();
        
        auto it = registry.modules.find(name);
        if (it == registry.modules.end()) {
            return false;
        }
        
        // Check if already loaded
        if (registry.loadedModules.count(name) > 0) {
            return true;  // Already loaded
        }
        
        auto& info = it->second;
        
        // Load dependencies first
        for (const auto& dep : info.dependencies) {
            if (!Load(env, dep, hostAPI)) {
                return false;  // Dependency failed to load
            }
        }
        
        // Initialize module
        if (info.isHostModule) {
            if (!hostAPI) {
                return false;  // Host module requires host API
            }
            info.hostInitFn(env, hostAPI);
        } else {
            info.initFn(env);
        }
        
        // Mark as loaded
        registry.loadedModules.insert(name);
        
        return true;
    }
    
    /**
     * Load all auto-load modules (standard library)
     */
    static void LoadAll(Environment& env, IHostAPI* hostAPI = nullptr) {
        auto& registry = getInstance();
        
        // Collect all auto-load modules
        std::vector<std::string> toLoad;
        for (const auto& [name, info] : registry.modules) {
            if (info.autoLoad && !info.isHostModule) {
                toLoad.push_back(name);
            }
        }
        
        // Sort for deterministic load order
        std::sort(toLoad.begin(), toLoad.end());
        
        // Load each module
        for (const auto& name : toLoad) {
            Load(env, name, hostAPI);
        }
    }
    
    /**
     * Load all host modules
     */
    static void LoadAllHost(Environment& env, IHostAPI* hostAPI) {
        if (!hostAPI) return;
        
        auto& registry = getInstance();
        
        // Collect all host modules
        std::vector<std::string> toLoad;
        for (const auto& [name, info] : registry.modules) {
            if (info.isHostModule && info.autoLoad) {
                toLoad.push_back(name);
            }
        }
        
        // Sort for deterministic load order
        std::sort(toLoad.begin(), toLoad.end());
        
        // Load each module
        for (const auto& name : toLoad) {
            Load(env, name, hostAPI);
        }
    }
    
    /**
     * Check if module is registered
     */
    static bool IsRegistered(const std::string& name) {
        auto& registry = getInstance();
        return registry.modules.count(name) > 0;
    }
    
    /**
     * Check if module is loaded
     */
    static bool IsLoaded(const std::string& name) {
        auto& registry = getInstance();
        return registry.loadedModules.count(name) > 0;
    }
    
    /**
     * Get list of all registered modules
     */
    static std::vector<std::string> GetModuleList() {
        auto& registry = getInstance();
        std::vector<std::string> names;
        for (const auto& [name, info] : registry.modules) {
            names.push_back(name);
        }
        std::sort(names.begin(), names.end());
        return names;
    }
    
    /**
     * Get module info
     */
    static const ModuleInfo* GetModuleInfo(const std::string& name) {
        auto& registry = getInstance();
        auto it = registry.modules.find(name);
        if (it == registry.modules.end()) {
            return nullptr;
        }
        return &it->second;
    }
    
    /**
     * Clear loaded modules (for testing/reloading)
     */
    static void ClearLoaded() {
        auto& registry = getInstance();
        registry.loadedModules.clear();
    }
    
private:
    // Singleton pattern
    static ModuleRegistry& getInstance() {
        static ModuleRegistry instance;
        return instance;
    }
    
    ModuleRegistry() = default;
    
    std::unordered_map<std::string, ModuleInfo> modules;
    std::unordered_set<std::string> loadedModules;
};

} // namespace havel

// ============================================================================
// Convenience macros for module registration
// ============================================================================

/**
 * Register a standard library module
 * 
 * Usage in module .cpp file:
 *   REGISTER_MODULE(array, "Array operations", registerArrayModule);
 */
#define REGISTER_MODULE(name, description, initFn) \
    namespace { \
    struct name##ModuleRegistrar { \
        name##ModuleRegistrar() { \
            havel::ModuleRegistry::Register(#name, description, initFn); \
        } \
    }; \
    static name##ModuleRegistrar name##Registrar; \
    }

/**
 * Register a host module
 * 
 * Usage in module .cpp file:
 *   REGISTER_HOST_MODULE(window, "Window management", registerWindowModule);
 */
#define REGISTER_HOST_MODULE(name, description, initFn) \
    namespace { \
    struct name##ModuleRegistrar { \
        name##ModuleRegistrar() { \
            havel::ModuleRegistry::RegisterHost(#name, description, initFn); \
        } \
    }; \
    static name##ModuleRegistrar name##Registrar; \
    }

/**
 * Register a module with dependencies
 */
#define REGISTER_MODULE_WITH_DEPS(name, description, initFn, ...) \
    namespace { \
    struct name##ModuleRegistrar { \
        name##ModuleRegistrar() { \
            havel::ModuleRegistry::Register(#name, description, initFn, true, __VA_ARGS__); \
        } \
    }; \
    static name##ModuleRegistrar name##Registrar; \
    }
