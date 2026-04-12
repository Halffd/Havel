#pragma once

#include "../../../extensions/ExtensionLoader.hpp"
#include "../../../extensions/HavelCAPI.h"
#include "../../../host/module/ExecutionPolicy.hpp"
#include "../../../host/module/ModuleLoader.hpp"
#include "../../runtime/HostContext.hpp"
#include "../core/Pipeline.hpp"
#include "../vm/VM.hpp"

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace havel::compiler {

// Forward declarations for modular bridge components
class IOBridge;
class SystemBridge;
class UIBridge;
class InputBridge;
class MediaBridge;
class NetworkBridge;
class AudioBridge;
class MPVBridge;
class DisplayBridge;
class ConfigBridge;
class ModeBridge;
class TimerBridge;
class AppBridge;
class ConcurrencyBridge;
class AutomationBridge;
class BrowserBridge;
class ToolsBridge;

/**
 * HostBridge - Composite bridge delegating to modular components
 *
 * ARCHITECTURE:
 * - HostBridge composes specialized bridge modules
 * - Each module handles a specific capability domain
 * - Execution policy is OPTIONAL (for embedding/sandboxing)
 * - Default: FULL ACCESS (no friction for normal users)
 */
class HostBridge : public std::enable_shared_from_this<HostBridge> {
public:
  explicit HostBridge(const ::havel::HostContext &ctx);
  explicit HostBridge(const ::havel::HostContext &ctx,
                      const ExecutionPolicy &policy);
  ~HostBridge();

  void install();
  void clear();
  void shutdown();

  // Accessors
  PipelineOptions &options() { return options_; }
  const PipelineOptions &options() const { return options_; }
  const HostContext &context() const { return *ctx_; }

  // Module registration
  void registerModule(const HostModule &module);
  void addVmSetup(std::function<void(VM &)> setupFn);

  // Execution policy (optional - for embedding)
  void setExecutionPolicy(const ExecutionPolicy &policy) {
    policy_ = policy;
    moduleLoader_.setExecutionPolicy(policy);
  }
  const ExecutionPolicy &executionPolicy() const { return policy_; }

  // Module loading (lazy loading)
  HostModuleLoader &moduleLoader() { return moduleLoader_; }

  // Timer checking (to be called from main event loop)
  void checkTimers();
  const HostModuleLoader &moduleLoader() const { return moduleLoader_; }

  // Extension loading (native .so modules)
  ExtensionLoader &extensionLoader() { return *extensionLoader_; }
  const ExtensionLoader &extensionLoader() const { return *extensionLoader_; }
  void loadExtension(const std::string &name);

  // Extension function registration (C API)
  void registerExtensionFunction(const std::string &fullName,
                                 BytecodeHostFunction fn);

  // Import system
  bool import(const std::string &importSpec);

  /** Lazy host module loading for \c use statements (requires \c ctx_->vm). */
  bool loadModule(const std::string &moduleName) {
    if (!ctx_ || !ctx_->vm) {
      return false;
    }
    return moduleLoader_.loadModule(moduleName, *ctx_->vm);
  }

  bool isModuleAvailable(const std::string &moduleName) const {
    const auto names = moduleLoader_.getAvailableModules();
    return std::find(names.begin(), names.end(), moduleName) != names.end();
  }

  // Mode callback registration
  void registerModeCallbacks(const std::string &modeName,
                             CallbackId conditionId, CallbackId enterId,
                             CallbackId exitId);

  // Mode binding state (for mode system)
  struct ModeBinding {
    std::string modeName;
    std::optional<CallbackId> condition_id;
    std::optional<CallbackId> enter_id;
    std::optional<CallbackId> exit_id;
  };

private:
  const ::havel::HostContext *ctx_;
  PipelineOptions options_;
  ExecutionPolicy policy_; // Optional - defaults to allow all

  std::vector<std::function<void(VM &)>> vm_setup_callbacks_;
  std::vector<HostModule> modules_;

  // Module loader (lazy loading)
  HostModuleLoader moduleLoader_;

  // Extension loader (native .so modules)
  std::unique_ptr<ExtensionLoader> extensionLoader_;

  // Mode system state
  std::unordered_map<std::string, ModeBinding> mode_bindings_;
  std::vector<std::string> mode_definition_order_;
  std::unordered_map<CallbackId, std::string> hotkey_binding_keys_;

  // Modular bridge components
  std::unique_ptr<IOBridge> ioBridge_;
  std::unique_ptr<SystemBridge> systemBridge_;
  std::unique_ptr<UIBridge> uiBridge_;
  std::unique_ptr<InputBridge> inputBridge_;
  std::unique_ptr<MediaBridge> mediaBridge_;
  std::unique_ptr<NetworkBridge> networkBridge_;
  std::unique_ptr<AudioBridge> audioBridge_;
  std::unique_ptr<MPVBridge> mpvBridge_;
  std::unique_ptr<DisplayBridge> displayBridge_;
  std::unique_ptr<ConfigBridge> configBridge_;
  std::unique_ptr<ModeBridge> modeBridge_;
  std::unique_ptr<TimerBridge> timerBridge_;
  std::unique_ptr<AppBridge> appBridge_;
  std::unique_ptr<ConcurrencyBridge> concurrencyBridge_;
  std::unique_ptr<AutomationBridge> automationBridge_;
  std::unique_ptr<BrowserBridge> browserBridge_;
  std::unique_ptr<ToolsBridge> toolsBridge_;

  // Initialize bridge modules
  void initBridges();
};

std::shared_ptr<HostBridge> createHostBridge(const ::havel::HostContext &ctx);
std::shared_ptr<HostBridge> createHostBridge(const ::havel::HostContext &ctx,
                                             const ExecutionPolicy &policy);

} // namespace havel::compiler
