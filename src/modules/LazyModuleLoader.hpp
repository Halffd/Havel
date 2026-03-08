/*
 * LazyModuleLoader.hpp
 *
 * Lazy-loading wrapper for host modules.
 * Modules are only loaded when first accessed, reducing startup time and memory.
 */
#pragma once

#include "../havel-lang/runtime/Environment.hpp"
#include "../havel-lang/runtime/Interpreter.hpp"
#include <unordered_map>
#include <functional>
#include <mutex>

namespace havel::modules {

/**
 * LazyModuleLoader - Defers module loading until first use
 * 
 * Instead of loading all modules at startup, modules are registered
 * with a loader function that's called only when the module is accessed.
 * 
 * Usage:
 *   LazyModuleLoader loader(env, interpreter);
 *   loader.registerLazy("screenshot", registerScreenshotModule);
 *   // Module loads only when user calls screenshot.full() etc.
 */
class LazyModuleLoader {
public:
    using ModuleLoaderFn = std::function<void(Environment&, HostContext&)>;
    
    explicit LazyModuleLoader(Environment& env, Interpreter* interpreter)
        : env_(env), interpreter_(interpreter), loaded_(false) {}
    
    /**
     * Register a module for lazy loading
     * @param name Module name (e.g., "screenshot", "pixel")
     * @param loaderFn Function that loads the module
     */
    void registerLazy(const std::string& name, ModuleLoaderFn loaderFn) {
        std::lock_guard<std::mutex> lock(mutex_);
        lazyModules_[name] = loaderFn;
    }
    
    /**
     * Load a specific module immediately
     * @param name Module name to load
     * @return true if loaded, false if already loaded or not found
     */
    bool loadModule(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = lazyModules_.find(name);
        if (it == lazyModules_.end()) {
            return false;  // Module not registered
        }
        
        if (loadedModules_.count(name)) {
            return false;  // Already loaded
        }
        
        // Load the module
        HostContext ctx = interpreter_->getHostContext();
        it->second(env_, ctx);
        loadedModules_.insert(name);
        
        return true;
    }
    
    /**
     * Check if a module is loaded
     */
    bool isLoaded(const std::string& name) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return loadedModules_.count(name) > 0;
    }
    
    /**
     * Load all registered modules (eager loading)
     */
    void loadAll() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (loaded_) return;
        
        HostContext ctx = interpreter_->getHostContext();
        for (const auto& [name, loaderFn] : lazyModules_) {
            if (!loadedModules_.count(name)) {
                loaderFn(env_, ctx);
                loadedModules_.insert(name);
            }
        }
        loaded_ = true;
    }
    
    /**
     * Create a lazy-loading wrapper for a module object
     * When any method is accessed, the module is loaded first
     */
    HavelValue createLazyWrapper(const std::string& moduleName, 
                                  ModuleLoaderFn loaderFn) {
        // Register for lazy loading
        registerLazy(moduleName, loaderFn);
        
        // Create a proxy object that loads the module on first access
        auto proxyObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
        
        // The __call__ method triggers loading
        (*proxyObj)["__load__"] = HavelValue(BuiltinFunction([this, moduleName](const std::vector<HavelValue>&) -> HavelResult {
            loadModule(moduleName);
            return HavelValue(nullptr);
        }));
        
        // The __get__ method triggers loading and forwards property access
        (*proxyObj)["__get__"] = HavelValue(BuiltinFunction([this, moduleName](const std::vector<HavelValue>& args) -> HavelResult {
            loadModule(moduleName);
            // After loading, the actual module object should be in the environment
            auto moduleVal = env_.Get(moduleName);
            if (moduleVal && moduleVal->isObject()) {
                return *moduleVal;
            }
            return HavelRuntimeError("Module '" + moduleName + "' not found after loading");
        }));
        
        return HavelValue(proxyObj);
    }

private:
    Environment& env_;
    Interpreter* interpreter_;
    std::unordered_map<std::string, ModuleLoaderFn> lazyModules_;
    std::unordered_set<std::string> loadedModules_;
    mutable std::mutex mutex_;
    bool loaded_;
};

/**
 * Helper to create lazy module proxy that auto-loads on first method call
 */
HavelValue CreateLazyModuleProxy(LazyModuleLoader& loader, 
                                  const std::string& moduleName,
                                  LazyModuleLoader::ModuleLoaderFn loaderFn);

} // namespace havel::modules
