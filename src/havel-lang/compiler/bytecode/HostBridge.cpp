#include "HostBridge.hpp"

#include "host/io/IOService.hpp"
#include "host/hotkey/HotkeyService.hpp"
#include "host/window/WindowService.hpp"
#include "host/mode/ModeService.hpp"
#include "host/process/ProcessService.hpp"
#include "core/HotkeyManager.hpp"
#include "core/IO.hpp"
#include "core/ModeManager.hpp"
#include "core/process/ProcessManager.hpp"
#include "window/WindowManager.hpp"

#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace havel::compiler {

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
  if (deps_.hotkey_manager) {
    for (const auto &[id, _] : hotkey_callback_roots_) {
      deps_.hotkey_manager->RemoveHotkey(static_cast<int>(id));
    }
  }
  hotkey_callback_roots_.clear();
  hotkey_binding_keys_.clear();
  mode_callback_roots_.clear();
  mode_bindings_.clear();
  mode_definition_order_.clear();
  window_event_callback_roots_.clear();
}

int64_t HostBridgeRegistry::pinLongLivedCallback(const BytecodeValue &value,
                                                 RootTable &table) {
  const int64_t id = next_callback_id_++;
  table.emplace(id, vm_.makeRoot(value));
  return id;
}

void HostBridgeRegistry::unpinLongLivedCallback(int64_t id, RootTable &table) {
  table.erase(id);
}

BytecodeValue HostBridgeRegistry::invokePinnedCallback(
    RootTable &table, int64_t id, const std::vector<BytecodeValue> &args) {
  auto it = table.find(id);
  if (it == table.end()) {
    throw std::runtime_error("callback id not found");
  }
  auto value = it->second.get();
  if (!value.has_value()) {
    throw std::runtime_error("callback root expired");
  }
  return vm_.call(*value, args);
}

