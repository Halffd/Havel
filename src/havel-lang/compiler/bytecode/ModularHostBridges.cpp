/*
 * ModularHostBridges.cpp - Modular bridge component implementations
 */
#include "ModularHostBridges.hpp"

#include "../../../gui/ClipboardManager.hpp"
#include "../../../host/async/AsyncService.hpp"
#include "../../../host/audio/AudioService.hpp"
#include "../../../host/automation/AutomationService.hpp"
#include "../../../host/browser/BrowserService.hpp"
#include "../../../host/brightness/BrightnessService.hpp"
#include "../../../host/chunker/TextChunkerService.hpp"
#include "../../../host/filesystem/FileSystemService.hpp"
#include "../../../host/hotkey/HotkeyService.hpp"
#include "../../../host/io/IOService.hpp"
#include "../../../host/io/MapManagerService.hpp"
#include "../../../host/process/ProcessService.hpp"
#include "../../../host/screenshot/ScreenshotService.hpp"
#include "../../../host/window/WindowService.hpp"
#include "../../../host/window/AltTabService.hpp"
#include "../../../host/media/MediaService.hpp"
#include "../../../host/network/NetworkService.hpp"
#include "../../../host/app/AppService.hpp"
#include "../../../window/WindowManager.hpp"

#include <QClipboard>
#include <fstream>
#include <sstream>
#include <chrono>

namespace havel::compiler {

// ============================================================================
// IOBridge Implementation
// ============================================================================

void IOBridge::install(PipelineOptions &options) {
  options.host_functions["send"] = [ctx = ctx_](const auto &args) {
    return handleSend(args, ctx);
  };
  options.host_functions["io.send"] = [ctx = ctx_](const auto &args) {
    return handleSend(args, ctx);
  };
  options.host_functions["io.sendKey"] = [ctx = ctx_](const auto &args) {
    return handleSendKey(args, ctx);
  };
  options.host_functions["io.mouseMove"] = [ctx = ctx_](const auto &args) {
    return handleMouseMove(args, ctx);
  };
  options.host_functions["io.mouseClick"] = [ctx = ctx_](const auto &args) {
    return handleMouseClick(args, ctx);
  };
  options.host_functions["io.getMousePosition"] = [ctx = ctx_](const auto &args) {
    return handleGetMousePosition(args, ctx);
  };
}

BytecodeValue IOBridge::handleSend(const std::vector<BytecodeValue> &args,
                                   const HostContext *ctx) {
  if (args.empty() || !ctx->io) {
    return BytecodeValue(false);
  }
  havel::host::IOService ioService(ctx->io);
  if (auto *keys = std::get_if<std::string>(&args[0])) {
    return BytecodeValue(ioService.sendKeys(*keys));
  }
  return BytecodeValue(false);
}

BytecodeValue IOBridge::handleSendKey(const std::vector<BytecodeValue> &args,
                                      const HostContext *ctx) {
  if (args.empty() || !ctx->io) {
    return BytecodeValue(false);
  }
  havel::host::IOService ioService(ctx->io);
  if (auto *key = std::get_if<std::string>(&args[0])) {
    return BytecodeValue(ioService.sendKey(*key));
  }
  return BytecodeValue(false);
}

BytecodeValue IOBridge::handleMouseMove(const std::vector<BytecodeValue> &args,
                                        const HostContext *ctx) {
  if (args.size() < 2 || !ctx->io) {
    return BytecodeValue(false);
  }
  havel::host::IOService ioService(ctx->io);
  int64_t dx = 0, dy = 0;
  if (auto *v = std::get_if<int64_t>(&args[0]))
    dx = *v;
  else if (auto *v = std::get_if<double>(&args[0]))
    dx = static_cast<int64_t>(*v);
  if (auto *v = std::get_if<int64_t>(&args[1]))
    dy = *v;
  else if (auto *v = std::get_if<double>(&args[1]))
    dy = static_cast<int64_t>(*v);
  return BytecodeValue(ioService.mouseMove(static_cast<int>(dx), static_cast<int>(dy)));
}

BytecodeValue IOBridge::handleMouseClick(const std::vector<BytecodeValue> &args,
                                         const HostContext *ctx) {
  if (args.empty() || !ctx->io) {
    return BytecodeValue(false);
  }
  havel::host::IOService ioService(ctx->io);
  int64_t button = 1;
  if (auto *v = std::get_if<int64_t>(&args[0]))
    button = *v;
  else if (auto *v = std::get_if<double>(&args[0]))
    button = static_cast<int64_t>(*v);
  return BytecodeValue(ioService.mouseClick(static_cast<int>(button)));
}

BytecodeValue IOBridge::handleGetMousePosition(const std::vector<BytecodeValue> &args,
                                               const HostContext *ctx) {
  (void)args;
  if (!ctx->io) {
    return BytecodeValue(nullptr);
  }
  // TODO: Implement getMousePosition in IOService
  return BytecodeValue(nullptr);
}

// ============================================================================
// SystemBridge Implementation
// ============================================================================

void SystemBridge::install(PipelineOptions &options) {
  options.host_functions["readFile"] = [ctx = ctx_](const auto &args) {
    return handleFileRead(args, ctx);
  };
  options.host_functions["writeFile"] = [ctx = ctx_](const auto &args) {
    return handleFileWrite(args, ctx);
  };
  options.host_functions["fileExists"] = [ctx = ctx_](const auto &args) {
    return handleFileExists(args, ctx);
  };
  options.host_functions["fileSize"] = [ctx = ctx_](const auto &args) {
    return handleFileSize(args, ctx);
  };
  options.host_functions["deleteFile"] = [ctx = ctx_](const auto &args) {
    return handleFileDelete(args, ctx);
  };
  options.host_functions["execute"] = [ctx = ctx_](const auto &args) {
    return handleProcessExecute(args, ctx);
  };
  options.host_functions["getpid"] = [ctx = ctx_](const auto &args) {
    return handleProcessGetPid(args, ctx);
  };
  options.host_functions["getppid"] = [ctx = ctx_](const auto &args) {
    return handleProcessGetPpid(args, ctx);
  };
  options.host_functions["process.find"] = [ctx = ctx_](const auto &args) {
    return handleProcessFind(args, ctx);
  };
}

BytecodeValue SystemBridge::handleFileRead(const std::vector<BytecodeValue> &args,
                                           const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("readFile() requires a file path");
  }
  const std::string *path = std::get_if<std::string>(&args[0]);
  if (!path) {
    throw std::runtime_error("readFile() requires a string path");
  }
  havel::host::FileSystemService fs;
  std::string content = fs.readFile(*path);
  if (content.empty()) {
    return BytecodeValue(nullptr);
  }
  return BytecodeValue(content);
}

