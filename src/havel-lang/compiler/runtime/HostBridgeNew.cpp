/*
 * HostBridgeNew.cpp - Clean implementation with private callbacks
 * 
 * PRINCIPLES:
 * - All callbacks are private/internal
 * - Lazy module loading via registry
 * - Permission boundary gates sensitive calls
 * - Service ≠ Module enforced structurally
 */
#include "HostBridgeNew.hpp"
#include "havel-lang/compiler/vm/VM.hpp"
#include "havel-lang/compiler/vm/VMApi.hpp"
#include "havel-lang/host/HostContext.hpp"
#include "havel-lang/host/ExecutionPolicy.hpp"

#include <algorithm>
#include <iostream>

namespace havel::compiler {

// ============================================================================
// Module Registry - Lazy Loading System
// ============================================================================

class ModuleRegistry {
public:
  void registerModule(const ModuleInfo &info) {
    modules_[info.name] = info;
  }

  bool isAvailable(const std::string &name) const {
    return modules_.count(name) > 0;
  }

  bool isLoaded(const std::string &name) const {
    auto it = modules_.find(name);
    return it != modules_.end() && it->second.loaded;
  }

  bool isEnabled(const std::string &name) const {
    auto it = modules_.find(name);
    return it != modules_.end() && it->second.enabled;
  }

  void markLoaded(const std::string &name) {
    auto it = modules_.find(name);
    if (it != modules_.end()) {
      it->second.loaded = true;
    }
  }

  void setEnabled(const std::string &name, bool enabled) {
    auto it = modules_.find(name);
    if (it != modules_.end()) {
      it->second.enabled = enabled;
    }
  }

  std::vector<std::string> getAvailable() const {
    std::vector<std::string> result;
    for (const auto &[name, _] : modules_) {
      result.push_back(name);
    }
    return result;
  }

  std::vector<std::string> getLoaded() const {
    std::vector<std::string> result;
    for (const auto &[name, info] : modules_) {
      if (info.loaded) {
        result.push_back(name);
      }
    }
    return result;
  }

  ModuleInfo getInfo(const std::string &name) const {
    auto it = modules_.find(name);
    if (it != modules_.end()) {
      return it->second;
    }
    return ModuleInfo{};
  }

  std::vector<ModuleCapability> getCapabilities(const std::string &name) const {
    auto it = modules_.find(name);
    if (it != modules_.end()) {
      return it->second.capabilities;
    }
    return {};
  }

private:
  std::unordered_map<std::string, ModuleInfo> modules_;
};

// ============================================================================
// Permission Manager
// ============================================================================

class PermissionManager {
public:
  explicit PermissionManager(const ExecutionPolicy *policy) : policy_(policy) {}

  bool hasPermission(ModuleCapability cap) const {
    if (!policy_) {
      return true; // Default: full access
    }
    // Map capabilities to policy checks
    switch (cap) {
      case ModuleCapability::FileSystem:
        return true; // TODO: Check policy
      case ModuleCapability::Network:
        return true;
      case ModuleCapability::Process:
        return true;
      case ModuleCapability::UI:
        return true;
      case ModuleCapability::Input:
        return true;
      case ModuleCapability::Clipboard:
        return true;
      case ModuleCapability::Screenshot:
        return true;
      case ModuleCapability::System:
        return true;
      default:
        return true;
    }
  }

  bool requestPermission(ModuleCapability cap) {
    // TODO: Implement user prompt for permission
    granted_.insert(cap);
    return true;
  }

  bool isGranted(ModuleCapability cap) const {
    return granted_.count(cap) > 0;
  }

private:
  const ExecutionPolicy *policy_;
  std::unordered_set<ModuleCapability> granted_;
};

// ============================================================================
// HostBridge Implementation (PIMPL)
// ============================================================================

class HostBridge::Impl {
public:
  explicit Impl(const havel::HostContext &ctx, const ExecutionPolicy *policy)
      : ctx_(ctx), policy_(policy ? *policy : ExecutionPolicy::DefaultPolicy()),
        permissions_(policy) {}

  void install(VM &vm);
  bool loadModule(const std::string &name);
  void registerService(const std::string &name, std::shared_ptr<ServiceHandle> service);
  
  // Private: Mode callbacks (internal only)
  void registerModeCallbacksInternal(const std::string &modeName,
                                       uint32_t conditionId,
                                       uint32_t enterId, 
                                       uint32_t exitId);

  const havel::HostContext &ctx_;
  ExecutionPolicy policy_;
  PermissionManager permissions_;
  ModuleRegistry registry_;
  
  VM *vm_ = nullptr;
  std::unordered_map<std::string, std::shared_ptr<ServiceHandle>> services_;
  
