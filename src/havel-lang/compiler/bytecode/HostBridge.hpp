#pragma once

#include "../../runtime/HostContext.hpp"
#include "Pipeline.hpp"
#include "VM.hpp"
#include "ModuleLoader.hpp"
#include "HostBridgeCapabilities.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>

namespace havel::compiler {

/**
 * Forward declarations for modular bridge components
 */
class IOBridge;
class SystemBridge;
class UIBridge;
class InputBridge;
class MediaBridge;
class AsyncBridge;
class AutomationBridge;
class BrowserBridge;
class ToolsBridge;

/**
 * HostBridge - Composite bridge delegating to modular components
 *
 * ARCHITECTURE:
 * - HostBridge composes specialized bridge modules
 * - Each module handles a specific capability domain
 * - Capabilities can be gated for sandboxing
 * - Easy to test individual modules
 * - Easy to embed with partial features
 */
class HostBridge : public std::enable_shared_from_this<HostBridge> {
public:
  explicit HostBridge(const havel::HostContext &ctx);
  explicit HostBridge(const havel::HostContext &ctx,
                      const HostBridgeCapabilities &caps);
  ~HostBridge();

  void install();
  void clear();
  void shutdown();

  // Accessors
  PipelineOptions &options() { return options_; }
  const PipelineOptions &options() const { return options_; }
  const HostContext &context() const { return *ctx_; }
  const HostBridgeCapabilities &capabilities() const { return caps_; }

  // Module registration
  void registerModule(const HostModule &module);
  void addVmSetup(std::function<void(VM &)> setupFn);

  // Capability management
  void setCapabilities(const HostBridgeCapabilities &caps) {
    caps_ = caps;
    moduleLoader_.setCapabilities(caps);
  }
  bool hasCapability(Capability cap) const { return caps_.has(cap); }

  // Runtime capability check for function calls
  bool checkFunctionCapability(const std::string &funcName) const {
    return moduleLoader_.checkCapability(funcName);
  }

  // Module loading (lazy loading)
  ModuleLoader &moduleLoader() { return moduleLoader_; }
  const ModuleLoader &moduleLoader() const { return moduleLoader_; }

  // Import system
  bool import(const std::string &importSpec);

  // Mode binding state (for mode system)
  struct ModeBinding {
    std::optional<CallbackId> condition_id;
    std::optional<CallbackId> enter_id;
    std::optional<CallbackId> exit_id;
  };

private:
  const havel::HostContext *ctx_;
  PipelineOptions options_;
  HostBridgeCapabilities caps_;

  std::vector<std::function<void(VM &)>> vm_setup_callbacks_;
  std::vector<HostModule> modules_;

  // Module loader (lazy loading, capability gating)
  ModuleLoader moduleLoader_;

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
  std::unique_ptr<AsyncBridge> asyncBridge_;
  std::unique_ptr<AutomationBridge> automationBridge_;
  std::unique_ptr<BrowserBridge> browserBridge_;
  std::unique_ptr<ToolsBridge> toolsBridge_;

  // Initialize bridge modules
  void initBridges();
};

std::shared_ptr<HostBridge> createHostBridge(const havel::HostContext &ctx);
std::shared_ptr<HostBridge>
createHostBridge(const havel::HostContext &ctx,
                 const HostBridgeCapabilities &caps);

} // namespace havel::compiler