BytecodeValue HostBridgeRegistry::handleWindowMoveToNextMonitor(
    const std::vector<BytecodeValue> &args) {
  if (!args.empty()) {
    throw std::runtime_error("window.moveToNextMonitor expects 0 arguments");
  }
  if (deps_.window_service) {
    deps_.window_service->moveActiveWindowToNextMonitor();
  } else if (deps_.window_manager) {
    havel::WindowManager::MoveWindowToNextMonitor();
  } else {
    throw std::runtime_error("window manager unavailable");
  }
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridgeRegistry::handleWindowGetActive(
    const std::vector<BytecodeValue> &args) {
  if (!args.empty()) {
    throw std::runtime_error("window.getActive expects 0 arguments");
  }
  havel::WindowInfo info;
  if (deps_.window_service) {
    info = deps_.window_service->getActiveWindowInfo();
  } else if (deps_.window_manager) {
    info = havel::WindowManager::getActiveWindowInfo();
  } else {
    throw std::runtime_error("window manager unavailable");
  }
  
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
  if (deps_.window_service) {
    return BytecodeValue(deps_.window_service->moveWindowToMonitor(id, monitor));
  } else if (deps_.window_manager) {
    return BytecodeValue(havel::WindowManager::moveWindowToMonitor(id, monitor));
  } else {
    throw std::runtime_error("window manager unavailable");
  }
}

BytecodeValue HostBridgeRegistry::handleWindowClose(
    const std::vector<BytecodeValue> &args) {
  const auto id = static_cast<uint64_t>(requireIntArg(args, 0, "window.close"));
  if (deps_.window_service) {
    return BytecodeValue(deps_.window_service->closeWindow(id));
  } else if (deps_.window_manager) {
    return BytecodeValue(havel::WindowManager::closeWindow(id));
  } else {
    throw std::runtime_error("window manager unavailable");
  }
}

BytecodeValue HostBridgeRegistry::handleWindowResize(
    const std::vector<BytecodeValue> &args) {
  const auto id = static_cast<uint64_t>(requireIntArg(args, 0, "window.resize"));
  const auto width = static_cast<int>(requireIntArg(args, 1, "window.resize"));
  const auto height = static_cast<int>(requireIntArg(args, 2, "window.resize"));
  if (deps_.window_service) {
    return BytecodeValue(deps_.window_service->resizeWindow(id, width, height));
  } else if (deps_.window_manager) {
    return BytecodeValue(havel::WindowManager::resizeWindow(id, width, height));
  } else {
    throw std::runtime_error("window manager unavailable");
  }
}

BytecodeValue HostBridgeRegistry::handleWindowOn(
    const std::vector<BytecodeValue> &args) {
  (void)requireStringArg(args, 0, "window.on");
  if (args.size() < 2) {
    throw std::runtime_error("window.on expects callback as second argument");
  }
  const auto id = pinLongLivedCallback(args[1], window_event_callback_roots_);
  // Event source wiring is host-specific; callback lifetime is GC-safe now.
  return BytecodeValue(id);
}

BytecodeValue HostBridgeRegistry::handleSend(
    const std::vector<BytecodeValue> &args) {
  const auto text = requireStringArg(args, 0, "send");
  if (deps_.io_service) {
    deps_.io_service->sendKeys(text);
  } else if (deps_.io) {
    deps_.io->Send(text.c_str());
  } else {
    throw std::runtime_error("io unavailable");
  }
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridgeRegistry::handleSendKey(
    const std::vector<BytecodeValue> &args) {
  const auto key = requireStringArg(args, 0, "io.sendKey");
  const bool press = requireBoolArg(args, 1, "io.sendKey", true);
  if (deps_.io_service) {
    if (press) {
      deps_.io_service->keyDown(key);
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      deps_.io_service->keyUp(key);
    } else {
      deps_.io_service->keyUp(key);
    }
  } else if (deps_.io) {
    const std::string seq =
        press ? "{" + key + " down}" : "{" + key + " up}";
    deps_.io->Send(seq.c_str());
  } else {
    throw std::runtime_error("io unavailable");
  }
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridgeRegistry::handleMouseMove(
    const std::vector<BytecodeValue> &args) {
  const auto x = static_cast<int>(requireIntArg(args, 0, "io.mouseMove"));
  const auto y = static_cast<int>(requireIntArg(args, 1, "io.mouseMove"));
  if (deps_.io_service) {
    return BytecodeValue(deps_.io_service->mouseMoveTo(x, y));
  } else if (deps_.io) {
    return BytecodeValue(deps_.io->MouseMove(x, y));
  } else {
    throw std::runtime_error("io unavailable");
  }
}

BytecodeValue HostBridgeRegistry::handleMouseClick(
    const std::vector<BytecodeValue> &args) {
  const auto button = static_cast<int>(requireIntArg(args, 0, "io.mouseClick"));
  if (deps_.io_service) {
    deps_.io_service->mouseClick(button);
  } else if (deps_.io) {
    deps_.io->MouseClick(button);
  } else {
    throw std::runtime_error("io unavailable");
  }
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridgeRegistry::handleGetMousePosition(
    const std::vector<BytecodeValue> &args) {
  if (!args.empty()) {
    throw std::runtime_error("io.getMousePosition expects 0 arguments");
  }
  std::pair<int, int> pos;
  if (deps_.io_service) {
    pos = deps_.io_service->getMousePosition();
  } else if (deps_.io) {
    pos = deps_.io->GetMousePosition();
  } else {
    throw std::runtime_error("io unavailable");
  }
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

  const int64_t id = pinLongLivedCallback(args[1], hotkey_callback_roots_);
  hotkey_binding_keys_[id] = key;
  
  bool ok = false;
  
  // Use service layer if available, otherwise fall back to core
  if (deps_.hotkey_service) {
    deps_.hotkey_service->registerHotkey(
        key,
        [weak_self = weak_from_this(), id]() {
          if (auto self = weak_self.lock()) {
            try {
              (void)self->invokePinnedCallback(self->hotkey_callback_roots_, id);
            } catch (const std::exception &e) {
              std::cerr << "[HostBridge][hotkey] callback failed: " << e.what()
                        << std::endl;
            }
          }
        },
        static_cast<int>(id));
    ok = true;  // Service doesn't return bool, assume success
  } else if (deps_.hotkey_manager) {
    ok = deps_.hotkey_manager->AddHotkey(
        key,
        [weak_self = weak_from_this(), id]() {
          if (auto self = weak_self.lock()) {
            try {
              (void)self->invokePinnedCallback(self->hotkey_callback_roots_, id);
            } catch (const std::exception &e) {
              std::cerr << "[HostBridge][hotkey] callback failed: " << e.what()
                        << std::endl;
            }
          }
        },
        static_cast<int>(id));
  } else {
    throw std::runtime_error("hotkey manager unavailable");
  }

  if (!ok) {
    unpinLongLivedCallback(id, hotkey_callback_roots_);
    hotkey_binding_keys_.erase(id);
    return BytecodeValue(false);
  }
  return BytecodeValue(static_cast<int64_t>(id));
}

BytecodeValue HostBridgeRegistry::handleHotkeyTrigger(
    const std::vector<BytecodeValue> &args) {
  const auto id = requireIntArg(args, 0, "hotkey.trigger");
  return invokePinnedCallback(hotkey_callback_roots_, id);
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
    binding.condition_id =
        pinLongLivedCallback(args[arg_index++], mode_callback_roots_);
  }
  binding.enter_id =
      pinLongLivedCallback(args[arg_index++], mode_callback_roots_);
  if (args.size() > arg_index) {
    binding.exit_id =
        pinLongLivedCallback(args[arg_index], mode_callback_roots_);
  }

  havel::ModeManager::ModeDefinition mode;
  mode.name = mode_name;
  if (binding.enter_id.has_value()) {
    const auto enter_id = *binding.enter_id;
    mode.onEnter = [weak_self = weak_from_this(), enter_id]() {
      if (auto self = weak_self.lock()) {
        try {
          (void)self->invokePinnedCallback(self->mode_callback_roots_, enter_id);
        } catch (const std::exception &e) {
          std::cerr << "[HostBridge][mode.enter] callback failed: " << e.what()
                    << std::endl;
        }
      }
    };
  }
  if (binding.exit_id.has_value()) {
    const auto exit_id = *binding.exit_id;
    mode.onExit = [weak_self = weak_from_this(), exit_id]() {
      if (auto self = weak_self.lock()) {
        try {
          (void)self->invokePinnedCallback(self->mode_callback_roots_, exit_id);
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
  if (deps_.mode_service) {
    deps_.mode_service->setMode(mode_name);
  } else if (deps_.mode_manager) {
    deps_.mode_manager->setMode(mode_name);
  } else {
    throw std::runtime_error("mode manager unavailable");
  }
  return BytecodeValue(true);
}

BytecodeValue HostBridgeRegistry::handleModeTick(
    const std::vector<BytecodeValue> &args) {
  if (!args.empty()) {
    throw std::runtime_error("mode.tick expects 0 arguments");
  }
  if (!deps_.mode_manager) {
    throw std::runtime_error("mode manager unavailable");
  }

  for (const auto &mode_name : mode_definition_order_) {
    auto it = mode_bindings_.find(mode_name);
    if (it == mode_bindings_.end()) {
      continue;
    }
    if (!it->second.condition_id.has_value()) {
      continue;
    }
    bool condition_met = false;
    try {
      BytecodeValue condition =
          invokePinnedCallback(mode_callback_roots_, *it->second.condition_id);
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
  
  std::vector<int32_t> matches;
  if (deps_.process_service) {
    matches = deps_.process_service->findProcesses(name);
  } else {
    auto coreMatches = havel::ProcessManager::findProcesses(name);
    matches.reserve(coreMatches.size());
    for (const auto& m : coreMatches) {
      matches.push_back(m.pid);
    }
  }

  const auto result = vm_.createHostArray();
  for (const auto &pid : matches) {
    vm_.pushHostArrayValue(result, static_cast<int64_t>(pid));
  }
  return BytecodeValue(result);
}

} // namespace havel::compiler
