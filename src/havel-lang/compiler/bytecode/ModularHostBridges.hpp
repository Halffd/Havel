#pragma once

/**
 * ModularHostBridges.hpp - Modular bridge components
 *
 * Each bridge handles a specific capability domain:
 * - IOBridge: keyboard, mouse input
 * - SystemBridge: filesystem, processes
 * - UIBridge: windows, clipboard, screenshots
 * - InputBridge: hotkeys, input remapping
 * - MediaBridge: audio, brightness
 * - AsyncBridge: timers, async operations
 * - AutomationBridge: automation tasks
 * - BrowserBridge: browser automation
 * - ToolsBridge: text chunker, utilities
 */

#include "HostBridge.hpp"
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
 * BridgeModule - Base interface for bridge components
 */
class BridgeModule {
public:
  virtual ~BridgeModule() = default;
  virtual void install(PipelineOptions &options) = 0;
  virtual bool isEnabled(const HostBridgeCapabilities &caps) const = 0;
};

/**
 * IOBridge - Keyboard and mouse input control
 */
class IOBridge : public BridgeModule {
public:
  explicit IOBridge(const HostContext *ctx) : ctx_(ctx) {}
  void install(PipelineOptions &options) override;
  bool isEnabled(const HostBridgeCapabilities &caps) const override {
    return caps.has(Capability::IO);
  }

private:
  const HostContext *ctx_;

  // Handlers
  static BytecodeValue handleSend(const std::vector<BytecodeValue> &args,
                                  const HostContext *ctx);
  static BytecodeValue handleSendKey(const std::vector<BytecodeValue> &args,
                                     const HostContext *ctx);
  static BytecodeValue
  handleMouseMove(const std::vector<BytecodeValue> &args,
                  const HostContext *ctx);
  static BytecodeValue
  handleMouseClick(const std::vector<BytecodeValue> &args,
                   const HostContext *ctx);
  static BytecodeValue
  handleGetMousePosition(const std::vector<BytecodeValue> &args,
                         const HostContext *ctx);
};

/**
 * SystemBridge - File system and process control
 */
class SystemBridge : public BridgeModule {
public:
  explicit SystemBridge(const HostContext *ctx) : ctx_(ctx) {}
  void install(PipelineOptions &options) override;
  bool isEnabled(const HostBridgeCapabilities &caps) const override {
    return caps.has(Capability::FileIO) || caps.has(Capability::ProcessExec);
  }

private:
  const HostContext *ctx_;

  // File handlers
  static BytecodeValue handleFileRead(const std::vector<BytecodeValue> &args,
                                      const HostContext *ctx);
  static BytecodeValue handleFileWrite(const std::vector<BytecodeValue> &args,
                                       const HostContext *ctx);
  static BytecodeValue handleFileExists(const std::vector<BytecodeValue> &args,
                                        const HostContext *ctx);
  static BytecodeValue handleFileSize(const std::vector<BytecodeValue> &args,
                                      const HostContext *ctx);
  static BytecodeValue handleFileDelete(const std::vector<BytecodeValue> &args,
                                        const HostContext *ctx);

  // Process handlers
  static BytecodeValue
  handleProcessExecute(const std::vector<BytecodeValue> &args,
                       const HostContext *ctx);
  static BytecodeValue handleProcessGetPid(const std::vector<BytecodeValue> &args,
                                           const HostContext *ctx);
  static BytecodeValue handleProcessGetPpid(const std::vector<BytecodeValue> &args,
                                            const HostContext *ctx);
  static BytecodeValue
  handleProcessFind(const std::vector<BytecodeValue> &args,
                    const HostContext *ctx);
};

/**
 * UIBridge - Window, clipboard, screenshot operations
 */
class UIBridge : public BridgeModule {
public:
  explicit UIBridge(const HostContext *ctx) : ctx_(ctx) {}
  void install(PipelineOptions &options) override;
  bool isEnabled(const HostBridgeCapabilities &caps) const override {
    return caps.has(Capability::WindowControl) || caps.has(Capability::ClipboardAccess) || caps.has(Capability::ScreenshotAccess);
  }

private:
  const HostContext *ctx_;

  // Window handlers
  static BytecodeValue
  handleWindowGetActive(const std::vector<BytecodeValue> &args,
                        const HostContext *ctx);
  static BytecodeValue handleWindowClose(const std::vector<BytecodeValue> &args,
                                         const HostContext *ctx);
  static BytecodeValue handleWindowResize(const std::vector<BytecodeValue> &args,
                                          const HostContext *ctx);
  static BytecodeValue
  handleWindowMoveToMonitor(const std::vector<BytecodeValue> &args,
                            const HostContext *ctx);
  static BytecodeValue
  handleWindowMoveToNextMonitor(const std::vector<BytecodeValue> &args,
                                const HostContext *ctx);

  // Clipboard handlers
  static BytecodeValue
  handleClipboardGet(const std::vector<BytecodeValue> &args,
                     const HostContext *ctx);
  static BytecodeValue
  handleClipboardSet(const std::vector<BytecodeValue> &args,
                     const HostContext *ctx);
  static BytecodeValue
  handleClipboardClear(const std::vector<BytecodeValue> &args,
                       const HostContext *ctx);

