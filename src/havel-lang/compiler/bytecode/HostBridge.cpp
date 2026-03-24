/*
 * HostBridge.cpp - Composite bridge delegating to modular components
 *
 * ARCHITECTURE:
 * - HostBridge composes specialized bridge modules
 * - Each module handles a specific capability domain
 * - Capabilities can be gated for sandboxing
 * - ModuleLoader provides lazy loading and import system
 */
#include "HostBridge.hpp"
#include "ModularHostBridges.hpp"

namespace havel::compiler {

HostBridge::HostBridge(const havel::HostContext &ctx)
    : ctx_(&ctx), caps_(HostBridgeCapabilities::Full()), moduleLoader_(*ctx_) {
  initBridges();
}

HostBridge::HostBridge(const havel::HostContext &ctx,
                       const HostBridgeCapabilities &caps)
    : ctx_(&ctx), caps_(caps), moduleLoader_(*ctx_) {
  moduleLoader_.setCapabilities(caps);
  initBridges();
}

HostBridge::~HostBridge() { shutdown(); }

void HostBridge::shutdown() {
  clear();
  options_.host_functions.clear();
  vm_setup_callbacks_.clear();
  vm_setup_callbacks_.shrink_to_fit();
  modules_.clear();
  ioBridge_.reset();
  systemBridge_.reset();
  uiBridge_.reset();
  inputBridge_.reset();
  mediaBridge_.reset();
  asyncBridge_.reset();
  automationBridge_.reset();
  browserBridge_.reset();
  toolsBridge_.reset();
}

void HostBridge::clear() {
  mode_bindings_.clear();
  mode_definition_order_.clear();
  hotkey_binding_keys_.clear();
}

void HostBridge::initBridges() {
  ioBridge_ = std::make_unique<IOBridge>(ctx_);
  systemBridge_ = std::make_unique<SystemBridge>(ctx_);
  uiBridge_ = std::make_unique<UIBridge>(ctx_);
  inputBridge_ = std::make_unique<InputBridge>(ctx_);
  mediaBridge_ = std::make_unique<MediaBridge>(ctx_);
  asyncBridge_ = std::make_unique<AsyncBridge>(ctx_);
  automationBridge_ = std::make_unique<AutomationBridge>(ctx_);
  browserBridge_ = std::make_unique<BrowserBridge>(ctx_);
  toolsBridge_ = std::make_unique<ToolsBridge>(ctx_);
}

void HostBridge::install() {
  options_.host_functions.reserve(64);
  vm_setup_callbacks_.reserve(16);

  // Install modular bridges based on capabilities
  if (ioBridge_ && caps_.has(Capability::IO)) {
    ioBridge_->install(options_);
  }
  if (systemBridge_) {
    if (caps_.has(Capability::FileIO) || caps_.has(Capability::ProcessExec)) {
      systemBridge_->install(options_);
    }
  }
  if (uiBridge_) {
    if (caps_.has(Capability::WindowControl) || caps_.has(Capability::ClipboardAccess) || caps_.has(Capability::ScreenshotAccess)) {
      uiBridge_->install(options_);
    }
  }
  if (inputBridge_) {
    if (caps_.has(Capability::HotkeyControl) || caps_.has(Capability::InputRemapping) || caps_.has(Capability::AltTabControl)) {
      inputBridge_->install(options_);
    }
  }
  if (mediaBridge_) {
    if (caps_.has(Capability::AudioControl) || caps_.has(Capability::BrightnessControl)) {
      mediaBridge_->install(options_);
    }
  }
  if (asyncBridge_ && caps_.has(Capability::AsyncOps)) {
    asyncBridge_->install(options_);
  }
  if (automationBridge_ && caps_.has(Capability::AutomationControl)) {
    automationBridge_->install(options_);
  }
  if (browserBridge_ && caps_.has(Capability::BrowserControl)) {
    browserBridge_->install(options_);
  }
  if (toolsBridge_ && caps_.has(Capability::TextChunkerAccess)) {
    toolsBridge_->install(options_);
  }

  // Run vm_setup callbacks
  for (auto &setupFn : vm_setup_callbacks_) {
    setupFn(*static_cast<VM *>(ctx_->vm));
  }
}

void HostBridge::registerModule(const HostModule &module) {
  modules_.push_back(module);
}

void HostBridge::addVmSetup(std::function<void(VM &)> setupFn) {
  vm_setup_callbacks_.push_back(std::move(setupFn));
}

std::shared_ptr<HostBridge> createHostBridge(const havel::HostContext &ctx) {
  return std::make_shared<HostBridge>(ctx);
}

std::shared_ptr<HostBridge>
createHostBridge(const havel::HostContext &ctx,
                 const HostBridgeCapabilities &caps) {
  return std::make_shared<HostBridge>(ctx, caps);
}

bool HostBridge::import(const std::string &importSpec) {
  if (!ctx_->vm) {
    return false;
  }
  return moduleLoader_.import(importSpec, *ctx_->vm);
}

} // namespace havel::compiler