BytecodeValue SystemBridge::handleFileWrite(const std::vector<BytecodeValue> &args,
                                            const HostContext *ctx) {
  (void)ctx;
  if (args.size() < 2) {
    throw std::runtime_error("writeFile() requires path and content");
  }
  const std::string *path = std::get_if<std::string>(&args[0]);
  const std::string *content = std::get_if<std::string>(&args[1]);
  if (!path || !content) {
    throw std::runtime_error("writeFile() requires string arguments");
  }
  havel::host::FileSystemService fs;
  return BytecodeValue(fs.writeFile(*path, *content));
}

BytecodeValue SystemBridge::handleFileExists(const std::vector<BytecodeValue> &args,
                                             const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("fileExists() requires a file path");
  }
  const std::string *path = std::get_if<std::string>(&args[0]);
  if (!path) {
    throw std::runtime_error("fileExists() requires a string path");
  }
  havel::host::FileSystemService fs;
  return BytecodeValue(fs.exists(*path));
}

BytecodeValue SystemBridge::handleFileSize(const std::vector<BytecodeValue> &args,
                                           const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("fileSize() requires a file path");
  }
  const std::string *path = std::get_if<std::string>(&args[0]);
  if (!path) {
    throw std::runtime_error("fileSize() requires a string path");
  }
  havel::host::FileSystemService fs;
  if (!fs.exists(*path)) {
    return BytecodeValue(static_cast<int64_t>(0));
  }
  return BytecodeValue(fs.getFileSize(*path));
}

BytecodeValue SystemBridge::handleFileDelete(const std::vector<BytecodeValue> &args,
                                             const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("deleteFile() requires a file path");
  }
  const std::string *path = std::get_if<std::string>(&args[0]);
  if (!path) {
    throw std::runtime_error("deleteFile() requires a string path");
  }
  havel::host::FileSystemService fs;
  return BytecodeValue(fs.deleteFile(*path));
}

BytecodeValue SystemBridge::handleProcessExecute(const std::vector<BytecodeValue> &args,
                                                 const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("execute() requires a command");
  }
  const std::string *command = std::get_if<std::string>(&args[0]);
  if (!command) {
    throw std::runtime_error("execute() requires a string command");
  }
  std::ostringstream output;
  FILE *pipe = popen(command->c_str(), "r");
  if (!pipe) {
    throw std::runtime_error("Failed to execute command: " + *command);
  }
  char buffer[128];
  while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    output << buffer;
  }
  pclose(pipe);
  return BytecodeValue(output.str());
}

BytecodeValue SystemBridge::handleProcessGetPid(const std::vector<BytecodeValue> &args,
                                                const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return BytecodeValue(static_cast<int64_t>(havel::host::ProcessService::getCurrentPid()));
}

BytecodeValue SystemBridge::handleProcessGetPpid(const std::vector<BytecodeValue> &args,
                                                 const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return BytecodeValue(static_cast<int64_t>(havel::host::ProcessService::getParentPid()));
}

BytecodeValue SystemBridge::handleProcessFind(const std::vector<BytecodeValue> &args,
                                              const HostContext *ctx) {
  if (args.empty()) {
    throw std::runtime_error("process.find() requires a process name");
  }
  const std::string *name = std::get_if<std::string>(&args[0]);
  if (!name) {
    throw std::runtime_error("process.find() requires a string argument");
  }
  auto pids = havel::host::ProcessService::findProcesses(*name);
  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm) {
    return BytecodeValue(nullptr);
  }
  auto arr = vm->createHostArray();
  for (int32_t pid : pids) {
    vm->pushHostArrayValue(arr, BytecodeValue(static_cast<int64_t>(pid)));
  }
  return BytecodeValue(arr);
}

// ============================================================================
// UIBridge Implementation
// ============================================================================

void UIBridge::install(PipelineOptions &options) {
  options.host_functions["window.active"] = [ctx = ctx_](const auto &args) {
    return handleWindowGetActive(args, ctx);
  };
  options.host_functions["window.close"] = [ctx = ctx_](const auto &args) {
    return handleWindowClose(args, ctx);
  };
  options.host_functions["window.resize"] = [ctx = ctx_](const auto &args) {
    return handleWindowResize(args, ctx);
  };
  options.host_functions["window.moveToMonitor"] = [ctx = ctx_](const auto &args) {
    return handleWindowMoveToMonitor(args, ctx);
  };
  options.host_functions["window.moveToNextMonitor"] = [ctx = ctx_](const auto &args) {
    return handleWindowMoveToNextMonitor(args, ctx);
  };
  options.host_functions["clipboard.get"] = [ctx = ctx_](const auto &args) {
    return handleClipboardGet(args, ctx);
  };
  options.host_functions["clipboard.set"] = [ctx = ctx_](const auto &args) {
    return handleClipboardSet(args, ctx);
  };
  options.host_functions["clipboard.clear"] = [ctx = ctx_](const auto &args) {
    return handleClipboardClear(args, ctx);
  };
  options.host_functions["screenshot.full"] = [ctx = ctx_](const auto &args) {
    return handleScreenshotFull(args, ctx);
  };
  options.host_functions["screenshot.monitor"] = [ctx = ctx_](const auto &args) {
    return handleScreenshotMonitor(args, ctx);
  };
}

BytecodeValue UIBridge::handleWindowGetActive(const std::vector<BytecodeValue> &args,
                                              const HostContext *ctx) {
  (void)args;
  if (!ctx->windowManager) {
    return BytecodeValue(nullptr);
  }
  havel::host::WindowService winService(ctx->windowManager);
  auto info = winService.getActiveWindowInfo();
  if (!info.valid) {
    return BytecodeValue(nullptr);
  }
  auto *vm = static_cast<VM *>(ctx->vm);
  auto obj = vm->createHostObject();
  vm->setHostObjectField(obj, "id", BytecodeValue(static_cast<int64_t>(info.id)));
  vm->setHostObjectField(obj, "title", BytecodeValue(info.title));
  vm->setHostObjectField(obj, "class", BytecodeValue(info.windowClass));
  return BytecodeValue(obj);
}

BytecodeValue UIBridge::handleWindowClose(const std::vector<BytecodeValue> &args,
                                          const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager) {
    return BytecodeValue(false);
  }
  uint64_t wid = 0;
  if (auto *v = std::get_if<int64_t>(&args[0]))
    wid = static_cast<uint64_t>(*v);
  else
    return BytecodeValue(false);
  havel::host::WindowService winService(ctx->windowManager);
  return BytecodeValue(winService.closeWindow(wid));
}

