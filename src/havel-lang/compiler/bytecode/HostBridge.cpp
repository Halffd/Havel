/*
 * HostBridge.cpp - Bridges VM to host services
 *
 * ARCHITECTURE:
 * - stdlib = PURE (Math, String, Array, Object, Type, Utility, Regex)
 * - host = OS access (File, Process, Timer, Hotkey, Window, Clipboard, IO)
 * - HostBridge exposes host services to VM
 */
#include "HostBridge.hpp"
#include "../../../gui/ClipboardManager.hpp"
#include "../../../host/async/AsyncService.hpp"
#include "../../../host/audio/AudioService.hpp"
#include "../../../host/automation/AutomationService.hpp"
#include "../../../host/browser/BrowserService.hpp"
#include "../../../host/brightness/BrightnessService.hpp"
#include "../../../host/filesystem/FileSystemService.hpp"
#include "../../../host/hotkey/HotkeyService.hpp"
#include "../../../host/io/IOService.hpp"
#include "../../../host/process/ProcessService.hpp"
#include "../../../host/screenshot/ScreenshotService.hpp"
#include "../../../host/window/WindowService.hpp"
#include <QClipboard>
#include <fstream>
#include <sstream>
#include <chrono>

namespace havel::compiler {

HostBridge::HostBridge(const havel::HostContext &ctx) : ctx_(&ctx) {
  // Note: io is optional for pure script execution mode
  // if (!ctx_->isValid()) {
  //   throw std::runtime_error("HostBridge: HostContext is invalid (io is
  //   null)");
  // }
}

HostBridge::~HostBridge() { shutdown(); }

void HostBridge::shutdown() {
  clear();
  options_.host_functions.clear();
  vm_setup_callbacks_.clear();
  vm_setup_callbacks_.shrink_to_fit();
  modules_.clear();
}

void HostBridge::clear() {
  hotkey_binding_keys_.clear();
  mode_bindings_.clear();
  mode_definition_order_.clear();
}

void HostBridge::install() {
  auto self = shared_from_this();
  auto &options = options_;

  // Reserve space
  options.host_functions.reserve(64);
  vm_setup_callbacks_.reserve(16);

  // ==========================================================================
  // IO handlers
  // ==========================================================================
  options.host_functions["send"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleSend(args);
      };
  options.host_functions["io.send"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleSend(args);
      };
  options.host_functions["io.sendKey"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleSendKey(args);
      };
  options.host_functions["io.mouseMove"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleMouseMove(args);
      };
  options.host_functions["io.mouseClick"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleMouseClick(args);
      };
  options.host_functions["io.getMousePosition"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleGetMousePosition(args);
      };

  // ==========================================================================
  // File System handlers
  // ==========================================================================
  options.host_functions["readFile"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleFileRead(args);
      };
  options.host_functions["writeFile"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleFileWrite(args);
      };
  options.host_functions["fileExists"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleFileExists(args);
      };
  options.host_functions["fileSize"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleFileSize(args);
      };
  options.host_functions["deleteFile"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleFileDelete(args);
      };

  // ==========================================================================
  // Process handlers
  // ==========================================================================
  options.host_functions["execute"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleProcessExecute(args);
      };
  options.host_functions["getpid"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleProcessGetPid(args);
      };
  options.host_functions["getppid"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleProcessGetPpid(args);
      };
  options.host_functions["process.find"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleProcessFind(args);
      };

  // ==========================================================================
  // Window handlers
  // ==========================================================================
  options.host_functions["window.active"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleWindowGetActive(args);
      };
  options.host_functions["window.close"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleWindowClose(args);
      };
  options.host_functions["window.resize"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleWindowResize(args);
      };
  options.host_functions["window.moveToMonitor"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleWindowMoveToMonitor(args);
      };
  options.host_functions["window.moveToNextMonitor"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleWindowMoveToNextMonitor(args);
      };

  // ==========================================================================
  // Hotkey handlers
  // ==========================================================================
  options.host_functions["hotkey.register"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleHotkeyRegister(args);
      };
  options.host_functions["hotkey.trigger"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleHotkeyTrigger(args);
      };

  // ==========================================================================
  // Mode handlers
  // ==========================================================================
  options.host_functions["mode.define"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleModeDefine(args);
      };
  options.host_functions["mode.set"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleModeSet(args);
      };
  options.host_functions["mode.tick"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleModeTick(args);
      };

  // ==========================================================================
  // Clipboard handlers
  // ==========================================================================
  options.host_functions["clipboard.get"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleClipboardGet(args);
      };
  options.host_functions["clipboard.set"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleClipboardSet(args);
      };
  options.host_functions["clipboard.clear"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleClipboardClear(args);
      };

  // ==========================================================================
  // Screenshot handlers
  // ==========================================================================
  options.host_functions["screenshot.full"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleScreenshotFull(args);
      };
  options.host_functions["screenshot.monitor"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleScreenshotMonitor(args);
      };

  // ==========================================================================
  // Async/Timer handlers
  // ==========================================================================
  options.host_functions["sleep"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleSleep(args);
      };
  options.host_functions["time.now"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleTimeNow(args);
      };

  // ==========================================================================
  // Audio handlers
  // ==========================================================================
  options.host_functions["audio.getVolume"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleAudioGetVolume(args);
      };
  options.host_functions["audio.setVolume"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleAudioSetVolume(args);
      };
  options.host_functions["audio.toggleMute"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleAudioToggleMute(args);
      };
  options.host_functions["audio.setMute"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleAudioSetMute(args);
      };
  options.host_functions["audio.isMuted"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleAudioIsMuted(args);
      };

  // ==========================================================================
  // Brightness handlers
  // ==========================================================================
  options.host_functions["brightness.get"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleBrightnessGet(args);
      };
  options.host_functions["brightness.set"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleBrightnessSet(args);
      };
  options.host_functions["brightness.getTemp"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleBrightnessGetTemp(args);
      };
  options.host_functions["brightness.setTemp"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleBrightnessSetTemp(args);
      };

  // ==========================================================================
  // Automation handlers
  // ==========================================================================
  options.host_functions["automation.createAutoClicker"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleAutomationCreateAutoClicker(args);
      };
  options.host_functions["automation.createAutoRunner"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleAutomationCreateAutoRunner(args);
      };
  options.host_functions["automation.createAutoKeyPresser"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleAutomationCreateAutoKeyPresser(args);
      };
  options.host_functions["automation.hasTask"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleAutomationHasTask(args);
      };
  options.host_functions["automation.removeTask"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleAutomationRemoveTask(args);
      };
  options.host_functions["automation.stopAll"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleAutomationStopAll(args);
      };

  // ==========================================================================
  // Browser handlers
  // ==========================================================================
  options.host_functions["browser.connect"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleBrowserConnect(args);
      };
  options.host_functions["browser.connectFirefox"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleBrowserConnectFirefox(args);
      };
  options.host_functions["browser.disconnect"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleBrowserDisconnect(args);
      };
  options.host_functions["browser.isConnected"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleBrowserIsConnected(args);
      };
  options.host_functions["browser.open"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleBrowserOpen(args);
      };
  options.host_functions["browser.newTab"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleBrowserNewTab(args);
      };
  options.host_functions["browser.goto"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleBrowserGoto(args);
      };
  options.host_functions["browser.back"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleBrowserBack(args);
      };
  options.host_functions["browser.forward"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleBrowserForward(args);
      };
  options.host_functions["browser.reload"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleBrowserReload(args);
      };
  options.host_functions["browser.listTabs"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleBrowserListTabs(args);
      };

  // Run vm_setup callbacks
  for (auto &setupFn : vm_setup_callbacks_) {
    setupFn(*static_cast<VM *>(ctx_->vm));
  }
}

void HostBridge::registerModule(const HostModule &module) {
  modules_.push_back(module);
  // STUBBED - module registration requires fixing HostFn/BytecodeHostFunction
  // mismatch
}

void HostBridge::addVmSetup(std::function<void(VM &)> setupFn) {
  vm_setup_callbacks_.push_back(std::move(setupFn));
}

// Stub handler implementations
BytecodeValue HostBridge::handleSend(const std::vector<BytecodeValue> &args) {
  if (args.empty() || !ctx_->io) {
    return BytecodeValue(false);
  }

  havel::host::IOService ioService(ctx_->io);
  if (auto *keys = std::get_if<std::string>(&args[0])) {
    return BytecodeValue(ioService.sendKeys(*keys));
  }
  return BytecodeValue(false);
}

BytecodeValue
HostBridge::handleSendKey(const std::vector<BytecodeValue> &args) {
  if (args.empty() || !ctx_->io) {
    return BytecodeValue(false);
  }

  havel::host::IOService ioService(ctx_->io);
  if (auto *key = std::get_if<std::string>(&args[0])) {
    return BytecodeValue(ioService.sendKey(*key));
  }
  return BytecodeValue(false);
}

BytecodeValue
HostBridge::handleMouseMove(const std::vector<BytecodeValue> &args) {
  if (args.size() < 2 || !ctx_->io) {
    return BytecodeValue(false);
  }

  havel::host::IOService ioService(ctx_->io);
  int64_t dx = 0;
  int64_t dy = 0;

  if (auto *v = std::get_if<int64_t>(&args[0]))
    dx = *v;
  else if (auto *v = std::get_if<double>(&args[0]))
    dx = static_cast<int64_t>(*v);

  if (auto *v = std::get_if<int64_t>(&args[1]))
    dy = *v;
  else if (auto *v = std::get_if<double>(&args[1]))
    dy = static_cast<int64_t>(*v);

  return BytecodeValue(
      ioService.mouseMove(static_cast<int>(dx), static_cast<int>(dy)));
}

BytecodeValue
HostBridge::handleMouseClick(const std::vector<BytecodeValue> &args) {
  if (args.empty() || !ctx_->io) {
    return BytecodeValue(false);
  }

  havel::host::IOService ioService(ctx_->io);
  int64_t button = 1; // Default to left click

  if (auto *v = std::get_if<int64_t>(&args[0]))
    button = *v;
  else if (auto *v = std::get_if<double>(&args[0]))
    button = static_cast<int64_t>(*v);

  return BytecodeValue(ioService.mouseClick(static_cast<int>(button)));
}

BytecodeValue
HostBridge::handleGetMousePosition(const std::vector<BytecodeValue> &args) {
  (void)args;
  if (!ctx_->io) {
    return BytecodeValue(nullptr);
  }

  havel::host::IOService ioService(ctx_->io);
  auto pos = ioService.getMousePosition();

  auto obj = ctx_->vm->createHostObject();
  ctx_->vm->setHostObjectField(obj, "x",
                               BytecodeValue(static_cast<int64_t>(pos.first)));
  ctx_->vm->setHostObjectField(obj, "y",
                               BytecodeValue(static_cast<int64_t>(pos.second)));
  return BytecodeValue(obj);
}

BytecodeValue HostBridge::handleWindowMoveToNextMonitor(
    const std::vector<BytecodeValue> &args) {
  (void)args;
  havel::host::WindowService::moveActiveWindowToNextMonitor();
  return BytecodeValue(true);
}

BytecodeValue
HostBridge::handleWindowGetActive(const std::vector<BytecodeValue> &args) {
  (void)args;
  if (!ctx_->windowManager) {
    return BytecodeValue(static_cast<int64_t>(0));
  }

  havel::host::WindowService winService(ctx_->windowManager);
  return BytecodeValue(static_cast<int64_t>(winService.getActiveWindow()));
}

BytecodeValue
HostBridge::handleWindowMoveToMonitor(const std::vector<BytecodeValue> &args) {
  if (args.size() < 2 || !ctx_->windowManager) {
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

  havel::host::WindowService winService(ctx_->windowManager);
  return BytecodeValue(winService.moveWindowToMonitor(wid, monitor));
}

BytecodeValue
HostBridge::handleWindowClose(const std::vector<BytecodeValue> &args) {
  if (args.empty() || !ctx_->windowManager) {
    return BytecodeValue(false);
  }

  uint64_t wid = 0;
  if (auto *v = std::get_if<int64_t>(&args[0]))
    wid = static_cast<uint64_t>(*v);
  else
    return BytecodeValue(false);

  havel::host::WindowService winService(ctx_->windowManager);
  return BytecodeValue(winService.closeWindow(wid));
}

BytecodeValue
HostBridge::handleWindowResize(const std::vector<BytecodeValue> &args) {
  if (args.size() < 3 || !ctx_->windowManager) {
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

  havel::host::WindowService winService(ctx_->windowManager);
  return BytecodeValue(winService.resizeWindow(wid, w, h));
}

BytecodeValue
HostBridge::handleWindowOn(const std::vector<BytecodeValue> &args) {
  (void)args;
  return BytecodeValue(nullptr);
}

BytecodeValue
HostBridge::handleHotkeyRegister(const std::vector<BytecodeValue> &args) {
  if (args.size() < 2 || !ctx_->hotkeyManager) {
    return BytecodeValue(false);
  }

  auto self = shared_from_this();
  std::string keyCombo;
  if (auto *v = std::get_if<std::string>(&args[0])) {
    keyCombo = *v;
  } else {
    return BytecodeValue(false);
  }

  BytecodeValue closure = args[1];
  CallbackId cid = registerCallback(closure);

  if (ctx_->hotkeyManager) {
    auto hotkeyManagerPtr = std::shared_ptr<havel::HotkeyManager>(
        ctx_->hotkeyManager, [](havel::HotkeyManager *) {});
    havel::host::HotkeyService hotkeyService(hotkeyManagerPtr);
    bool success = hotkeyService.registerHotkey(
        keyCombo, [self, cid]() { self->invokeCallback(cid); });

    if (success) {
      hotkey_binding_keys_[cid] = keyCombo;
      return BytecodeValue(static_cast<int64_t>(cid));
    }
  }

  releaseCallback(cid);
  return BytecodeValue(false);
}

BytecodeValue
HostBridge::handleHotkeyTrigger(const std::vector<BytecodeValue> &args) {
  (void)args;
  return BytecodeValue(nullptr);
}

BytecodeValue
HostBridge::handleModeDefine(const std::vector<BytecodeValue> &args) {
  if (args.empty() || !ctx_->modeManager) {
    return BytecodeValue(false);
  }

  std::string modeName;
  if (auto *v = std::get_if<std::string>(&args[0])) {
    modeName = *v;
  } else {
    return BytecodeValue(false);
  }

  // Placeholder for mode definition logic
  // In a full implementation, this would interact with ModeManager
  // and store the ModeBinding (condition, enter, exit callbacks)
  mode_definition_order_.push_back(modeName);
  return BytecodeValue(true);
}

BytecodeValue
HostBridge::handleModeSet(const std::vector<BytecodeValue> &args) {
  if (args.empty() || !ctx_->modeManager) {
    return BytecodeValue(false);
  }

  std::string modeName;
  if (auto *v = std::get_if<std::string>(&args[0])) {
    modeName = *v;
  } else {
    return BytecodeValue(false);
  }

  // ModeManager transition logic would go here
  return BytecodeValue(true);
}

BytecodeValue
HostBridge::handleModeTick(const std::vector<BytecodeValue> &args) {
  (void)args;
  return BytecodeValue(nullptr);
}

BytecodeValue
HostBridge::handleProcessFind(const std::vector<BytecodeValue> &args) {
  if (args.empty()) {
    return BytecodeValue(nullptr);
  }

  std::string name;
  if (auto *v = std::get_if<std::string>(&args[0])) {
    name = *v;
  } else {
    return BytecodeValue(nullptr);
  }

  auto pids = havel::host::ProcessService::findProcesses(name);
  auto array_ref = ctx_->vm->createHostArray();
  for (int32_t pid : pids) {
    ctx_->vm->pushHostArrayValue(array_ref,
                                 BytecodeValue(static_cast<int64_t>(pid)));
  }
  return BytecodeValue(array_ref);
}

BytecodeValue
HostBridge::handleClipboardGet(const std::vector<BytecodeValue> &args) {
  (void)args;
  if (!ctx_->clipboardManager) {
    return BytecodeValue(nullptr);
  }

  auto *clipboard = ctx_->clipboardManager->getClipboard();
  if (!clipboard) {
    return BytecodeValue(nullptr);
  }

  return BytecodeValue(clipboard->text().toStdString());
}

BytecodeValue
HostBridge::handleClipboardSet(const std::vector<BytecodeValue> &args) {
  if (args.empty() || !ctx_->clipboardManager) {
    return BytecodeValue(false);
  }

  auto *clipboard = ctx_->clipboardManager->getClipboard();
  if (!clipboard) {
    return BytecodeValue(false);
  }

  if (auto *val = std::get_if<std::string>(&args[0])) {
    clipboard->setText(QString::fromStdString(*val));
    return BytecodeValue(true);
  }

  return BytecodeValue(false);
}

BytecodeValue
HostBridge::handleClipboardClear(const std::vector<BytecodeValue> &args) {
  (void)args;
  if (!ctx_->clipboardManager) {
    return BytecodeValue(false);
  }

  auto *clipboard = ctx_->clipboardManager->getClipboard();
  if (!clipboard) {
    return BytecodeValue(false);
  }

  clipboard->clear();
  return BytecodeValue(true);
}

BytecodeValue
HostBridge::handleScreenshotFull(const std::vector<BytecodeValue> &args) {
  (void)args;
  havel::host::ScreenshotService service;
  auto data = service.captureFullDesktop();
  auto geom = service.getMonitorGeometry(0);
  if (geom.size() < 4)
    return BytecodeValue(nullptr);

  // NOTE: VMImage is NOT currently in BytecodeValue variant.
  // Returning nullptr until variant is updated or image is handled via
  // HostObject.
  return BytecodeValue(nullptr);
}

BytecodeValue
HostBridge::handleScreenshotMonitor(const std::vector<BytecodeValue> &args) {
  if (args.empty())
    return BytecodeValue(nullptr);

  int monitor = 0;
  if (auto *v = std::get_if<int64_t>(&args[0]))
    monitor = static_cast<int>(*v);

  havel::host::ScreenshotService service;
  auto data = service.captureMonitor(monitor);
  auto geom = service.getMonitorGeometry(monitor);
  if (geom.size() < 4)
    return BytecodeValue(nullptr);

  return BytecodeValue(nullptr);
}

BytecodeValue
HostBridge::handleScreenshotWindow(const std::vector<BytecodeValue> &args) {
  (void)args;
  return BytecodeValue(nullptr);
}

BytecodeValue
HostBridge::handleScreenshotRegion(const std::vector<BytecodeValue> &args) {
  if (args.size() < 4)
    return BytecodeValue(nullptr);

  int x = 0, y = 0, w = 0, h = 0;
  if (auto *v = std::get_if<int64_t>(&args[0]))
    x = static_cast<int>(*v);
  if (auto *v = std::get_if<int64_t>(&args[1]))
    y = static_cast<int>(*v);
  if (auto *v = std::get_if<int64_t>(&args[2]))
    w = static_cast<int>(*v);
  if (auto *v = std::get_if<int64_t>(&args[3]))
    h = static_cast<int>(*v);

  return BytecodeValue(nullptr);
}

// ============================================================================
// File System Handlers - uses FileSystemService
// ============================================================================

BytecodeValue
HostBridge::handleFileRead(const std::vector<BytecodeValue> &args) {
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

BytecodeValue
HostBridge::handleFileWrite(const std::vector<BytecodeValue> &args) {
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

BytecodeValue
HostBridge::handleFileExists(const std::vector<BytecodeValue> &args) {
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

BytecodeValue
HostBridge::handleFileSize(const std::vector<BytecodeValue> &args) {
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

BytecodeValue
HostBridge::handleFileDelete(const std::vector<BytecodeValue> &args) {
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

// ============================================================================
// Process Handlers - uses ProcessService
// ============================================================================

BytecodeValue
HostBridge::handleProcessExecute(const std::vector<BytecodeValue> &args) {
  if (args.empty()) {
    throw std::runtime_error("execute() requires a command");
  }

  const std::string *command = std::get_if<std::string>(&args[0]);
  if (!command) {
    throw std::runtime_error("execute() requires a string command");
  }

  // Execute command and capture output
  std::ostringstream output;
  FILE *pipe = popen(command->c_str(), "r");
  if (!pipe) {
    throw std::runtime_error("Failed to execute command: " + *command);
  }

  char buffer[128];
  while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    output << buffer;
  }

  int exit_code = pclose(pipe);

  // Return result as object with output and exit_code
  // For now, just return the output string
  return BytecodeValue(output.str());
}

BytecodeValue
HostBridge::handleProcessGetPid(const std::vector<BytecodeValue> &args) {
  (void)args;
  return BytecodeValue(static_cast<int64_t>(havel::host::ProcessService::getCurrentPid()));
}

BytecodeValue
HostBridge::handleProcessGetPpid(const std::vector<BytecodeValue> &args) {
  (void)args;
  return BytecodeValue(static_cast<int64_t>(havel::host::ProcessService::getParentPid()));
}

BytecodeValue
HostBridge::handleProcessFind(const std::vector<BytecodeValue> &args) {
  if (args.empty()) {
    throw std::runtime_error("process.find() requires a process name");
  }

  const std::string *name = std::get_if<std::string>(&args[0]);
  if (!name) {
    throw std::runtime_error("process.find() requires a string argument");
  }

  auto pids = havel::host::ProcessService::findProcesses(*name);

  // Create array of PIDs
  auto *vm = static_cast<VM *>(ctx_->vm);
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
// Callback Management
// ============================================================================

CallbackId HostBridge::registerCallback(const BytecodeValue &closure) {
  if (!ctx_->vm) {
    throw std::runtime_error("VM not available for callback registration");
  }
  return ctx_->vm->registerCallback(closure);
}

BytecodeValue
HostBridge::invokeCallback(CallbackId id,
                           const std::vector<BytecodeValue> &args) {
  if (!ctx_->vm) {
    throw std::runtime_error("VM not available for callback invocation");
  }
  return ctx_->vm->invokeCallback(id, args);
}

void HostBridge::releaseCallback(CallbackId id) {
  if (!ctx_->vm) {
    throw std::runtime_error("VM not available for callback release");
  }
  ctx_->vm->releaseCallback(id);
}

// ============================================================================
// Async/Timer Handlers - uses AsyncService
// ============================================================================

BytecodeValue HostBridge::handleSleep(const std::vector<BytecodeValue> &args) {
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

  // Use AsyncService if available, otherwise fall back to direct sleep
  if (ctx_->asyncService) {
    ctx_->asyncService->sleep(static_cast<int>(ms));
  } else {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
  }

  return BytecodeValue(nullptr);
}

BytecodeValue HostBridge::handleTimeNow(const std::vector<BytecodeValue> &args) {
  (void)args;
  auto now = std::chrono::system_clock::now();
  auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                       now.time_since_epoch())
                       .count();
  return BytecodeValue(static_cast<int64_t>(timestamp));
}

// ============================================================================
// Audio Handlers - uses AudioService
// ============================================================================

BytecodeValue HostBridge::handleAudioGetVolume(const std::vector<BytecodeValue> &args) {
  (void)args;
  if (!ctx_->audioManager) {
    return BytecodeValue(0.0);
  }
  havel::host::AudioService audioService(ctx_->audioManager);
  return BytecodeValue(audioService.getVolume());
}

BytecodeValue HostBridge::handleAudioSetVolume(const std::vector<BytecodeValue> &args) {
  if (args.empty()) {
    throw std::runtime_error("audio.setVolume() requires a volume value (0.0-1.0)");
  }

  double volume = 0.0;
  if (std::holds_alternative<int64_t>(args[0])) {
    volume = static_cast<double>(std::get<int64_t>(args[0])) / 100.0;
  } else if (std::holds_alternative<double>(args[0])) {
    volume = std::get<double>(args[0]);
  } else {
    throw std::runtime_error("audio.setVolume() requires a number");
  }

  if (!ctx_->audioManager) {
    return BytecodeValue(false);
  }
  havel::host::AudioService audioService(ctx_->audioManager);
  audioService.setVolume(volume);
  return BytecodeValue(true);
}

BytecodeValue HostBridge::handleAudioToggleMute(const std::vector<BytecodeValue> &args) {
  (void)args;
  if (!ctx_->audioManager) {
    return BytecodeValue(false);
  }
  havel::host::AudioService audioService(ctx_->audioManager);
  audioService.toggleMute();
  return BytecodeValue(true);
}

BytecodeValue HostBridge::handleAudioSetMute(const std::vector<BytecodeValue> &args) {
  if (args.empty()) {
    throw std::runtime_error("audio.setMute() requires a boolean");
  }

  bool muted = false;
  if (std::holds_alternative<bool>(args[0])) {
    muted = std::get<bool>(args[0]);
  } else {
    throw std::runtime_error("audio.setMute() requires a boolean");
  }

  if (!ctx_->audioManager) {
    return BytecodeValue(false);
  }
  havel::host::AudioService audioService(ctx_->audioManager);
  audioService.setMute(muted);
  return BytecodeValue(true);
}

BytecodeValue HostBridge::handleAudioIsMuted(const std::vector<BytecodeValue> &args) {
  (void)args;
  if (!ctx_->audioManager) {
    return BytecodeValue(false);
  }
  havel::host::AudioService audioService(ctx_->audioManager);
  return BytecodeValue(audioService.isMuted());
}

// ============================================================================
// Brightness Handlers - uses BrightnessService
// ============================================================================

BytecodeValue HostBridge::handleBrightnessGet(const std::vector<BytecodeValue> &args) {
  int64_t monitorIndex = -1;
  if (!args.empty() && std::holds_alternative<int64_t>(args[0])) {
    monitorIndex = std::get<int64_t>(args[0]);
  }

  if (!ctx_->brightnessManager) {
    return BytecodeValue(0.0);
  }
  havel::host::BrightnessService brightnessService(ctx_->brightnessManager);
  return BytecodeValue(brightnessService.getBrightness(static_cast<int>(monitorIndex)));
}

BytecodeValue HostBridge::handleBrightnessSet(const std::vector<BytecodeValue> &args) {
  if (args.empty()) {
    throw std::runtime_error("brightness.set() requires a brightness value (0.0-1.0)");
  }

  double brightness = 0.0;
  if (std::holds_alternative<int64_t>(args[0])) {
    brightness = static_cast<double>(std::get<int64_t>(args[0])) / 100.0;
  } else if (std::holds_alternative<double>(args[0])) {
    brightness = std::get<double>(args[0]);
  } else {
    throw std::runtime_error("brightness.set() requires a number");
  }

  int64_t monitorIndex = -1;
  if (args.size() > 1 && std::holds_alternative<int64_t>(args[1])) {
    monitorIndex = std::get<int64_t>(args[1]);
  }

  if (!ctx_->brightnessManager) {
    return BytecodeValue(false);
  }
  havel::host::BrightnessService brightnessService(ctx_->brightnessManager);
  brightnessService.setBrightness(brightness, static_cast<int>(monitorIndex));
  return BytecodeValue(true);
}

BytecodeValue HostBridge::handleBrightnessGetTemp(const std::vector<BytecodeValue> &args) {
  int64_t monitorIndex = -1;
  if (!args.empty() && std::holds_alternative<int64_t>(args[0])) {
    monitorIndex = std::get<int64_t>(args[0]);
  }

  if (!ctx_->brightnessManager) {
    return BytecodeValue(6500);
  }
  havel::host::BrightnessService brightnessService(ctx_->brightnessManager);
  return BytecodeValue(static_cast<int64_t>(brightnessService.getTemperature(static_cast<int>(monitorIndex))));
}

BytecodeValue HostBridge::handleBrightnessSetTemp(const std::vector<BytecodeValue> &args) {
  if (args.empty()) {
    throw std::runtime_error("brightness.setTemp() requires a temperature value");
  }

  int64_t temperature = 6500;
  if (std::holds_alternative<int64_t>(args[0])) {
    temperature = std::get<int64_t>(args[0]);
  } else {
    throw std::runtime_error("brightness.setTemp() requires an integer");
  }

  int64_t monitorIndex = -1;
  if (args.size() > 1 && std::holds_alternative<int64_t>(args[1])) {
    monitorIndex = std::get<int64_t>(args[1]);
  }

  if (!ctx_->brightnessManager) {
    return BytecodeValue(false);
  }
  havel::host::BrightnessService brightnessService(ctx_->brightnessManager);
  brightnessService.setTemperature(static_cast<int>(temperature), static_cast<int>(monitorIndex));
  return BytecodeValue(true);
}

// ============================================================================
// Automation Handlers - uses AutomationService
// ============================================================================

BytecodeValue HostBridge::handleAutomationCreateAutoClicker(const std::vector<BytecodeValue> &args) {
  if (args.empty()) {
    throw std::runtime_error("automation.createAutoClicker() requires a task name");
  }

  const std::string *name = std::get_if<std::string>(&args[0]);
  if (!name) {
    throw std::runtime_error("automation.createAutoClicker() requires a string name");
  }

  std::string button = "left";
  int intervalMs = 100;

  if (args.size() > 1) {
    if (auto *s = std::get_if<std::string>(&args[1])) {
      button = *s;
    }
  }
  if (args.size() > 2) {
    if (auto *v = std::get_if<int64_t>(&args[2])) {
      intervalMs = static_cast<int>(*v);
    }
  }

  // Note: AutomationService requires IO, which may not be available in all contexts
  // For now, return false to indicate not available
  return BytecodeValue(false);
}

BytecodeValue HostBridge::handleAutomationCreateAutoRunner(const std::vector<BytecodeValue> &args) {
  if (args.empty()) {
    throw std::runtime_error("automation.createAutoRunner() requires a task name");
  }

  const std::string *name = std::get_if<std::string>(&args[0]);
  if (!name) {
    throw std::runtime_error("automation.createAutoRunner() requires a string name");
  }

  std::string direction = "right";
  int intervalMs = 50;

  if (args.size() > 1) {
    if (auto *s = std::get_if<std::string>(&args[1])) {
      direction = *s;
    }
  }
  if (args.size() > 2) {
    if (auto *v = std::get_if<int64_t>(&args[2])) {
      intervalMs = static_cast<int>(*v);
    }
  }

  return BytecodeValue(false);
}

BytecodeValue HostBridge::handleAutomationCreateAutoKeyPresser(const std::vector<BytecodeValue> &args) {
  if (args.size() < 2) {
    throw std::runtime_error("automation.createAutoKeyPresser() requires name and key");
  }

  const std::string *name = std::get_if<std::string>(&args[0]);
  const std::string *key = std::get_if<std::string>(&args[1]);
  if (!name || !key) {
    throw std::runtime_error("automation.createAutoKeyPresser() requires string arguments");
  }

  int intervalMs = 100;
  if (args.size() > 2) {
    if (auto *v = std::get_if<int64_t>(&args[2])) {
      intervalMs = static_cast<int>(*v);
    }
  }

  return BytecodeValue(false);
}

BytecodeValue HostBridge::handleAutomationHasTask(const std::vector<BytecodeValue> &args) {
  if (args.empty()) {
    throw std::runtime_error("automation.hasTask() requires a task name");
  }

  const std::string *name = std::get_if<std::string>(&args[0]);
  if (!name) {
    throw std::runtime_error("automation.hasTask() requires a string name");
  }

  return BytecodeValue(false);
}

BytecodeValue HostBridge::handleAutomationRemoveTask(const std::vector<BytecodeValue> &args) {
  if (args.empty()) {
    throw std::runtime_error("automation.removeTask() requires a task name");
  }

  const std::string *name = std::get_if<std::string>(&args[0]);
  if (!name) {
    throw std::runtime_error("automation.removeTask() requires a string name");
  }

  return BytecodeValue(false);
}

BytecodeValue HostBridge::handleAutomationStopAll(const std::vector<BytecodeValue> &args) {
  (void)args;
  return BytecodeValue(false);
}

// ============================================================================
// Browser Handlers - uses BrowserService
// ============================================================================

BytecodeValue HostBridge::handleBrowserConnect(const std::vector<BytecodeValue> &args) {
  std::string browserUrl = "http://localhost:9222";

  if (!args.empty()) {
    if (auto *s = std::get_if<std::string>(&args[0])) {
      browserUrl = *s;
    }
  }

  havel::host::BrowserService browser;
  return BytecodeValue(browser.connect(browserUrl));
}

BytecodeValue HostBridge::handleBrowserConnectFirefox(const std::vector<BytecodeValue> &args) {
  int port = 2828;

  if (!args.empty()) {
    if (auto *v = std::get_if<int64_t>(&args[0])) {
      port = static_cast<int>(*v);
    }
  }

  havel::host::BrowserService browser;
  return BytecodeValue(browser.connectFirefox(port));
}

BytecodeValue HostBridge::handleBrowserDisconnect(const std::vector<BytecodeValue> &args) {
  (void)args;
  havel::host::BrowserService browser;
  browser.disconnect();
  return BytecodeValue(true);
}

BytecodeValue HostBridge::handleBrowserIsConnected(const std::vector<BytecodeValue> &args) {
  (void)args;
  havel::host::BrowserService browser;
  return BytecodeValue(browser.isConnected());
}

BytecodeValue HostBridge::handleBrowserOpen(const std::vector<BytecodeValue> &args) {
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

BytecodeValue HostBridge::handleBrowserNewTab(const std::vector<BytecodeValue> &args) {
  std::string url;

  if (!args.empty()) {
    if (auto *s = std::get_if<std::string>(&args[0])) {
      url = *s;
    }
  }

  havel::host::BrowserService browser;
  return BytecodeValue(browser.newTab(url));
}

BytecodeValue HostBridge::handleBrowserGoto(const std::vector<BytecodeValue> &args) {
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

BytecodeValue HostBridge::handleBrowserBack(const std::vector<BytecodeValue> &args) {
  (void)args;
  havel::host::BrowserService browser;
  return BytecodeValue(browser.back());
}

BytecodeValue HostBridge::handleBrowserForward(const std::vector<BytecodeValue> &args) {
  (void)args;
  havel::host::BrowserService browser;
  return BytecodeValue(browser.forward());
}

BytecodeValue HostBridge::handleBrowserReload(const std::vector<BytecodeValue> &args) {
  bool ignoreCache = false;

  if (!args.empty()) {
    if (auto *b = std::get_if<bool>(&args[0])) {
      ignoreCache = *b;
    }
  }

  havel::host::BrowserService browser;
  return BytecodeValue(browser.reload(ignoreCache));
}

BytecodeValue HostBridge::handleBrowserListTabs(const std::vector<BytecodeValue> &args) {
  (void)args;
  havel::host::BrowserService browser;
  auto tabs = browser.listTabs();

  // Create array of tab objects
  auto *vm = static_cast<VM *>(ctx_->vm);
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

std::shared_ptr<HostBridge> createHostBridge(const havel::HostContext &ctx) {
  return std::make_shared<HostBridge>(ctx);
}

} // namespace havel::compiler
