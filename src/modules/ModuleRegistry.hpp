/*
 * ModuleRegistry.hpp - Auto-registration system for single-file modules
 * 
 * PURPOSE:
 * - Allow modules to self-register without central registry files
 * - Support single-file modules (header + implementation in one .cpp)
 * - Automatic discovery and registration at startup
 * 
 * USAGE - Single File Module:
 *   // MyModule.cpp - complete module in one file
 *   #include "havel-lang/compiler/vm/VMApi.hpp"
 *   #include "modules/ModuleRegistry.hpp"
 *   
 *   REGISTER_MODULE("myModule", [](havel::compiler::VMApi& api) {
 *       api.registerFunction("myFunc", [](const auto& args) {
 *           return Value::makeInt(42);
 *       });
 *   });
 * 
 * USAGE - With Priority:
 *   REGISTER_MODULE_PRIORITY("critical", 100, [](VMApi& api) {
 *       // Higher priority = registered first
 *   });
 * 
 * USAGE - Conditional Registration:
 *   REGISTER_MODULE_IF("advanced", hasFeature(), [](VMApi& api) {
 *       // Only registers if condition is true
 *   });
 */

#pragma once

#include "havel-lang/compiler/vm/VMApi.hpp"
#include <functional>
#include <string>
#include <vector>
#include <memory>
#include <mutex>

namespace havel {
namespace modules {

// Module registration function signature
using ModuleRegisterFunc = std::function<void(compiler::VMApi&)>;

// Module metadata
struct ModuleInfo {
    std::string name;
    ModuleRegisterFunc registerFunc;
    int priority = 0;           // Higher = registered first
    bool enabled = true;
    std::string description;
    
    ModuleInfo(const std::string& n, ModuleRegisterFunc f, int p = 0)
        : name(n), registerFunc(std::move(f)), priority(p) {}
};

// Global module registry - singleton
class ModuleRegistry {
public:
    static ModuleRegistry& instance() {
        static ModuleRegistry instance;
        return instance;
    }
    
    // Register a module
    void registerModule(const std::string& name, 
                       ModuleRegisterFunc func,
                       int priority = 0,
                       const std::string& description = "") {
        std::lock_guard<std::mutex> lock(mutex_);
        modules_.push_back({name, std::move(func), priority});
        modules_.back().description = description;
        // Sort by priority (higher first)
        std::sort(modules_.begin(), modules_.end(), 
                 [](const auto& a, const auto& b) { return a.priority > b.priority; });
    }
    
    // Register all discovered modules with VMApi
    void registerAll(compiler::VMApi& api) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& module : modules_) {
            if (module.enabled && module.registerFunc) {
                module.registerFunc(api);
            }
        }
    }
    
    // Get list of registered module names
    std::vector<std::string> getModuleNames() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> names;
        for (const auto& m : modules_) {
            names.push_back(m.name);
        }
        return names;
    }
    
    // Enable/disable a module
    void setEnabled(const std::string& name, bool enabled) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& m : modules_) {
            if (m.name == name) {
                m.enabled = enabled;
                return;
            }
        }
    }
    
    // Clear all registrations (mainly for testing)
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        modules_.clear();
    }
    
private:
    ModuleRegistry() = default;
    ~ModuleRegistry() = default;
    ModuleRegistry(const ModuleRegistry&) = delete;
    ModuleRegistry& operator=(const ModuleRegistry&) = delete;
    
    mutable std::mutex mutex_;
    std::vector<ModuleInfo> modules_;
};

// Auto-registration helper using static initialization
struct AutoRegistrant {
    AutoRegistrant(const std::string& name, 
                  ModuleRegisterFunc func,
                  int priority = 0,
                  const std::string& description = "") {
        ModuleRegistry::instance().registerModule(name, std::move(func), priority, description);
    }
};

// Conditional auto-registration
struct ConditionalAutoRegistrant {
    ConditionalAutoRegistrant(bool condition,
                             const std::string& name,
                             ModuleRegisterFunc func,
                             int priority = 0) {
        if (condition) {
            ModuleRegistry::instance().registerModule(name, std::move(func), priority);
        }
    }
};

} // namespace modules
} // namespace havel

// ============================================================================
// REGISTRATION MACROS
// ============================================================================

// Register a module (basic)
#define REGISTER_MODULE(name, func) \
    static ::havel::modules::AutoRegistrant \
        _havel_module_##name##_registrant(name, func, 0, "");

// Register a module with priority (higher = earlier registration)
#define REGISTER_MODULE_PRIORITY(name, priority, func) \
    static ::havel::modules::AutoRegistrant \
        _havel_module_##name##_registrant(name, func, priority, "");

// Register a module with description
#define REGISTER_MODULE_DESC(name, desc, func) \
    static ::havel::modules::AutoRegistrant \
        _havel_module_##name##_registrant(name, func, 0, desc);

// Register module conditionally (compile-time or run-time condition)
#define REGISTER_MODULE_IF(name, condition, func) \
    static ::havel::modules::ConditionalAutoRegistrant \
        _havel_module_##name##_registrant(condition, name, func, 0);

// Register all discovered modules with VMApi
#define REGISTER_ALL_MODULES(api) \
    ::havel::modules::ModuleRegistry::instance().registerAll(api)

// Get list of auto-registered module names
#define GET_REGISTERED_MODULES() \
    ::havel::modules::ModuleRegistry::instance().getModuleNames()