BytecodeValue UIBridge::handleWindowResize(const std::vector<BytecodeValue> &args,
                                           const HostContext *ctx) {
  if (args.size() < 3 || !ctx->windowManager) {
    return BytecodeValue(false);
  }
  uint64_t wid = 0;
  if (auto *v = std::get_if<int64_t>(&args[0]))
    wid = static_cast<uint64_t>(*v);
  else
    return BytecodeValue(false);
  int w = 0, h = 0;
  if (auto *v = std::get_if<int64_t>(&args[1]))
    w = static_cast<int>(*v);
  else if (auto *v = std::get_if<double>(&args[1]))
    w = static_cast<int>(*v);
  if (auto *v = std::get_if<int64_t>(&args[2]))
    h = static_cast<int>(*v);
  else if (auto *v = std::get_if<double>(&args[2]))
    h = static_cast<int>(*v);
  havel::host::WindowService winService(ctx->windowManager);
  return BytecodeValue(winService.resizeWindow(wid, w, h));
}

BytecodeValue UIBridge::handleWindowMoveToMonitor(const std::vector<BytecodeValue> &args,
                                                  const HostContext *ctx) {
  if (args.size() < 2 || !ctx->windowManager) {
    return BytecodeValue(false);
  }
  uint64_t wid = 0;
  if (auto *v = std::get_if<int64_t>(&args[0]))
    wid = static_cast<uint64_t>(*v);
  else
    return BytecodeValue(false);
  int monitor = 0;
  if (auto *v = std::get_if<int64_t>(&args[1]))
    monitor = static_cast<int>(*v);
  else if (auto *v = std::get_if<double>(&args[1]))
    monitor = static_cast<int>(*v);
  havel::host::WindowService winService(ctx->windowManager);
  return BytecodeValue(winService.moveWindowToMonitor(wid, monitor));
}

BytecodeValue UIBridge::handleWindowMoveToNextMonitor(const std::vector<BytecodeValue> &args,
                                                      const HostContext *ctx) {
  (void)args;
  if (!ctx->windowManager) {
    return BytecodeValue(false);
  }
  // TODO: Implement move to next monitor
  havel::host::WindowService winService(ctx->windowManager);
  return BytecodeValue(winService.moveWindowToMonitor(0, 0));
}

BytecodeValue UIBridge::handleClipboardGet(const std::vector<BytecodeValue> &args,
                                           const HostContext *ctx) {
  (void)args;
  if (!ctx->clipboardManager) {
    return BytecodeValue(nullptr);
  }
  auto *clipboard = ctx->clipboardManager->getClipboard();
  if (!clipboard) {
    return BytecodeValue(nullptr);
  }
  return BytecodeValue(clipboard->text().toStdString());
}

BytecodeValue UIBridge::handleClipboardSet(const std::vector<BytecodeValue> &args,
                                           const HostContext *ctx) {
  if (args.empty() || !ctx->clipboardManager) {
    return BytecodeValue(false);
  }
  auto *clipboard = ctx->clipboardManager->getClipboard();
  if (!clipboard) {
    return BytecodeValue(false);
  }
  if (auto *val = std::get_if<std::string>(&args[0])) {
    clipboard->setText(QString::fromStdString(*val));
    return BytecodeValue(true);
  }
  return BytecodeValue(false);
}

BytecodeValue UIBridge::handleClipboardClear(const std::vector<BytecodeValue> &args,
                                             const HostContext *ctx) {
  (void)args;
  if (!ctx->clipboardManager) {
    return BytecodeValue(false);
  }
  auto *clipboard = ctx->clipboardManager->getClipboard();
  if (!clipboard) {
    return BytecodeValue(false);
  }
  clipboard->clear();
  return BytecodeValue(true);
}

BytecodeValue UIBridge::handleScreenshotFull(const std::vector<BytecodeValue> &args,
                                             const HostContext *ctx) {
  (void)args;
  (void)ctx;
  havel::host::ScreenshotService service;
  auto data = service.captureFullDesktop();
  (void)data;
  return BytecodeValue(nullptr);
}

BytecodeValue UIBridge::handleScreenshotMonitor(const std::vector<BytecodeValue> &args,
                                                const HostContext *ctx) {
  (void)ctx;
  int monitor = 0;
  if (!args.empty()) {
    if (auto *v = std::get_if<int64_t>(&args[0]))
      monitor = static_cast<int>(*v);
  }
  havel::host::ScreenshotService service;
  auto data = service.captureMonitor(monitor);
  (void)data;
  return BytecodeValue(nullptr);
}

// ============================================================================
// InputBridge Implementation
// ============================================================================

void InputBridge::install(PipelineOptions &options) {
  options.host_functions["hotkey.register"] = [ctx = ctx_](const auto &args) {
    return handleHotkeyRegister(args, ctx);
  };
  options.host_functions["hotkey.trigger"] = [ctx = ctx_](const auto &args) {
    return handleHotkeyTrigger(args, ctx);
  };
  options.host_functions["mapmanager.map"] = [ctx = ctx_](const auto &args) {
    return handleMapManagerMap(args, ctx);
  };
  options.host_functions["mapmanager.getCurrentProfile"] = [ctx = ctx_](const auto &args) {
    return handleMapManagerGetCurrentProfile(args, ctx);
  };
  options.host_functions["alttab.show"] = [ctx = ctx_](const auto &args) {
    return handleAltTabShow(args, ctx);
  };
  options.host_functions["alttab.hide"] = [ctx = ctx_](const auto &args) {
    return handleAltTabHide(args, ctx);
  };
  options.host_functions["alttab.toggle"] = [ctx = ctx_](const auto &args) {
    return handleAltTabToggle(args, ctx);
  };
  options.host_functions["alttab.next"] = [ctx = ctx_](const auto &args) {
    return handleAltTabNext(args, ctx);
  };
  options.host_functions["alttab.previous"] = [ctx = ctx_](const auto &args) {
    return handleAltTabPrevious(args, ctx);
  };
  options.host_functions["alttab.select"] = [ctx = ctx_](const auto &args) {
    return handleAltTabSelect(args, ctx);
  };
  options.host_functions["alttab.getWindows"] = [ctx = ctx_](const auto &args) {
    return handleAltTabGetWindows(args, ctx);
  };
}

BytecodeValue InputBridge::handleHotkeyRegister(const std::vector<BytecodeValue> &args,
                                                const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return BytecodeValue(false);
}

BytecodeValue InputBridge::handleHotkeyTrigger(const std::vector<BytecodeValue> &args,
                                               const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return BytecodeValue(false);
}

BytecodeValue InputBridge::handleMapManagerMap(const std::vector<BytecodeValue> &args,
                                               const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return BytecodeValue(false);
}

