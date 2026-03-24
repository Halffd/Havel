#pragma once

/**
 * ModuleLoader.hpp - Dynamic module loading with capability gating
 *
 * Inspired by Python's import system:
 * - Lazy loading (modules loaded on first use)
 * - Module cache (loaded once, reused)
 * - Capability gating (sandbox mode, permissions)
 * - Extension loading (dynamic plugins)
 */

#include "VM.hpp"
#include "../../runtime/HostContext.hpp"
#include "HostBridgeCapabilities.hpp"

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
  Capability requiredCaps = Capability::None;
};

/**
 * ModuleLoader - Dynamic module management
 *
 * Responsibilities:
 * - Lazy module loading (on first import)
 * - Module caching (loaded once)
 * - Capability validation (at load AND call time)
 * - Extension module loading (dlopen/dylib)
 */
class ModuleLoader {
public:
  explicit ModuleLoader(const HostContext &ctx);
  ~ModuleLoader();

  // =========================================================================
  // Module registration
  // =========================================================================

  /// Register a built-in module (provided by HostBridge)
  /// NOTE: Does NOT load - registers metadata only (lazy loading)
  void registerBuiltin(const std::string &name, const ModuleInfo &info);

  /// Register a stdlib module (pure VM)
  void registerStdlib(const std::string &name,
                      std::function<void(VM &)> initFn,
                      const ModuleInfo &info);

  // =========================================================================
  // Module loading (lazy)
  // =========================================================================

  /// Load a module (called on first use via 'use' statement)
  /// @param name Module name (e.g., "clipboard", "window", "io")
  /// @param vm VM instance to register into
  /// @return true if loaded successfully
  bool loadModule(const std::string &name, VM &vm);

  /// Check if module is already loaded
  bool isLoaded(const std::string &name) const;

  /// Get list of available modules
  std::vector<std::string> getAvailableModules() const;

  /// Get list of loaded modules
  std::vector<std::string> getLoadedModules() const;

  // =========================================================================
  // Capability management (runtime enforcement)
  // =========================================================================

  /// Check if module can be loaded (capability check)
  bool canLoadModule(const std::string &name) const;

  /// Check if capability is enabled (runtime check for function calls)
  bool hasCapability(Capability cap) const { return hasCapability(cap); }

  /// Set capability flags (for sandboxing)
  void setCapabilities(HostBridgeCapabilities caps) { caps_ = caps; }

  /// Get current capabilities
  const HostBridgeCapabilities &getCapabilities() const { return caps_; }

  /// Runtime capability check for function calls
  bool checkCapability(const std::string &functionName) const;

  // =========================================================================
  // Extension loading (dynamic plugins)
  // =========================================================================

  /// Load extension module from shared library
  /// @param path Path to .so/.dll file
  /// @return module name or empty on failure
  std::string loadExtension(const std::string &path);

  /// Unload extension module
  bool unloadExtension(const std::string &name);

  // =========================================================================
  // Import system
  // =========================================================================

  /// Parse and execute import: "use clipboard" or "use io.*"
  /// @param importSpec Import specification
  /// @param vm VM instance
  /// @return true if successful
  bool import(const std::string &importSpec, VM &vm);

  /// Import module and inject symbols into VM scope
  bool importWithBinding(const std::string &moduleName, VM &vm);

  /// Import specific member: "use clipboard.get as clip_get"
  bool importMember(const std::string &moduleName, 
                    const std::string &memberName,
                    const std::string &alias,
                    VM &vm);

private:
  const HostContext &ctx_;
  HostBridgeCapabilities caps_;

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
 * Supports:
 * - "use clipboard"
 * - "use clipboard.get"
 * - "use clipboard.get as clip_get"
 * - "use io.*"
 * - "use fs.path.read" (nested - future)
 */
ImportSpec parseImportSpec(const std::string &spec);

} // namespace havel::compiler
