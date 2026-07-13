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
 * - NetworkBridge: HTTP requests
 * - DisplayBridge: monitor info
 * - ConfigBridge: configuration
 * - ModeBridge: mode management
 * - TimerBridge: timers
 * - AppBridge: application info
 */

#include "havel-lang/host/ExecutionPolicy.hpp"
#include "havel-lang/compiler/core/Pipeline.hpp"
#include "havel-lang/compiler/vm/VM.hpp"
#include "havel-lang/runtime/HostContext.hpp"
#include "havel-lang/runtime/Modules.hpp"

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
  virtual void install(compiler::PipelineOptions &options) = 0;
};

/**
 * IOBridge - Keyboard and mouse input control
 */
class IOBridge : public BridgeModule {
public:
    explicit IOBridge(const HostContext *ctx) : ctx_(ctx) {}
    void install(compiler::PipelineOptions &options) override;

    const HostContext *ctx_;
    static Value handleSend(const std::vector<Value> &args,
                          const HostContext *ctx);
    static Value handleSendKey(const std::vector<Value> &args,
                             const HostContext *ctx);
    static Value handleSendText(const std::vector<Value> &args,
                              const HostContext *ctx);
    static Value handleWait(const std::vector<Value> &args,
                          const HostContext *ctx);
    static Value handleMouseClick(const std::vector<Value> &args,
                                const HostContext *ctx);
    static Value handleMouseMoveTo(const std::vector<Value> &args,
                                 const HostContext *ctx);
    static Value handleMouseMoveRel(const std::vector<Value> &args,
                                  const HostContext *ctx);
    static Value handleMouseScroll(const std::vector<Value> &args,
                                 const HostContext *ctx);
    static Value handleMouseDown(const std::vector<Value> &args,
                               const HostContext *ctx);
    static Value handleMouseUp(const std::vector<Value> &args,
                             const HostContext *ctx);
    static Value handleMousePos(const std::vector<Value> &args,
                              const HostContext *ctx);
    static Value handleMouseSetSpeed(const std::vector<Value> &args,
                                 const HostContext *ctx);
    static Value handleMouseSetAccel(const std::vector<Value> &args,
                                 const HostContext *ctx);
    static Value handleMouseSetDPI(const std::vector<Value> &args,
                                 const HostContext *ctx);
    static Value handleSuspend(const std::vector<Value> &args,
                             const HostContext *ctx);
    static Value handleKeyDown(const std::vector<Value> &args,
                               const HostContext *ctx);
    static Value handleKeyUp(const std::vector<Value> &args,
                             const HostContext *ctx);
    static Value handleGetKey(const std::vector<Value> &args,
                              const HostContext *ctx);
    static Value handleIsKeyPressed(const std::vector<Value> &args,
                                    const HostContext *ctx);
    static Value handleGetExecutorMode(const std::vector<Value> &args,
                                       const HostContext *ctx);
    static Value handleSetExecutorMode(const std::vector<Value> &args,
                                       const HostContext *ctx);
    static Value handleSendModifiers(const std::vector<Value> &args,
                                        const HostContext *ctx);
    static Value handleSendKeyState(const std::vector<Value> &args,
                                       const HostContext *ctx);
    // Forwarding declarations used by other bridge install()
    static Value handleSystemDetect(const std::vector<Value> &args,
                                      const HostContext *ctx);
    static Value handleSystemHardware(const std::vector<Value> &args,
                                       const HostContext *ctx);
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
    static Value handleProcessExecute(const std::vector<Value> &args,
                                       const HostContext *ctx);
    static Value handleProcessGetPid(const std::vector<Value> &args,
                                      const HostContext *ctx);
    static Value handleProcessGetPpid(const std::vector<Value> &args,
                                       const HostContext *ctx);
    static Value handleProcessFind(const std::vector<Value> &args,
                                    const HostContext *ctx);
    static Value handleProcessExists(const std::vector<Value> &args,
                                      const HostContext *ctx);
    static Value handleProcessKill(const std::vector<Value> &args,
                                    const HostContext *ctx);
    static Value handleProcessNice(const std::vector<Value> &args,
                                    const HostContext *ctx);
    static Value handleProcessRun(const std::vector<Value> &args,
                                   const HostContext *ctx);
    static Value handleProcessRunCapture(const std::vector<Value> &args,
                                          const HostContext *ctx);
    static Value handleProcessRunDetached(const std::vector<Value> &args,
                                           const HostContext *ctx);
    static Value handleMediaPlay(const std::vector<Value> &args,
                                  const HostContext *ctx);
    static Value handleWindowGetActive(const std::vector<Value> &args,
                                        const HostContext *ctx);
    static Value handleWindowCmd(const std::vector<Value> &args,
                                  const HostContext *ctx);
    static Value handleWindowFind(const std::vector<Value> &args,
                                   const HostContext *ctx);
    static Value handleWindowClose(const std::vector<Value> &args,
                                    const HostContext *ctx);
    static Value handleWindowResize(const std::vector<Value> &args,
                                     const HostContext *ctx);
    static Value handleWindowMove(const std::vector<Value> &args,
                                   const HostContext *ctx);
    static Value handleWindowMoveRel(const std::vector<Value> &args,
                                      const HostContext *ctx);
    static Value handleWindowMoveToMonitor(const std::vector<Value> &args,
                                            const HostContext *ctx);
    static Value handleWindowMoveToNextMonitor(const std::vector<Value> &args,
                                                 const HostContext *ctx);
    static Value handleWindowFocus(const std::vector<Value> &args,
                                    const HostContext *ctx);
    static Value handleWindowMinimize(const std::vector<Value> &args,
                                       const HostContext *ctx);
    static Value handleWindowMaximize(const std::vector<Value> &args,
                                       const HostContext *ctx);
    static Value handleWindowHide(const std::vector<Value> &args,
                                   const HostContext *ctx);
    static Value handleWindowShow(const std::vector<Value> &args,
                                   const HostContext *ctx);
    static Value handleWindowAny(const std::vector<Value> &args,
                                  const HostContext *ctx);
    static Value handleWindowCount(const std::vector<Value> &args,
                                    const HostContext *ctx);
    static Value handleWindowFilter(const std::vector<Value> &args,
                                     const HostContext *ctx);
    static Value handleWindowRestore(const std::vector<Value> &args,
                                      const HostContext *ctx);
    static Value handleWindowSnap(const std::vector<Value> &args,
                                   const HostContext *ctx);
    static Value handleWindowCenter(const std::vector<Value> &args,
                                     const HostContext *ctx);
    static Value handleWindowFullscreen(const std::vector<Value> &args,
                                         const HostContext *ctx);
    static Value handleWindowMoveResize(const std::vector<Value> &args,
                                         const HostContext *ctx);
    static Value handleWindowSetAlwaysOnTop(const std::vector<Value> &args,
                                             const HostContext *ctx);
    static Value handleWindowPos(const std::vector<Value> &args,
                                  const HostContext *ctx);
    static Value handleWindowList(const std::vector<Value> &args,
                                   const HostContext *ctx);
    static Value handleWindowTitle(const std::vector<Value> &args,
                                    const HostContext *ctx);
    static Value handleWindowClass(const std::vector<Value> &args,
                                    const HostContext *ctx);
    static Value handleWindowExe(const std::vector<Value> &args,
                                  const HostContext *ctx);
    static Value handleWindowPid(const std::vector<Value> &args,
                                  const HostContext *ctx);
    static Value handleWindowId(const std::vector<Value> &args,
                                 const HostContext *ctx);
    static Value handleWindowArea(const std::vector<Value> &args,
                                   const HostContext *ctx);
    static Value handleWindowEach(const std::vector<Value> &args,
                                   const HostContext *ctx);
    static Value handleWindowSort(const std::vector<Value> &args,
                                   const HostContext *ctx);
    static Value handleWindowMap(const std::vector<Value> &args,
                                  const HostContext *ctx);
    static Value handleWindowUnmap(const std::vector<Value> &args,
                                    const HostContext *ctx);
    static Value handleWindowPin(const std::vector<Value> &args,
                                  const HostContext *ctx);
    static Value handleWindowWait(const std::vector<Value> &args,
                                   const HostContext *ctx);
    static Value handleWindowCloseObj(const std::vector<Value> &args,
                                       const HostContext *ctx);
    static Value handleWindowHideObj(const std::vector<Value> &args,
                                      const HostContext *ctx);
    static Value handleWindowShowObj(const std::vector<Value> &args,
                                      const HostContext *ctx);
    static Value handleWindowFocusObj(const std::vector<Value> &args,
                                       const HostContext *ctx);
    static Value handleWindowMinObj(const std::vector<Value> &args,
                                     const HostContext *ctx);
    static Value handleWindowMaxObj(const std::vector<Value> &args,
                                     const HostContext *ctx);
    static Value handleWindowResizeObj(const std::vector<Value> &args,
                                        const HostContext *ctx);
    static Value handleWindowMoveObj(const std::vector<Value> &args,
                                      const HostContext *ctx);
    static Value handleWindowRestoreObj(const std::vector<Value> &args,
                                         const HostContext *ctx);
    static Value handleWindowSnapObj(const std::vector<Value> &args,
                                      const HostContext *ctx);
    static Value handleWindowCenterObj(const std::vector<Value> &args,
                                        const HostContext *ctx);
    static Value handleWindowFullscreenObj(const std::vector<Value> &args,
                                            const HostContext *ctx);
    static Value handleWindowMoveResizeObj(const std::vector<Value> &args,
                                            const HostContext *ctx);
    static Value handleWindowSetAlwaysOnTopObj(const std::vector<Value> &args,
                                                const HostContext *ctx);
    static Value handleWindowPosObj(const std::vector<Value> &args,
                                     const HostContext *ctx);
    static Value handleWindowTitleObj(const std::vector<Value> &args,
                                       const HostContext *ctx);
    static Value handleWindowClassObj(const std::vector<Value> &args,
                                       const HostContext *ctx);
    static Value handleWindowExeObj(const std::vector<Value> &args,
                                     const HostContext *ctx);
    static Value handleWindowPidObj(const std::vector<Value> &args,
                                     const HostContext *ctx);
    static Value handleWindowMapObj(const std::vector<Value> &args,
                                     const HostContext *ctx);
    static Value handleWindowUnmapObj(const std::vector<Value> &args,
                                       const HostContext *ctx);
    static Value handleWindowPinObj(const std::vector<Value> &args,
                                     const HostContext *ctx);
    static Value handleWindowWaitObj(const std::vector<Value> &args,
                                      const HostContext *ctx);
    static Value handleGroupAdd(const std::vector<Value> &args,
                                 const HostContext *ctx);
    static Value handleGroupRemove(const std::vector<Value> &args,
                                    const HostContext *ctx);
    static Value handleGroupGet(const std::vector<Value> &args,
                                 const HostContext *ctx);
    static Value handleGroupList(const std::vector<Value> &args,
                                  const HostContext *ctx);
    static Value handleGroupFind(const std::vector<Value> &args,
                                  const HostContext *ctx);
    static Value handleGroupFindBy(const std::vector<Value> &args,
                                    const HostContext *ctx);
    static Value handleClipboardGet(const std::vector<Value> &args,
                                     const HostContext *ctx);
    static Value handleClipboardSet(const std::vector<Value> &args,
                                     const HostContext *ctx);
    static Value handleClipboardClear(const std::vector<Value> &args,
                                       const HostContext *ctx);
    static Value handleScreenshotFull(const std::vector<Value> &args,
                                       const HostContext *ctx);
    static Value handleScreenshotMonitor(const std::vector<Value> &args,
                                          const HostContext *ctx);
    static Value handleGUINotify(const std::vector<Value> &args,
                                  const HostContext *ctx);
    static Value handleHotkeyRegister(const std::vector<Value> &args,
                                       const HostContext *ctx);
    static Value handleHotkeyRegisterConditional(const std::vector<Value> &args,
                                                   const HostContext *ctx);
    static Value handleHotkeyEnable(const std::vector<Value> &args,
                                     const HostContext *ctx);
    static Value handleHotkeyDisable(const std::vector<Value> &args,
                                      const HostContext *ctx);
    static Value handleHotkeyRemove(const std::vector<Value> &args,
                                     const HostContext *ctx);
    static Value handleHotkeyTrigger(const std::vector<Value> &args,
                                      const HostContext *ctx);
    static Value handleHotkeyList(const std::vector<Value> &args,
                                   const HostContext *ctx);
    static Value handleHotkeyOnAnyKey(const std::vector<Value> &args,
                                       const HostContext *ctx);
    static Value handleMapManagerMap(const std::vector<Value> &args,
                                      const HostContext *ctx);
    static Value handleMapManagerGetCurrentProfile(const std::vector<Value> &args,
                                                     const HostContext *ctx);
    static Value handleAltTabShow(const std::vector<Value> &args,
                                   const HostContext *ctx);
    static Value handleAltTabHide(const std::vector<Value> &args,
                                   const HostContext *ctx);
    static Value handleAltTabToggle(const std::vector<Value> &args,
                                     const HostContext *ctx);
    static Value handleAltTabNext(const std::vector<Value> &args,
                                   const HostContext *ctx);
    static Value handleAltTabPrevious(const std::vector<Value> &args,
                                       const HostContext *ctx);
    static Value handleAltTabSelect(const std::vector<Value> &args,
                                     const HostContext *ctx);
    static Value handleAltTabGetWindows(const std::vector<Value> &args,
                                         const HostContext *ctx);
    static Value handleIOOnKeyDown(const std::vector<Value> &args,
                                    const HostContext *ctx);
    static Value handleIOOnKeyUp(const std::vector<Value> &args,
                                  const HostContext *ctx);
    static Value handleIOOnKey(const std::vector<Value> &args,
                                const HostContext *ctx);
    static Value handleIOOnButton(const std::vector<Value> &args,
                                   const HostContext *ctx);
    static Value handleIOOnMouse(const std::vector<Value> &args,
                                  const HostContext *ctx);
    static Value handleIOOnEvent(const std::vector<Value> &args,
                                  const HostContext *ctx);
    static Value handleAutomationCreateAutoClicker(const std::vector<Value> &args,
                                                     const HostContext *ctx);
    static Value handleAutomationCreateAutoRunner(const std::vector<Value> &args,
                                                    const HostContext *ctx);
    static Value handleAutomationCreateAutoKeyPresser(const std::vector<Value> &args,
                                                        const HostContext *ctx);
    static Value handleAutomationHasTask(const std::vector<Value> &args,
                                          const HostContext *ctx);
    static Value handleAutomationRemoveTask(const std::vector<Value> &args,
                                             const HostContext *ctx);
    static Value handleAutomationStopAll(const std::vector<Value> &args,
                                          const HostContext *ctx);
    static Value handleTextChunkerSetText(const std::vector<Value> &args,
                                           const HostContext *ctx);
    static Value handleTextChunkerGetText(const std::vector<Value> &args,
                                           const HostContext *ctx);
    static Value handleTextChunkerSetChunkSize(const std::vector<Value> &args,
                                                 const HostContext *ctx);
    static Value handleTextChunkerGetTotalChunks(const std::vector<Value> &args,
                                                   const HostContext *ctx);
    static Value handleTextChunkerGetCurrentChunk(const std::vector<Value> &args,
                                                    const HostContext *ctx);
    static Value handleTextChunkerSetCurrentChunk(const std::vector<Value> &args,
                                                    const HostContext *ctx);
    static Value handleTextChunkerGetChunk(const std::vector<Value> &args,
                                            const HostContext *ctx);
    static Value handleTextChunkerGetNextChunk(const std::vector<Value> &args,
                                                const HostContext *ctx);
    static Value handleTextChunkerGetPreviousChunk(const std::vector<Value> &args,
                                                    const HostContext *ctx);
    static Value handleTextChunkerGoToFirst(const std::vector<Value> &args,
                                              const HostContext *ctx);
    static Value handleTextChunkerGoToLast(const std::vector<Value> &args,
                                             const HostContext *ctx);
    static Value handleTextChunkerClear(const std::vector<Value> &args,
                                         const HostContext *ctx);
    static Value handleMediaPlayPause(const std::vector<Value> &args,
                                       const HostContext *ctx);
    static Value handleMediaPause(const std::vector<Value> &args,
                                   const HostContext *ctx);
    static Value handleMediaStop(const std::vector<Value> &args,
                                  const HostContext *ctx);
    static Value handleMediaNext(const std::vector<Value> &args,
                                  const HostContext *ctx);
    static Value handleMediaPrevious(const std::vector<Value> &args,
                                      const HostContext *ctx);
    static Value handleMediaGetVolume(const std::vector<Value> &args,
                                       const HostContext *ctx);
    static Value handleMediaSetVolume(const std::vector<Value> &args,
                                       const HostContext *ctx);
    static Value handleMediaGetActivePlayer(const std::vector<Value> &args,
                                             const HostContext *ctx);
    static Value handleMediaSetActivePlayer(const std::vector<Value> &args,
                                             const HostContext *ctx);
    static Value handleMediaGetAvailablePlayers(const std::vector<Value> &args,
                                                  const HostContext *ctx);
    static Value handleGetMonitors(const std::vector<Value> &args,
                                    const HostContext *ctx);
    static Value handleGetPrimary(const std::vector<Value> &args,
                                   const HostContext *ctx);
    static Value handleGetCount(const std::vector<Value> &args,
                                 const HostContext *ctx);
    static Value handleGetMonitorsArea(const std::vector<Value> &args,
                                        const HostContext *ctx);
    static Value handleIsX11(const std::vector<Value> &args,
                              const HostContext *ctx);
    static Value handleIsWayland(const std::vector<Value> &args,
                                  const HostContext *ctx);
    static Value handleIsWindows(const std::vector<Value> &args,
                                  const HostContext *ctx);
    static Value handleProtocol(const std::vector<Value> &args,
                                 const HostContext *ctx);
    static Value handleWm(const std::vector<Value> &args,
                           const HostContext *ctx);
    static Value handleDisplayNum(const std::vector<Value> &args,
                                   const HostContext *ctx);
    static Value handleMonitorsResolution(const std::vector<Value> &args,
                                           const HostContext *ctx);
    static Value handleGet(const std::vector<Value> &args,
                            const HostContext *ctx);
    static Value handleSet(const std::vector<Value> &args,
                            const HostContext *ctx);
    static Value handleSave(const std::vector<Value> &args,
                             const HostContext *ctx);
    static Value handleGetCurrent(const std::vector<Value> &args,
                                   const HostContext *ctx);
    static Value handleGetPrevious(const std::vector<Value> &args,
                                    const HostContext *ctx);
    static Value handleAfter(const std::vector<Value> &args,
                              const HostContext *ctx);
    static Value handleEvery(const std::vector<Value> &args,
                              const HostContext *ctx);
    static Value handleAppGetName(const std::vector<Value> &args,
                                   const HostContext *ctx);
    static Value handleAppGetVersion(const std::vector<Value> &args,
                                      const HostContext *ctx);
    static Value handleAppGetOS(const std::vector<Value> &args,
                                 const HostContext *ctx);
    static Value handleAppGetHostname(const std::vector<Value> &args,
                                       const HostContext *ctx);
    static Value handleAppGetUsername(const std::vector<Value> &args,
                                       const HostContext *ctx);
    static Value handleAppGetHomeDir(const std::vector<Value> &args,
                                      const HostContext *ctx);
    static Value handleAppGetCpuCores(const std::vector<Value> &args,
                                       const HostContext *ctx);
    static Value handleAppGetEnv(const std::vector<Value> &args,
                                  const HostContext *ctx);
    static Value handleAppSetEnv(const std::vector<Value> &args,
                                  const HostContext *ctx);
    static Value handleAppOpenUrl(const std::vector<Value> &args,
                                   const HostContext *ctx);
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
    static Value handleActiveResize(const std::vector<Value> &args,
                                     const HostContext *ctx);
};