BytecodeValue InputBridge::handleMapManagerGetCurrentProfile(const std::vector<BytecodeValue> &args,
                                                             const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return BytecodeValue(std::string("default"));
}

BytecodeValue InputBridge::handleAltTabShow(const std::vector<BytecodeValue> &args,
                                            const HostContext *ctx) {
  (void)args;
  (void)ctx;
  havel::host::AltTabService altTab;
  altTab.show();
  return BytecodeValue(true);
}

BytecodeValue InputBridge::handleAltTabHide(const std::vector<BytecodeValue> &args,
                                            const HostContext *ctx) {
  (void)args;
  (void)ctx;
  havel::host::AltTabService altTab;
  altTab.hide();
  return BytecodeValue(true);
}

BytecodeValue InputBridge::handleAltTabToggle(const std::vector<BytecodeValue> &args,
                                              const HostContext *ctx) {
  (void)args;
  (void)ctx;
  havel::host::AltTabService altTab;
  altTab.toggle();
  return BytecodeValue(true);
}

BytecodeValue InputBridge::handleAltTabNext(const std::vector<BytecodeValue> &args,
                                            const HostContext *ctx) {
  (void)args;
  (void)ctx;
  havel::host::AltTabService altTab;
  altTab.next();
  return BytecodeValue(true);
}

BytecodeValue InputBridge::handleAltTabPrevious(const std::vector<BytecodeValue> &args,
                                                const HostContext *ctx) {
  (void)args;
  (void)ctx;
  havel::host::AltTabService altTab;
  altTab.previous();
  return BytecodeValue(true);
}

BytecodeValue InputBridge::handleAltTabSelect(const std::vector<BytecodeValue> &args,
                                              const HostContext *ctx) {
  (void)args;
  (void)ctx;
  havel::host::AltTabService altTab;
  altTab.select();
  return BytecodeValue(true);
}

BytecodeValue InputBridge::handleAltTabGetWindows(const std::vector<BytecodeValue> &args,
                                                  const HostContext *ctx) {
  (void)args;
  havel::host::AltTabService altTab;
  auto windows = altTab.getWindows();
  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm) {
    return BytecodeValue(nullptr);
  }
  auto arr = vm->createHostArray();
  for (const auto &win : windows) {
    auto winObj = vm->createHostObject();
    vm->setHostObjectField(winObj, "title", BytecodeValue(win.title));
    vm->setHostObjectField(winObj, "className", BytecodeValue(win.className));
    vm->setHostObjectField(winObj, "processName", BytecodeValue(win.processName));
    vm->setHostObjectField(winObj, "windowId", BytecodeValue(static_cast<int64_t>(win.windowId)));
    vm->setHostObjectField(winObj, "active", BytecodeValue(win.active));
    vm->pushHostArrayValue(arr, BytecodeValue(winObj));
  }
  return BytecodeValue(arr);
}

// ============================================================================
// AsyncBridge Implementation
// ============================================================================

void AsyncBridge::install(PipelineOptions &options) {
  options.host_functions["sleep"] = [ctx = ctx_](const auto &args) {
    return handleSleep(args, ctx);
  };
  options.host_functions["time.now"] = [ctx = ctx_](const auto &args) {
    return handleTimeNow(args, ctx);
  };
}

BytecodeValue AsyncBridge::handleSleep(const std::vector<BytecodeValue> &args,
                                       const HostContext *ctx) {
  if (args.empty()) {
    throw std::runtime_error("sleep() requires milliseconds");
  }
  int64_t ms = 0;
  if (std::holds_alternative<int64_t>(args[0])) {
    ms = std::get<int64_t>(args[0]);
  } else if (std::holds_alternative<double>(args[0])) {
    ms = static_cast<int64_t>(std::get<double>(args[0]));
  } else {
    throw std::runtime_error("sleep() requires a number");
  }
  if (ms < 0) {
    throw std::runtime_error("sleep() milliseconds must be non-negative");
  }
  if (ctx->asyncService) {
    ctx->asyncService->sleep(static_cast<int>(ms));
  } else {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
  }
  return BytecodeValue(nullptr);
}

BytecodeValue AsyncBridge::handleTimeNow(const std::vector<BytecodeValue> &args,
                                         const HostContext *ctx) {
  (void)args;
  (void)ctx;
  auto now = std::chrono::system_clock::now();
  auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
  return BytecodeValue(static_cast<int64_t>(timestamp));
}

// ============================================================================
// AutomationBridge Implementation
// ============================================================================

void AutomationBridge::install(PipelineOptions &options) {
  options.host_functions["automation.createAutoClicker"] = [ctx = ctx_](const auto &args) {
    return handleAutomationCreateAutoClicker(args, ctx);
  };
  options.host_functions["automation.createAutoRunner"] = [ctx = ctx_](const auto &args) {
    return handleAutomationCreateAutoRunner(args, ctx);
  };
  options.host_functions["automation.createAutoKeyPresser"] = [ctx = ctx_](const auto &args) {
    return handleAutomationCreateAutoKeyPresser(args, ctx);
  };
  options.host_functions["automation.hasTask"] = [ctx = ctx_](const auto &args) {
    return handleAutomationHasTask(args, ctx);
  };
  options.host_functions["automation.removeTask"] = [ctx = ctx_](const auto &args) {
    return handleAutomationRemoveTask(args, ctx);
  };
  options.host_functions["automation.stopAll"] = [ctx = ctx_](const auto &args) {
    return handleAutomationStopAll(args, ctx);
  };
}

BytecodeValue AutomationBridge::handleAutomationCreateAutoClicker(const std::vector<BytecodeValue> &args,
                                                                  const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return BytecodeValue(false);
}

BytecodeValue AutomationBridge::handleAutomationCreateAutoRunner(const std::vector<BytecodeValue> &args,
                                                                 const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return BytecodeValue(false);
}

BytecodeValue AutomationBridge::handleAutomationCreateAutoKeyPresser(const std::vector<BytecodeValue> &args,
                                                                     const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return BytecodeValue(false);
}

BytecodeValue AutomationBridge::handleAutomationHasTask(const std::vector<BytecodeValue> &args,
                                                        const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return BytecodeValue(false);
}

BytecodeValue AutomationBridge::handleAutomationRemoveTask(const std::vector<BytecodeValue> &args,
                                                           const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return BytecodeValue(false);
}

BytecodeValue AutomationBridge::handleAutomationStopAll(const std::vector<BytecodeValue> &args,
                                                        const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return BytecodeValue(false);
}

// ============================================================================
// BrowserBridge Implementation
// ============================================================================

