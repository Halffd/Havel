#pragma once

#include "../../runtime/HostContext.hpp"
#include "Pipeline.hpp"
#include "VM.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>

namespace havel::compiler {

/**
 * HostBridgeCapabilities - Capability gating for security/sandboxing
 *
 * Controls which host features are available to scripts.
 * Used for:
 * - Sandboxing untrusted scripts
 * - Partial embedding (headless mode, CLI-only)
 * - User permission prompts
 */
struct HostBridgeCapabilities {
  bool ioControl = true;              // send, mouse, keyboard
  bool fileIO = true;                 // readFile, writeFile
  bool processExec = true;            // execute, getpid
  bool windowControl = true;          // window operations
  bool hotkeyControl = true;          // hotkey registration
  bool modeControl = true;            // mode system
  bool clipboardAccess = true;        // clipboard get/set
  bool screenshotAccess = true;       // screenshots
  bool asyncOps = true;               // sleep, timers
  bool audioControl = true;           // volume, mute
  bool brightnessControl = true;      // brightness, temperature
  bool automationControl = true;      // auto-clicker, automation
  bool browserControl = true;         // browser automation
  bool textChunkerAccess = true;      // text chunking
  bool inputRemapping = true;         // key remapping, macros
  bool altTabControl = true;          // window switcher

  // Convenience presets
  static HostBridgeCapabilities Full() { return {}; }

  static HostBridgeCapabilities Sandbox() {
    HostBridgeCapabilities caps;
    caps.fileIO = false;
    caps.processExec = false;
    caps.hotkeyControl = false;
    caps.windowControl = false;
    caps.automationControl = false;
    caps.browserControl = false;
    caps.inputRemapping = false;
    return caps;
  }

  static HostBridgeCapabilities Minimal() {
    HostBridgeCapabilities caps;
    caps.ioControl = true;  // Only basic IO
    caps.fileIO = false;
    caps.processExec = false;
    caps.windowControl = false;
    caps.hotkeyControl = false;
    caps.modeControl = false;
    caps.clipboardAccess = false;
    caps.screenshotAccess = false;
    caps.asyncOps = true;
    caps.audioControl = false;
    caps.brightnessControl = false;
    caps.automationControl = false;
    caps.browserControl = false;
    caps.textChunkerAccess = false;
    caps.inputRemapping = false;
    caps.altTabControl = false;
    return caps;
  }
};

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
  void setCapabilities(const HostBridgeCapabilities &caps) { caps_ = caps; }
  bool hasCapability(const std::string &name) const;

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