  // Private: Mode bindings (internal only)
  std::unordered_map<std::string, ModeBinding> mode_bindings_;
  std::vector<std::string> mode_definition_order_;
  std::vector<std::string> hotkey_binding_keys_;
};

// ============================================================================
// HostBridge Public Implementation
// ============================================================================

HostBridge::HostBridge(const havel::HostContext &ctx)
    : pImpl(std::make_unique<Impl>(ctx, nullptr)) {}

HostBridge::HostBridge(const havel::HostContext &ctx, const ExecutionPolicy &policy)
    : pImpl(std::make_unique<Impl>(ctx, &policy)) {}

HostBridge::~HostBridge() = default;

void HostBridge::shutdown() {
  clear();
  pImpl->services_.clear();
  pImpl->vm_ = nullptr;
}

void HostBridge::clear() {
  pImpl->mode_bindings_.clear();
  pImpl->mode_definition_order_.clear();
  pImpl->hotkey_binding_keys_.clear();
}

void HostBridge::install() {
  if (!pImpl->vm_) {
    std::cerr << "HostBridge: No VM bound, cannot install\n";
    return;
  }

  // Install core modules (eager)
  installCoreModules();
}

void HostBridge::bindToVM(VM &vm) {
  pImpl->vm_ = &vm;
}

VM* HostBridge::getVM() const {
  return pImpl->vm_;
}

// ==================================================================
// Module Loading (Lazy)
// ==================================================================

bool HostBridge::loadModule(const std::string &moduleName) {
  if (!pImpl->registry_.isAvailable(moduleName)) {
    std::cerr << "HostBridge: Module '" << moduleName << "' not available\n";
    return false;
  }

  if (pImpl->registry_.isLoaded(moduleName)) {
    return true; // Already loaded
  }

  // Check permissions for module capabilities
  auto caps = pImpl->registry_.getCapabilities(moduleName);
  for (auto cap : caps) {
    if (!hasPermission(cap)) {
      std::cerr << "HostBridge: Module '" << moduleName << "' requires permission\n";
      return false;
    }
  }

  // Load and install the module
  pImpl->registry_.markLoaded(moduleName);
  installLazyModule(moduleName);
  
  return true;
}

bool HostBridge::isModuleLoaded(const std::string &moduleName) const {
  return pImpl->registry_.isLoaded(moduleName);
}

bool HostBridge::isModuleAvailable(const std::string &moduleName) const {
  return pImpl->registry_.isAvailable(moduleName);
}

std::vector<std::string> HostBridge::getAvailableModules() const {
  return pImpl->registry_.getAvailable();
}

std::vector<std::string> HostBridge::getLoadedModules() const {
  return pImpl->registry_.getLoaded();
}

ModuleInfo HostBridge::getModuleInfo(const std::string &moduleName) const {
  return pImpl->registry_.getInfo(moduleName);
}

// ==================================================================
// Permission Boundary
// ==================================================================

bool HostBridge::hasPermission(ModuleCapability cap) const {
  return pImpl->permissions_.hasPermission(cap);
}

bool HostBridge::requestPermission(ModuleCapability cap) {
  return pImpl->permissions_.requestPermission(cap);
}

// ==================================================================
// Service Registration
// ==================================================================

void HostBridge::registerService(const std::string &name, 
                                  std::shared_ptr<ServiceHandle> service) {
  pImpl->services_[name] = service;
}

std::shared_ptr<ServiceHandle> HostBridge::getService(const std::string &name) const {
  auto it = pImpl->services_.find(name);
  if (it != pImpl->services_.end()) {
    return it->second;
  }
  return nullptr;
}

// ==================================================================
// Private Methods
// ==================================================================

void HostBridge::registerModeCallbacksInternal(const std::string &modeName,
                                                uint32_t conditionId,
                                                uint32_t enterId, 
                                                uint32_t exitId) {
  ModeBinding binding;
  binding.modeName = modeName;
  binding.condition_id = conditionId;
  binding.enter_id = enterId;
  binding.exit_id = exitId;
  pImpl->mode_bindings_[modeName] = std::move(binding);
  pImpl->mode_definition_order_.push_back(modeName);
}

void HostBridge::initBridges() {
  // Initialize bridge submodules
  // This is called during construction
}

void HostBridge::installCoreModules() {
  // Eagerly install core VM modules (stdlib)
  // These don't require permissions
  if (!pImpl->vm_) return;

  VMApi api(*pImpl->vm_);

  // Register stdlib modules (pure VM, no host access)
  // Math, String, Array, Object, Type, Regex, etc.
  
  // The actual registration happens through the modular bridge system
  // which is separate from this clean HostBridge
}

void HostBridge::installLazyModule(const std::string &moduleName) {
  if (!pImpl->vm_) return;

  VMApi api(*pImpl->vm_);

  // Install host service modules lazily
  // These require permissions and are only loaded on 'use' statement
  
  // Map module names to service registrations
  // This is where clipboard, window, fs, etc. get registered
}

bool HostBridge::checkPermissionInternal(ModuleCapability cap, 
                                          const std::string &operation) const {
  if (!hasPermission(cap)) {
    std::cerr << "HostBridge: Permission denied for '" << operation << "'\n";
    return false;
  }
  return true;
}

} // namespace havel::compiler
