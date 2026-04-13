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

#include "ExecutionPolicy.hpp"
#include "havel-lang/compiler/core/Pipeline.hpp"
#include "havel-lang/compiler/vm/VM.hpp"
#include "havel-lang/runtime/HostContext.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

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

  const HostContext *ctx_;
  static Value handleSend(const std::vector<Value> &args,
                                  const HostContext *ctx);
  static Value handleSendKey(const std::vector<Value> &args,
                                     const HostContext *ctx);
  static Value handleSendText(const std::vector<Value> &args,
                                       const HostContext *ctx);
  static Value handleWait(const std::vector<Value> &args,
                                  const HostContext *ctx);
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
  static Value handleFileRead(const std::vector<Value> &args,
                                      const HostContext *ctx);
  static Value handleFileWrite(const std::vector<Value> &args,
                                       const HostContext *ctx);
  static Value handleFileExists(const std::vector<Value> &args,
                                        const HostContext *ctx);
  static Value handleFileSize(const std::vector<Value> &args,
                                      const HostContext *ctx);
  static Value handleFileDelete(const std::vector<Value> &args,
                                        const HostContext *ctx);
  // Process operations
  static Value
  handleProcessExecute(const std::vector<Value> &args,
                       const HostContext *ctx);
  static Value
  handleProcessGetPid(const std::vector<Value> &args,
                      const HostContext *ctx);
  static Value
  handleProcessGetPpid(const std::vector<Value> &args,
                       const HostContext *ctx);
  static Value handleProcessFind(const std::vector<Value> &args,
                                         const HostContext *ctx);
  static Value
  handleProcessExists(const std::vector<Value> &args,
                      const HostContext *ctx);
  static Value handleProcessKill(const std::vector<Value> &args,
                                         const HostContext *ctx);
  static Value handleProcessNice(const std::vector<Value> &args,
                                         const HostContext *ctx);
  static Value handleProcessRun(const std::vector<Value> &args,
                                        const HostContext *ctx);
  static Value
  handleProcessRunDetached(const std::vector<Value> &args,
                           const HostContext *ctx);
  // Alias from MediaBridge
  static Value handleMediaPlay(const std::vector<Value> &args,
                                       const HostContext *ctx);
  // System detection
  static Value
  handleSystemDetect(const std::vector<Value> &args,
                     const HostContext *ctx);
  static Value
  handleSystemHardware(const std::vector<Value> &args,
                       const HostContext *ctx);
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
  static Value
  handleWindowGetActive(const std::vector<Value> &args,
                        const HostContext *ctx);
  static Value handleWindowCmd(const std::vector<Value> &args,
                                       const HostContext *ctx);
  static Value handleWindowFind(const std::vector<Value> &args,
                                        const HostContext *ctx);
  static Value handleWindowClose(const std::vector<Value> &args,
                                         const HostContext *ctx);
  static Value
  handleWindowResize(const std::vector<Value> &args,
                     const HostContext *ctx);
  static Value handleWindowMove(const std::vector<Value> &args,
                                        const HostContext *ctx);
  static Value
  handleWindowMoveToMonitor(const std::vector<Value> &args,
                            const HostContext *ctx);
  static Value
  handleWindowMoveToNextMonitor(const std::vector<Value> &args,
                                const HostContext *ctx);
  static Value handleWindowFocus(const std::vector<Value> &args,
                                         const HostContext *ctx);
  static Value
  handleWindowMinimize(const std::vector<Value> &args,
                       const HostContext *ctx);
  static Value
  handleWindowMaximize(const std::vector<Value> &args,
                       const HostContext *ctx);
  static Value handleWindowHide(const std::vector<Value> &args,
                                        const HostContext *ctx);
  static Value handleWindowShow(const std::vector<Value> &args,
                                        const HostContext *ctx);
  // Window query functions
  static Value handleWindowAny(const std::vector<Value> &args,
                                       const HostContext *ctx);
  static Value handleWindowCount(const std::vector<Value> &args,
                                         const HostContext *ctx);
  static Value
  handleWindowFilter(const std::vector<Value> &args,
                     const HostContext *ctx);
  // Active window namespace functions
  static Value handleActiveGet(const std::vector<Value> &args,
                                       const HostContext *ctx);
  static Value handleActiveTitle(const std::vector<Value> &args,
                                         const HostContext *ctx);
  static Value handleActiveClass(const std::vector<Value> &args,
                                         const HostContext *ctx);
  static Value handleActiveExe(const std::vector<Value> &args,
                                       const HostContext *ctx);
  static Value handleActivePid(const std::vector<Value> &args,
                                       const HostContext *ctx);
  static Value handleActiveClose(const std::vector<Value> &args,
                                         const HostContext *ctx);
  static Value handleActiveMin(const std::vector<Value> &args,
                                       const HostContext *ctx);
  static Value handleActiveMax(const std::vector<Value> &args,
                                       const HostContext *ctx);
  static Value handleActiveHide(const std::vector<Value> &args,
                                        const HostContext *ctx);
  static Value handleActiveShow(const std::vector<Value> &args,
                                        const HostContext *ctx);
  static Value handleActiveMove(const std::vector<Value> &args,
                                        const HostContext *ctx);
  static Value
  handleActiveResize(const std::vector<Value> &args,
                     const HostContext *ctx);
  static Value
  handleWindowCloseObj(const std::vector<Value> &args,
                       const HostContext *ctx);
  static Value
  handleWindowHideObj(const std::vector<Value> &args,
                      const HostContext *ctx);
  static Value
  handleWindowShowObj(const std::vector<Value> &args,
                      const HostContext *ctx);
  static Value
  handleWindowFocusObj(const std::vector<Value> &args,
                       const HostContext *ctx);
  static Value
  handleWindowMinObj(const std::vector<Value> &args,
                     const HostContext *ctx);
  static Value
  handleWindowMaxObj(const std::vector<Value> &args,
                     const HostContext *ctx);
  static Value
  handleWindowResizeObj(const std::vector<Value> &args,
                        const HostContext *ctx);
  static Value
  handleWindowMoveObj(const std::vector<Value> &args,
                      const HostContext *ctx);
  // Clipboard operations
  static Value
  handleClipboardGet(const std::vector<Value> &args,
                     const HostContext *ctx);
  static Value
  handleClipboardSet(const std::vector<Value> &args,
                     const HostContext *ctx);
  static Value
  handleClipboardClear(const std::vector<Value> &args,
                       const HostContext *ctx);
  static Value
  handleSelectionGet(const std::vector<Value> &args,
                     const HostContext *ctx);
  static Value
  handleMousePosGet(const std::vector<Value> &args,
                    const HostContext *ctx);
  // Screenshot operations
  static Value
  handleScreenshotFull(const std::vector<Value> &args,
                       const HostContext *ctx);
  static Value
  handleScreenshotMonitor(const std::vector<Value> &args,
                          const HostContext *ctx);
  static Value handleGUINotify(const std::vector<Value> &args,
                                       const HostContext *ctx);
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
  static Value
  handleHotkeyRegister(const std::vector<Value> &args,
                       const HostContext *ctx);
  static Value
  handleHotkeyTrigger(const std::vector<Value> &args,
                      const HostContext *ctx);
  static Value
  handleHotkeyList(const std::vector<Value> &args,
                   const HostContext *ctx);
  static Value
  handleMapManagerMap(const std::vector<Value> &args,
                      const HostContext *ctx);
  static Value
  handleMapManagerGetCurrentProfile(const std::vector<Value> &args,
                                    const HostContext *ctx);
  static Value handleAltTabShow(const std::vector<Value> &args,
                                        const HostContext *ctx);
  static Value handleAltTabHide(const std::vector<Value> &args,
                                        const HostContext *ctx);
  static Value
  handleAltTabToggle(const std::vector<Value> &args,
                     const HostContext *ctx);
  static Value handleAltTabNext(const std::vector<Value> &args,
                                        const HostContext *ctx);
  static Value
  handleAltTabPrevious(const std::vector<Value> &args,
                       const HostContext *ctx);
  static Value
  handleAltTabSelect(const std::vector<Value> &args,
                     const HostContext *ctx);
  static Value
  handleAltTabGetWindows(const std::vector<Value> &args,
                         const HostContext *ctx);
};