/**
 * SystemBridge - Filesystem and process operations
 */
class SystemBridge : public BridgeModule {
public:
    explicit SystemBridge(const HostContext *ctx) : ctx_(ctx) {}
    void install(compiler::PipelineOptions &options) override;
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
    static Value handleProcessExecute(const std::vector<Value> &args,
                                       const HostContext *ctx);
    static Value handleProcessGetPid(const std::vector<Value> &args,
                                      const HostContext *ctx);
    static Value handleProcessGetPpid(const std::vector<Value> &args,
                                       const HostContext *ctx);
    static Value handleProcessFind(const std::vector<Value> &args,
                                    const HostContext *ctx);
    static Value handleProcessExists(const std::vector<Value> &args,
                                      const HostContext *ctx);
    static Value handleProcessKill(const std::vector<Value> &args,
                                    const HostContext *ctx);
    static Value handleProcessNice(const std::vector<Value> &args,
                                    const HostContext *ctx);
    static Value handleProcessRun(const std::vector<Value> &args,
                                   const HostContext *ctx);
    static Value handleProcessRunCapture(const std::vector<Value> &args,
                                          const HostContext *ctx);
    static Value handleProcessRunDetached(const std::vector<Value> &args,
                                           const HostContext *ctx);
    static Value handleSystemDetect(const std::vector<Value> &args,
                                     const HostContext *ctx);
    static Value handleSystemHardware(const std::vector<Value> &args,
                                      const HostContext *ctx);
    static Value handleMediaPlay(const std::vector<Value> &args,
                                  const HostContext *ctx);
private:
    const HostContext *ctx_;
};

