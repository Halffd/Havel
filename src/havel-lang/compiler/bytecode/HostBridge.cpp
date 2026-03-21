#include "HostBridge.hpp"

#include "host/ServiceRegistry.hpp"
#include "host/io/IOService.hpp"
#include "host/hotkey/HotkeyService.hpp"
#include "host/window/WindowService.hpp"
#include "host/mode/ModeService.hpp"
#include "host/process/ProcessService.hpp"
#include "host/clipboard/ClipboardService.hpp"
#include "host/audio/AudioService.hpp"
#include "host/brightness/BrightnessService.hpp"
#include "host/screenshot/ScreenshotService.hpp"
#include "window/WindowQuery.hpp"  // For WindowInfo
#include "core/ModeManager.hpp"  // TEMPORARY for mode.define/mode.tick

#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace havel::compiler {

// Helper: Get service from registry with fallback to legacy
template<typename ServiceT, typename LegacyGetter>
auto getService(const HostBridgeDependencies& deps, LegacyGetter legacyGetter) {
    if (deps.services) {
        auto service = deps.services->get<ServiceT>();
        if (service) return service;
    }
    return legacyGetter();
}


namespace {
std::string requireStringArg(const std::vector<BytecodeValue> &args, size_t index,
                             const std::string &fn_name) {
  if (index >= args.size() || !std::holds_alternative<std::string>(args[index])) {
    throw std::runtime_error(fn_name + " expects string argument at index " +
                             std::to_string(index));
  }
  return std::get<std::string>(args[index]);
}

int64_t requireIntArg(const std::vector<BytecodeValue> &args, size_t index,
                      const std::string &fn_name) {
  if (index >= args.size()) {
    throw std::runtime_error(fn_name + " missing integer argument at index " +
                             std::to_string(index));
  }
  if (std::holds_alternative<int64_t>(args[index])) {
    return std::get<int64_t>(args[index]);
  }
  if (std::holds_alternative<double>(args[index])) {
    return static_cast<int64_t>(std::get<double>(args[index]));
  }
  throw std::runtime_error(fn_name + " expects integer argument at index " +
                           std::to_string(index));
}

bool requireBoolArg(const std::vector<BytecodeValue> &args, size_t index,
                    const std::string &fn_name, bool default_value = true) {
  if (index >= args.size()) {
    return default_value;
  }
  if (std::holds_alternative<bool>(args[index])) {
    return std::get<bool>(args[index]);
  }
  if (std::holds_alternative<int64_t>(args[index])) {
    return std::get<int64_t>(args[index]) != 0;
  }
  throw std::runtime_error(fn_name + " expects boolean argument at index " +
                           std::to_string(index));
}
} // namespace

HostBridgeRegistry::HostBridgeRegistry(VM &vm, HostBridgeDependencies deps)
    : vm_(vm), deps_(std::move(deps)) {}

HostBridgeRegistry::~HostBridgeRegistry() {
  clear();
}

std::shared_ptr<HostBridgeRegistry>
createHostBridgeRegistry(VM &vm, HostBridgeDependencies deps) {
  return std::make_shared<HostBridgeRegistry>(vm, std::move(deps));
}

