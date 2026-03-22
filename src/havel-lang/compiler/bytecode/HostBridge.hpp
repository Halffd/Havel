#pragma once

#include "Pipeline.hpp"
#include "VM.hpp"
#include "../../runtime/HostContext.hpp"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace havel::compiler {

/**
 * HostBridge - Bridges VM host functions to injected services
 * 
 * ARCHITECTURE:
 * - Dependencies are INJECTED via HostContext (not pulled from registry)
 * - Embedders provide custom capabilities via ctx.caps
 * - No hidden dependencies or global state
 * 
 * USAGE:
 *   HostContext ctx;
 *   ctx.io = std::make_shared<IO>();
 *   ctx.caps["custom"] = std::make_shared<MyCustomCap>();
 *   
 *   auto bridge = std::make_unique<HostBridge>(vm, ctx);
 *   bridge->install();
 */
class HostBridge
    : public std::enable_shared_from_this<HostBridge> {
public:
  HostBridge(const havel::HostContext& ctx);
  ~HostBridge();

  void install();
  void clear();
  void shutdown();  // Explicit shutdown to clear containers before exit

  // Accessors for stdlib registration
  PipelineOptions& options() { return options_; }
  const PipelineOptions& options() const { return options_; }
  
  // Get context (for embedders to inspect/modify)
  const HostContext& context() const { return *ctx_; }

  // Register custom module (embedder extension point)
  void registerModule(const HostModule& module);

  // Add vm_setup callback (accumulates with previous callbacks)
  void addVmSetup(std::function<void(VM&)> setupFn);

private:
  const havel::HostContext* ctx_;
  PipelineOptions options_;
  
  // Accumulated vm_setup callbacks
  std::vector<std::function<void(VM&)>> vm_setup_callbacks_;
  
  // Registered modules
  std::vector<HostModule> modules_;

  // Handler methods
  BytecodeValue handleWindowMoveToNextMonitor(
      const std::vector<BytecodeValue> &args);
  BytecodeValue handleWindowGetActive(const std::vector<BytecodeValue> &args);
  BytecodeValue handleWindowMoveToMonitor(const std::vector<BytecodeValue> &args);
  BytecodeValue handleWindowClose(const std::vector<BytecodeValue> &args);
  BytecodeValue handleWindowResize(const std::vector<BytecodeValue> &args);
  BytecodeValue handleWindowOn(const std::vector<BytecodeValue> &args);
  BytecodeValue handleSend(const std::vector<BytecodeValue> &args);
  BytecodeValue handleSendKey(const std::vector<BytecodeValue> &args);
  BytecodeValue handleMouseMove(const std::vector<BytecodeValue> &args);
  BytecodeValue handleMouseClick(const std::vector<BytecodeValue> &args);
  BytecodeValue handleGetMousePosition(const std::vector<BytecodeValue> &args);
  BytecodeValue handleHotkeyRegister(const std::vector<BytecodeValue> &args);
  BytecodeValue handleHotkeyTrigger(const std::vector<BytecodeValue> &args);
  BytecodeValue handleModeDefine(const std::vector<BytecodeValue> &args);
  BytecodeValue handleModeSet(const std::vector<BytecodeValue> &args);
  BytecodeValue handleModeTick(const std::vector<BytecodeValue> &args);
  BytecodeValue handleProcessFind(const std::vector<BytecodeValue> &args);
  BytecodeValue handleClipboardGet(const std::vector<BytecodeValue> &args);
  BytecodeValue handleClipboardSet(const std::vector<BytecodeValue> &args);
  BytecodeValue handleClipboardClear(const std::vector<BytecodeValue> &args);
  BytecodeValue handleScreenshotFull(const std::vector<BytecodeValue> &args);
  BytecodeValue handleScreenshotMonitor(const std::vector<BytecodeValue> &args);
  BytecodeValue handleScreenshotWindow(const std::vector<BytecodeValue> &args);
  BytecodeValue handleScreenshotRegion(const std::vector<BytecodeValue> &args);

  // Callback management through VM (internal - not exposed as host functions)
  CallbackId registerCallback(const BytecodeValue &closure);
  BytecodeValue invokeCallback(CallbackId id, std::span<BytecodeValue> args = {});
  void releaseCallback(CallbackId id);

  struct ModeBinding {
    std::optional<CallbackId> condition_id;
    std::optional<CallbackId> enter_id;
    std::optional<CallbackId> exit_id;
  };

  std::unordered_map<std::string, ModeBinding> mode_bindings_;
  std::vector<std::string> mode_definition_order_;
  std::unordered_map<CallbackId, std::string> hotkey_binding_keys_;
};

/**
 * Create HostBridge with injected context
 * 
 * This is the primary factory function for embedders.
 */
std::shared_ptr<HostBridge> createHostBridge(const havel::HostContext& ctx);

} // namespace havel::compiler