/**
 * UIBridge - Windows, clipboard, screenshots, AltTab
 */
class UIBridge : public BridgeModule {
public:
    explicit UIBridge(const HostContext *ctx) : ctx_(ctx) {}
    void install(compiler::PipelineOptions &options) override;
    static Value handleWindowGetActive(const std::vector<Value> &args,
                                        const HostContext *ctx);
    static Value handleWindowCmd(const std::vector<Value> &args,
                                  const HostContext *ctx);
    static Value handleWindowFind(const std::vector<Value> &args,
                                   const HostContext *ctx);
    static Value handleWindowClose(const std::vector<Value> &args,
                                    const HostContext *ctx);
    static Value handleWindowResize(const std::vector<Value> &args,
                                     const HostContext *ctx);
    static Value handleWindowMove(const std::vector<Value> &args,
                                   const HostContext *ctx);
    static Value handleWindowMoveRel(const std::vector<Value> &args,
                                      const HostContext *ctx);
    static Value handleWindowMoveToMonitor(const std::vector<Value> &args,
                                            const HostContext *ctx);
    static Value handleWindowMoveToNextMonitor(const std::vector<Value> &args,
                                                 const HostContext *ctx);
    static Value handleWindowFocus(const std::vector<Value> &args,
                                    const HostContext *ctx);
    static Value handleWindowMinimize(const std::vector<Value> &args,
                                       const HostContext *ctx);
    static Value handleWindowMaximize(const std::vector<Value> &args,
                                       const HostContext *ctx);
    static Value handleWindowHide(const std::vector<Value> &args,
                                   const HostContext *ctx);
    static Value handleWindowShow(const std::vector<Value> &args,
                                   const HostContext *ctx);
    static Value handleWindowAny(const std::vector<Value> &args,
                                  const HostContext *ctx);
    static Value handleWindowCount(const std::vector<Value> &args,
                                    const HostContext *ctx);
    static Value handleWindowFilter(const std::vector<Value> &args,
                                     const HostContext *ctx);
    static Value handleWindowRestore(const std::vector<Value> &args,
                                      const HostContext *ctx);
    static Value handleWindowSnap(const std::vector<Value> &args,
                                   const HostContext *ctx);
    static Value handleWindowCenter(const std::vector<Value> &args,
                                     const HostContext *ctx);
    static Value handleWindowFullscreen(const std::vector<Value> &args,
                                         const HostContext *ctx);
    static Value handleWindowMoveResize(const std::vector<Value> &args,
                                         const HostContext *ctx);
    static Value handleWindowSetAlwaysOnTop(const std::vector<Value> &args,
                                             const HostContext *ctx);
    static Value handleWindowPos(const std::vector<Value> &args,
                                  const HostContext *ctx);
    static Value handleWindowList(const std::vector<Value> &args,
                                   const HostContext *ctx);
    static Value handleWindowTitle(const std::vector<Value> &args,
                                    const HostContext *ctx);
    static Value handleWindowClass(const std::vector<Value> &args,
                                    const HostContext *ctx);
    static Value handleWindowExe(const std::vector<Value> &args,
                                  const HostContext *ctx);
    static Value handleWindowPid(const std::vector<Value> &args,
                                  const HostContext *ctx);
    static Value handleWindowId(const std::vector<Value> &args,
                                 const HostContext *ctx);
    static Value handleWindowArea(const std::vector<Value> &args,
                                   const HostContext *ctx);
    static Value handleWindowEach(const std::vector<Value> &args,
                                   const HostContext *ctx);
    static Value handleWindowSort(const std::vector<Value> &args,
                                   const HostContext *ctx);
    static Value handleWindowMap(const std::vector<Value> &args,
                                  const HostContext *ctx);
    static Value handleWindowUnmap(const std::vector<Value> &args,
                                    const HostContext *ctx);
    static Value handleWindowPin(const std::vector<Value> &args,
                                  const HostContext *ctx);
    static Value handleWindowWait(const std::vector<Value> &args,
                                   const HostContext *ctx);
    static Value handleWindowCloseObj(const std::vector<Value> &args,
                                       const HostContext *ctx);
    static Value handleWindowHideObj(const std::vector<Value> &args,
                                      const HostContext *ctx);
    static Value handleWindowShowObj(const std::vector<Value> &args,
                                      const HostContext *ctx);
    static Value handleWindowFocusObj(const std::vector<Value> &args,
                                       const HostContext *ctx);
    static Value handleWindowMinObj(const std::vector<Value> &args,
                                     const HostContext *ctx);
    static Value handleWindowMaxObj(const std::vector<Value> &args,
                                     const HostContext *ctx);
    static Value handleWindowResizeObj(const std::vector<Value> &args,
                                        const HostContext *ctx);
    static Value handleWindowMoveObj(const std::vector<Value> &args,
                                      const HostContext *ctx);
    static Value handleWindowRestoreObj(const std::vector<Value> &args,
                                         const HostContext *ctx);
    static Value handleWindowSnapObj(const std::vector<Value> &args,
                                      const HostContext *ctx);
    static Value handleWindowCenterObj(const std::vector<Value> &args,
                                        const HostContext *ctx);
    static Value handleWindowFullscreenObj(const std::vector<Value> &args,
                                            const HostContext *ctx);
    static Value handleWindowMoveResizeObj(const std::vector<Value> &args,
                                            const HostContext *ctx);
    static Value handleWindowSetAlwaysOnTopObj(const std::vector<Value> &args,
                                                const HostContext *ctx);
    static Value handleWindowPosObj(const std::vector<Value> &args,
                                     const HostContext *ctx);
    static Value handleWindowTitleObj(const std::vector<Value> &args,
                                       const HostContext *ctx);
    static Value handleWindowClassObj(const std::vector<Value> &args,
                                       const HostContext *ctx);
    static Value handleWindowExeObj(const std::vector<Value> &args,
                                     const HostContext *ctx);
    static Value handleWindowPidObj(const std::vector<Value> &args,
                                     const HostContext *ctx);
    static Value handleWindowMapObj(const std::vector<Value> &args,
                                     const HostContext *ctx);
    static Value handleWindowUnmapObj(const std::vector<Value> &args,
                                       const HostContext *ctx);
    static Value handleWindowPinObj(const std::vector<Value> &args,
                                     const HostContext *ctx);
    static Value handleWindowWaitObj(const std::vector<Value> &args,
                                      const HostContext *ctx);
    static Value handleGroupAdd(const std::vector<Value> &args,
                                 const HostContext *ctx);
    static Value handleGroupRemove(const std::vector<Value> &args,
                                    const HostContext *ctx);
    static Value handleGroupGet(const std::vector<Value> &args,
                                 const HostContext *ctx);
    static Value handleGroupList(const std::vector<Value> &args,
                                  const HostContext *ctx);
    static Value handleGroupFind(const std::vector<Value> &args,
                                  const HostContext *ctx);
    static Value handleGroupFindBy(const std::vector<Value> &args,
                                    const HostContext *ctx);
    static Value handleClipboardGet(const std::vector<Value> &args,
                                     const HostContext *ctx);
    static Value handleClipboardSet(const std::vector<Value> &args,
                                     const HostContext *ctx);
    static Value handleClipboardClear(const std::vector<Value> &args,
                                       const HostContext *ctx);
    static Value handleScreenshotFull(const std::vector<Value> &args,
                                       const HostContext *ctx);
    static Value handleScreenshotMonitor(const std::vector<Value> &args,
                                          const HostContext *ctx);
    static Value handleGUINotify(const std::vector<Value> &args,
                                  const HostContext *ctx);
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
    static Value handleActiveResize(const std::vector<Value> &args,
                                     const HostContext *ctx);
private:
    const HostContext *ctx_;
};

