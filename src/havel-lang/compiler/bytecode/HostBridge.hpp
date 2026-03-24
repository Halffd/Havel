#pragma once

#include "../../runtime/HostContext.hpp"
#include "Pipeline.hpp"
#include "VM.hpp"

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
class HostBridge : public std::enable_shared_from_this<HostBridge> {
public:
  HostBridge(const havel::HostContext &ctx);
  ~HostBridge();

  void install();
  void clear();
  void shutdown(); // Explicit shutdown to clear containers before exit

  // Accessors for stdlib registration
  PipelineOptions &options() { return options_; }
  const PipelineOptions &options() const { return options_; }

  // Get context (for embedders to inspect/modify)
  const HostContext &context() const { return *ctx_; }

  // Register custom module (embedder extension point)
  void registerModule(const HostModule &module);

  // Add vm_setup callback (accumulates with previous callbacks)
  void addVmSetup(std::function<void(VM &)> setupFn);

private:
  const havel::HostContext *ctx_;
  PipelineOptions options_;

  // Accumulated vm_setup callbacks
  std::vector<std::function<void(VM &)>> vm_setup_callbacks_;

  // Registered modules
  std::vector<HostModule> modules_;

  // Handler methods - IO
  BytecodeValue handleSend(const std::vector<BytecodeValue> &args);
  BytecodeValue handleSendKey(const std::vector<BytecodeValue> &args);
  BytecodeValue handleMouseMove(const std::vector<BytecodeValue> &args);
  BytecodeValue handleMouseClick(const std::vector<BytecodeValue> &args);
  BytecodeValue handleGetMousePosition(const std::vector<BytecodeValue> &args);

  // Handler methods - File System
  BytecodeValue handleFileRead(const std::vector<BytecodeValue> &args);
  BytecodeValue handleFileWrite(const std::vector<BytecodeValue> &args);
  BytecodeValue handleFileExists(const std::vector<BytecodeValue> &args);
  BytecodeValue handleFileSize(const std::vector<BytecodeValue> &args);
  BytecodeValue handleFileDelete(const std::vector<BytecodeValue> &args);

  // Handler methods - Process
  BytecodeValue handleProcessExecute(const std::vector<BytecodeValue> &args);
  BytecodeValue handleProcessGetPid(const std::vector<BytecodeValue> &args);
  BytecodeValue handleProcessGetPpid(const std::vector<BytecodeValue> &args);
  BytecodeValue handleProcessFind(const std::vector<BytecodeValue> &args);

  // Handler methods - Window
  BytecodeValue handleWindowGetActive(const std::vector<BytecodeValue> &args);
  BytecodeValue handleWindowClose(const std::vector<BytecodeValue> &args);
  BytecodeValue handleWindowResize(const std::vector<BytecodeValue> &args);
  BytecodeValue handleWindowMoveToMonitor(const std::vector<BytecodeValue> &args);
  BytecodeValue handleWindowMoveToNextMonitor(const std::vector<BytecodeValue> &args);
  BytecodeValue handleWindowOn(const std::vector<BytecodeValue> &args);

  // Handler methods - Hotkey
  BytecodeValue handleHotkeyRegister(const std::vector<BytecodeValue> &args);
  BytecodeValue handleHotkeyTrigger(const std::vector<BytecodeValue> &args);

  // Handler methods - Mode
  BytecodeValue handleModeDefine(const std::vector<BytecodeValue> &args);
  BytecodeValue handleModeSet(const std::vector<BytecodeValue> &args);
  BytecodeValue handleModeTick(const std::vector<BytecodeValue> &args);

  // Handler methods - Clipboard
  BytecodeValue handleClipboardGet(const std::vector<BytecodeValue> &args);
  BytecodeValue handleClipboardSet(const std::vector<BytecodeValue> &args);
  BytecodeValue handleClipboardClear(const std::vector<BytecodeValue> &args);

  // Handler methods - Screenshot
  BytecodeValue handleScreenshotFull(const std::vector<BytecodeValue> &args);
  BytecodeValue handleScreenshotMonitor(const std::vector<BytecodeValue> &args);
  BytecodeValue handleScreenshotWindow(const std::vector<BytecodeValue> &args);
  BytecodeValue handleScreenshotRegion(const std::vector<BytecodeValue> &args);

