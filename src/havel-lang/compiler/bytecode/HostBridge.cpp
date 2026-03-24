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
  if (ioBridge_ && caps_.ioControl) {
    ioBridge_->install(options_);
  }
  if (systemBridge_) {
    if (caps_.fileIO || caps_.processExec) {
      systemBridge_->install(options_);
    }
  }
  if (uiBridge_) {
    if (caps_.windowControl || caps_.clipboardAccess || caps_.screenshotAccess) {
      uiBridge_->install(options_);
    }
  }
  if (inputBridge_) {
    if (caps_.hotkeyControl || caps_.inputRemapping || caps_.altTabControl) {
      inputBridge_->install(options_);
    }
  }
  if (mediaBridge_) {
    if (caps_.audioControl || caps_.brightnessControl) {
      mediaBridge_->install(options_);
    }
  }
  if (asyncBridge_ && caps_.asyncOps) {
    asyncBridge_->install(options_);
  }
  if (automationBridge_ && caps_.automationControl) {
    automationBridge_->install(options_);
  }
  if (browserBridge_ && caps_.browserControl) {
    browserBridge_->install(options_);
  }
  if (toolsBridge_ && caps_.textChunkerAccess) {
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

bool HostBridge::hasCapability(const std::string &name) const {
  if (name == "io") return caps_.ioControl;
  if (name == "fileIO") return caps_.fileIO;
  if (name == "processExec") return caps_.processExec;
  if (name == "windowControl") return caps_.windowControl;
  if (name == "hotkeyControl") return caps_.hotkeyControl;
  if (name == "modeControl") return caps_.modeControl;
  if (name == "clipboardAccess") return caps_.clipboardAccess;
  if (name == "screenshotAccess") return caps_.screenshotAccess;
  if (name == "asyncOps") return caps_.asyncOps;
  if (name == "audioControl") return caps_.audioControl;
  if (name == "brightnessControl") return caps_.brightnessControl;
  if (name == "automationControl") return caps_.automationControl;
  if (name == "browserControl") return caps_.browserControl;
  if (name == "textChunkerAccess") return caps_.textChunkerAccess;
  if (name == "inputRemapping") return caps_.inputRemapping;
  if (name == "altTabControl") return caps_.altTabControl;
  return false;
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
