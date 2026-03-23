/*
 * HostBridge.cpp - STUBBED for bytecode VM migration
 * Full implementation requires fixing HostContext/VM integration
 */
#include "HostBridge.hpp"

namespace havel::compiler {

HostBridge::HostBridge(const havel::HostContext& ctx)
    : ctx_(&ctx) {
  if (!ctx_->isValid()) {
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
  hotkey_binding_keys_.clear();
  mode_bindings_.clear();
  mode_definition_order_.clear();
}

void HostBridge::install() {
  auto self = shared_from_this();
  auto& options = options_;

  // Reserve space
  options.host_functions.reserve(64);
  vm_setup_callbacks_.reserve(16);

  // Register basic host functions
  options.host_functions["send"] = [self](const std::vector<BytecodeValue> &args) {
    return self->handleSend(args);
  };
  options.host_functions["io.send"] = [self](const std::vector<BytecodeValue> &args) {
    return self->handleSend(args);
  };
  options.host_functions["io.sendKey"] = [self](const std::vector<BytecodeValue> &args) {
    return self->handleSendKey(args);
  };
  options.host_functions["io.mouseMove"] = [self](const std::vector<BytecodeValue> &args) {
    return self->handleMouseMove(args);
  };
  options.host_functions["io.mouseClick"] = [self](const std::vector<BytecodeValue> &args) {
    return self->handleMouseClick(args);
  };
  options.host_functions["io.getMousePosition"] = [self](const std::vector<BytecodeValue> &args) {
    return self->handleGetMousePosition(args);
  };

  // Run vm_setup callbacks
  for (auto& setupFn : vm_setup_callbacks_) {
    setupFn(*static_cast<VM*>(ctx_->vm));
  }
}

void HostBridge::registerModule(const HostModule& module) {
  modules_.push_back(module);
  // STUBBED - module registration requires fixing HostFn/BytecodeHostFunction mismatch
}

void HostBridge::addVmSetup(std::function<void(VM&)> setupFn) {
  vm_setup_callbacks_.push_back(std::move(setupFn));
}

// Stub handler implementations
BytecodeValue HostBridge::handleSend(const std::vector<BytecodeValue> &args) {
  (void)args;
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridge::handleSendKey(const std::vector<BytecodeValue> &args) {
  (void)args;
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridge::handleMouseMove(const std::vector<BytecodeValue> &args) {
  (void)args;
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridge::handleMouseClick(const std::vector<BytecodeValue> &args) {
  (void)args;
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridge::handleGetMousePosition(const std::vector<BytecodeValue> &args) {
  (void)args;
  auto obj = static_cast<VM*>(ctx_->vm)->createHostObject();
  static_cast<VM*>(ctx_->vm)->setHostObjectField(obj, "x", BytecodeValue(static_cast<int64_t>(0)));
  static_cast<VM*>(ctx_->vm)->setHostObjectField(obj, "y", BytecodeValue(static_cast<int64_t>(0)));
  return BytecodeValue(obj);
}

BytecodeValue HostBridge::handleWindowMoveToNextMonitor(const std::vector<BytecodeValue> &args) {
  (void)args;
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridge::handleWindowGetActive(const std::vector<BytecodeValue> &args) {
  (void)args;
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridge::handleWindowMoveToMonitor(const std::vector<BytecodeValue> &args) {
  (void)args;
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridge::handleWindowClose(const std::vector<BytecodeValue> &args) {
  (void)args;
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridge::handleWindowResize(const std::vector<BytecodeValue> &args) {
  (void)args;
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridge::handleWindowOn(const std::vector<BytecodeValue> &args) {
  (void)args;
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridge::handleHotkeyRegister(const std::vector<BytecodeValue> &args) {
  (void)args;
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridge::handleHotkeyTrigger(const std::vector<BytecodeValue> &args) {
  (void)args;
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridge::handleModeDefine(const std::vector<BytecodeValue> &args) {
  (void)args;
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridge::handleModeSet(const std::vector<BytecodeValue> &args) {
  (void)args;
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridge::handleModeTick(const std::vector<BytecodeValue> &args) {
  (void)args;
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridge::handleProcessFind(const std::vector<BytecodeValue> &args) {
  (void)args;
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridge::handleClipboardGet(const std::vector<BytecodeValue> &args) {
  (void)args;
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridge::handleClipboardSet(const std::vector<BytecodeValue> &args) {
  (void)args;
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridge::handleClipboardClear(const std::vector<BytecodeValue> &args) {
  (void)args;
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridge::handleScreenshotFull(const std::vector<BytecodeValue> &args) {
  (void)args;
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridge::handleScreenshotMonitor(const std::vector<BytecodeValue> &args) {
  (void)args;
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridge::handleScreenshotWindow(const std::vector<BytecodeValue> &args) {
  (void)args;
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridge::handleScreenshotRegion(const std::vector<BytecodeValue> &args) {
  (void)args;
  return BytecodeValue(nullptr);
}

CallbackId HostBridge::registerCallback(const BytecodeValue &closure) {
  if (!ctx_->vm) {
    throw std::runtime_error("VM not available for callback registration");
  }
  return static_cast<VM*>(ctx_->vm)->registerCallback(closure);
}

BytecodeValue HostBridge::invokeCallback(CallbackId id, std::span<BytecodeValue> args) {
  if (!ctx_->vm) {
    throw std::runtime_error("VM not available for callback invocation");
  }
  return static_cast<VM*>(ctx_->vm)->invokeCallback(id, args);
}

void HostBridge::releaseCallback(CallbackId id) {
  if (!ctx_->vm) {
    throw std::runtime_error("VM not available for callback release");
  }
  static_cast<VM*>(ctx_->vm)->releaseCallback(id);
}

std::shared_ptr<HostBridge> createHostBridge(const havel::HostContext& ctx) {
  return std::make_shared<HostBridge>(ctx);
}

} // namespace havel::compiler
