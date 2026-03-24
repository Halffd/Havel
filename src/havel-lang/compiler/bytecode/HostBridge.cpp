/*
 * HostBridge.cpp - STUBBED for bytecode VM migration
 * Full implementation requires fixing HostContext/VM integration
 */
#include "HostBridge.hpp"
#include "../../../gui/ClipboardManager.hpp"
#include "../../../host/hotkey/HotkeyService.hpp"
#include "../../../host/io/IOService.hpp"
#include "../../../host/process/ProcessService.hpp"
#include "../../../host/screenshot/ScreenshotService.hpp"
#include "../../../host/window/WindowService.hpp"
#include <QClipboard>

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

  // Register basic host functions
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

std::shared_ptr<HostBridge> createHostBridge(const havel::HostContext &ctx) {
  return std::make_shared<HostBridge>(ctx);
}

} // namespace havel::compiler
