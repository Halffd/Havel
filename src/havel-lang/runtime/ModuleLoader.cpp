#include "ModuleLoader.hpp"
#include "c/ModulePlugin.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <cstdlib>
#include <system_error>

#ifndef _WIN32
#include <dlfcn.h>
#else
#include <windows.h>
#endif

namespace havel {

ModuleLoader::~ModuleLoader() {
    unloadNativeExtensions();
}

// Build a BytecodeCache ResolvedModule. If the .hv source next to .hvc exists
// (or was located), record its canonical path so the VM can re-verify the
// embedded source hash against the live file content.
static ModuleLoader::ResolvedModule makeBcCache(const std::filesystem::path &hvcPath,
                                                const std::filesystem::path &hvPath,
                                                const std::string &modulePath) {
    namespace fs = std::filesystem;
    std::string bcPath;
    try { bcPath = fs::canonical(hvcPath).string(); }
    catch (...) { bcPath = hvcPath.string(); }
    std::string srcPath;
    if (fs::exists(hvPath)) {
        try { srcPath = fs::canonical(hvPath).string(); }
        catch (...) { srcPath = hvPath.string(); }
    }
    return ModuleLoader::ResolvedModule{ModuleLoader::ResolvedModule::BytecodeCache,
                                        bcPath, modulePath, srcPath};
}

void ModuleLoader::addSearchPath(const std::string& path) {
  searchPaths_.push_back(path);
}

void ModuleLoader::addModuleSoPath(const std::string& path) {
  moduleSoPaths_.push_back(path);
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
  // If already in cache, return Cached type — BUT first drop the entry if the
  // underlying source/.hvc filename has been touched since we cached it.
  if (cache_.count(modulePath) > 0) {
    if (!isFreshLocked(modulePath)) {
      cache_.erase(modulePath);
      freshness_.erase(modulePath);
    } else {
      return ResolvedModule{ResolvedModule::Cached, "", modulePath};
    }
  }

  // 1b. Check self-hosted modules path first (compiled .hvc bytecode)
  if (!self_hosted_modules_path_.empty()) {
    // Prefer .hvc only if it is present and not older than the corresponding
    // .hv source on disk (mtime check). When the source is missing, the cache
    // is admissible as-is.
    fs::path selfHostedHvc = fs::path(self_hosted_modules_path_) / (name + ".hvc");
    fs::path selfHostedHv  = fs::path(self_hosted_modules_path_) / (name + ".hv");
    if (fs::exists(selfHostedHvc)) {
      auto hvcTime = fs::last_write_time(selfHostedHvc);
      bool newerOrEqual = !fs::exists(selfHostedHv) ||
                          hvcTime >= fs::last_write_time(selfHostedHv);
      if (newerOrEqual) return makeBcCache(selfHostedHvc, selfHostedHv, modulePath);
    }

    // modules/lang/<name>.hvc subdirectory convention
    fs::path langHvc = fs::path(self_hosted_modules_path_) / "modules" / "lang" / (name + ".hvc");
    fs::path langHv  = fs::path(self_hosted_modules_path_) / "modules" / "lang" / (name + ".hv");
    if (fs::exists(langHvc)) {
      auto hvcTime = fs::last_write_time(langHvc);
      bool newerOrEqual = !fs::exists(langHv) ||
                          hvcTime >= fs::last_write_time(langHv);
      if (newerOrEqual) return makeBcCache(langHvc, langHv, modulePath);
    }
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
        if (fs::last_write_time(hvcPath) >= fs::last_write_time(hvPath)) {
          return makeBcCache(hvcPath, hvPath, modulePath);
        }
        return ResolvedModule{ResolvedModule::UserSource,
                              fs::canonical(hvPath).string(), modulePath, ""};
      }
      if (hvcExists) {
        return makeBcCache(hvcPath, hvPath, modulePath);
      }
      if (hvExists) {
        return ResolvedModule{ResolvedModule::UserSource,
                              fs::canonical(hvPath).string(), modulePath, ""};
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
    fs::path srcPath  = fs::path(scriptDir) / (name + ".hv");
    if (fs::exists(cachePath)) {
      if (fs::exists(srcPath) &&
          fs::last_write_time(cachePath) < fs::last_write_time(srcPath)) {
        // source newer - skip cache, fall through
      } else {
        return makeBcCache(cachePath, srcPath, modulePath);
      }
    }
    // Also check old .hbc extension for backwards compat
    fs::path hbcPath = fs::path(scriptDir) / "__cache__" / (name + ".hbc");
    if (fs::exists(hbcPath)) {
      if (fs::exists(srcPath) &&
          fs::last_write_time(hbcPath) < fs::last_write_time(srcPath)) {
        // source newer - skip cache
      } else {
        return makeBcCache(hbcPath, srcPath, modulePath);
      }
    }
  }

  // 4. Check stdlibPath_ for name.hvc or name.hv
  if (!stdlibPath_.empty()) {
    fs::path stdlibHvcPath = fs::path(stdlibPath_) / (name + ".hvc");
    fs::path stdlibHvPath = fs::path(stdlibPath_) / (name + ".hv");
    bool hvcExists = fs::exists(stdlibHvcPath);
    bool hvExists = fs::exists(stdlibHvPath);
    if (hvcExists && hvExists) {
      if (fs::last_write_time(stdlibHvcPath) >= fs::last_write_time(stdlibHvPath)) {
        return makeBcCache(stdlibHvcPath, stdlibHvPath, modulePath);
      }
      return ResolvedModule{ResolvedModule::StdlibSource,
                            fs::canonical(stdlibHvPath).string(), modulePath, ""};
    }
    if (hvcExists) {
      // No source side-by-side - use cache but no sourcePath for hash check.
      return makeBcCache(stdlibHvcPath, stdlibHvPath, modulePath);
    }
    if (hvExists) {
      return ResolvedModule{ResolvedModule::StdlibSource,
                            fs::canonical(stdlibHvPath).string(), modulePath, ""};
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
      if (fs::last_write_time(pkgHvcPath) >= fs::last_write_time(pkgHvPath)) {
        return makeBcCache(pkgHvcPath, pkgHvPath, modulePath);
      }
      return ResolvedModule{ResolvedModule::PackageSource,
                            fs::canonical(pkgHvPath).string(), modulePath, ""};
    }
    if (hvcExists) {
      return makeBcCache(pkgHvcPath, pkgHvPath, modulePath);
    }
    if (hvExists) {
      return ResolvedModule{ResolvedModule::PackageSource,
                            fs::canonical(pkgHvPath).string(), modulePath, ""};
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
      if (fs::last_write_time(hvcPath) >= fs::last_write_time(hvPath)) {
        return makeBcCache(hvcPath, hvPath, modulePath);
      }
      return ResolvedModule{ResolvedModule::UserSource,
                            fs::canonical(hvPath).string(), modulePath, ""};
    }
    if (hvcExists) {
      return makeBcCache(hvcPath, hvPath, modulePath);
    }
    if (hvExists) {
      return ResolvedModule{ResolvedModule::UserSource,
                            fs::canonical(hvPath).string(), modulePath, ""};
    }

    // Try name/name.hv or name/name.hvc (package style)
    fs::path pkgDir = spDir / name;
    fs::path hvPkgPath = pkgDir / (name + ".hv");
    fs::path hvcPkgPath = pkgDir / (name + ".hvc");
    hvcExists = fs::exists(hvcPkgPath);
    hvExists = fs::exists(hvPkgPath);
    if (hvcExists && hvExists) {
      if (fs::last_write_time(hvcPkgPath) >= fs::last_write_time(hvPkgPath)) {
        return makeBcCache(hvcPkgPath, hvPkgPath, modulePath);
      }
      return ResolvedModule{ResolvedModule::UserSource,
                            fs::canonical(hvPkgPath).string(), modulePath, ""};
    }
    if (hvcExists) {
      return makeBcCache(hvcPkgPath, hvPkgPath, modulePath);
    }
    if (hvExists) {
      return ResolvedModule{ResolvedModule::UserSource,
                            fs::canonical(hvPkgPath).string(), modulePath, ""};
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

  // 7b. Check module .so paths for havel_mod_<name>.so (plugin naming convention)
  for (const auto& sp : moduleSoPaths_) {
    fs::path soPath = fs::path(sp) / ("havel_mod_" + name + ".so");
    if (fs::exists(soPath)) {
      return ResolvedModule{ResolvedModule::NativeExtension,
        fs::canonical(soPath).string(), modulePath};
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
        *outValue = it->second.exports;
    }
    return true;
}

bool ModuleLoader::getCachedGlobals(const std::string& key, std::shared_ptr<std::unordered_map<std::string, core::Value>>* outGlobals) const {
    auto it = cache_.find(key);
    if (it == cache_.end()) {
        return false;
    }
    if (outGlobals) {
        *outGlobals = it->second.globals_snapshot;
    }
    return true;
}

void ModuleLoader::putCache(const std::string& key, core::Value value) {
    cache_[key] = CachedModule{value, nullptr};
}

void ModuleLoader::putCache(const std::string& key, core::Value value,
                            const std::string &sourcePath, const std::string &bytecodePath) {
    cache_[key] = CachedModule{value, nullptr};
    freshness_[key] = CacheFreshness{sourcePath, bytecodePath,
                                     std::max(mtimeNs(sourcePath), mtimeNs(bytecodePath))};
}

void ModuleLoader::putCacheWithGlobals(const std::string& key, core::Value value, std::shared_ptr<std::unordered_map<std::string, core::Value>> globals) {
    cache_[key] = CachedModule{value, globals};
}

void ModuleLoader::putCacheWithGlobals(const std::string& key, core::Value value, std::shared_ptr<std::unordered_map<std::string, core::Value>> globals,
                                       const std::string &sourcePath, const std::string &bytecodePath) {
    cache_[key] = CachedModule{value, globals};
    freshness_[key] = CacheFreshness{sourcePath, bytecodePath,
                                     std::max(mtimeNs(sourcePath), mtimeNs(bytecodePath))};
}

void ModuleLoader::clearCache() {
    cache_.clear();
    freshness_.clear();
}

void ModuleLoader::invalidate(const std::string& key) {
    cache_.erase(key);
    freshness_.erase(key);
}

long long ModuleLoader::mtimeNs(const std::string &path) {
    if (path.empty()) return 0;
    namespace fs = std::filesystem;
    std::error_code ec;
    auto t = fs::last_write_time(path, ec);
    if (ec) return 0;
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        t.time_since_epoch()).count();
}

bool ModuleLoader::isFreshLocked(const std::string &key) const {
    auto it = freshness_.find(key);
    if (it == freshness_.end()) return true; // no hint, treat as fresh
    const auto &h = it->second;
    // Compare the most-recently-touched side. If both source and .hvc were
    // recorded, just check the newer of the two (which is what we'd inspect
    // during resolve); in practice we check both and bail if either changed.
    if (!h.src.empty()) {
        long long m = mtimeNs(h.src);
        if (m != h.mtime_ns && !h.hvc.empty()) return false;
        // For source-only entries (no .hvc), expect mtime to match.
        if (h.hvc.empty() && m != h.mtime_ns) return false;
    }
    if (!h.hvc.empty()) {
        long long m = mtimeNs(h.hvc);
        if (m != h.mtime_ns) return false;
    }
    return true;
}

std::vector<core::Value> ModuleLoader::cachedValues() const {
    std::vector<core::Value> values;
    values.reserve(cache_.size());
    for (const auto& [key, val] : cache_) {
        values.push_back(val.exports);
    }
    return values;
}

std::optional<ModuleLoader::NativeHandle>
ModuleLoader::loadNativeExtension(const std::string& path) {
  auto it = nativeHandles_.find(path);
  if (it != nativeHandles_.end()) {
    return it->second;
  }

#ifndef _WIN32
  void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!handle) {
    std::cerr << "dlopen failed: " << dlerror() << std::endl;
    return std::nullopt;
  }

  using InfoFn = const HavelModuleABI *(*)(void);
  InfoFn info_fn = reinterpret_cast<InfoFn>(dlsym(handle, "havel_module_info"));
  if (info_fn) {
    const HavelModuleABI *abi = info_fn();
    if (abi && abi->abi_version >= 1 &&
        abi->abi_version <= HAVEL_MODULE_ABI_VERSION && abi->register_fn) {
      // register_fn must be called with VMApi* from the VM
      // For now just store the handle; the VM will call register_fn
    }
  }
#else
  HMODULE handle = LoadLibraryA(path.c_str());
  if (!handle) {
    std::cerr << "LoadLibrary failed: " << GetLastError() << std::endl;
    return std::nullopt;
  }
#endif

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