/**
 * MediaBridge - Media playback control
 */
class MediaBridge : public BridgeModule {
public:
  explicit MediaBridge(const HostContext *ctx) : ctx_(ctx) {}
  void install(PipelineOptions &options) override;

  const HostContext *ctx_;
  static Value
  handleMediaPlayPause(const std::vector<Value> &args,
                       const HostContext *ctx);
  static Value handleMediaPlay(const std::vector<Value> &args,
                                       const HostContext *ctx);
  static Value handleMediaPause(const std::vector<Value> &args,
                                        const HostContext *ctx);
  static Value handleMediaStop(const std::vector<Value> &args,
                                       const HostContext *ctx);
  static Value handleMediaNext(const std::vector<Value> &args,
                                       const HostContext *ctx);
  static Value
  handleMediaPrevious(const std::vector<Value> &args,
                      const HostContext *ctx);
  static Value
  handleMediaGetVolume(const std::vector<Value> &args,
                       const HostContext *ctx);
  static Value
  handleMediaSetVolume(const std::vector<Value> &args,
                       const HostContext *ctx);
  static Value
  handleMediaGetActivePlayer(const std::vector<Value> &args,
                             const HostContext *ctx);
  static Value
  handleMediaSetActivePlayer(const std::vector<Value> &args,
                             const HostContext *ctx);
  static Value
  handleMediaGetAvailablePlayers(const std::vector<Value> &args,
                                 const HostContext *ctx);
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
  static Value handleSleep(const std::vector<Value> &args,
                                   const HostContext *ctx);
  static Value handleTimeNow(const std::vector<Value> &args,
                                     const HostContext *ctx);

