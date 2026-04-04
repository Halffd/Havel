/*
 * HostBridge.hpp - Clean CPython-style architecture
 * 
 * ARCHITECTURE PRINCIPLES:
 * - VM owns closures
 * - HostBridge manages handles/services
 * - Scripts see nothing of internals
 * - Private callbacks (not public API)
 * - Lazy module loading via 'use' keyword
 * - Permission boundary for sensitive operations
 */
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace havel {
struct HostContext;
class ExecutionPolicy;
} // namespace havel

namespace havel::compiler {

class VM;
class VMApi;
class Value;

// ============================================================================
// Module Registry - Lazy Loading System
// ============================================================================

enum class ModuleCapability {
  None,
  FileSystem,      // File operations
  Network,         // Network access
  Process,         // Process spawning
  UI,              // Window/UI manipulation
  Input,           // Input injection
  Clipboard,       // Clipboard access
  Screenshot,      // Screen capture
  System,          // System info/modification
  Async,           // Async operations
  Extension        // External extensions
};

struct ModuleInfo {
  std::string name;
  std::string version;
  std::vector<ModuleCapability> capabilities;
  bool loaded = false;
  bool enabled = false;  // Can be disabled even if loaded
};

// ============================================================================
// Service Handle - Opaque reference to Host Service
// ============================================================================

class ServiceHandle {
public:
  virtual ~ServiceHandle() = default;
  virtual std::string getServiceName() const = 0;
  virtual std::vector<ModuleCapability> getCapabilities() const = 0;
  virtual bool isAvailable() const = 0;
};

// ============================================================================
// HostBridge - Clean Public API
// ============================================================================

class HostBridge {
public:
  explicit HostBridge(const havel::HostContext &ctx);
  HostBridge(const havel::HostContext &ctx, const ExecutionPolicy &policy);
  ~HostBridge();

  // Shutdown and cleanup
  void shutdown();
  void clear();

  // ==================================================================
  // PUBLIC API - What scripts can see
  // ==================================================================

  // Install all enabled modules into VM
  void install();

  // Lazy module loading - called when 'use moduleName' is encountered
  bool loadModule(const std::string &moduleName);
  bool isModuleLoaded(const std::string &moduleName) const;
  bool isModuleAvailable(const std::string &moduleName) const;

  // Permission check - gate sensitive operations
  bool hasPermission(ModuleCapability cap) const;
  bool requestPermission(ModuleCapability cap);

  // Get module info
  std::vector<std::string> getAvailableModules() const;
  std::vector<std::string> getLoadedModules() const;
  ModuleInfo getModuleInfo(const std::string &moduleName) const;

  // ==================================================================
  // Service Registration - For internal bridge modules only
  // ==================================================================
  
  // Register a service (called by bridge submodules, NOT from scripts)
  void registerService(const std::string &name, std::shared_ptr<ServiceHandle> service);
  std::shared_ptr<ServiceHandle> getService(const std::string &name) const;

  // ==================================================================
  // VM Binding
  // ==================================================================
  
  void bindToVM(VM &vm);
  VM* getVM() const;

private:
  // ==================================================================
  // PRIVATE INTERNALS - Never exposed to scripts
  // ==================================================================

  // Internal callback system (private - not public API)
  using CallbackId = uint32_t;
  
  struct ModeBinding {
    std::string modeName;
    CallbackId condition_id = 0;
    CallbackId enter_id = 0;
    CallbackId exit_id = 0;
  };

  // Private: Mode callback registration (internal use only)
  void registerModeCallbacksInternal(const std::string &modeName,
                                     CallbackId conditionId,
                                     CallbackId enterId, 
                                     CallbackId exitId);
  
  // Private: Callback invocation (internal use only)
  bool invokeCallbackInternal(CallbackId id, const std::vector<Value> &args);

  // Private: Initialize bridge submodules
  void initBridges();

  // Private: Eager module installation (for core modules only)
  void installCoreModules();

  // Private: Lazy module installation (triggered by 'use')
  void installLazyModule(const std::string &moduleName);

  // Private: Check permission for specific operation
  bool checkPermissionInternal(ModuleCapability cap, const std::string &operation) const;

  // ==================================================================
  // Implementation Details
  // ==================================================================
  
  class Impl;
  std::unique_ptr<Impl> pImpl;
};

} // namespace havel::compiler
