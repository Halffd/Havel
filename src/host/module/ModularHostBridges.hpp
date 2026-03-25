#pragma once

/**
 * ModularHostBridges.hpp - Modular bridge components
 *
 * Each bridge handles a specific domain:
 * - IOBridge: keyboard, mouse input
 * - SystemBridge: filesystem, processes
 * - UIBridge: windows, clipboard, screenshots
 * - InputBridge: hotkeys, input remapping, AltTab
 * - MediaBridge: audio, brightness
 * - AsyncBridge: timers, async operations
 * - AutomationBridge: automation tasks
 * - BrowserBridge: browser automation
 * - ToolsBridge: text chunker
 */

#include "runtime/HostContext.hpp"
#include "ExecutionPolicy.hpp"
#include "havel-lang/compiler/bytecode/Pipeline.hpp"
#include "havel-lang/compiler/bytecode/VM.hpp"

#include <memory>
#include <string>
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
};

/**
 * IOBridge - Keyboard and mouse input control
 */
class IOBridge : public BridgeModule {
public:
  explicit IOBridge(const HostContext *ctx) : ctx_(ctx) {}
  void install(PipelineOptions &options) override;

private:
  const HostContext *ctx_;
  static BytecodeValue handleSend(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleSendKey(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleMouseMove(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleMouseClick(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleGetMousePosition(const std::vector<BytecodeValue> &args, const HostContext *ctx);
};

/**
 * SystemBridge - File system and process control
 */
class SystemBridge : public BridgeModule {
public:
  explicit SystemBridge(const HostContext *ctx) : ctx_(ctx) {}
  void install(PipelineOptions &options) override;

private:
  const HostContext *ctx_;
  // File operations
  static BytecodeValue handleFileRead(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleFileWrite(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleFileExists(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleFileSize(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleFileDelete(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  // Process operations
  static BytecodeValue handleProcessExecute(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleProcessGetPid(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleProcessGetPpid(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleProcessFind(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleProcessExists(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleProcessKill(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleProcessNice(const std::vector<BytecodeValue> &args, const HostContext *ctx);
};

/**
 * UIBridge - Window, clipboard, screenshot operations
 */
class UIBridge : public BridgeModule {
public:
  explicit UIBridge(const HostContext *ctx) : ctx_(ctx) {}
  void install(PipelineOptions &options) override;

private:
  const HostContext *ctx_;
  // Window operations
  static BytecodeValue handleWindowGetActive(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleWindowClose(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleWindowResize(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleWindowMove(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleWindowMoveToMonitor(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleWindowMoveToNextMonitor(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleWindowFocus(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleWindowMinimize(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleWindowMaximize(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  // Clipboard operations
  static BytecodeValue handleClipboardGet(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleClipboardSet(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleClipboardClear(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  // Screenshot operations
  static BytecodeValue handleScreenshotFull(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleScreenshotMonitor(const std::vector<BytecodeValue> &args, const HostContext *ctx);
};

/**
 * InputBridge - Hotkeys and input remapping
 */
class InputBridge : public BridgeModule {
public:
  explicit InputBridge(const HostContext *ctx) : ctx_(ctx) {}
  void install(PipelineOptions &options) override;

private:
  const HostContext *ctx_;
  static BytecodeValue handleHotkeyRegister(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleHotkeyTrigger(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleMapManagerMap(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleMapManagerGetCurrentProfile(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleAltTabShow(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleAltTabHide(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleAltTabToggle(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleAltTabNext(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleAltTabPrevious(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleAltTabSelect(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleAltTabGetWindows(const std::vector<BytecodeValue> &args, const HostContext *ctx);
};

/**
 * MediaBridge - Media playback control
 */
class MediaBridge : public BridgeModule {
public:
  explicit MediaBridge(const HostContext *ctx) : ctx_(ctx) {}
  void install(PipelineOptions &options) override;

private:
  const HostContext *ctx_;
  static BytecodeValue handleMediaPlayPause(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleMediaPlay(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleMediaPause(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleMediaStop(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleMediaNext(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleMediaPrevious(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleMediaGetVolume(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleMediaSetVolume(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleMediaGetActivePlayer(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleMediaSetActivePlayer(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleMediaGetAvailablePlayers(const std::vector<BytecodeValue> &args, const HostContext *ctx);
};

/**
 * AsyncBridge - Timers and async operations
 */
class AsyncBridge : public BridgeModule {
public:
  explicit AsyncBridge(const HostContext *ctx) : ctx_(ctx) {}
  void install(PipelineOptions &options) override;

private:
  const HostContext *ctx_;
  static BytecodeValue handleSleep(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleTimeNow(const std::vector<BytecodeValue> &args, const HostContext *ctx);
};

/**
 * AutomationBridge - Automation tasks
 */
class AutomationBridge : public BridgeModule {
public:
  explicit AutomationBridge(const HostContext *ctx) : ctx_(ctx) {}
  void install(PipelineOptions &options) override;

private:
  const HostContext *ctx_;
  static BytecodeValue handleAutomationCreateAutoClicker(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleAutomationCreateAutoRunner(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleAutomationCreateAutoKeyPresser(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleAutomationHasTask(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleAutomationRemoveTask(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleAutomationStopAll(const std::vector<BytecodeValue> &args, const HostContext *ctx);
};

/**
 * BrowserBridge - Browser automation
 */
class BrowserBridge : public BridgeModule {
public:
  explicit BrowserBridge(const HostContext *ctx) : ctx_(ctx) {}
  void install(PipelineOptions &options) override;

private:
  const HostContext *ctx_;
  static BytecodeValue handleBrowserConnect(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleBrowserConnectFirefox(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleBrowserDisconnect(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleBrowserIsConnected(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleBrowserOpen(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleBrowserNewTab(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleBrowserGoto(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleBrowserBack(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleBrowserForward(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleBrowserReload(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleBrowserListTabs(const std::vector<BytecodeValue> &args, const HostContext *ctx);
};

/**
 * ToolsBridge - Text chunker and utilities
 */
class ToolsBridge : public BridgeModule {
public:
  explicit ToolsBridge(const HostContext *ctx) : ctx_(ctx) {}
  void install(PipelineOptions &options) override;

private:
  const HostContext *ctx_;
  static BytecodeValue handleTextChunkerSetText(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleTextChunkerGetText(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleTextChunkerSetChunkSize(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleTextChunkerGetTotalChunks(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleTextChunkerGetCurrentChunk(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleTextChunkerSetCurrentChunk(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleTextChunkerGetChunk(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleTextChunkerGetNextChunk(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleTextChunkerGetPreviousChunk(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleTextChunkerGoToFirst(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleTextChunkerGoToLast(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleTextChunkerClear(const std::vector<BytecodeValue> &args, const HostContext *ctx);
};

/**
 * NetworkBridge - Network operations
 */
class NetworkBridge : public BridgeModule {
public:
  explicit NetworkBridge(const HostContext *ctx) : ctx_(ctx) {}
  void install(PipelineOptions &options) override;

private:
  const HostContext *ctx_;
  static BytecodeValue handleNetworkGet(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleNetworkPost(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleNetworkDownload(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleNetworkIsOnline(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleNetworkGetExternalIp(const std::vector<BytecodeValue> &args, const HostContext *ctx);
};

/**
 * AudioBridge - Audio control
 */
class AudioBridge : public BridgeModule {
public:
  explicit AudioBridge(const HostContext *ctx) : ctx_(ctx) {}
  void install(PipelineOptions &options) override;

private:
  const HostContext *ctx_;
  static BytecodeValue handleGetVolume(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleSetVolume(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleIsMuted(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleSetMute(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleToggleMute(const std::vector<BytecodeValue> &args, const HostContext *ctx);
};

/**
 * DisplayBridge - Display/monitor info
 */
class DisplayBridge : public BridgeModule {
public:
  explicit DisplayBridge(const HostContext *ctx) : ctx_(ctx) {}
  void install(PipelineOptions &options) override;

private:
  const HostContext *ctx_;
  static BytecodeValue handleGetMonitors(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleGetPrimary(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleGetCount(const std::vector<BytecodeValue> &args, const HostContext *ctx);
};

/**
 * ConfigBridge - Configuration access
 */
class ConfigBridge : public BridgeModule {
public:
  explicit ConfigBridge(const HostContext *ctx) : ctx_(ctx) {}
  void install(PipelineOptions &options) override;

private:
  const HostContext *ctx_;
  static BytecodeValue handleGet(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleSet(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleSave(const std::vector<BytecodeValue> &args, const HostContext *ctx);
};

/**
 * ModeBridge - Mode management
 */
class ModeBridge : public BridgeModule {
public:
  explicit ModeBridge(const HostContext *ctx) : ctx_(ctx) {}
  void install(PipelineOptions &options) override;

private:
  const HostContext *ctx_;
  static BytecodeValue handleGetCurrent(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleSet(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleGetPrevious(const std::vector<BytecodeValue> &args, const HostContext *ctx);
};

/**
 * TimerBridge - Timer operations
 */
class TimerBridge : public BridgeModule {
public:
  explicit TimerBridge(const HostContext *ctx) : ctx_(ctx) {}
  void install(PipelineOptions &options) override;

private:
  const HostContext *ctx_;
  static BytecodeValue handleAfter(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleEvery(const std::vector<BytecodeValue> &args, const HostContext *ctx);
};

/**
 * AsyncBridge - Async operations (already exists, just adding to header)
 */

/**
 * AppBridge - Application and system info
 */
class AppBridge : public BridgeModule {
public:
  explicit AppBridge(const HostContext *ctx) : ctx_(ctx) {}
  void install(PipelineOptions &options) override;

private:
  const HostContext *ctx_;
  static BytecodeValue handleAppGetName(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleAppGetVersion(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleAppGetOS(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleAppGetHostname(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleAppGetUsername(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleAppGetHomeDir(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleAppGetCpuCores(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleAppGetEnv(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleAppSetEnv(const std::vector<BytecodeValue> &args, const HostContext *ctx);
  static BytecodeValue handleAppOpenUrl(const std::vector<BytecodeValue> &args, const HostContext *ctx);
};

} // namespace havel::compiler