void BrowserBridge::install(PipelineOptions &options) {
  options.host_functions["browser.connect"] = [ctx = ctx_](const auto &args) {
    return handleBrowserConnect(args, ctx);
  };
  options.host_functions["browser.connectFirefox"] = [ctx = ctx_](const auto &args) {
    return handleBrowserConnectFirefox(args, ctx);
  };
  options.host_functions["browser.disconnect"] = [ctx = ctx_](const auto &args) {
    return handleBrowserDisconnect(args, ctx);
  };
  options.host_functions["browser.isConnected"] = [ctx = ctx_](const auto &args) {
    return handleBrowserIsConnected(args, ctx);
  };
  options.host_functions["browser.open"] = [ctx = ctx_](const auto &args) {
    return handleBrowserOpen(args, ctx);
  };
  options.host_functions["browser.newTab"] = [ctx = ctx_](const auto &args) {
    return handleBrowserNewTab(args, ctx);
  };
  options.host_functions["browser.goto"] = [ctx = ctx_](const auto &args) {
    return handleBrowserGoto(args, ctx);
  };
  options.host_functions["browser.back"] = [ctx = ctx_](const auto &args) {
    return handleBrowserBack(args, ctx);
  };
  options.host_functions["browser.forward"] = [ctx = ctx_](const auto &args) {
    return handleBrowserForward(args, ctx);
  };
  options.host_functions["browser.reload"] = [ctx = ctx_](const auto &args) {
    return handleBrowserReload(args, ctx);
  };
  options.host_functions["browser.listTabs"] = [ctx = ctx_](const auto &args) {
    return handleBrowserListTabs(args, ctx);
  };
}

BytecodeValue BrowserBridge::handleBrowserConnect(const std::vector<BytecodeValue> &args,
                                                  const HostContext *ctx) {
  (void)ctx;
  std::string browserUrl = "http://localhost:9222";
  if (!args.empty()) {
    if (auto *s = std::get_if<std::string>(&args[0])) {
      browserUrl = *s;
    }
  }
  havel::host::BrowserService browser;
  return BytecodeValue(browser.connect(browserUrl));
}

BytecodeValue BrowserBridge::handleBrowserConnectFirefox(const std::vector<BytecodeValue> &args,
                                                         const HostContext *ctx) {
  (void)ctx;
  int port = 2828;
  if (!args.empty()) {
    if (auto *v = std::get_if<int64_t>(&args[0])) {
      port = static_cast<int>(*v);
    }
  }
  havel::host::BrowserService browser;
  return BytecodeValue(browser.connectFirefox(port));
}

BytecodeValue BrowserBridge::handleBrowserDisconnect(const std::vector<BytecodeValue> &args,
                                                     const HostContext *ctx) {
  (void)args;
  (void)ctx;
  havel::host::BrowserService browser;
  browser.disconnect();
  return BytecodeValue(true);
}

BytecodeValue BrowserBridge::handleBrowserIsConnected(const std::vector<BytecodeValue> &args,
                                                      const HostContext *ctx) {
  (void)args;
  (void)ctx;
  havel::host::BrowserService browser;
  return BytecodeValue(browser.isConnected());
}

BytecodeValue BrowserBridge::handleBrowserOpen(const std::vector<BytecodeValue> &args,
                                               const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("browser.open() requires a URL");
  }
  const std::string *url = std::get_if<std::string>(&args[0]);
  if (!url) {
    throw std::runtime_error("browser.open() requires a string URL");
  }
  havel::host::BrowserService browser;
  return BytecodeValue(browser.open(*url));
}

BytecodeValue BrowserBridge::handleBrowserNewTab(const std::vector<BytecodeValue> &args,
                                                 const HostContext *ctx) {
  (void)ctx;
  std::string url;
  if (!args.empty()) {
    if (auto *s = std::get_if<std::string>(&args[0])) {
      url = *s;
    }
  }
  havel::host::BrowserService browser;
  return BytecodeValue(browser.newTab(url));
}

BytecodeValue BrowserBridge::handleBrowserGoto(const std::vector<BytecodeValue> &args,
                                               const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("browser.goto() requires a URL");
  }
  const std::string *url = std::get_if<std::string>(&args[0]);
  if (!url) {
    throw std::runtime_error("browser.goto() requires a string URL");
  }
  havel::host::BrowserService browser;
  return BytecodeValue(browser.gotoUrl(*url));
}

BytecodeValue BrowserBridge::handleBrowserBack(const std::vector<BytecodeValue> &args,
                                               const HostContext *ctx) {
  (void)args;
  (void)ctx;
  havel::host::BrowserService browser;
  return BytecodeValue(browser.back());
}

BytecodeValue BrowserBridge::handleBrowserForward(const std::vector<BytecodeValue> &args,
                                                  const HostContext *ctx) {
  (void)args;
  (void)ctx;
  havel::host::BrowserService browser;
  return BytecodeValue(browser.forward());
}

BytecodeValue BrowserBridge::handleBrowserReload(const std::vector<BytecodeValue> &args,
                                                 const HostContext *ctx) {
  (void)ctx;
  bool ignoreCache = false;
  if (!args.empty()) {
    if (auto *b = std::get_if<bool>(&args[0])) {
      ignoreCache = *b;
    }
  }
  havel::host::BrowserService browser;
  return BytecodeValue(browser.reload(ignoreCache));
}

BytecodeValue BrowserBridge::handleBrowserListTabs(const std::vector<BytecodeValue> &args,
                                                   const HostContext *ctx) {
  (void)args;
  havel::host::BrowserService browser;
  auto tabs = browser.listTabs();
  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm) {
    return BytecodeValue(nullptr);
  }
  auto arr = vm->createHostArray();
  for (const auto &tab : tabs) {
    auto tabObj = vm->createHostObject();
    vm->setHostObjectField(tabObj, "id", BytecodeValue(static_cast<int64_t>(tab.id)));
    vm->setHostObjectField(tabObj, "title", BytecodeValue(tab.title));
    vm->setHostObjectField(tabObj, "url", BytecodeValue(tab.url));
    vm->setHostObjectField(tabObj, "type", BytecodeValue(tab.type));
    vm->pushHostArrayValue(arr, BytecodeValue(tabObj));
  }
  return BytecodeValue(arr);
}

// ============================================================================
// ToolsBridge Implementation
// ============================================================================

namespace {
  havel::host::TextChunkerService g_textChunker;
}