/**
 * InputBridge - Hotkeys, input remapping, AltTab
 */
class InputBridge : public BridgeModule {
public:
    explicit InputBridge(const HostContext *ctx) : ctx_(ctx) {}
    void install(compiler::PipelineOptions &options) override;
    static Value handleHotkeyRegister(const std::vector<Value> &args,
                                       const HostContext *ctx);
    static Value handleHotkeyRegisterConditional(const std::vector<Value> &args,
                                                   const HostContext *ctx);
    static Value handleHotkeyEnable(const std::vector<Value> &args,
                                     const HostContext *ctx);
    static Value handleHotkeyDisable(const std::vector<Value> &args,
                                      const HostContext *ctx);
    static Value handleHotkeyRemove(const std::vector<Value> &args,
                                     const HostContext *ctx);
    static Value handleHotkeyTrigger(const std::vector<Value> &args,
                                      const HostContext *ctx);
    static Value handleHotkeyList(const std::vector<Value> &args,
                                   const HostContext *ctx);
    static Value handleHotkeyOnAnyKey(const std::vector<Value> &args,
                                       const HostContext *ctx);
    static Value handleMapManagerMap(const std::vector<Value> &args,
                                      const HostContext *ctx);
    static Value handleMapManagerGetCurrentProfile(const std::vector<Value> &args,
                                                     const HostContext *ctx);
    static Value handleAltTabShow(const std::vector<Value> &args,
                                   const HostContext *ctx);
    static Value handleAltTabHide(const std::vector<Value> &args,
                                   const HostContext *ctx);
    static Value handleAltTabToggle(const std::vector<Value> &args,
                                     const HostContext *ctx);
    static Value handleAltTabNext(const std::vector<Value> &args,
                                   const HostContext *ctx);
    static Value handleAltTabPrevious(const std::vector<Value> &args,
                                       const HostContext *ctx);
    static Value handleAltTabSelect(const std::vector<Value> &args,
                                     const HostContext *ctx);
    static Value handleAltTabGetWindows(const std::vector<Value> &args,
                                         const HostContext *ctx);
private:
    const HostContext *ctx_;
};

