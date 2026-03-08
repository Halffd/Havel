/*
 * LazyModuleLoader.cpp
 *
 * Lazy-loading wrapper for host modules.
 */
#include "LazyModuleLoader.hpp"

namespace havel::modules {

HavelValue CreateLazyModuleProxy(LazyModuleLoader& loader, 
                                  const std::string& moduleName,
                                  LazyModuleLoader::ModuleLoaderFn loaderFn) {
    // Register for lazy loading
    loader.registerLazy(moduleName, loaderFn);
    
    // Create a proxy object that loads the module on first access
    auto proxyObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
    
    // Store module name for access
    (*proxyObj)["__module_name__"] = HavelValue(moduleName);
    (*proxyObj)["__loaded__"] = HavelValue(false);
    
    // Create a __call__ method that loads and forwards
    (*proxyObj)["__call__"] = HavelValue(BuiltinFunction([&loader, moduleName, loaderFn](const std::vector<HavelValue>& args) -> HavelResult {
        // Load module on first call
        loader.loadModule(moduleName);
        
        // Get the actual module from environment
        auto moduleVal = loader.env_.Get(moduleName);
        if (!moduleVal || !moduleVal->isObject()) {
            return HavelRuntimeError("Module '" + moduleName + "' failed to load");
        }
        
        // Check if module has __call__
        auto& moduleObj = moduleVal->asObject();
        auto callIt = moduleObj->find("__call__");
        if (callIt != moduleObj->end() && callIt->second.is<BuiltinFunction>()) {
            return callIt->second.get<BuiltinFunction>()(args);
        }
        
        return HavelRuntimeError("Module '" + moduleName + "' is not callable");
    }));
    
    return HavelValue(proxyObj);
}

} // namespace havel::modules