void ToolsBridge::install(PipelineOptions &options) {
  options.host_functions["textchunker.setText"] = [ctx = ctx_](const auto &args) {
    return handleTextChunkerSetText(args, ctx);
  };
  options.host_functions["textchunker.getText"] = [ctx = ctx_](const auto &args) {
    return handleTextChunkerGetText(args, ctx);
  };
  options.host_functions["textchunker.setChunkSize"] = [ctx = ctx_](const auto &args) {
    return handleTextChunkerSetChunkSize(args, ctx);
  };
  options.host_functions["textchunker.getTotalChunks"] = [ctx = ctx_](const auto &args) {
    return handleTextChunkerGetTotalChunks(args, ctx);
  };
  options.host_functions["textchunker.getCurrentChunk"] = [ctx = ctx_](const auto &args) {
    return handleTextChunkerGetCurrentChunk(args, ctx);
  };
  options.host_functions["textchunker.setCurrentChunk"] = [ctx = ctx_](const auto &args) {
    return handleTextChunkerSetCurrentChunk(args, ctx);
  };
  options.host_functions["textchunker.getChunk"] = [ctx = ctx_](const auto &args) {
    return handleTextChunkerGetChunk(args, ctx);
  };
  options.host_functions["textchunker.getNextChunk"] = [ctx = ctx_](const auto &args) {
    return handleTextChunkerGetNextChunk(args, ctx);
  };
  options.host_functions["textchunker.getPreviousChunk"] = [ctx = ctx_](const auto &args) {
    return handleTextChunkerGetPreviousChunk(args, ctx);
  };
  options.host_functions["textchunker.goToFirst"] = [ctx = ctx_](const auto &args) {
    return handleTextChunkerGoToFirst(args, ctx);
  };
  options.host_functions["textchunker.goToLast"] = [ctx = ctx_](const auto &args) {
    return handleTextChunkerGoToLast(args, ctx);
  };
  options.host_functions["textchunker.clear"] = [ctx = ctx_](const auto &args) {
    return handleTextChunkerClear(args, ctx);
  };
}

BytecodeValue ToolsBridge::handleTextChunkerSetText(const std::vector<BytecodeValue> &args,
                                                    const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("textchunker.setText() requires text");
  }
  const std::string *text = std::get_if<std::string>(&args[0]);
  if (!text) {
    throw std::runtime_error("textchunker.setText() requires a string");
  }
  g_textChunker.setText(*text);
  return BytecodeValue(true);
}

BytecodeValue ToolsBridge::handleTextChunkerGetText(const std::vector<BytecodeValue> &args,
                                                    const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return BytecodeValue(g_textChunker.getText());
}

BytecodeValue ToolsBridge::handleTextChunkerSetChunkSize(const std::vector<BytecodeValue> &args,
                                                         const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("textchunker.setChunkSize() requires a size");
  }
  int64_t size = 20000;
  if (std::holds_alternative<int64_t>(args[0])) {
    size = std::get<int64_t>(args[0]);
  }
  g_textChunker.setChunkSize(static_cast<size_t>(size));
  return BytecodeValue(true);
}

BytecodeValue ToolsBridge::handleTextChunkerGetTotalChunks(const std::vector<BytecodeValue> &args,
                                                           const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return BytecodeValue(static_cast<int64_t>(g_textChunker.getTotalChunks()));
}

BytecodeValue ToolsBridge::handleTextChunkerGetCurrentChunk(const std::vector<BytecodeValue> &args,
                                                            const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return BytecodeValue(static_cast<int64_t>(g_textChunker.getCurrentChunk()));
}

BytecodeValue ToolsBridge::handleTextChunkerSetCurrentChunk(const std::vector<BytecodeValue> &args,
                                                            const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("textchunker.setCurrentChunk() requires a chunk index");
  }
  int64_t index = 0;
  if (std::holds_alternative<int64_t>(args[0])) {
    index = std::get<int64_t>(args[0]);
  }
  g_textChunker.setCurrentChunk(static_cast<int>(index));
  return BytecodeValue(true);
}

BytecodeValue ToolsBridge::handleTextChunkerGetChunk(const std::vector<BytecodeValue> &args,
                                                     const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("textchunker.getChunk() requires a chunk index");
  }
  int64_t index = 0;
  if (std::holds_alternative<int64_t>(args[0])) {
    index = std::get<int64_t>(args[0]);
  }
  return BytecodeValue(g_textChunker.getChunk(static_cast<int>(index)));
}

BytecodeValue ToolsBridge::handleTextChunkerGetNextChunk(const std::vector<BytecodeValue> &args,
                                                         const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return BytecodeValue(g_textChunker.getNextChunk());
}

BytecodeValue ToolsBridge::handleTextChunkerGetPreviousChunk(const std::vector<BytecodeValue> &args,
                                                             const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return BytecodeValue(g_textChunker.getPreviousChunk());
}

BytecodeValue ToolsBridge::handleTextChunkerGoToFirst(const std::vector<BytecodeValue> &args,
                                                      const HostContext *ctx) {
  (void)args;
  (void)ctx;
  g_textChunker.goToFirst();
  return BytecodeValue(true);
}

BytecodeValue ToolsBridge::handleTextChunkerGoToLast(const std::vector<BytecodeValue> &args,
                                                     const HostContext *ctx) {
  (void)args;
  (void)ctx;
  g_textChunker.goToLast();
  return BytecodeValue(true);
}

BytecodeValue ToolsBridge::handleTextChunkerClear(const std::vector<BytecodeValue> &args,
                                                  const HostContext *ctx) {
  (void)args;
  (void)ctx;
  g_textChunker.clear();
  return BytecodeValue(true);
}

// ============================================================================
// MediaBridge Implementation
// ============================================================================

void MediaBridge::install(PipelineOptions &options) {
  options.host_functions["media.playPause"] = [ctx = ctx_](const auto &args) {
    return handleMediaPlayPause(args, ctx);
  };
  options.host_functions["media.play"] = [ctx = ctx_](const auto &args) {
    return handleMediaPlay(args, ctx);
  };
  options.host_functions["media.pause"] = [ctx = ctx_](const auto &args) {
    return handleMediaPause(args, ctx);
  };
  options.host_functions["media.stop"] = [ctx = ctx_](const auto &args) {
    return handleMediaStop(args, ctx);
  };
  options.host_functions["media.next"] = [ctx = ctx_](const auto &args) {
    return handleMediaNext(args, ctx);
  };
  options.host_functions["media.previous"] = [ctx = ctx_](const auto &args) {
    return handleMediaPrevious(args, ctx);
  };
  options.host_functions["media.getVolume"] = [ctx = ctx_](const auto &args) {
    return handleMediaGetVolume(args, ctx);
  };
  options.host_functions["media.setVolume"] = [ctx = ctx_](const auto &args) {
    return handleMediaSetVolume(args, ctx);
  };
  options.host_functions["media.getActivePlayer"] = [ctx = ctx_](const auto &args) {
    return handleMediaGetActivePlayer(args, ctx);
  };
  options.host_functions["media.setActivePlayer"] = [ctx = ctx_](const auto &args) {
    return handleMediaSetActivePlayer(args, ctx);
  };
  options.host_functions["media.getAvailablePlayers"] = [ctx = ctx_](const auto &args) {
    return handleMediaGetAvailablePlayers(args, ctx);
  };
}