/**
 * MediaBridge - Audio, brightness
 */
class MediaBridge : public BridgeModule {
public:
    explicit MediaBridge(const HostContext *ctx) : ctx_(ctx) {}
    void install(compiler::PipelineOptions &options) override;
    static Value handleMediaPlayPause(const std::vector<Value> &args,
                                       const HostContext *ctx);
    static Value handleMediaPlay(const std::vector<Value> &args,
                                  const HostContext *ctx);
    static Value handleMediaPause(const std::vector<Value> &args,
                                   const HostContext *ctx);
    static Value handleMediaStop(const std::vector<Value> &args,
                                  const HostContext *ctx);
    static Value handleMediaNext(const std::vector<Value> &args,
                                  const HostContext *ctx);
    static Value handleMediaPrevious(const std::vector<Value> &args,
                                      const HostContext *ctx);
    static Value handleMediaGetVolume(const std::vector<Value> &args,
                                       const HostContext *ctx);
    static Value handleMediaSetVolume(const std::vector<Value> &args,
                                       const HostContext *ctx);
    static Value handleMediaGetActivePlayer(const std::vector<Value> &args,
                                             const HostContext *ctx);
    static Value handleMediaSetActivePlayer(const std::vector<Value> &args,
                                             const HostContext *ctx);
    static Value handleMediaGetAvailablePlayers(const std::vector<Value> &args,
                                                  const HostContext *ctx);
private:
    const HostContext *ctx_;
};