  // Screenshot handlers
  static BytecodeValue
  handleScreenshotFull(const std::vector<BytecodeValue> &args,
                       const HostContext *ctx);
  static BytecodeValue
  handleScreenshotMonitor(const std::vector<BytecodeValue> &args,
                          const HostContext *ctx);
};

/**
 * InputBridge - Hotkeys and input remapping
 */
class InputBridge : public BridgeModule {
public:
  explicit InputBridge(const HostContext *ctx) : ctx_(ctx) {}
  void install(PipelineOptions &options) override;
  bool isEnabled(const HostBridgeCapabilities &caps) const override {
    return caps.has(Capability::HotkeyControl) || caps.has(Capability::InputRemapping) || caps.has(Capability::AltTabControl);
  }

private:
  const HostContext *ctx_;

  // Hotkey handlers
  static BytecodeValue
  handleHotkeyRegister(const std::vector<BytecodeValue> &args,
                       const HostContext *ctx);
  static BytecodeValue
  handleHotkeyTrigger(const std::vector<BytecodeValue> &args,
                      const HostContext *ctx);

  // MapManager handlers (stubs - require IO)
  static BytecodeValue handleMapManagerMap(const std::vector<BytecodeValue> &args,
                                           const HostContext *ctx);
  static BytecodeValue
  handleMapManagerGetCurrentProfile(const std::vector<BytecodeValue> &args,
                                    const HostContext *ctx);

  // AltTab handlers
  static BytecodeValue handleAltTabShow(const std::vector<BytecodeValue> &args,
                                        const HostContext *ctx);
  static BytecodeValue handleAltTabHide(const std::vector<BytecodeValue> &args,
                                        const HostContext *ctx);
  static BytecodeValue handleAltTabToggle(const std::vector<BytecodeValue> &args,
                                          const HostContext *ctx);
  static BytecodeValue handleAltTabNext(const std::vector<BytecodeValue> &args,
                                        const HostContext *ctx);
  static BytecodeValue
  handleAltTabPrevious(const std::vector<BytecodeValue> &args,
                       const HostContext *ctx);
  static BytecodeValue handleAltTabSelect(const std::vector<BytecodeValue> &args,
                                          const HostContext *ctx);
  static BytecodeValue
  handleAltTabGetWindows(const std::vector<BytecodeValue> &args,
                         const HostContext *ctx);
};

/**
 * MediaBridge - Audio and brightness control
 */
class MediaBridge : public BridgeModule {
public:
  explicit MediaBridge(const HostContext *ctx) : ctx_(ctx) {}
  void install(PipelineOptions &options) override;
  bool isEnabled(const HostBridgeCapabilities &caps) const override {
    return caps.has(Capability::AudioControl) || caps.has(Capability::BrightnessControl);
  }

private:
  const HostContext *ctx_;

  // Audio handlers
  static BytecodeValue
  handleAudioGetVolume(const std::vector<BytecodeValue> &args,
                       const HostContext *ctx);
  static BytecodeValue
  handleAudioSetVolume(const std::vector<BytecodeValue> &args,
                       const HostContext *ctx);
  static BytecodeValue
  handleAudioToggleMute(const std::vector<BytecodeValue> &args,
                        const HostContext *ctx);
  static BytecodeValue handleAudioSetMute(const std::vector<BytecodeValue> &args,
                                          const HostContext *ctx);
  static BytecodeValue handleAudioIsMuted(const std::vector<BytecodeValue> &args,
                                          const HostContext *ctx);

  // Brightness handlers
  static BytecodeValue
  handleBrightnessGet(const std::vector<BytecodeValue> &args,
                      const HostContext *ctx);
  static BytecodeValue
  handleBrightnessSet(const std::vector<BytecodeValue> &args,
                      const HostContext *ctx);
  static BytecodeValue
  handleBrightnessGetTemp(const std::vector<BytecodeValue> &args,
                          const HostContext *ctx);
  static BytecodeValue
  handleBrightnessSetTemp(const std::vector<BytecodeValue> &args,
                          const HostContext *ctx);
};

/**
 * AsyncBridge - Timers and async operations
 */
class AsyncBridge : public BridgeModule {
public:
  explicit AsyncBridge(const HostContext *ctx) : ctx_(ctx) {}
  void install(PipelineOptions &options) override;
  bool isEnabled(const HostBridgeCapabilities &caps) const override {
    return caps.has(Capability::AsyncOps);
  }

private:
  const HostContext *ctx_;

  static BytecodeValue handleSleep(const std::vector<BytecodeValue> &args,
                                   const HostContext *ctx);
  static BytecodeValue handleTimeNow(const std::vector<BytecodeValue> &args,
                                     const HostContext *ctx);
};

/**
 * AutomationBridge - Automation tasks
 */