  // Async task functions
  static Value handleAsyncRun(const std::vector<Value> &args,
                                      const HostContext *ctx);
  static Value handleAsyncAwait(const std::vector<Value> &args,
                                        const HostContext *ctx);
  static Value handleAsyncCancel(const std::vector<Value> &args,
                                         const HostContext *ctx);
  static Value
  handleAsyncIsRunning(const std::vector<Value> &args,
                       const HostContext *ctx);

  // Channel functions
  static Value
  handleChannelCreate(const std::vector<Value> &args,
                      const HostContext *ctx);
  static Value handleChannelSend(const std::vector<Value> &args,
                                         const HostContext *ctx);
  static Value
  handleChannelReceive(const std::vector<Value> &args,
                       const HostContext *ctx);
  static Value
  handleChannelTryReceive(const std::vector<Value> &args,
                          const HostContext *ctx);
  static Value
  handleChannelClose(const std::vector<Value> &args,
                     const HostContext *ctx);

  // Thread/Timer controls
  static Value
  handleThreadCreate(const std::vector<Value> &args,
                     const HostContext *ctx);
  static Value handleThreadSend(const std::vector<Value> &args,
                                        const HostContext *ctx);
  static Value handleThreadPause(const std::vector<Value> &args,
                                         const HostContext *ctx);
  static Value
  handleThreadResume(const std::vector<Value> &args,
                     const HostContext *ctx);
  static Value handleThreadStop(const std::vector<Value> &args,
                                        const HostContext *ctx);
  static Value
  handleThreadRunning(const std::vector<Value> &args,
                      const HostContext *ctx);
  static Value
  handleIntervalCreate(const std::vector<Value> &args,
                       const HostContext *ctx);
  static Value
  handleIntervalPause(const std::vector<Value> &args,
                      const HostContext *ctx);
  static Value
  handleIntervalResume(const std::vector<Value> &args,
                       const HostContext *ctx);
  static Value
  handleIntervalStop(const std::vector<Value> &args,
                     const HostContext *ctx);
  static Value
  handleTimeoutCreate(const std::vector<Value> &args,
                      const HostContext *ctx);
  static Value
  handleTimeoutCancel(const std::vector<Value> &args,
                      const HostContext *ctx);
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
  static Value
  handleAutomationCreateAutoClicker(const std::vector<Value> &args,
                                    const HostContext *ctx);
  static Value
  handleAutomationCreateAutoRunner(const std::vector<Value> &args,
                                   const HostContext *ctx);
  static Value
  handleAutomationCreateAutoKeyPresser(const std::vector<Value> &args,
                                       const HostContext *ctx);
  static Value
  handleAutomationHasTask(const std::vector<Value> &args,
                          const HostContext *ctx);
  static Value
  handleAutomationRemoveTask(const std::vector<Value> &args,
                             const HostContext *ctx);
  static Value
  handleAutomationStopAll(const std::vector<Value> &args,
                          const HostContext *ctx);
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
  static Value
  handleBrowserConnect(const std::vector<Value> &args,
                       const HostContext *ctx);
  static Value
  handleBrowserConnectFirefox(const std::vector<Value> &args,
                              const HostContext *ctx);
  static Value
  handleBrowserDisconnect(const std::vector<Value> &args,
                          const HostContext *ctx);
  static Value
  handleBrowserIsConnected(const std::vector<Value> &args,
                           const HostContext *ctx);
  static Value handleBrowserOpen(const std::vector<Value> &args,
                                         const HostContext *ctx);
  static Value
  handleBrowserNewTab(const std::vector<Value> &args,
                      const HostContext *ctx);
  static Value handleBrowserGoto(const std::vector<Value> &args,
                                         const HostContext *ctx);
  static Value handleBrowserBack(const std::vector<Value> &args,
                                         const HostContext *ctx);
  static Value
  handleBrowserForward(const std::vector<Value> &args,
                       const HostContext *ctx);
  static Value
  handleBrowserReload(const std::vector<Value> &args,
                      const HostContext *ctx);
  static Value
  handleBrowserListTabs(const std::vector<Value> &args,
                        const HostContext *ctx);
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
  static Value
  handleTextChunkerSetText(const std::vector<Value> &args,
                           const HostContext *ctx);
  static Value
  handleTextChunkerGetText(const std::vector<Value> &args,
                           const HostContext *ctx);
  static Value
  handleTextChunkerSetChunkSize(const std::vector<Value> &args,
                                const HostContext *ctx);
  static Value
  handleTextChunkerGetTotalChunks(const std::vector<Value> &args,
                                  const HostContext *ctx);
  static Value
  handleTextChunkerGetCurrentChunk(const std::vector<Value> &args,
                                   const HostContext *ctx);
  static Value
  handleTextChunkerSetCurrentChunk(const std::vector<Value> &args,
                                   const HostContext *ctx);
  static Value
  handleTextChunkerGetChunk(const std::vector<Value> &args,
                            const HostContext *ctx);
  static Value
  handleTextChunkerGetNextChunk(const std::vector<Value> &args,
                                const HostContext *ctx);
  static Value
  handleTextChunkerGetPreviousChunk(const std::vector<Value> &args,
                                    const HostContext *ctx);
  static Value
  handleTextChunkerGoToFirst(const std::vector<Value> &args,
                             const HostContext *ctx);
  static Value
  handleTextChunkerGoToLast(const std::vector<Value> &args,
                            const HostContext *ctx);
  static Value
  handleTextChunkerClear(const std::vector<Value> &args,
                         const HostContext *ctx);
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
  static Value handleNetworkGet(const std::vector<Value> &args,
                                        const HostContext *ctx);
  static Value handleNetworkPost(const std::vector<Value> &args,
                                         const HostContext *ctx);
  static Value
  handleNetworkDownload(const std::vector<Value> &args,
                        const HostContext *ctx);
  static Value
  handleNetworkIsOnline(const std::vector<Value> &args,
                        const HostContext *ctx);
  static Value
  handleNetworkGetExternalIp(const std::vector<Value> &args,
                             const HostContext *ctx);
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
  static Value handleGetVolume(const std::vector<Value> &args,
                                       const HostContext *ctx);
  static Value handleSetVolume(const std::vector<Value> &args,
                                       const HostContext *ctx);
  static Value handleIsMuted(const std::vector<Value> &args,
                                     const HostContext *ctx);
  static Value handleSetMute(const std::vector<Value> &args,
                                     const HostContext *ctx);
  static Value handleToggleMute(const std::vector<Value> &args,
                                        const HostContext *ctx);
  static Value handleGetDevices(const std::vector<Value> &args,
                                        const HostContext *ctx);
  static Value
  handleFindDeviceByIndex(const std::vector<Value> &args,
                          const HostContext *ctx);
  static Value
  handleFindDeviceByName(const std::vector<Value> &args,
                         const HostContext *ctx);
  static Value
  handleSetDefaultOutput(const std::vector<Value> &args,
                         const HostContext *ctx);
  static Value
  handleGetDefaultOutput(const std::vector<Value> &args,
                         const HostContext *ctx);
  static Value
  handlePlayTestSound(const std::vector<Value> &args,
                      const HostContext *ctx);
  static Value
  handleIncreaseVolume(const std::vector<Value> &args,
                       const HostContext *ctx);
  static Value
  handleDecreaseVolume(const std::vector<Value> &args,
                       const HostContext *ctx);
};