/**
 * AsyncBridge - Timers, async operations
 */
class AsyncBridge : public BridgeModule {
public:
    explicit AsyncBridge(const HostContext *ctx) : ctx_(ctx) {}
    void install(compiler::PipelineOptions &options) override;
    static Value handleSleep(const std::vector<Value> &args,
                              const HostContext *ctx);
    static Value handleTimeNow(const std::vector<Value> &args,
                                const HostContext *ctx);
    static Value handleAsyncRun(const std::vector<Value> &args,
                                 const HostContext *ctx);
    static Value handleAsyncAwait(const std::vector<Value> &args,
                                   const HostContext *ctx);
    static Value handleAsyncCancel(const std::vector<Value> &args,
                                    const HostContext *ctx);
    static Value handleAsyncIsRunning(const std::vector<Value> &args,
                                       const HostContext *ctx);
    static Value handleChannelCreate(const std::vector<Value> &args,
                                      const HostContext *ctx);
    static Value handleChannelSend(const std::vector<Value> &args,
                                    const HostContext *ctx);
    static Value handleChannelReceive(const std::vector<Value> &args,
                                       const HostContext *ctx);
    static Value handleChannelTryReceive(const std::vector<Value> &args,
                                          const HostContext *ctx);
    static Value handleChannelClose(const std::vector<Value> &args,
                                     const HostContext *ctx);
    static Value handleThreadCreate(const std::vector<Value> &args,
                                     const HostContext *ctx);
    static Value handleThreadSend(const std::vector<Value> &args,
                                   const HostContext *ctx);
    static Value handleThreadPause(const std::vector<Value> &args,
                                    const HostContext *ctx);
    static Value handleThreadResume(const std::vector<Value> &args,
                                     const HostContext *ctx);
    static Value handleThreadStop(const std::vector<Value> &args,
                                   const HostContext *ctx);
    static Value handleThreadRunning(const std::vector<Value> &args,
                                      const HostContext *ctx);
    static Value handleIntervalCreate(const std::vector<Value> &args,
                                       const HostContext *ctx);
    static Value handleIntervalPause(const std::vector<Value> &args,
                                      const HostContext *ctx);
    static Value handleIntervalResume(const std::vector<Value> &args,
                                       const HostContext *ctx);
    static Value handleIntervalStop(const std::vector<Value> &args,
                                     const HostContext *ctx);
    static Value handleTimeoutCreate(const std::vector<Value> &args,
                                      const HostContext *ctx);
    static Value handleTimeoutCancel(const std::vector<Value> &args,
                                      const HostContext *ctx);
private:
    const HostContext *ctx_;
};

