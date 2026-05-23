#include "ModuleLoader.hpp"
#include <filesystem>
#include <iostream>
#include <cstdlib>

#ifndef _WIN32
#include <dlfcn.h>
#else
#include <windows.h>
#endif

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
    // Also try with .hvc extension for absolute paths
    fs::path hvcPath = fs::path(modulePath).replace_extension(".hvc");
    if (fs::exists(hvcPath)) {
      return ResolvedModule{ResolvedModule::BytecodeCache,
                            fs::canonical(hvcPath).string(), modulePath};
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
    // Also try .hvc variant
    fs::path hvcPath = fs::path(scriptDir) / (modulePath + ".hvc");
    if (modulePath.ends_with(".hv")) {
      hvcPath = fs::path(scriptDir) / (modulePath.substr(0, modulePath.size() - 3) + ".hvc");
    }
    if (fs::exists(hvcPath)) {
      return ResolvedModule{ResolvedModule::BytecodeCache,
                            fs::canonical(hvcPath).string(), modulePath};
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
  // scriptDir/name.hvc (prefer pre-compiled), then name.hv
  if (!scriptDir.empty()) {
    fs::path scriptDirPath(scriptDir);

    // Prefer .hvc if it exists and is newer than .hv (or .hv absent)
    auto pickHvOrHvc = [&](const fs::path& basePath) -> std::optional<ResolvedModule> {
      fs::path hvPath = fs::path(basePath) / (name + ".hv");
      fs::path hvcPath = fs::path(basePath) / (name + ".hvc");
      bool hvExists = fs::exists(hvPath);
      bool hvcExists = fs::exists(hvcPath);
      if (hvcExists && hvExists) {
        auto hvcTime = fs::last_write_time(hvcPath);
        auto hvTime = fs::last_write_time(hvPath);
        if (hvcTime >= hvTime) {
          return ResolvedModule{ResolvedModule::BytecodeCache,
                                fs::canonical(hvcPath).string(), modulePath};
        }
        return ResolvedModule{ResolvedModule::UserSource,
                              fs::canonical(hvPath).string(), modulePath};
      }
      if (hvcExists) {
        return ResolvedModule{ResolvedModule::BytecodeCache,
                              fs::canonical(hvcPath).string(), modulePath};
      }
      if (hvExists) {
        return ResolvedModule{ResolvedModule::UserSource,
                              fs::canonical(hvPath).string(), modulePath};
      }
      return std::nullopt;
    };

    auto local = pickHvOrHvc(scriptDirPath);
    if (local) return local;

    // Package-style: scriptDir/name/name.hv or name.hvc
    fs::path pkgDir = scriptDirPath / name;
    auto pkg = pickHvOrHvc(pkgDir);
    if (pkg) return pkg;
  }

  // 3. Check __cache__/name.hvc relative to scriptDir (old .hbc path, now .hvc)
  if (!scriptDir.empty()) {
    fs::path cachePath = fs::path(scriptDir) / "__cache__" / (name + ".hvc");
    if (fs::exists(cachePath)) {
      return ResolvedModule{ResolvedModule::BytecodeCache,
                            fs::canonical(cachePath).string(), modulePath};
    }
    // Also check old .hbc extension for backwards compat
    fs::path hbcPath = fs::path(scriptDir) / "__cache__" / (name + ".hbc");
    if (fs::exists(hbcPath)) {
      return ResolvedModule{ResolvedModule::BytecodeCache,
                            fs::canonical(hbcPath).string(), modulePath};
    }
  }

  // 4. Check stdlibPath_ for name.hvc or name.hv
  if (!stdlibPath_.empty()) {
    fs::path stdlibHvcPath = fs::path(stdlibPath_) / (name + ".hvc");
    fs::path stdlibHvPath = fs::path(stdlibPath_) / (name + ".hv");
    bool hvcExists = fs::exists(stdlibHvcPath);
    bool hvExists = fs::exists(stdlibHvPath);
    if (hvcExists && hvExists) {
      auto hvcTime = fs::last_write_time(stdlibHvcPath);
      auto hvTime = fs::last_write_time(stdlibHvPath);
      if (hvcTime >= hvTime) {
        return ResolvedModule{ResolvedModule::BytecodeCache,
                              fs::canonical(stdlibHvcPath).string(), modulePath};
      }
      return ResolvedModule{ResolvedModule::StdlibSource,
                            fs::canonical(stdlibHvPath).string(), modulePath};
    }
    if (hvcExists) {
      return ResolvedModule{ResolvedModule::BytecodeCache,
                            fs::canonical(stdlibHvcPath).string(), modulePath};
    }
    if (hvExists) {
      return ResolvedModule{ResolvedModule::StdlibSource,
                            fs::canonical(stdlibHvPath).string(), modulePath};
    }
  }

  // 5. Check ~/.havel/packages/name/name.hvc or name.hv
  if (const char* home = std::getenv("HOME")) {
    fs::path pkgDir = fs::path(home) / ".havel" / "packages" / name;
    fs::path pkgHvcPath = pkgDir / (name + ".hvc");
    fs::path pkgHvPath = pkgDir / (name + ".hv");
    bool hvcExists = fs::exists(pkgHvcPath);
    bool hvExists = fs::exists(pkgHvPath);
    if (hvcExists && hvExists) {
      auto hvcTime = fs::last_write_time(pkgHvcPath);
      auto hvTime = fs::last_write_time(pkgHvPath);
      if (hvcTime >= hvTime) {
        return ResolvedModule{ResolvedModule::BytecodeCache,
                              fs::canonical(pkgHvcPath).string(), modulePath};
      }
      return ResolvedModule{ResolvedModule::PackageSource,
                            fs::canonical(pkgHvPath).string(), modulePath};
    }
    if (hvcExists) {
      return ResolvedModule{ResolvedModule::BytecodeCache,
                            fs::canonical(pkgHvcPath).string(), modulePath};
    }
    if (hvExists) {
      return ResolvedModule{ResolvedModule::PackageSource,
                            fs::canonical(pkgHvPath).string(), modulePath};
    }
  }

  // 6. Check each user search path for name.hvc, name.hv, or name/name.hv
  for (const auto& sp : searchPaths_) {
    fs::path spDir(sp);

    // Prefer .hvc if available and newer
    fs::path hvPath = spDir / (name + ".hv");
    fs::path hvcPath = spDir / (name + ".hvc");
    bool hvcExists = fs::exists(hvcPath);
    bool hvExists = fs::exists(hvPath);
    if (hvcExists && hvExists) {
      auto hvcTime = fs::last_write_time(hvcPath);
      auto hvTime = fs::last_write_time(hvPath);
      if (hvcTime >= hvTime) {
        return ResolvedModule{ResolvedModule::BytecodeCache,
                              fs::canonical(hvcPath).string(), modulePath};
      }
      return ResolvedModule{ResolvedModule::UserSource,
                            fs::canonical(hvPath).string(), modulePath};
    }
    if (hvcExists) {
      return ResolvedModule{ResolvedModule::BytecodeCache,
                            fs::canonical(hvcPath).string(), modulePath};
    }
    if (hvExists) {
      return ResolvedModule{ResolvedModule::UserSource,
                            fs::canonical(hvPath).string(), modulePath};
    }

    // Try name/name.hv or name/name.hvc (package style)
    fs::path pkgDir = spDir / name;
    fs::path hvPkgPath = pkgDir / (name + ".hv");
    fs::path hvcPkgPath = pkgDir / (name + ".hvc");
    hvcExists = fs::exists(hvcPkgPath);
    hvExists = fs::exists(hvPkgPath);
    if (hvcExists && hvExists) {
      auto hvcTime = fs::last_write_time(hvcPkgPath);
      auto hvTime = fs::last_write_time(hvPkgPath);
      if (hvcTime >= hvTime) {
        return ResolvedModule{ResolvedModule::BytecodeCache,
                              fs::canonical(hvcPkgPath).string(), modulePath};
      }
      return ResolvedModule{ResolvedModule::UserSource,
                            fs::canonical(hvPkgPath).string(), modulePath};
    }
    if (hvcExists) {
      return ResolvedModule{ResolvedModule::BytecodeCache,
                            fs::canonical(hvcPkgPath).string(), modulePath};
    }
    if (hvExists) {
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

bool ModuleLoader::getCached(const std::string& key, core::Value* outValue) const {
    auto it = cache_.find(key);
    if (it == cache_.end()) {
        return false;
    }
    if (outValue) {
        *outValue = it->second;
    }
    return true;
}

void ModuleLoader::putCache(const std::string& key, core::Value value) {
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

#ifndef _WIN32
    // dlopen the .so file
    void* handle = dlopen(path.c_str(), RTLD_LAZY);
    if (!handle) {
        std::cerr << "dlopen failed: " << dlerror() << std::endl;
        return std::nullopt;
    }

    // Try to find havel_module_init symbol
    using InitFn = void (*)(void);
    InitFn init_fn = reinterpret_cast<InitFn>(dlsym(handle, "havel_module_init"));
    if (init_fn) {
        init_fn();
    }
#else
    // Windows implementation (LoadLibrary)
    HMODULE handle = LoadLibraryA(path.c_str());
    if (!handle) {
        std::cerr << "LoadLibrary failed: " << GetLastError() << std::endl;
        return std::nullopt;
    }

    using InitFn = void (*)(void);
    InitFn init_fn = reinterpret_cast<InitFn>(GetProcAddress(handle, "havel_module_init"));
    if (init_fn) {
        init_fn();
    }
#endif

    // Store handle
    NativeHandle nh;
    nh.dlHandle = static_cast<void*>(handle);
    nh.name = path;
    nativeHandles_[path] = nh;

    return nh;
}

void ModuleLoader::unloadNativeExtensions() {
    for (auto& [name, handle] : nativeHandles_) {
        if (handle.dlHandle) {
#ifndef _WIN32
            dlclose(handle.dlHandle);
#else
            FreeLibrary(static_cast<HMODULE>(handle.dlHandle));
#endif
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
