/*
 * HostBridge.cpp - Composite bridge delegating to modular components
 *
 * ARCHITECTURE:
 * - HostBridge composes specialized bridge modules
 * - Each module handles a specific capability domain
 * - Execution policy is OPTIONAL (for embedding/sandboxing)
 * - Default: FULL ACCESS (no friction for normal users)
 */
#include "HostBridge.hpp"
#include "../../../host/module/ModularHostBridges.hpp"

#include "../../../host/media/MediaService.hpp"
#include "../../../host/network/NetworkService.hpp"
#include "../../../host/app/AppService.hpp"

namespace havel::compiler {

// Helper function to get type name from BytecodeValue
static std::string getTypeName(const BytecodeValue& value) {
  if (std::holds_alternative<std::nullptr_t>(value)) return "null";
  if (std::holds_alternative<bool>(value)) return "bool";
  if (std::holds_alternative<int64_t>(value)) return "int";
  if (std::holds_alternative<double>(value)) return "float";
  if (std::holds_alternative<std::string>(value)) return "string";
  if (std::holds_alternative<ArrayRef>(value)) return "array";
  if (std::holds_alternative<ObjectRef>(value)) return "object";
  return "unknown";
}

HostBridge::HostBridge(const havel::HostContext &ctx)
    : ctx_(&ctx), policy_(ExecutionPolicy::DefaultPolicy()), moduleLoader_(*ctx_) {
  extensionLoader_ = std::make_unique<ExtensionLoader>();
  initBridges();
}

HostBridge::HostBridge(const havel::HostContext &ctx,
                       const ExecutionPolicy &policy)
    : ctx_(&ctx), policy_(policy), moduleLoader_(*ctx_) {
  extensionLoader_ = std::make_unique<ExtensionLoader>();
  moduleLoader_.setExecutionPolicy(policy);
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
  networkBridge_.reset();
  appBridge_.reset();
  asyncBridge_.reset();
  automationBridge_.reset();
  browserBridge_.reset();
  toolsBridge_.reset();
  extensionLoader_.reset();
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
  networkBridge_ = std::make_unique<NetworkBridge>(ctx_);
  appBridge_ = std::make_unique<AppBridge>(ctx_);
  asyncBridge_ = std::make_unique<AsyncBridge>(ctx_);
  automationBridge_ = std::make_unique<AutomationBridge>(ctx_);
  browserBridge_ = std::make_unique<BrowserBridge>(ctx_);
  toolsBridge_ = std::make_unique<ToolsBridge>(ctx_);
}

void HostBridge::install() {
  options_.host_functions.reserve(64);
  vm_setup_callbacks_.reserve(16);

  // Install all bridge modules (policy checks happen at call time if needed)
  ioBridge_->install(options_);
  systemBridge_->install(options_);
  uiBridge_->install(options_);
  inputBridge_->install(options_);
  mediaBridge_->install(options_);
  networkBridge_->install(options_);
  appBridge_->install(options_);
  asyncBridge_->install(options_);
  automationBridge_->install(options_);
  browserBridge_->install(options_);
  toolsBridge_->install(options_);

  // Install extension loading functions
  auto self = shared_from_this();
  options_.host_functions["extension.load"] = [self](const std::vector<BytecodeValue>& args) {
    if (args.empty() || !std::holds_alternative<std::string>(args[0])) {
      return BytecodeValue(false);
    }
    std::string name = std::get<std::string>(args[0]);
    return BytecodeValue(self->extensionLoader_->loadExtensionByName(name));
  };
  options_.host_functions["extension.isLoaded"] = [self](const std::vector<BytecodeValue>& args) {
    if (args.empty() || !std::holds_alternative<std::string>(args[0])) {
      return BytecodeValue(false);
    }
    std::string name = std::get<std::string>(args[0]);
    return BytecodeValue(self->extensionLoader_->isLoaded(name));
  };
  options_.host_functions["extension.list"] = [self](const std::vector<BytecodeValue>& args) {
    (void)args;
    auto names = self->extensionLoader_->getLoadedExtensions();
    auto *vm = static_cast<VM *>(self->ctx_->vm);
    if (!vm) {
      return BytecodeValue(nullptr);
    }
    auto arr = vm->createHostArray();
    for (const auto& name : names) {
      vm->pushHostArrayValue(arr, BytecodeValue(name));
    }
    return BytecodeValue(arr);
  };
  options_.host_functions["extension.addSearchPath"] = [self](const std::vector<BytecodeValue>& args) {
    if (args.empty() || !std::holds_alternative<std::string>(args[0])) {
      return BytecodeValue(false);
    }
    std::string path = std::get<std::string>(args[0]);
    self->extensionLoader_->addSearchPath(path);
    return BytecodeValue(true);
  };

  // Register any.* dispatch methods for runtime type-based method calls
  auto registerAnyMethod = [self, &options = options_](const std::string& methodName) {
    options.host_functions["any." + methodName] = [self, methodName](const std::vector<BytecodeValue>& args) {
      if (args.empty()) {
        return BytecodeValue(nullptr);
      }
      
      // Determine type and dispatch to appropriate module
      std::string type = getTypeName(args[0]);
      std::string modulePrefix;
      if (type == "string") modulePrefix = "string";
      else if (type == "array") modulePrefix = "array";
      else if (type == "object") modulePrefix = "object";
      else return BytecodeValue(nullptr);
      
      std::string fullName = modulePrefix + "." + methodName;
      
      // Look up and call the appropriate function
      auto it = self->options_.host_functions.find(fullName);
      if (it != self->options_.host_functions.end()) {
        return it->second(args);
      }
      return BytecodeValue(nullptr);
    };
  };
  
  // Register all any.* methods
  registerAnyMethod("len");
  registerAnyMethod("trim");
  registerAnyMethod("upper");
  registerAnyMethod("lower");
  registerAnyMethod("includes");
  registerAnyMethod("startswith");
  registerAnyMethod("endswith");
  registerAnyMethod("find");
  registerAnyMethod("replace");
  registerAnyMethod("split");
  registerAnyMethod("join");
  registerAnyMethod("sub");
  registerAnyMethod("push");
  registerAnyMethod("pop");
  registerAnyMethod("get");
  registerAnyMethod("set");
  registerAnyMethod("sort");
  registerAnyMethod("filter");
  registerAnyMethod("map");
  registerAnyMethod("reduce");

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

void HostBridge::registerExtensionFunction(const std::string &fullName, BytecodeHostFunction fn) {
  options_.host_functions[fullName] = std::move(fn);
}

std::shared_ptr<HostBridge> createHostBridge(const havel::HostContext &ctx) {
  return std::make_shared<HostBridge>(ctx);
}

std::shared_ptr<HostBridge>
createHostBridge(const havel::HostContext &ctx,
                 const ExecutionPolicy &policy) {
  return std::make_shared<HostBridge>(ctx, policy);
}

bool HostBridge::import(const std::string &importSpec) {
  if (!ctx_->vm) {
    return false;
  }
  return moduleLoader_.import(importSpec, *ctx_->vm);
}

void HostBridge::loadExtension(const std::string &name) {
  if (extensionLoader_) {
    extensionLoader_->loadExtensionByName(name);
  }
}

} // namespace havel::compiler
