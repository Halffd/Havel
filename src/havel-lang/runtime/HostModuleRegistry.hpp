/*
 * HostModuleRegistry.hpp
 *
 * Central registry for host modules.
 * Uses map-based registration instead of direct Define calls.
 */
#pragma once

#include "../../host/HostContext.hpp"
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <memory>

namespace havel {

class Environment;

/**
 * Module registration function type
 */
using ModuleInitFunc = std::function<void(Environment&, HostContext&)>;

/**
 * Module metadata
 */
struct ModuleInfo {
    std::string name;
    std::string description;
    ModuleInitFunc initFunc;
    bool autoLoad = true;  // Load automatically on startup
};

/**
 * HostModuleRegistry - Central registry for all host modules
 */
class HostModuleRegistry {
public:
    static HostModuleRegistry& getInstance();
    
    void registerModule(const std::string& name, 
                       ModuleInitFunc initFunc,
                       const std::string& description = "",
                       bool autoLoad = true);
    
    void loadModule(const std::string& name, Environment& env, HostContext& ctx);
    void loadAllModules(Environment& env, HostContext& ctx);
    std::vector<std::string> getModuleNames() const;
    bool hasModule(const std::string& name) const;

private:
    HostModuleRegistry() = default;
    std::unordered_map<std::string, ModuleInfo> modules_;
};

/**
 * Helper macro for registering modules
 */
#define REGISTER_HOST_MODULE(Name, InitFunc, Desc, AutoLoad) \
    namespace { \
        struct Name##ModuleRegistrar { \
            Name##ModuleRegistrar() { \
                HostModuleRegistry::getInstance().registerModule( \
                    #Name, InitFunc, Desc, AutoLoad \
                ); \
            } \
        }; \
        static Name##ModuleRegistrar registrar__; \
    }

} // namespace havel
