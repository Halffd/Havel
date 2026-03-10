/*
 * Module.hpp
 *
 * Module interface for host API registration.
 * 
 * This provides a clean separation between the language runtime
 * and host-specific integrations (window management, audio, etc.)
 */
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace havel {

// Forward declarations
class Environment;
struct HavelValue;
struct HostContext;

/**
 * Module interface for registering host APIs
 */
class Module {
public:
    virtual ~Module() = default;
    virtual std::string name() const = 0;
    virtual void registerBuiltin(Environment& env, HostContext& ctx) = 0;
    virtual void initialize(HostContext& ctx) { (void)ctx; }
    virtual void shutdown(HostContext& ctx) { (void)ctx; }
};

/**
 * Module registry - manages all registered modules
 */
class ModuleRegistry {
public:
    static ModuleRegistry& getInstance() {
        static ModuleRegistry instance;
        return instance;
    }
    
    void registerModule(std::unique_ptr<Module> module) {
        modules_.push_back(std::move(module));
    }
    
    const std::vector<std::unique_ptr<Module>>& getModules() const {
        return modules_;
    }
    
    Module* findModule(const std::string& name) {
        for (auto& mod : modules_) {
            if (mod->name() == name) return mod.get();
        }
        return nullptr;
    }

private:
    ModuleRegistry() = default;
    std::vector<std::unique_ptr<Module>> modules_;
};

/**
 * Helper to register a module at startup
 */
#define REGISTER_MODULE(ModuleClass) \
    namespace { \
        struct ModuleClass##Registrar { \
            ModuleClass##Registrar() { \
                ModuleRegistry::getInstance().registerModule( \
                    std::make_unique<ModuleClass>() \
                ); \
            } \
        }; \
        static ModuleClass##Registrar registrar__; \
    }

/**
 * Function-based module registration (for existing modules)
 */
using ModuleRegisterFunc = std::function<void(Environment&, HostContext&)>;

class ModuleLoader {
public:
    static ModuleLoader& getInstance() {
        static ModuleLoader instance;
        return instance;
    }
    
    void registerModuleFunc(const std::string& name, ModuleRegisterFunc func) {
        moduleFuncs_[name] = func;
    }
    
    void loadModule(const std::string& name, Environment& env, HostContext& ctx) {
        auto it = moduleFuncs_.find(name);
        if (it != moduleFuncs_.end()) {
            it->second(env, ctx);
        }
    }
    
    void loadAllModules(Environment& env, HostContext& ctx) {
        for (auto& [name, func] : moduleFuncs_) {
            func(env, ctx);
        }
    }
    
    const std::vector<std::string>& getModuleNames() const {
        static std::vector<std::string> names;
        if (names.empty()) {
            for (auto& [name, _] : moduleFuncs_) {
                names.push_back(name);
            }
        }
        return names;
    }

private:
    ModuleLoader() = default;
    std::unordered_map<std::string, ModuleRegisterFunc> moduleFuncs_;
};

/**
 * Helper macro for function-based module registration
 */
#define REGISTER_MODULE_FUNC(ModuleName, RegisterFunc) \
    namespace { \
        struct ModuleName##FuncRegistrar { \
            ModuleName##FuncRegistrar() { \
                ModuleLoader::getInstance().registerModuleFunc( \
                    #ModuleName, RegisterFunc \
                ); \
            } \
        }; \
        static ModuleName##FuncRegistrar registrar__; \
    }

} // namespace havel