BytecodeValue MediaBridge::handleMediaPlayPause(const std::vector<BytecodeValue> &args,
                                                const HostContext *ctx) {
  (void)args;
  try {
    havel::host::MediaService media;
    media.playPause();
    return BytecodeValue(true);
  } catch (...) {
    return BytecodeValue(false);
  }
}

BytecodeValue MediaBridge::handleMediaPlay(const std::vector<BytecodeValue> &args,
                                           const HostContext *ctx) {
  (void)args;
  try {
    havel::host::MediaService media;
    media.play();
    return BytecodeValue(true);
  } catch (...) {
    return BytecodeValue(false);
  }
}

BytecodeValue MediaBridge::handleMediaPause(const std::vector<BytecodeValue> &args,
                                            const HostContext *ctx) {
  (void)args;
  try {
    havel::host::MediaService media;
    media.pause();
    return BytecodeValue(true);
  } catch (...) {
    return BytecodeValue(false);
  }
}

BytecodeValue MediaBridge::handleMediaStop(const std::vector<BytecodeValue> &args,
                                           const HostContext *ctx) {
  (void)args;
  try {
    havel::host::MediaService media;
    media.stop();
    return BytecodeValue(true);
  } catch (...) {
    return BytecodeValue(false);
  }
}

BytecodeValue MediaBridge::handleMediaNext(const std::vector<BytecodeValue> &args,
                                           const HostContext *ctx) {
  (void)args;
  try {
    havel::host::MediaService media;
    media.next();
    return BytecodeValue(true);
  } catch (...) {
    return BytecodeValue(false);
  }
}

BytecodeValue MediaBridge::handleMediaPrevious(const std::vector<BytecodeValue> &args,
                                               const HostContext *ctx) {
  (void)args;
  try {
    havel::host::MediaService media;
    media.previous();
    return BytecodeValue(true);
  } catch (...) {
    return BytecodeValue(false);
  }
}

BytecodeValue MediaBridge::handleMediaGetVolume(const std::vector<BytecodeValue> &args,
                                                const HostContext *ctx) {
  (void)args;
  try {
    havel::host::MediaService media;
    return BytecodeValue(media.getVolume());
  } catch (...) {
    return BytecodeValue(0.0);
  }
}

BytecodeValue MediaBridge::handleMediaSetVolume(const std::vector<BytecodeValue> &args,
                                                const HostContext *ctx) {
  if (args.empty()) {
    throw std::runtime_error("media.setVolume() requires a volume value (0.0-1.0)");
  }
  double volume = 0.0;
  if (std::holds_alternative<double>(args[0])) {
    volume = std::get<double>(args[0]);
  } else if (std::holds_alternative<int64_t>(args[0])) {
    volume = static_cast<double>(std::get<int64_t>(args[0])) / 100.0;
  }
  try {
    havel::host::MediaService media;
    media.setVolume(volume);
    return BytecodeValue(true);
  } catch (...) {
    return BytecodeValue(false);
  }
}

BytecodeValue MediaBridge::handleMediaGetActivePlayer(const std::vector<BytecodeValue> &args,
                                                      const HostContext *ctx) {
  (void)args;
  try {
    havel::host::MediaService media;
    return BytecodeValue(media.getActivePlayer());
  } catch (...) {
    return BytecodeValue(std::string(""));
  }
}

BytecodeValue MediaBridge::handleMediaSetActivePlayer(const std::vector<BytecodeValue> &args,
                                                      const HostContext *ctx) {
  if (args.empty()) {
    throw std::runtime_error("media.setActivePlayer() requires a player name");
  }
  const std::string *name = std::get_if<std::string>(&args[0]);
  if (!name) {
    throw std::runtime_error("media.setActivePlayer() requires a string");
  }
  try {
    havel::host::MediaService media;
    media.setActivePlayer(*name);
    return BytecodeValue(true);
  } catch (...) {
    return BytecodeValue(false);
  }
}

BytecodeValue MediaBridge::handleMediaGetAvailablePlayers(const std::vector<BytecodeValue> &args,
                                                          const HostContext *ctx) {
  (void)args;
  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm) {
    return BytecodeValue(nullptr);
  }
  try {
    havel::host::MediaService media;
    auto players = media.getAvailablePlayers();
    auto arr = vm->createHostArray();
    for (const auto &player : players) {
      vm->pushHostArrayValue(arr, BytecodeValue(player));
    }
    return BytecodeValue(arr);
  } catch (...) {
    return BytecodeValue(nullptr);
  }
}

// ============================================================================
// NetworkBridge Implementation
// ============================================================================

void NetworkBridge::install(PipelineOptions &options) {
  options.host_functions["network.get"] = [ctx = ctx_](const auto &args) {
    return handleNetworkGet(args, ctx);
  };
  options.host_functions["network.post"] = [ctx = ctx_](const auto &args) {
    return handleNetworkPost(args, ctx);
  };
  options.host_functions["network.isOnline"] = [ctx = ctx_](const auto &args) {
    return handleNetworkIsOnline(args, ctx);
  };
  options.host_functions["network.getExternalIp"] = [ctx = ctx_](const auto &args) {
    return handleNetworkGetExternalIp(args, ctx);
  };
}

BytecodeValue NetworkBridge::handleNetworkGet(const std::vector<BytecodeValue> &args,
                                              const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("network.get() requires a URL");
  }
  const std::string *url = std::get_if<std::string>(&args[0]);
  if (!url) {
    throw std::runtime_error("network.get() requires a string URL");
  }
  int timeout_ms = 30000;
  if (args.size() > 1 && std::holds_alternative<int64_t>(args[1])) {
    timeout_ms = static_cast<int>(std::get<int64_t>(args[1]));
  }
  try {
    havel::host::NetworkService net;
    auto response = net.get(*url, timeout_ms);
    if (response.success) {
      return BytecodeValue(response.body);
    } else {
      return BytecodeValue(nullptr);
    }
  } catch (...) {
    return BytecodeValue(nullptr);
  }
}