/**
 * MPVBridge - MPV media player control
 */
class MPVBridge : public BridgeModule {
public:
  explicit MPVBridge(const HostContext *ctx) : ctx_(ctx) {}
  void install(PipelineOptions &options) override;

private:
  const HostContext *ctx_;
  static Value handleVolumeUp(const std::vector<Value> &args,
                                      const HostContext *ctx);
  static Value handleVolumeDown(const std::vector<Value> &args,
                                        const HostContext *ctx);
  static Value handleToggleMute(const std::vector<Value> &args,
                                        const HostContext *ctx);
  static Value handleStop(const std::vector<Value> &args,
                                  const HostContext *ctx);
  static Value handleNext(const std::vector<Value> &args,
                                  const HostContext *ctx);
  static Value handlePrevious(const std::vector<Value> &args,
                                      const HostContext *ctx);
  static Value handleSeek(const std::vector<Value> &args,
                                  const HostContext *ctx);
  static Value handleSubSeek(const std::vector<Value> &args,
                                     const HostContext *ctx);
  static Value handleAddSpeed(const std::vector<Value> &args,
                                      const HostContext *ctx);
  static Value handleAddSubScale(const std::vector<Value> &args,
                                         const HostContext *ctx);
  static Value handleAddSubDelay(const std::vector<Value> &args,
                                         const HostContext *ctx);
  static Value handleCycle(const std::vector<Value> &args,
                                   const HostContext *ctx);
  static Value
  handleCopySubtitle(const std::vector<Value> &args,
                     const HostContext *ctx);
  static Value handleIPCSet(const std::vector<Value> &args,
                                    const HostContext *ctx);
  static Value handleIPCRestart(const std::vector<Value> &args,
                                        const HostContext *ctx);
  static Value handleScreenshot(const std::vector<Value> &args,
                                        const HostContext *ctx);
  static Value handleCmd(const std::vector<Value> &args,
                                 const HostContext *ctx);
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
  static Value handleGetMonitors(const std::vector<Value> &args,
                                         const HostContext *ctx);
  static Value handleGetPrimary(const std::vector<Value> &args,
                                        const HostContext *ctx);
  static Value handleGetCount(const std::vector<Value> &args,
                                      const HostContext *ctx);
  static Value
  handleGetMonitorsArea(const std::vector<Value> &args,
                        const HostContext *ctx);
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
  static Value handleGet(const std::vector<Value> &args,
                                 const HostContext *ctx);
  static Value handleSet(const std::vector<Value> &args,
                                 const HostContext *ctx);
  static Value handleSave(const std::vector<Value> &args,
                                  const HostContext *ctx);
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
  static Value handleRegister(const std::vector<Value> &args,
                                      const HostContext *ctx);
  static Value handleGetCurrent(const std::vector<Value> &args,
                                        const HostContext *ctx);
  static Value handleSet(const std::vector<Value> &args,
                                 const HostContext *ctx);
  static Value handleGetPrevious(const std::vector<Value> &args,
                                         const HostContext *ctx);
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
  static Value handleAfter(const std::vector<Value> &args,
                                   const HostContext *ctx);
  static Value handleEvery(const std::vector<Value> &args,
                                   const HostContext *ctx);
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
  static Value handleAppGetName(const std::vector<Value> &args,
                                        const HostContext *ctx);
  static Value
  handleAppGetVersion(const std::vector<Value> &args,
                      const HostContext *ctx);
  static Value handleAppGetOS(const std::vector<Value> &args,
                                      const HostContext *ctx);
  static Value
  handleAppGetHostname(const std::vector<Value> &args,
                       const HostContext *ctx);
  static Value
  handleAppGetUsername(const std::vector<Value> &args,
                       const HostContext *ctx);
  static Value
  handleAppGetHomeDir(const std::vector<Value> &args,
                      const HostContext *ctx);
  static Value
  handleAppGetCpuCores(const std::vector<Value> &args,
                       const HostContext *ctx);
  static Value handleAppGetEnv(const std::vector<Value> &args,
                                       const HostContext *ctx);
  static Value handleAppSetEnv(const std::vector<Value> &args,
                                       const HostContext *ctx);
  static Value handleAppOpenUrl(const std::vector<Value> &args,
                                        const HostContext *ctx);
};

} // namespace havel::compiler
