#include "RuntimeModuleLoader.hpp"
#include "../compiler/vm/VM.hpp"

namespace havel {

// Static singleton instance using a static default VM (for backward compatibility)
RuntimeModuleLoader& RuntimeModuleLoader::getInstance() {
    static compiler::VM defaultVm;
    static RuntimeModuleLoader instance(defaultVm);
    return instance;
}

void RuntimeModuleLoader::addSearchPath(const std::string& path) {
    vm_.addModuleSearchPath(path);
}

void RuntimeModuleLoader::setSearchPaths(const std::vector<std::string>& paths) {
    for (const auto& path : paths) {
        vm_.addModuleSearchPath(path);
    }
}

std::optional<std::string> RuntimeModuleLoader::resolve(const std::string& name) {
    auto resolved = vm_.moduleLoader().resolve(name, "");
    if (resolved && !resolved->canonicalPath.empty()) {
        return resolved->canonicalPath;
    }
    return std::nullopt;
}

core::Value RuntimeModuleLoader::load(const std::string& name) {
    return vm_.loadModule(name);
}

core::Value RuntimeModuleLoader::require(const std::string& name) {
    return vm_.loadModule(name);
}

void RuntimeModuleLoader::registerBuiltin(const std::string& name, core::Value exports) {
    (void)name;
    (void)exports;
}

void RuntimeModuleLoader::registerNativeLoader(std::function<void*(const std::string&)> loader) {
    (void)loader;
}

bool RuntimeModuleLoader::isCached(const std::string& name) {
    return vm_.moduleLoader().isCached(name);
}

void RuntimeModuleLoader::clearCache() {
    vm_.moduleLoader().clearCache();
}

void RuntimeModuleLoader::invalidate(const std::string& name) {
    (void)name;
}

std::vector<std::string> RuntimeModuleLoader::getLoadedModules() const {
    return {};
}

} // namespace havel