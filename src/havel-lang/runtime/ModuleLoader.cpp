#include "ModuleLoader.hpp"
#include "../../utils/Logger.hpp"
#include "../compiler/runtime/HostBridge.hpp"
#include <dlfcn.h>
#include <filesystem>
#include <iostream>
#include <cstdlib>

namespace havel {

ModuleLoader::~ModuleLoader() {
    unloadNativeExtensions();
}

void ModuleLoader::addSearchPath(const std::string& path) {
    searchPaths_.push_back(path);
}

void ModuleLoader::setStdlibPath(const std::string& path) {
    stdlibPath_ = path;
}

std::optional<ModuleLoader::ResolvedModule>
ModuleLoader::resolve(const std::string& modulePath,
                      const std::string& scriptDir) const {
    namespace fs = std::filesystem;

    std::string name = modulePath;

    // Check if path is absolute
    if (fs::path(modulePath).is_absolute()) {
        if (fs::exists(modulePath)) {
            return ResolvedModule{ResolvedModule::UserSource, modulePath, modulePath};
        }
        return std::nullopt;
    }

    // Handle explicit relative paths starting with ./ or ../
    if (modulePath.starts_with("./") || modulePath.starts_with("../")) {
        fs::path resolved = fs::path(scriptDir) / modulePath;
        if (fs::exists(resolved)) {
            return ResolvedModule{ResolvedModule::UserSource,
                                fs::canonical(resolved).string(), modulePath};
        }
        return std::nullopt;
    }

    // For bare module names, try priority search

    // 1. Check cache (already loaded?)
    // If already in cache, return Cached type
    if (cache_.count(modulePath) > 0) {
        return ResolvedModule{ResolvedModule::Cached, "", modulePath};
    }

    // 2. Check script directory first for local modules:
    //    scriptDir/name.hv and scriptDir/name/name.hv
    if (!scriptDir.empty()) {
        fs::path scriptDirPath(scriptDir);

        fs::path localHvPath = scriptDirPath / (name + ".hv");
        if (fs::exists(localHvPath)) {
            return ResolvedModule{ResolvedModule::UserSource,
                                fs::canonical(localHvPath).string(), modulePath};
        }

        fs::path localPkgHvPath = scriptDirPath / name / (name + ".hv");
        if (fs::exists(localPkgHvPath)) {
            return ResolvedModule{ResolvedModule::UserSource,
                                fs::canonical(localPkgHvPath).string(), modulePath};
        }
    }

    // 3. Check __cache__/name.hbc relative to scriptDir
    if (!scriptDir.empty()) {
        fs::path cachePath = fs::path(scriptDir) / "__cache__" / (name + ".hbc");
        if (fs::exists(cachePath)) {
            return ResolvedModule{ResolvedModule::BytecodeCache,
                                fs::canonical(cachePath).string(), modulePath};
        }
    }

    // 4. Check stdlibPath_/name.hv (bundled source)
    if (!stdlibPath_.empty()) {
        fs::path stdlibPath = fs::path(stdlibPath_) / (name + ".hv");
        if (fs::exists(stdlibPath)) {
            return ResolvedModule{ResolvedModule::StdlibSource,
                                fs::canonical(stdlibPath).string(), modulePath};
        }
    }

    // 5. Check ~/.havel/packages/name/name.hv
    if (const char* home = std::getenv("HOME")) {
        fs::path pkgPath = fs::path(home) / ".havel" / "packages" / name / (name + ".hv");
        if (fs::exists(pkgPath)) {
            return ResolvedModule{ResolvedModule::PackageSource,
                                fs::canonical(pkgPath).string(), modulePath};
        }
    }

    // 6. Check each user search path for name.hv or name/name.hv
    for (const auto& sp : searchPaths_) {
        fs::path spDir(sp);

        // Try name.hv
        fs::path hvPath = spDir / (name + ".hv");
        if (fs::exists(hvPath)) {
            return ResolvedModule{ResolvedModule::UserSource,
                                fs::canonical(hvPath).string(), modulePath};
        }

        // Try name/name.hv (package style)
        fs::path hvPkgPath = spDir / name / (name + ".hv");
        if (fs::exists(hvPkgPath)) {
            return ResolvedModule{ResolvedModule::UserSource,
                                fs::canonical(hvPkgPath).string(), modulePath};
        }
    }

    // 7. Check each search path for native extensions (.so)
    for (const auto& sp : searchPaths_) {
        fs::path spDir(sp);

        // Try name.so
        fs::path soPath = spDir / (name + ".so");
        if (fs::exists(soPath)) {
            return ResolvedModule{ResolvedModule::NativeExtension,
                                fs::canonical(soPath).string(), modulePath};
        }

        // Try libhavel_name.so
        fs::path libPath = spDir / ("libhavel_" + name + ".so");
        if (fs::exists(libPath)) {
            return ResolvedModule{ResolvedModule::NativeExtension,
                                fs::canonical(libPath).string(), modulePath};
        }
    }

    // 8. Check for host builtin module
    if (hostFns_.count(name) > 0 || envModules_.count(name) > 0) {
        return ResolvedModule{ResolvedModule::HostBuiltin, "", modulePath};
    }

    return std::nullopt;
}

bool ModuleLoader::isCached(const std::string& key) const {
    return cache_.count(key) > 0;
}

bool ModuleLoader::getCached(const std::string& key, void** outValue) const {
    auto it = cache_.find(key);
    if (it == cache_.end()) {
        return false;
    }
    if (outValue) {
        *outValue = it->second;
    }
    return true;
}

void ModuleLoader::putCache(const std::string& key, void* value) {
    cache_[key] = value;
}

void ModuleLoader::clearCache() {
    cache_.clear();
}

std::optional<ModuleLoader::NativeHandle>
ModuleLoader::loadNativeExtension(const std::string& path) {
    // Check if already loaded
    auto it = nativeHandles_.find(path);
    if (it != nativeHandles_.end()) {
        return it->second;
    }

    // dlopen the .so file
    void* handle = dlopen(path.c_str(), RTLD_LAZY);
    if (!handle) {
        havel::error("dlopen failed: {}", dlerror());
        return std::nullopt;
    }

    // Try to find havel_module_init symbol
    using InitFn = void (*)(void);
    InitFn init_fn = reinterpret_cast<InitFn>(dlsym(handle, "havel_module_init"));
    if (init_fn) {
        init_fn();
    }

    // Store handle
    NativeHandle nh;
    nh.dlHandle = handle;
    nh.name = path;
    nativeHandles_[path] = nh;

    return nh;
}

void ModuleLoader::unloadNativeExtensions() {
    for (auto& [name, handle] : nativeHandles_) {
        if (handle.dlHandle) {
            dlclose(handle.dlHandle);
        }
    }
    nativeHandles_.clear();
}

// ============================================================================
// Backward compatibility: Environment-based host module registry
// ============================================================================

void ModuleLoader::add(const std::string& name, ModuleFn fn) {
    envModules_[name] = fn;
    hostModuleFlags_[name] = false;
    interpreterModuleFlags_[name] = false;
}

void ModuleLoader::addInterpreter(const std::string& name, InterpreterModuleFn fn) {
    interpreterFns_[name] = fn;
    hostModuleFlags_[name] = false;
    interpreterModuleFlags_[name] = true;
}

void ModuleLoader::addHost(const std::string& name, HostModuleFn fn) {
    envModules_[name] = [](Environment &env) {
        // Placeholder - will fail at load time if hostAPI not provided
    };
    hostFns_[name] = fn;
    hostModuleFlags_[name] = true;
    interpreterModuleFlags_[name] = false;
}

bool ModuleLoader::load(Environment& env, const std::string& name,
                     std::shared_ptr<IHostAPI> hostAPI,
                     Interpreter* interpreter) {
    // Check if interpreter module
    if (interpreterModuleFlags_[name]) {
        auto it = interpreterFns_.find(name);
        if (it != interpreterFns_.end()) {
            it->second(env, interpreter);
            envLoaded_.insert(name);
            return true;
        }
        return false;
    }

    // Check if host module
    if (hostModuleFlags_[name]) {
        if (!hostAPI) {
            throw std::runtime_error("Host module '" + name + "' requires host API");
        }
        auto hostIt = hostFns_.find(name);
        if (hostIt != hostFns_.end()) {
            hostIt->second(env, hostAPI);
            envLoaded_.insert(name);
            return true;
        }
        return false;
    }

    // Standard module
    auto it = envModules_.find(name);
    if (it == envModules_.end()) {
        return false;
    }

    it->second(env);
    envLoaded_.insert(name);
    return true;
}

bool ModuleLoader::has(const std::string& name) const {
    return envModules_.count(name) > 0 || hostFns_.count(name) > 0 ||
           interpreterFns_.count(name) > 0;
}

bool ModuleLoader::isLoaded(const std::string& name) const {
    return envLoaded_.count(name) > 0;
}

std::vector<std::string> ModuleLoader::list() const {
    std::vector<std::string> names;
    for (const auto& [name, fn] : envModules_) {
        names.push_back(name);
    }
    return names;
}

void ModuleLoader::clearLoaded() {
    envLoaded_.clear();
}

} // namespace havel