void HostBridgeRegistry::install(PipelineOptions &options) {
  const auto self = shared_from_this();

  options.host_functions["window.moveToNextMonitor"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleWindowMoveToNextMonitor(args);
      };
  options.host_functions["window.getActive"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleWindowGetActive(args);
      };
  options.host_functions["window.moveToMonitor"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleWindowMoveToMonitor(args);
      };
  options.host_functions["window.close"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleWindowClose(args);
      };
  options.host_functions["window.resize"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleWindowResize(args);
      };
  options.host_functions["window.on"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleWindowOn(args);
      };

  options.host_functions["send"] = [self](const std::vector<BytecodeValue> &args) {
    return self->handleSend(args);
  };
  options.host_functions["io.Send"] = [self](const std::vector<BytecodeValue> &args) {
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

  options.host_functions["hotkey.register"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleHotkeyRegister(args);
      };
  options.host_functions["hotkey.trigger"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleHotkeyTrigger(args);
      };

  options.host_functions["mode.define"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleModeDefine(args);
      };
  options.host_functions["mode.set"] = [self](const std::vector<BytecodeValue> &args) {
    return self->handleModeSet(args);
  };
  options.host_functions["mode.tick"] = [self](const std::vector<BytecodeValue> &args) {
    return self->handleModeTick(args);
  };

  options.host_functions["process.find"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleProcessFind(args);
      };

  options.host_functions["screenshot.full"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleScreenshotFull(args);
      };
  options.host_functions["screenshot.monitor"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleScreenshotMonitor(args);
      };
  options.host_functions["screenshot.window"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleScreenshotWindow(args);
      };
  options.host_functions["screenshot.region"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleScreenshotRegion(args);
      };

  options.vm_setup = [self](VM &vm) {
    auto registerObject = [&vm](const std::string &name,
                                const std::vector<std::pair<std::string, std::string>>
                                    &methods) {
      auto object = vm.createHostObject();
      for (const auto &[prop, host_name] : methods) {
        vm.setHostObjectField(object, prop, HostFunctionRef{.name = host_name});
      }
      vm.setGlobal(name, object);
    };

    registerObject("window", {{"moveToNextMonitor", "window.moveToNextMonitor"},
                              {"getActive", "window.getActive"},
                              {"moveToMonitor", "window.moveToMonitor"},
                              {"close", "window.close"},
                              {"resize", "window.resize"},
                              {"on", "window.on"}});
    registerObject("io", {{"Send", "io.Send"},
                          {"sendKey", "io.sendKey"},
                          {"mouseMove", "io.mouseMove"},
                          {"mouseClick", "io.mouseClick"},
                          {"getMousePosition", "io.getMousePosition"}});
    registerObject("system", {{"gc", "system.gc"}, {"gcStats", "system.gcStats"}});
    registerObject("hotkey", {{"register", "hotkey.register"},
                              {"trigger", "hotkey.trigger"}});
    registerObject("mode", {{"define", "mode.define"},
                            {"set", "mode.set"},
                            {"tick", "mode.tick"}});
    registerObject("process", {{"find", "process.find"}});
  };
}

void HostBridgeRegistry::clear() {
  if (!deps_.services) return;
  auto hotkeyService = deps_.services->get<host::HotkeyService>();
  if (hotkeyService) {
    for (const auto &[id, _] : hotkey_binding_keys_) {
      hotkeyService->removeHotkey(static_cast<int>(id));
      releaseCallback(id);
    }
  }
  hotkey_binding_keys_.clear();
  
  // Release mode callbacks
  for (auto& [mode_name, binding] : mode_bindings_) {
    if (binding.condition_id) releaseCallback(*binding.condition_id);
    if (binding.enter_id) releaseCallback(*binding.enter_id);
    if (binding.exit_id) releaseCallback(*binding.exit_id);
  }
  mode_bindings_.clear();
  mode_definition_order_.clear();
}

CallbackId HostBridgeRegistry::registerCallback(const BytecodeValue &closure) {
  return vm_.registerCallback(closure);
}

BytecodeValue HostBridgeRegistry::invokeCallback(CallbackId id, std::span<BytecodeValue> args) {
  return vm_.invokeCallback(id, args);
}

void HostBridgeRegistry::releaseCallback(CallbackId id) {
  vm_.releaseCallback(id);
}

BytecodeValue HostBridgeRegistry::handleWindowMoveToNextMonitor(
    const std::vector<BytecodeValue> &args) {
  if (!args.empty()) {
    throw std::runtime_error("window.moveToNextMonitor expects 0 arguments");
  }
  auto windowService = deps_.services->get<host::WindowService>();
  if (!windowService) {
    throw std::runtime_error("WindowService not registered");
  }
  windowService->moveActiveWindowToNextMonitor();
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridgeRegistry::handleWindowGetActive(
    const std::vector<BytecodeValue> &args) {
  if (!args.empty()) {
    throw std::runtime_error("window.getActive expects 0 arguments");
  }
  auto windowService = deps_.services->get<host::WindowService>();
  if (!windowService) {
    throw std::runtime_error("WindowService not registered");
  }
  
  auto info = windowService->getActiveWindowInfo();
  if (!info.valid) {
    return BytecodeValue(nullptr);
  }
  
  auto object = vm_.createHostObject();
  vm_.setHostObjectField(object, "id", static_cast<int64_t>(info.id));
  vm_.setHostObjectField(object, "title", info.title);
  vm_.setHostObjectField(object, "class", info.windowClass);
  vm_.setHostObjectField(object, "pid", static_cast<int64_t>(info.pid));
  vm_.setHostObjectField(object, "exe", info.exe);
  vm_.setHostObjectField(object, "x", static_cast<int64_t>(info.x));
  vm_.setHostObjectField(object, "y", static_cast<int64_t>(info.y));
  vm_.setHostObjectField(object, "width", static_cast<int64_t>(info.width));
  vm_.setHostObjectField(object, "height", static_cast<int64_t>(info.height));
  return BytecodeValue(object);
}

BytecodeValue HostBridgeRegistry::handleWindowMoveToMonitor(
    const std::vector<BytecodeValue> &args) {
  const auto id = static_cast<uint64_t>(
      requireIntArg(args, 0, "window.moveToMonitor"));
  const auto monitor =
      static_cast<int>(requireIntArg(args, 1, "window.moveToMonitor"));
  auto windowService = deps_.services->get<host::WindowService>();
  if (!windowService) {
    throw std::runtime_error("WindowService not registered");
  }
  return BytecodeValue(windowService->moveWindowToMonitor(id, monitor));
}

BytecodeValue HostBridgeRegistry::handleWindowClose(
    const std::vector<BytecodeValue> &args) {
  const auto id = static_cast<uint64_t>(requireIntArg(args, 0, "window.close"));
  auto windowService = deps_.services->get<host::WindowService>();
  if (!windowService) {
    throw std::runtime_error("WindowService not registered");
  }
  return BytecodeValue(windowService->closeWindow(id));
}

BytecodeValue HostBridgeRegistry::handleWindowResize(
    const std::vector<BytecodeValue> &args) {
  const auto id = static_cast<uint64_t>(requireIntArg(args, 0, "window.resize"));
  const auto width = static_cast<int>(requireIntArg(args, 1, "window.resize"));
  const auto height = static_cast<int>(requireIntArg(args, 2, "window.resize"));
  auto windowService = deps_.services->get<host::WindowService>();
  if (!windowService) {
    throw std::runtime_error("WindowService not registered");
  }
  return BytecodeValue(windowService->resizeWindow(id, width, height));
}

BytecodeValue HostBridgeRegistry::handleWindowOn(
    const std::vector<BytecodeValue> &args) {
  (void)requireStringArg(args, 0, "window.on");
  if (args.size() < 2) {
    throw std::runtime_error("window.on expects callback as second argument");
  }
  // Register callback through VM (VM owns the closure)
  CallbackId id = registerCallback(args[1]);
  // Event source wiring is host-specific; callback lifetime is GC-safe now.
  return BytecodeValue(static_cast<int64_t>(id));
}

BytecodeValue HostBridgeRegistry::handleSend(
    const std::vector<BytecodeValue> &args) {
  const auto text = requireStringArg(args, 0, "send");
  auto ioService = deps_.services->get<host::IOService>();
  if (!ioService) {
    throw std::runtime_error("IOService not registered");
  }
  ioService->sendKeys(text);
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridgeRegistry::handleSendKey(
    const std::vector<BytecodeValue> &args) {
  const auto key = requireStringArg(args, 0, "io.sendKey");
  const bool press = requireBoolArg(args, 1, "io.sendKey", true);
  
  auto ioService = deps_.services->get<host::IOService>();
  if (!ioService) {
    throw std::runtime_error("IOService not registered");
  }
  
  if (press) {
    ioService->keyDown(key);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ioService->keyUp(key);
  } else {
    ioService->keyUp(key);
  }
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridgeRegistry::handleMouseMove(
    const std::vector<BytecodeValue> &args) {
  const auto x = static_cast<int>(requireIntArg(args, 0, "io.mouseMove"));
  const auto y = static_cast<int>(requireIntArg(args, 1, "io.mouseMove"));
  
  auto ioService = deps_.services->get<host::IOService>();
  if (!ioService) {
    throw std::runtime_error("IOService not registered");
  }
  return BytecodeValue(ioService->mouseMoveTo(x, y));
}

BytecodeValue HostBridgeRegistry::handleMouseClick(
    const std::vector<BytecodeValue> &args) {
  const auto button = static_cast<int>(requireIntArg(args, 0, "io.mouseClick"));
  
  auto ioService = deps_.services->get<host::IOService>();
  if (!ioService) {
    throw std::runtime_error("IOService not registered");
  }
  ioService->mouseClick(button);
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridgeRegistry::handleGetMousePosition(
    const std::vector<BytecodeValue> &args) {
  if (!args.empty()) {
    throw std::runtime_error("io.getMousePosition expects 0 arguments");
  }
  
  auto ioService = deps_.services->get<host::IOService>();
  if (!ioService) {
    throw std::runtime_error("IOService not registered");
  }
  
  auto pos = ioService->getMousePosition();
  auto object = vm_.createHostObject();
  vm_.setHostObjectField(object, "x", static_cast<int64_t>(pos.first));
  vm_.setHostObjectField(object, "y", static_cast<int64_t>(pos.second));
  return BytecodeValue(object);
}

BytecodeValue HostBridgeRegistry::handleHotkeyRegister(
    const std::vector<BytecodeValue> &args) {
  const auto key = requireStringArg(args, 0, "hotkey.register");
  if (args.size() < 2) {
    throw std::runtime_error("hotkey.register expects callback as second argument");
  }

  auto hotkeyService = deps_.services->get<host::HotkeyService>();
  if (!hotkeyService) {
    throw std::runtime_error("HotkeyService not registered");
  }

  // Register callback through VM (VM owns the closure)
  CallbackId callbackId = registerCallback(args[1]);
  hotkey_binding_keys_[callbackId] = key;
  
  hotkeyService->registerHotkey(
      key,
      [weak_self = weak_from_this(), callbackId]() {
        if (auto self = weak_self.lock()) {
          try {
            self->invokeCallback(callbackId);
          } catch (const std::exception &e) {
            std::cerr << "[HostBridge][hotkey] callback failed: " << e.what()
                      << std::endl;
          }
        }
      },
      static_cast<int>(callbackId));
  
  return BytecodeValue(static_cast<int64_t>(callbackId));
}

BytecodeValue HostBridgeRegistry::handleHotkeyTrigger(
    const std::vector<BytecodeValue> &args) {
  const auto id = static_cast<CallbackId>(requireIntArg(args, 0, "hotkey.trigger"));
  invokeCallback(id);
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridgeRegistry::handleModeDefine(
    const std::vector<BytecodeValue> &args) {
  const auto mode_name = requireStringArg(args, 0, "mode.define");
  if (args.size() < 2) {
    throw std::runtime_error(
        "mode.define expects at least mode name and enter callback");
  }
  if (!deps_.mode_manager) {
    throw std::runtime_error("mode manager unavailable");
  }

  ModeBinding binding;
  size_t arg_index = 1;
  if (args.size() >= 3) {
    binding.condition_id = registerCallback(args[arg_index++]);
  }
  binding.enter_id = registerCallback(args[arg_index++]);
  if (args.size() > arg_index) {
    binding.exit_id = registerCallback(args[arg_index]);
  }

  havel::ModeManager::ModeDefinition mode;
  mode.name = mode_name;
  
  // Store callback IDs in ModeManager (not closures!)
  // ModeManager will call back through HostBridge which invokes through VM
  if (binding.enter_id) {
    auto enterId = *binding.enter_id;
    mode.onEnter = [weak_self = weak_from_this(), enterId]() {
      if (auto self = weak_self.lock()) {
        try {
          self->invokeCallback(enterId);
        } catch (const std::exception &e) {
          std::cerr << "[HostBridge][mode.enter] callback failed: " << e.what()
                    << std::endl;
        }
      }
    };
  }
  if (binding.exit_id) {
    auto exitId = *binding.exit_id;
    mode.onExit = [weak_self = weak_from_this(), exitId]() {
      if (auto self = weak_self.lock()) {
        try {
          self->invokeCallback(exitId);
        } catch (const std::exception &e) {
          std::cerr << "[HostBridge][mode.exit] callback failed: " << e.what()
                    << std::endl;
        }
      }
    };
  }

  deps_.mode_manager->defineMode(std::move(mode));
  if (mode_bindings_.find(mode_name) == mode_bindings_.end()) {
    mode_definition_order_.push_back(mode_name);
  }
  mode_bindings_[mode_name] = std::move(binding);
  return BytecodeValue(true);
}

BytecodeValue HostBridgeRegistry::handleModeSet(
    const std::vector<BytecodeValue> &args) {
  const auto mode_name = requireStringArg(args, 0, "mode.set");
  auto modeService = deps_.services->get<host::ModeService>();
  if (!modeService) {
    throw std::runtime_error("ModeService not registered");
  }
  modeService->setMode(mode_name);
  return BytecodeValue(true);
}

BytecodeValue HostBridgeRegistry::handleModeTick(
    const std::vector<BytecodeValue> &args) {
  if (!args.empty()) {
    throw std::runtime_error("mode.tick expects 0 arguments");
  }
  // Mode tick evaluates conditions and triggers mode transitions
  // This requires VM callback invocation, so it stays in HostBridge
  // ModeService handles simple mode operations only
  auto modeService = deps_.services->get<host::ModeService>();
  if (!modeService) {
    throw std::runtime_error("ModeService not registered");
  }

  for (const auto &mode_name : mode_definition_order_) {
    auto it = mode_bindings_.find(mode_name);
    if (it == mode_bindings_.end()) {
      continue;
    }
    if (!it->second.condition_id) {
      continue;
    }
    bool condition_met = false;
    try {
      BytecodeValue condition = invokeCallback(*it->second.condition_id);
      if (std::holds_alternative<bool>(condition)) {
        condition_met = std::get<bool>(condition);
      } else if (std::holds_alternative<int64_t>(condition)) {
        condition_met = std::get<int64_t>(condition) != 0;
      } else if (std::holds_alternative<double>(condition)) {
        condition_met = std::get<double>(condition) != 0.0;
      }
    } catch (const std::exception &e) {
      std::cerr << "[HostBridge][mode.tick] condition callback failed: "
                << e.what() << std::endl;
    }
    if (condition_met) {
      deps_.mode_manager->setMode(mode_name);
      return BytecodeValue(mode_name);
    }
  }

  return BytecodeValue(nullptr);
}

BytecodeValue HostBridgeRegistry::handleProcessFind(
    const std::vector<BytecodeValue> &args) {
  const auto name = requireStringArg(args, 0, "process.find");
  
  auto processService = deps_.services->get<host::ProcessService>();
  if (!processService) {
    throw std::runtime_error("ProcessService not registered");
  }
  
  auto matches = processService->findProcesses(name);

  const auto result = vm_.createHostArray();
  for (const auto &pid : matches) {
    vm_.pushHostArrayValue(result, static_cast<int64_t>(pid));
  }
  return BytecodeValue(result);
}

BytecodeValue HostBridgeRegistry::handleClipboardGet(
    const std::vector<BytecodeValue> &args) {
  (void)args;
  auto clipboardService = deps_.services->get<host::ClipboardService>();
  if (!clipboardService) {
    throw std::runtime_error("ClipboardService not registered");
  }
  return BytecodeValue(clipboardService->getText());
}

BytecodeValue HostBridgeRegistry::handleClipboardSet(
    const std::vector<BytecodeValue> &args) {
  const auto text = requireStringArg(args, 0, "clipboard.set");
  auto clipboardService = deps_.services->get<host::ClipboardService>();
  if (!clipboardService) {
    throw std::runtime_error("ClipboardService not registered");
  }
  return BytecodeValue(clipboardService->setText(text));
}

BytecodeValue HostBridgeRegistry::handleClipboardClear(
    const std::vector<BytecodeValue> &args) {
  (void)args;
  auto clipboardService = deps_.services->get<host::ClipboardService>();
  if (!clipboardService) {
    throw std::runtime_error("ClipboardService not registered");
  }
  return BytecodeValue(clipboardService->clear());
}

// ============================================================================
// Screenshot handlers - translate QImage → VMImage (GC-managed)
// ============================================================================

BytecodeValue HostBridgeRegistry::handleScreenshotFull(
    const std::vector<BytecodeValue> &args) {
  (void)args;
  auto screenshotService = deps_.services->get<host::ScreenshotService>();
  if (!screenshotService) {
    throw std::runtime_error("ScreenshotService not registered");
  }

  auto data = screenshotService->captureFullDesktop();
  if (data.empty()) {
    return BytecodeValue(nullptr);
  }

  // Create GC-managed VMImage (assume RGBA from ScreenshotService)
  // For now, use placeholder dimensions - ScreenshotService should return metadata
  int width = 1920;  // TODO: Get from service
  int height = 1080;
  auto vmImage = vm_.createImageFromRGBA(width, height, data);
  
  // Return as object with metadata
  auto imgObj = vm_.createHostObject();
  vm_.setHostObjectField(imgObj, "width", static_cast<int64_t>(width));
  vm_.setHostObjectField(imgObj, "height", static_cast<int64_t>(height));
  vm_.setHostObjectField(imgObj, "format", static_cast<int64_t>(0));  // RGBA8
  
  return BytecodeValue(imgObj);
}

BytecodeValue HostBridgeRegistry::handleScreenshotMonitor(
    const std::vector<BytecodeValue> &args) {
  int monitorIndex = 0;
  if (!args.empty()) {
    monitorIndex = static_cast<int>(requireIntArg(args, 0, "screenshot.monitor"));
  }

  auto screenshotService = deps_.services->get<host::ScreenshotService>();
  if (!screenshotService) {
    throw std::runtime_error("ScreenshotService not registered");
  }

  auto data = screenshotService->captureMonitor(monitorIndex);
  if (data.empty()) {
    return BytecodeValue(nullptr);
  }

  // Create GC-managed VMImage
  int width = 1920;  // TODO: Get from service
  int height = 1080;
  auto vmImage = vm_.createImageFromRGBA(width, height, data);
  
  auto imgObj = vm_.createHostObject();
  vm_.setHostObjectField(imgObj, "width", static_cast<int64_t>(width));
  vm_.setHostObjectField(imgObj, "height", static_cast<int64_t>(height));
  vm_.setHostObjectField(imgObj, "format", static_cast<int64_t>(0));  // RGBA8
  
  return BytecodeValue(imgObj);
}

BytecodeValue HostBridgeRegistry::handleScreenshotWindow(
    const std::vector<BytecodeValue> &args) {
  (void)args;
  auto screenshotService = deps_.services->get<host::ScreenshotService>();
  if (!screenshotService) {
    throw std::runtime_error("ScreenshotService not registered");
  }

  auto data = screenshotService->captureActiveWindow();
  if (data.empty()) {
    return BytecodeValue(nullptr);
  }

  // Create GC-managed VMImage
  int width = 800;  // TODO: Get from service
  int height = 600;
  auto vmImage = vm_.createImageFromRGBA(width, height, data);
  
  auto imgObj = vm_.createHostObject();
  vm_.setHostObjectField(imgObj, "width", static_cast<int64_t>(width));
  vm_.setHostObjectField(imgObj, "height", static_cast<int64_t>(height));
  vm_.setHostObjectField(imgObj, "format", static_cast<int64_t>(0));  // RGBA8
  
  return BytecodeValue(imgObj);
}

BytecodeValue HostBridgeRegistry::handleScreenshotRegion(
    const std::vector<BytecodeValue> &args) {
  if (args.size() < 4) {
    throw std::runtime_error("screenshot.region requires (x, y, width, height)");
  }

  int x = static_cast<int>(requireIntArg(args, 0, "screenshot.region"));
  int y = static_cast<int>(requireIntArg(args, 1, "screenshot.region"));
  int width = static_cast<int>(requireIntArg(args, 2, "screenshot.region"));
  int height = static_cast<int>(requireIntArg(args, 3, "screenshot.region"));

  auto screenshotService = deps_.services->get<host::ScreenshotService>();
  if (!screenshotService) {
    throw std::runtime_error("ScreenshotService not registered");
  }

  auto data = screenshotService->captureRegion(x, y, width, height);
  if (data.empty()) {
    return BytecodeValue(nullptr);
  }

  // Create GC-managed VMImage with actual dimensions
  auto vmImage = vm_.createImageFromRGBA(width, height, data);
  
  auto imgObj = vm_.createHostObject();
  vm_.setHostObjectField(imgObj, "width", static_cast<int64_t>(width));
  vm_.setHostObjectField(imgObj, "height", static_cast<int64_t>(height));
  vm_.setHostObjectField(imgObj, "format", static_cast<int64_t>(0));  // RGBA8
  
  return BytecodeValue(imgObj);
}

} // namespace havel::compiler
