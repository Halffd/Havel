#include "HostBridge.hpp"

#include <thread>  // for std::this_thread::sleep_for

#include "../../../core/io/MouseController.hpp"  // For ParseMouseButton, ParseDuration
#include "../../../core/io/IO.hpp"
#include "../../../core/io/EventListener.hpp"
#include "../../../host/io/IOService.hpp"
#include "../../../host/hotkey/HotkeyService.hpp"
#include "../../../host/window/WindowService.hpp"
#include "../../../host/mode/ModeService.hpp"
#include "../../../host/process/ProcessService.hpp"
#include "../../../host/clipboard/ClipboardService.hpp"
#include "../../../host/audio/AudioService.hpp"
#include "../../../host/brightness/BrightnessService.hpp"
#include "../../../host/screenshot/ScreenshotService.hpp"
#include "../../../window/WindowQuery.hpp"  // For WindowInfo
#include "../../../core/ModeManager.hpp"  // TEMPORARY for mode.define/mode.tick

#include "../../stdlib/MathModule.hpp"
#include "../../stdlib/StringModule.hpp"
#include "../../stdlib/TypeModule.hpp"
#include "../../stdlib/UtilityModule.hpp"
#include "../../stdlib/ArrayModule.hpp"

#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace havel::compiler {

HostBridge::HostBridge(VM &vm, HostContext ctx)
    : vm_(vm), ctx_(std::move(ctx)) {
  if (!ctx_.isValid()) {
    throw std::runtime_error("HostBridge: HostContext is invalid (io is null)");
  }
}

HostBridge::~HostBridge() {
  shutdown();
}

void HostBridge::shutdown() {
  clear();
  options_.host_functions.clear();
  vm_setup_callbacks_.clear();
  vm_setup_callbacks_.shrink_to_fit();
  modules_.clear();
}

void HostBridge::clear() {
  // Release hotkey callbacks
  if (ctx_.hotkeyManager) {
    for (const auto &[id, _] : hotkey_binding_keys_) {
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

void HostBridge::registerModule(const HostModule& module) {
  modules_.push_back(module);
  
  // Register module functions in VM
  for (const auto& [name, fn] : module.functions) {
    std::string fullName = module.name + "." + name;
    options_.host_functions.emplace(fullName, fn);
    options_.host_global_names.insert(module.name);
  }
}

void HostBridge::addVmSetup(std::function<void(VM&)> setupFn) {
  vm_setup_callbacks_.push_back(std::move(setupFn));
}

void HostBridge::install() {
  auto self = shared_from_this();
  auto& options = options_;

  // Reserve space to reduce rehashing
  options.host_functions.reserve(64);
  vm_setup_callbacks_.reserve(16);

  // Register stdlib modules with VM (VM-native, no Environment dependency)
  registerMathModuleVM(*this);
  registerStringModuleVM(*this);
  registerTypeModuleVM(*this);
  registerUtilityModuleVM(*this);
  registerArrayModuleVM(*this);

  // Register host functions through HostBridge
  // These use injected services from HostContext
  
  // Window functions
  options.host_functions.emplace("window.moveToNextMonitor",
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleWindowMoveToNextMonitor(args);
      });
  options.host_functions.emplace("window.getActive",
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleWindowGetActive(args);
      });
  
  // IO functions
  options.host_functions.emplace("send", [self](const std::vector<BytecodeValue> &args) {
    return self->handleSend(args);
  });
  options.host_functions.emplace("io.Send", [self](const std::vector<BytecodeValue> &args) {
    return self->handleSend(args);
  });
  options.host_functions.emplace("io.sendKey",
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleSendKey(args);
      });
  options.host_functions.emplace("io.mouseMove",
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleMouseMove(args);
      });
  options.host_functions.emplace("io.mouseClick",
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleMouseClick(args);
      });
  options.host_functions.emplace("io.getMousePosition",
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleGetMousePosition(args);
      });

  // Register registered modules
  for (const auto& module : modules_) {
    for (const auto& [name, fn] : module.functions) {
      std::string fullName = module.name + "." + name;
      options.host_functions.emplace(fullName, fn);
      options.host_global_names.insert(module.name);
    }
  }

  // Run vm_setup callbacks
  for (auto& setupFn : vm_setup_callbacks_) {
    setupFn(vm_);
  }
}

// Helper to get required service
template<typename T>
static std::shared_ptr<T> requireService(HostContext& ctx, const std::string& name) {
  // For now, services are accessed directly from context
  // This is a simplified implementation - full version would use capabilities
  return nullptr;
}