/**
 * AutomationBridge - Automation tasks
 */
class AutomationBridge : public BridgeModule {
public:
    explicit AutomationBridge(const HostContext *ctx) : ctx_(ctx) {}
    void install(compiler::PipelineOptions &options) override;
    static Value handleAutomationCreateAutoClicker(const std::vector<Value> &args,
                                                     const HostContext *ctx);
    static Value handleAutomationCreateAutoRunner(const std::vector<Value> &args,
                                                    const HostContext *ctx);
    static Value handleAutomationCreateAutoKeyPresser(const std::vector<Value> &args,
                                                        const HostContext *ctx);
    static Value handleAutomationHasTask(const std::vector<Value> &args,
                                          const HostContext *ctx);
    static Value handleAutomationRemoveTask(const std::vector<Value> &args,
                                             const HostContext *ctx);
    static Value handleAutomationStopAll(const std::vector<Value> &args,
                                          const HostContext *ctx);
private:
    const HostContext *ctx_;
};

/**
 * BrowserBridge - Browser automation
 */
class BrowserBridge : public BridgeModule {
public:
    explicit BrowserBridge(const HostContext *ctx) : ctx_(ctx) {}
    void install(compiler::PipelineOptions &options) override;
private:
    const HostContext *ctx_;
};

/**
 * ToolsBridge - Text chunker
 */
class ToolsBridge : public BridgeModule {
public:
    explicit ToolsBridge(const HostContext *ctx) : ctx_(ctx) {}
    void install(compiler::PipelineOptions &options) override;
    static Value handleTextChunkerSetText(const std::vector<Value> &args,
                                           const HostContext *ctx);
    static Value handleTextChunkerGetText(const std::vector<Value> &args,
                                           const HostContext *ctx);
    static Value handleTextChunkerSetChunkSize(const std::vector<Value> &args,
                                                 const HostContext *ctx);
    static Value handleTextChunkerGetTotalChunks(const std::vector<Value> &args,
                                                   const HostContext *ctx);
    static Value handleTextChunkerGetCurrentChunk(const std::vector<Value> &args,
                                                    const HostContext *ctx);
    static Value handleTextChunkerSetCurrentChunk(const std::vector<Value> &args,
                                                    const HostContext *ctx);
    static Value handleTextChunkerGetChunk(const std::vector<Value> &args,
                                            const HostContext *ctx);
    static Value handleTextChunkerGetNextChunk(const std::vector<Value> &args,
                                                const HostContext *ctx);
    static Value handleTextChunkerGetPreviousChunk(const std::vector<Value> &args,
                                                    const HostContext *ctx);
    static Value handleTextChunkerGoToFirst(const std::vector<Value> &args,
                                              const HostContext *ctx);
    static Value handleTextChunkerGoToLast(const std::vector<Value> &args,
                                             const HostContext *ctx);
    static Value handleTextChunkerClear(const std::vector<Value> &args,
                                         const HostContext *ctx);
private:
    const HostContext *ctx_;
};

