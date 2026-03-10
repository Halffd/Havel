/*
 * HostModuleRegistry.cpp
 *
 * Central registry for host modules - implementation.
 */
#include "HostModuleRegistry.hpp"
#include "Environment.hpp"

namespace havel {

HostModuleRegistry& HostModuleRegistry::getInstance() {
    static HostModuleRegistry instance;
    return instance;
}

void HostModuleRegistry::registerModule(const std::string& name, 
                                       ModuleInitFunc initFunc,
                                       const std::string& description,
                                       bool autoLoad) {
    modules_[name] = ModuleInfo{name, description, std::move(initFunc), autoLoad};
}

void HostModuleRegistry::loadModule(const std::string& name, Environment& env, HostContext& ctx) {
    auto it = modules_.find(name);
    if (it != modules_.end()) {
        it->second.initFunc(env, ctx);
    }
}

void HostModuleRegistry::loadAllModules(Environment& env, HostContext& ctx) {
    for (auto& [name, info] : modules_) {
        if (info.autoLoad) {
            info.initFunc(env, ctx);
        }
    }
}

std::vector<std::string> HostModuleRegistry::getModuleNames() const {
    std::vector<std::string> names;
    for (const auto& [name, _] : modules_) {
        names.push_back(name);
    }
    return names;
}

bool HostModuleRegistry::hasModule(const std::string& name) const {
    return modules_.count(name) > 0;
}

} // namespace havel