// Stub implementations - full implementation would use injected services
BytecodeValue HostBridge::handleSend(const std::vector<BytecodeValue> &args) {
  if (args.empty()) {
    throw std::runtime_error("send expects at least 1 argument");
  }
  
  // Use injected IO from context
  if (!ctx_.io) {
    throw std::runtime_error("IO not available");
  }
  
  // Implementation would call ctx_.io->Send(...)
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridge::handleSendKey(const std::vector<BytecodeValue> &args) {
  if (args.empty()) {
    throw std::runtime_error("sendKey expects at least 1 argument");
  }
  
  if (!ctx_.io) {
    throw std::runtime_error("IO not available");
  }
  
  // Implementation would call ctx_.io->SendKey(...)
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridge::handleMouseMove(const std::vector<BytecodeValue> &args) {
  if (args.size() < 2) {
    throw std::runtime_error("mouseMove expects x, y");
  }
  
  if (!ctx_.io) {
    throw std::runtime_error("IO not available");
  }
  
  // Implementation would call ctx_.io->MouseMove(...)
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridge::handleMouseClick(const std::vector<BytecodeValue> &args) {
  int button = 1;
  if (!args.empty()) {
    if (std::holds_alternative<int64_t>(args[0])) {
      button = static_cast<int>(std::get<int64_t>(args[0]));
    } else if (std::holds_alternative<std::string>(args[0])) {
      auto parsed = ParseMouseButton(std::get<std::string>(args[0]));
      if (!parsed) {
        throw std::runtime_error("mouseClick: invalid button");
      }
      button = *parsed;
    } else {
      throw std::runtime_error("mouseClick: expects string or int");
    }
  }

  if (!ctx_.io) {
    throw std::runtime_error("IO not available");
  }
  
  // Implementation would call ctx_.io->MouseClick(...)
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridge::handleGetMousePosition(const std::vector<BytecodeValue> &args) {
  if (!args.empty()) {
    throw std::runtime_error("getMousePosition expects 0 arguments");
  }

  if (!ctx_.io) {
    throw std::runtime_error("IO not available");
  }
  
  // Implementation would call ctx_.io->GetMousePosition()
  auto object = vm_.createImageFromRGBA(0, 0, nullptr);  // Placeholder
  return BytecodeValue(object);
}

// Stub implementations for other handlers
BytecodeValue HostBridge::handleWindowMoveToNextMonitor(const std::vector<BytecodeValue> &args) {
  if (!ctx_.windowManager) {
    throw std::runtime_error("WindowManager not available");
  }
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridge::handleWindowGetActive(const std::vector<BytecodeValue> &args) {
  if (!ctx_.windowManager) {
    throw std::runtime_error("WindowManager not available");
  }
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridge::handleWindowMoveToMonitor(const std::vector<BytecodeValue> &args) {
  if (!ctx_.windowManager) {
    throw std::runtime_error("WindowManager not available");
  }
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridge::handleWindowClose(const std::vector<BytecodeValue> &args) {
  if (!ctx_.windowManager) {
    throw std::runtime_error("WindowManager not available");
  }
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridge::handleWindowResize(const std::vector<BytecodeValue> &args) {
  if (!ctx_.windowManager) {
    throw std::runtime_error("WindowManager not available");
  }
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridge::handleWindowOn(const std::vector<BytecodeValue> &args) {
  if (!ctx_.windowManager) {
    throw std::runtime_error("WindowManager not available");
  }
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridge::handleHotkeyRegister(const std::vector<BytecodeValue> &args) {
  if (!ctx_.hotkeyManager) {
    throw std::runtime_error("HotkeyManager not available");
  }
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridge::handleHotkeyTrigger(const std::vector<BytecodeValue> &args) {
  if (!ctx_.hotkeyManager) {
    throw std::runtime_error("HotkeyManager not available");
  }
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridge::handleModeDefine(const std::vector<BytecodeValue> &args) {
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridge::handleModeSet(const std::vector<BytecodeValue> &args) {
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridge::handleModeTick(const std::vector<BytecodeValue> &args) {
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridge::handleProcessFind(const std::vector<BytecodeValue> &args) {
  if (!ctx_.processManager) {
    throw std::runtime_error("ProcessManager not available");
  }
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridge::handleClipboardGet(const std::vector<BytecodeValue> &args) {
  if (!ctx_.clipboardManager) {
    throw std::runtime_error("ClipboardManager not available");
  }
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridge::handleClipboardSet(const std::vector<BytecodeValue> &args) {
  if (!ctx_.clipboardManager) {
    throw std::runtime_error("ClipboardManager not available");
  }
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridge::handleClipboardClear(const std::vector<BytecodeValue> &args) {
  if (!ctx_.clipboardManager) {
    throw std::runtime_error("ClipboardManager not available");
  }
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridge::handleScreenshotFull(const std::vector<BytecodeValue> &args) {
  if (!ctx_.screenshotManager) {
    throw std::runtime_error("ScreenshotManager not available");
  }
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridge::handleScreenshotMonitor(const std::vector<BytecodeValue> &args) {
  if (!ctx_.screenshotManager) {
    throw std::runtime_error("ScreenshotManager not available");
  }
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridge::handleScreenshotWindow(const std::vector<BytecodeValue> &args) {
  if (!ctx_.screenshotManager) {
    throw std::runtime_error("ScreenshotManager not available");
  }
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridge::handleScreenshotRegion(const std::vector<BytecodeValue> &args) {
  if (!ctx_.screenshotManager) {
    throw std::runtime_error("ScreenshotManager not available");
  }
  return BytecodeValue(nullptr);
}

CallbackId HostBridge::registerCallback(const BytecodeValue &closure) {
  return vm_.registerCallback(closure);
}

BytecodeValue HostBridge::invokeCallback(CallbackId id, std::span<BytecodeValue> args) {
  return vm_.invokeCallback(id, args);
}

void HostBridge::releaseCallback(CallbackId id) {
  vm_.releaseCallback(id);
}

std::shared_ptr<HostBridge> createHostBridge(VM& vm, HostContext ctx) {
  return std::make_shared<HostBridge>(vm, std::move(ctx));
}

} // namespace havel::compiler