class AutomationBridge : public BridgeModule {
public:
  explicit AutomationBridge(const HostContext *ctx) : ctx_(ctx) {}
  void install(PipelineOptions &options) override;
  bool isEnabled(const HostBridgeCapabilities &caps) const override {
    return caps.has(Capability::AutomationControl);
  }

private:
  const HostContext *ctx_;

  static BytecodeValue
  handleAutomationCreateAutoClicker(const std::vector<BytecodeValue> &args,
                                    const HostContext *ctx);
  static BytecodeValue
  handleAutomationCreateAutoRunner(const std::vector<BytecodeValue> &args,
                                   const HostContext *ctx);
  static BytecodeValue
  handleAutomationCreateAutoKeyPresser(const std::vector<BytecodeValue> &args,
                                       const HostContext *ctx);
  static BytecodeValue
  handleAutomationHasTask(const std::vector<BytecodeValue> &args,
                          const HostContext *ctx);
  static BytecodeValue
  handleAutomationRemoveTask(const std::vector<BytecodeValue> &args,
                             const HostContext *ctx);
  static BytecodeValue
  handleAutomationStopAll(const std::vector<BytecodeValue> &args,
                          const HostContext *ctx);
};

/**
 * BrowserBridge - Browser automation
 */
class BrowserBridge : public BridgeModule {
public:
  explicit BrowserBridge(const HostContext *ctx) : ctx_(ctx) {}
  void install(PipelineOptions &options) override;
  bool isEnabled(const HostBridgeCapabilities &caps) const override {
    return caps.has(Capability::BrowserControl);
  }

private:
  const HostContext *ctx_;

  static BytecodeValue handleBrowserConnect(const std::vector<BytecodeValue> &args,
                                            const HostContext *ctx);
  static BytecodeValue
  handleBrowserConnectFirefox(const std::vector<BytecodeValue> &args,
                              const HostContext *ctx);
  static BytecodeValue
  handleBrowserDisconnect(const std::vector<BytecodeValue> &args,
                          const HostContext *ctx);
  static BytecodeValue
  handleBrowserIsConnected(const std::vector<BytecodeValue> &args,
                           const HostContext *ctx);
  static BytecodeValue handleBrowserOpen(const std::vector<BytecodeValue> &args,
                                         const HostContext *ctx);
  static BytecodeValue handleBrowserNewTab(const std::vector<BytecodeValue> &args,
                                           const HostContext *ctx);
  static BytecodeValue handleBrowserGoto(const std::vector<BytecodeValue> &args,
                                         const HostContext *ctx);
  static BytecodeValue handleBrowserBack(const std::vector<BytecodeValue> &args,
                                         const HostContext *ctx);
  static BytecodeValue handleBrowserForward(const std::vector<BytecodeValue> &args,
                                            const HostContext *ctx);
  static BytecodeValue handleBrowserReload(const std::vector<BytecodeValue> &args,
                                           const HostContext *ctx);
  static BytecodeValue
  handleBrowserListTabs(const std::vector<BytecodeValue> &args,
                        const HostContext *ctx);
};

/**
 * ToolsBridge - Text chunker and utilities
 */
class ToolsBridge : public BridgeModule {
public:
  explicit ToolsBridge(const HostContext *ctx) : ctx_(ctx) {}
  void install(PipelineOptions &options) override;
  bool isEnabled(const HostBridgeCapabilities &caps) const override {
    return caps.has(Capability::TextChunkerAccess);
  }

private:
  const HostContext *ctx_;

  static BytecodeValue handleTextChunkerSetText(const std::vector<BytecodeValue> &args,
                                                const HostContext *ctx);
  static BytecodeValue handleTextChunkerGetText(const std::vector<BytecodeValue> &args,
                                                const HostContext *ctx);
  static BytecodeValue
  handleTextChunkerSetChunkSize(const std::vector<BytecodeValue> &args,
                                const HostContext *ctx);
  static BytecodeValue
  handleTextChunkerGetTotalChunks(const std::vector<BytecodeValue> &args,
                                  const HostContext *ctx);
  static BytecodeValue
  handleTextChunkerGetCurrentChunk(const std::vector<BytecodeValue> &args,
                                   const HostContext *ctx);
  static BytecodeValue
  handleTextChunkerSetCurrentChunk(const std::vector<BytecodeValue> &args,
                                   const HostContext *ctx);
  static BytecodeValue handleTextChunkerGetChunk(const std::vector<BytecodeValue> &args,
                                                 const HostContext *ctx);
  static BytecodeValue
  handleTextChunkerGetNextChunk(const std::vector<BytecodeValue> &args,
                                const HostContext *ctx);
  static BytecodeValue
  handleTextChunkerGetPreviousChunk(const std::vector<BytecodeValue> &args,
                                    const HostContext *ctx);
  static BytecodeValue
  handleTextChunkerGoToFirst(const std::vector<BytecodeValue> &args,
                             const HostContext *ctx);
  static BytecodeValue handleTextChunkerGoToLast(const std::vector<BytecodeValue> &args,
                                                 const HostContext *ctx);
  static BytecodeValue handleTextChunkerClear(const std::vector<BytecodeValue> &args,
                                              const HostContext *ctx);
};

} // namespace havel::compiler