  // Handler methods - Async/Timer
  BytecodeValue handleSleep(const std::vector<BytecodeValue> &args);
  BytecodeValue handleTimeNow(const std::vector<BytecodeValue> &args);

  // Handler methods - Audio
  BytecodeValue handleAudioGetVolume(const std::vector<BytecodeValue> &args);
  BytecodeValue handleAudioSetVolume(const std::vector<BytecodeValue> &args);
  BytecodeValue handleAudioToggleMute(const std::vector<BytecodeValue> &args);
  BytecodeValue handleAudioSetMute(const std::vector<BytecodeValue> &args);
  BytecodeValue handleAudioIsMuted(const std::vector<BytecodeValue> &args);

  // Handler methods - Brightness
  BytecodeValue handleBrightnessGet(const std::vector<BytecodeValue> &args);
  BytecodeValue handleBrightnessSet(const std::vector<BytecodeValue> &args);
  BytecodeValue handleBrightnessGetTemp(const std::vector<BytecodeValue> &args);
  BytecodeValue handleBrightnessSetTemp(const std::vector<BytecodeValue> &args);

  // Handler methods - Automation
  BytecodeValue handleAutomationCreateAutoClicker(const std::vector<BytecodeValue> &args);
  BytecodeValue handleAutomationCreateAutoRunner(const std::vector<BytecodeValue> &args);
  BytecodeValue handleAutomationCreateAutoKeyPresser(const std::vector<BytecodeValue> &args);
  BytecodeValue handleAutomationHasTask(const std::vector<BytecodeValue> &args);
  BytecodeValue handleAutomationRemoveTask(const std::vector<BytecodeValue> &args);
  BytecodeValue handleAutomationStopAll(const std::vector<BytecodeValue> &args);

  // Handler methods - Browser
  BytecodeValue handleBrowserConnect(const std::vector<BytecodeValue> &args);
  BytecodeValue handleBrowserConnectFirefox(const std::vector<BytecodeValue> &args);
  BytecodeValue handleBrowserDisconnect(const std::vector<BytecodeValue> &args);
  BytecodeValue handleBrowserIsConnected(const std::vector<BytecodeValue> &args);
  BytecodeValue handleBrowserOpen(const std::vector<BytecodeValue> &args);
  BytecodeValue handleBrowserNewTab(const std::vector<BytecodeValue> &args);
  BytecodeValue handleBrowserGoto(const std::vector<BytecodeValue> &args);
  BytecodeValue handleBrowserBack(const std::vector<BytecodeValue> &args);
  BytecodeValue handleBrowserForward(const std::vector<BytecodeValue> &args);
  BytecodeValue handleBrowserReload(const std::vector<BytecodeValue> &args);
  BytecodeValue handleBrowserListTabs(const std::vector<BytecodeValue> &args);

  // Handler methods - TextChunker
  BytecodeValue handleTextChunkerSetText(const std::vector<BytecodeValue> &args);
  BytecodeValue handleTextChunkerGetText(const std::vector<BytecodeValue> &args);
  BytecodeValue handleTextChunkerSetChunkSize(const std::vector<BytecodeValue> &args);
  BytecodeValue handleTextChunkerGetTotalChunks(const std::vector<BytecodeValue> &args);
  BytecodeValue handleTextChunkerGetCurrentChunk(const std::vector<BytecodeValue> &args);
  BytecodeValue handleTextChunkerSetCurrentChunk(const std::vector<BytecodeValue> &args);
  BytecodeValue handleTextChunkerGetChunk(const std::vector<BytecodeValue> &args);
  BytecodeValue handleTextChunkerGetNextChunk(const std::vector<BytecodeValue> &args);
  BytecodeValue handleTextChunkerGetPreviousChunk(const std::vector<BytecodeValue> &args);
  BytecodeValue handleTextChunkerGoToFirst(const std::vector<BytecodeValue> &args);
  BytecodeValue handleTextChunkerGoToLast(const std::vector<BytecodeValue> &args);
  BytecodeValue handleTextChunkerClear(const std::vector<BytecodeValue> &args);

  // Callback management through VM (internal - not exposed as host functions)
  CallbackId registerCallback(const BytecodeValue &closure);
  BytecodeValue invokeCallback(CallbackId id,
                               const std::vector<BytecodeValue> &args = {});
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
std::shared_ptr<HostBridge> createHostBridge(const havel::HostContext &ctx);

} // namespace havel::compiler