/**
 * NetworkBridge - HTTP requests
 */
class NetworkBridge : public BridgeModule {
public:
    explicit NetworkBridge(const HostContext *ctx) : ctx_(ctx) {}
    void install(compiler::PipelineOptions &options) override;
private:
    const HostContext *ctx_;
};

/**
 * DisplayBridge - Monitor information
 */
class DisplayBridge : public BridgeModule {
public:
    explicit DisplayBridge(const HostContext *ctx) : ctx_(ctx) {}
    void install(compiler::PipelineOptions &options) override;
    static Value handleGetMonitors(const std::vector<Value> &args,
                                    const HostContext *ctx);
    static Value handleGetPrimary(const std::vector<Value> &args,
                                   const HostContext *ctx);
    static Value handleGetCount(const std::vector<Value> &args,
                                 const HostContext *ctx);
    static Value handleGetMonitorsArea(const std::vector<Value> &args,
                                        const HostContext *ctx);
    static Value handleIsX11(const std::vector<Value> &args,
                              const HostContext *ctx);
    static Value handleIsWayland(const std::vector<Value> &args,
                                  const HostContext *ctx);
    static Value handleIsWindows(const std::vector<Value> &args,
                                  const HostContext *ctx);
    static Value handleProtocol(const std::vector<Value> &args,
                                 const HostContext *ctx);
    static Value handleWm(const std::vector<Value> &args,
                           const HostContext *ctx);
    static Value handleDisplayNum(const std::vector<Value> &args,
                                   const HostContext *ctx);
    static Value handleMonitorsResolution(const std::vector<Value> &args,
                                           const HostContext *ctx);
private:
    const HostContext *ctx_;
};

/**
 * ConfigBridge - Configuration management
 */
class ConfigBridge : public BridgeModule {
public:
    explicit ConfigBridge(const HostContext *ctx) : ctx_(ctx) {}
    void install(compiler::PipelineOptions &options) override;
    static Value handleGet(const std::vector<Value> &args,
                            const HostContext *ctx);
    static Value handleSet(const std::vector<Value> &args,
                            const HostContext *ctx);
    static Value handleSave(const std::vector<Value> &args,
                             const HostContext *ctx);
private:
    const HostContext *ctx_;
};

/**
 * ModeBridge - Mode management
 */
class ModeBridge : public BridgeModule {
public:
    explicit ModeBridge(const HostContext *ctx) : ctx_(ctx) {}
    void install(compiler::PipelineOptions &options) override;
    static Value handleRegister(const std::vector<Value> &args,
                                 const HostContext *ctx);
    static Value handleGetCurrent(const std::vector<Value> &args,
                                   const HostContext *ctx);
    static Value handleSet(const std::vector<Value> &args,
                            const HostContext *ctx);
    static Value handleGetPrevious(const std::vector<Value> &args,
                                    const HostContext *ctx);
    static Value handleList(const std::vector<Value> &args,
                             const HostContext *ctx);
    static Value handleTime(const std::vector<Value> &args,
                             const HostContext *ctx);
    static Value handleTransitions(const std::vector<Value> &args,
                                    const HostContext *ctx);
private:
    const HostContext *ctx_;
};

/**
 * TimerBridge - Timers
 */
class TimerBridge : public BridgeModule {
public:
    explicit TimerBridge(const HostContext *ctx) : ctx_(ctx) {}
    void install(compiler::PipelineOptions &options) override;
    static Value handleAfter(const std::vector<Value> &args,
                              const HostContext *ctx);
    static Value handleEvery(const std::vector<Value> &args,
                              const HostContext *ctx);
private:
    const HostContext *ctx_;
};

/**
 * AppBridge - Application information
 */
class AppBridge : public BridgeModule {
public:
    explicit AppBridge(const HostContext *ctx) : ctx_(ctx) {}
    void install(compiler::PipelineOptions &options) override;
    static Value handleAppGetName(const std::vector<Value> &args,
                                   const HostContext *ctx);
    static Value handleAppGetVersion(const std::vector<Value> &args,
                                      const HostContext *ctx);
    static Value handleAppGetOS(const std::vector<Value> &args,
                                 const HostContext *ctx);
    static Value handleAppGetHostname(const std::vector<Value> &args,
                                       const HostContext *ctx);
    static Value handleAppGetUsername(const std::vector<Value> &args,
                                       const HostContext *ctx);
    static Value handleAppGetHomeDir(const std::vector<Value> &args,
                                      const HostContext *ctx);
    static Value handleAppGetCpuCores(const std::vector<Value> &args,
                                       const HostContext *ctx);
    static Value handleAppGetEnv(const std::vector<Value> &args,
                                  const HostContext *ctx);
    static Value handleAppSetEnv(const std::vector<Value> &args,
                                  const HostContext *ctx);
    static Value handleAppOpenUrl(const std::vector<Value> &args,
                                   const HostContext *ctx);
private:
    const HostContext *ctx_;
};

} // namespace havel::compiler
