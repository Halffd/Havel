#pragma once

/**
 * ModuleLoader.hpp - Dynamic module loading
 *
 * Simple lazy loading:
 * - Modules loaded on first 'use' statement
 * - Module cache (loaded once, reused)
 * - Optional execution policy (for embedding)
 *
 * NOT a security layer - just loading mechanics.
 */

#include "VM.hpp"
#include "../../runtime/HostContext.hpp"
#include "ExecutionPolicy.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <functional>
#include <vector>

namespace havel::compiler {

/**
 * ModuleInfo - Metadata about a module
 */
struct ModuleInfo {
  std::string name;
  std::string version;
  bool isBuiltin = false;      // Provided by HostBridge
  bool isExtension = false;    // Dynamically loaded (.so/.dll)
  std::string extensionPath;   // Path for extension modules
};

/**
 * ModuleLoader - Dynamic module management
 *
 * Responsibilities:
 * - Lazy module loading (on first import)
 * - Module caching (loaded once)
 * - Extension module loading (dlopen/dylib)
 *
 * NOT responsible for:
 * - Security/capability checks (that's ExecutionPolicy)
 * - Permission enforcement (that's HostBridge/services)
 */
class ModuleLoader {
public:
  explicit ModuleLoader(const HostContext &ctx);
  ~ModuleLoader();

  // =========================================================================
  // Module registration
  // =========================================================================

  /// Register a built-in module (provided by HostBridge)
  void registerBuiltin(const std::string &name, const ModuleInfo &info);

  /// Register a stdlib module (pure VM)
  void registerStdlib(const std::string &name,
                      std::function<void(VM &)> initFn,
                      const ModuleInfo &info);

  // =========================================================================
  // Module loading (lazy)
  // =========================================================================

  /// Load a module (called on first use via 'use' statement)
  bool loadModule(const std::string &name, VM &vm);

  /// Check if module is already loaded
  bool isLoaded(const std::string &name) const;

  /// Get list of available modules
  std::vector<std::string> getAvailableModules() const;

  /// Get list of loaded modules
  std::vector<std::string> getLoadedModules() const;

  // =========================================================================
  // Execution policy (optional - for embedding)
  // =========================================================================

  /// Set execution policy (sandbox restrictions)
  void setExecutionPolicy(const ExecutionPolicy &policy) { policy_ = policy; }

  /// Check if action is allowed by policy
  bool canExecute(const std::string &action) const {
    return policy_.canExecute(action);
  }

  // =========================================================================
  // Extension loading (dynamic plugins)
  // =========================================================================

  /// Load extension module from shared library
  std::string loadExtension(const std::string &path);

  /// Unload extension module
  bool unloadExtension(const std::string &name);

  // =========================================================================
  // Import system
  // =========================================================================

  /// Parse and execute import: "use clipboard" or "use io.*"
  bool import(const std::string &importSpec, VM &vm);

private:
  const HostContext &ctx_;
  ExecutionPolicy policy_;  // Optional - defaults to allow all

  // Module registry (metadata only - lazy loading)
  std::unordered_map<std::string, ModuleInfo> registry_;
  std::unordered_map<std::string, std::function<void(VM &)>> stdlibInitFns_;

  // Loaded modules cache
  std::unordered_map<std::string, bool> loadedModules_;

  // Extension module handles (for unloading)
  std::unordered_map<std::string, void *> extensionHandles_;

  // Internal helpers
  bool loadBuiltin(const std::string &name, VM &vm);
  bool loadStdlib(const std::string &name, VM &vm);
};

/**
 * ImportSpec - Parsed import statement
 */
struct ImportSpec {
  std::string moduleName;      // "clipboard"
  std::vector<std::string> members; // ["get", "set"] or empty for all
  bool importAll = false;      // "use io.*"
  std::string alias;           // "use clipboard as cb"
};

/**
 * Parse import specification
 */
ImportSpec parseImportSpec(const std::string &spec);

} // namespace havel::compiler
