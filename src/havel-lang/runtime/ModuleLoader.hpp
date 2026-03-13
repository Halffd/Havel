// ModuleLoader.hpp
// Simple, explicit module loading system
// No singletons, no static registration, no macro magic

#pragma once
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <stdexcept>

namespace havel {

class Environment;
class IHostAPI;

/**
 * Module registration function
 */
using ModuleFn = std::function<void(Environment&)>;

/**
 * Host module registration function (has IHostAPI access)
 */
using HostModuleFn = std::function<void(Environment&, IHostAPI*)>;

/**
 * Simple module registry - NOT a singleton
 * Each Interpreter can have its own registry
 */
class ModuleLoader {
public:
    ModuleLoader() = default;  // Public constructor
    
    /**
     * Register a standard library module
     */
    void add(const std::string& name, ModuleFn fn) {
        modules[name] = fn;
        hostModules[name] = false;
    }
    
    /**
     * Register a host module
     */
    void addHost(const std::string& name, HostModuleFn fn) {
        modules[name] = [fn](Environment& env) {
            // Wrapper - will fail at load time if hostAPI not provided
        };
        hostModules[name] = true;
        hostFns[name] = fn;
    }
    
    /**
     * Load a module
     */
    bool load(Environment& env, const std::string& name, IHostAPI* hostAPI = nullptr) {
        auto it = modules.find(name);
        if (it == modules.end()) {
            return false;
        }

        // Check if host module
        if (hostModules[name]) {
            if (!hostAPI) {
                throw std::runtime_error("Host module '" + name + "' requires host API");
            }
            auto hostIt = hostFns.find(name);
            if (hostIt != hostFns.end()) {
                hostIt->second(env, hostAPI);
            }
        } else {
            it->second(env);
        }

        loaded.insert(name);
        return true;
    }
    
    /**
     * Load all registered modules
     */
    void loadAll(Environment& env, IHostAPI* hostAPI = nullptr) {
        for (const auto& [name, fn] : modules) {
            load(env, name, hostAPI);
        }
    }
    
    /**
     * Check if module exists
     */
    bool has(const std::string& name) const {
        return modules.count(name) > 0;
    }
    
    /**
     * Check if module is loaded
     */
    bool isLoaded(const std::string& name) const {
        return loaded.count(name) > 0;
    }
    
    /**
     * Get list of all modules
     */
    std::vector<std::string> list() const {
        std::vector<std::string> names;
        for (const auto& [name, fn] : modules) {
            names.push_back(name);
        }
        return names;
    }
    
    /**
     * Clear loaded state (for testing)
     */
    void clearLoaded() {
        loaded.clear();
    }
    
private:
    std::unordered_map<std::string, ModuleFn> modules;
    std::unordered_map<std::string, HostModuleFn> hostFns;
    std::unordered_map<std::string, bool> hostModules;
    std::unordered_set<std::string> loaded;
};

} // namespace havel