BytecodeValue NetworkBridge::handleNetworkPost(const std::vector<BytecodeValue> &args,
                                               const HostContext *ctx) {
  (void)ctx;
  if (args.size() < 2) {
    throw std::runtime_error("network.post() requires URL and data");
  }
  const std::string *url = std::get_if<std::string>(&args[0]);
  const std::string *data = std::get_if<std::string>(&args[1]);
  if (!url || !data) {
    throw std::runtime_error("network.post() requires string arguments");
  }
  std::string content_type = "application/json";
  if (args.size() > 2 && std::holds_alternative<std::string>(args[2])) {
    content_type = std::get<std::string>(args[2]);
  }
  int timeout_ms = 30000;
  if (args.size() > 3 && std::holds_alternative<int64_t>(args[3])) {
    timeout_ms = static_cast<int>(std::get<int64_t>(args[3]));
  }
  try {
    havel::host::NetworkService net;
    auto response = net.post(*url, *data, content_type, timeout_ms);
    if (response.success) {
      return BytecodeValue(response.body);
    } else {
      return BytecodeValue(nullptr);
    }
  } catch (...) {
    return BytecodeValue(nullptr);
  }
}

BytecodeValue NetworkBridge::handleNetworkIsOnline(const std::vector<BytecodeValue> &args,
                                                   const HostContext *ctx) {
  (void)args;
  (void)ctx;
  try {
    havel::host::NetworkService net;
    return BytecodeValue(net.isOnline());
  } catch (...) {
    return BytecodeValue(false);
  }
}

BytecodeValue NetworkBridge::handleNetworkGetExternalIp(const std::vector<BytecodeValue> &args,
                                                        const HostContext *ctx) {
  (void)args;
  (void)ctx;
  try {
    havel::host::NetworkService net;
    return BytecodeValue(net.getExternalIp());
  } catch (...) {
    return BytecodeValue(std::string(""));
  }
}

// ============================================================================
// AppBridge Implementation
// ============================================================================

void AppBridge::install(PipelineOptions &options) {
  options.host_functions["app.getName"] = [ctx = ctx_](const auto &args) {
    return handleAppGetName(args, ctx);
  };
  options.host_functions["app.getVersion"] = [ctx = ctx_](const auto &args) {
    return handleAppGetVersion(args, ctx);
  };
  options.host_functions["app.getOS"] = [ctx = ctx_](const auto &args) {
    return handleAppGetOS(args, ctx);
  };
  options.host_functions["app.getHostname"] = [ctx = ctx_](const auto &args) {
    return handleAppGetHostname(args, ctx);
  };
  options.host_functions["app.getUsername"] = [ctx = ctx_](const auto &args) {
    return handleAppGetUsername(args, ctx);
  };
  options.host_functions["app.getHomeDir"] = [ctx = ctx_](const auto &args) {
    return handleAppGetHomeDir(args, ctx);
  };
  options.host_functions["app.getCpuCores"] = [ctx = ctx_](const auto &args) {
    return handleAppGetCpuCores(args, ctx);
  };
  options.host_functions["app.getEnv"] = [ctx = ctx_](const auto &args) {
    return handleAppGetEnv(args, ctx);
  };
  options.host_functions["app.setEnv"] = [ctx = ctx_](const auto &args) {
    return handleAppSetEnv(args, ctx);
  };
  options.host_functions["app.openUrl"] = [ctx = ctx_](const auto &args) {
    return handleAppOpenUrl(args, ctx);
  };
}

BytecodeValue AppBridge::handleAppGetName(const std::vector<BytecodeValue> &args,
                                          const HostContext *ctx) {
  (void)args;
  (void)ctx;
  havel::host::AppService app;
  return BytecodeValue(app.getAppName());
}

BytecodeValue AppBridge::handleAppGetVersion(const std::vector<BytecodeValue> &args,
                                             const HostContext *ctx) {
  (void)args;
  (void)ctx;
  havel::host::AppService app;
  return BytecodeValue(app.getAppVersion());
}

BytecodeValue AppBridge::handleAppGetOS(const std::vector<BytecodeValue> &args,
                                        const HostContext *ctx) {
  (void)args;
  (void)ctx;
  havel::host::AppService app;
  return BytecodeValue(app.getOS());
}

BytecodeValue AppBridge::handleAppGetHostname(const std::vector<BytecodeValue> &args,
                                              const HostContext *ctx) {
  (void)args;
  (void)ctx;
  havel::host::AppService app;
  return BytecodeValue(app.getHostname());
}

BytecodeValue AppBridge::handleAppGetUsername(const std::vector<BytecodeValue> &args,
                                              const HostContext *ctx) {
  (void)args;
  (void)ctx;
  havel::host::AppService app;
  return BytecodeValue(app.getUsername());
}

BytecodeValue AppBridge::handleAppGetHomeDir(const std::vector<BytecodeValue> &args,
                                             const HostContext *ctx) {
  (void)args;
  (void)ctx;
  havel::host::AppService app;
  return BytecodeValue(app.getHomeDir());
}

BytecodeValue AppBridge::handleAppGetCpuCores(const std::vector<BytecodeValue> &args,
                                              const HostContext *ctx) {
  (void)args;
  (void)ctx;
  havel::host::AppService app;
  return BytecodeValue(static_cast<int64_t>(app.getCpuCores()));
}

BytecodeValue AppBridge::handleAppGetEnv(const std::vector<BytecodeValue> &args,
                                         const HostContext *ctx) {
  if (args.empty()) {
    throw std::runtime_error("app.getEnv() requires a variable name");
  }
  const std::string *name = std::get_if<std::string>(&args[0]);
  if (!name) {
    throw std::runtime_error("app.getEnv() requires a string");
  }
  havel::host::AppService app;
  return BytecodeValue(app.getEnv(*name));
}

BytecodeValue AppBridge::handleAppSetEnv(const std::vector<BytecodeValue> &args,
                                         const HostContext *ctx) {
  if (args.size() < 2) {
    throw std::runtime_error("app.setEnv() requires name and value");
  }
  const std::string *name = std::get_if<std::string>(&args[0]);
  const std::string *value = std::get_if<std::string>(&args[1]);
  if (!name || !value) {
    throw std::runtime_error("app.setEnv() requires string arguments");
  }
  havel::host::AppService app;
  return BytecodeValue(app.setEnv(*name, *value));
}

BytecodeValue AppBridge::handleAppOpenUrl(const std::vector<BytecodeValue> &args,
                                          const HostContext *ctx) {
  if (args.empty()) {
    throw std::runtime_error("app.openUrl() requires a URL");
  }
  const std::string *url = std::get_if<std::string>(&args[0]);
  if (!url) {
    throw std::runtime_error("app.openUrl() requires a string URL");
  }
  havel::host::AppService app;
  return BytecodeValue(app.openUrl(*url));
}

} // namespace havel::compiler
