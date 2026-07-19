#pragma once
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "core/Value.hpp"

namespace fs_time = std::filesystem;

namespace havel {

class Environment;
class IHostAPI;
class Interpreter;

using ModuleFn = std::function<void(Environment &)>;
using InterpreterModuleFn = std::function<void(Environment &, Interpreter *)>;
using HostModuleFn = std::function<void(Environment &, std::shared_ptr<IHostAPI>)>;

class ModuleLoader {
public:
    std::string self_hosted_modules_path_ = "";

    void setSelfHostedPath(const std::string& path) { self_hosted_modules_path_ = path; }

    // --- Resolved module info ---
    struct ResolvedModule {
        enum Type {
            Cached,           // Already in module cache
            BytecodeCache,    // .hbc file in __cache__/
            StdlibSource,     // .hv file in stdlib/
            PackageSource,    // .hv file in ~/.havel/packages/
            UserSource,       // .hv file in user search paths
            NativeExtension, // .so file
            HostBuiltin      // Host module (registered via host_function_globals_)
        };
        Type type;
        std::string canonicalPath;  // Resolved absolute path (empty for Cached/HostBuiltin)
        std::string moduleName;     // Original module name
        // For BytecodeCache: canonical path of the source .hv (or empty if no source on disk)
        // VM.cpp uses this to re-verify hash to detect mods to source.
        std::string sourcePath;
    };

    // --- Native extension handle ---
    struct NativeHandle {
        void* dlHandle = nullptr;
        std::string name;
    };

    // Cached module entry: exports + globals snapshot for internal function calls
    struct CachedModule {
        core::Value exports;
        std::shared_ptr<std::unordered_map<std::string, core::Value>> globals_snapshot;
    };

    ModuleLoader() = default;
    ~ModuleLoader();

    // Non-copyable, movable
    ModuleLoader(const ModuleLoader&) = delete;
    ModuleLoader& operator=(const ModuleLoader&) = delete;
    ModuleLoader(ModuleLoader&&) = default;
    ModuleLoader& operator=(ModuleLoader&&) = default;

    // ========================================================================
    // Search path management
    // ========================================================================
  void addSearchPath(const std::string& path);
  void addModuleSoPath(const std::string& path);
  void setStdlibPath(const std::string& path);
  const std::vector<std::string>& getSearchPaths() const { return searchPaths_; }
  const std::vector<std::string>& getModuleSoPaths() const { return moduleSoPaths_; }
  const std::string& getStdlibPath() const { return stdlibPath_; }

    // ========================================================================
    // Path resolution (priority-based)
    // ========================================================================
    std::optional<ResolvedModule> resolve(const std::string& modulePath,
                                           const std::string& scriptDir) const;

    // ========================================================================
    // Module cache (for VM to store/retrieve loaded module exports)
    // ========================================================================
    bool isCached(const std::string& key) const;
    bool getCached(const std::string& key, core::Value* outValue) const;
    bool getCachedGlobals(const std::string& key, std::shared_ptr<std::unordered_map<std::string, core::Value>>* outGlobals) const;
    void putCache(const std::string& key, core::Value value);
    void putCacheWithGlobals(const std::string& key, core::Value value, std::shared_ptr<std::unordered_map<std::string, core::Value>> globals);
    void clearCache();
    void invalidate(const std::string& key);
    std::vector<core::Value> cachedValues() const;

    // ========================================================================
    // Native extension loading (.so via dlopen)
    // ========================================================================
    std::optional<NativeHandle> loadNativeExtension(const std::string& path);
    void unloadNativeExtensions();

    // ========================================================================
    // Backward compatibility: Environment-based host module registry
    // (deprecated - kept for HostModules.hpp/cpp which still reference it)
    // ========================================================================
    void add(const std::string& name, ModuleFn fn);
    void addInterpreter(const std::string& name, InterpreterModuleFn fn);
    void addHost(const std::string& name, HostModuleFn fn);
    bool load(Environment& env, const std::string& name,
               std::shared_ptr<IHostAPI> hostAPI = nullptr,
               Interpreter* interpreter = nullptr);
    bool has(const std::string& name) const;
    bool isLoaded(const std::string& name) const;
    std::vector<std::string> list() const;
    void clearLoaded();

private:
  // Search paths for .hv module resolution
  std::vector<std::string> searchPaths_;
  std::string stdlibPath_; // Bundled stdlib directory
  // Search paths for native module .so resolution (havel_mod_<name>.so)
  std::vector<std::string> moduleSoPaths_;

    // Module cache: canonicalKey -> CachedModule (exports + globals snapshot)
    std::unordered_map<std::string, CachedModule> cache_;

    // Per-cached-key freshness hints so a cache hit can still detect that the
    // underlying .hv source (or .hvc file) was modified and self-invalidate
    // without going all the way to a fresh resolve().
    //   key  = same as `cache_` key (canonical path or module name)
    //   src  = canonical path of the source .hv (empty if cache had no source)
    //   hvc  = canonical path of the .hvc (empty if cached from source)
    //   mtime_ns = nanoseconds since epoch of the most recently inspected file.
    struct CacheFreshness {
        std::string src;
        std::string hvc;
        long long mtime_ns = 0;
    };
    mutable std::unordered_map<std::string, CacheFreshness> freshness_;

    // True iff the source/.hvc on disk matches the recorded freshness hint.
    // If false, the entry must be invalidated before returning Cached.
    bool isFreshLocked(const std::string &key) const;
    long long mtimeNs(const std::string &path) const;

  // Native extension handles (for dlopen/dlclose)
  std::unordered_map<std::string, NativeHandle> nativeHandles_;

  // Backward compat: Environment-based module registry (deprecated)
  std::unordered_map<std::string, ModuleFn> envModules_;
  std::unordered_map<std::string, HostModuleFn> hostFns_;
  std::unordered_map<std::string, InterpreterModuleFn> interpreterFns_;
  std::unordered_map<std::string, bool> hostModuleFlags_;
  std::unordered_map<std::string, bool> interpreterModuleFlags_;
  std::unordered_set<std::string> envLoaded_;
};

} // namespace havel