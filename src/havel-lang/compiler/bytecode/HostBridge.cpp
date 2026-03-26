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

// Helper function to compare two BytecodeValues for equality
static bool valuesEqual(const BytecodeValue& a, const BytecodeValue& b) {
  if (a.index() != b.index()) return false;
  if (std::holds_alternative<std::nullptr_t>(a)) return true;
  if (std::holds_alternative<bool>(a)) return std::get<bool>(a) == std::get<bool>(b);
  if (std::holds_alternative<int64_t>(a)) return std::get<int64_t>(a) == std::get<int64_t>(b);
  if (std::holds_alternative<double>(a)) return std::get<double>(a) == std::get<double>(b);
  if (std::holds_alternative<std::string>(a)) return std::get<std::string>(a) == std::get<std::string>(b);
  if (std::holds_alternative<ArrayRef>(a)) return std::get<ArrayRef>(a).id == std::get<ArrayRef>(b).id;
  if (std::holds_alternative<ObjectRef>(a)) return std::get<ObjectRef>(a).id == std::get<ObjectRef>(b).id;
  return false;
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
  audioBridge_.reset();
  displayBridge_.reset();
  configBridge_.reset();
  modeBridge_.reset();
  timerBridge_.reset();
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
  audioBridge_ = std::make_unique<AudioBridge>(ctx_);
  displayBridge_ = std::make_unique<DisplayBridge>(ctx_);
  configBridge_ = std::make_unique<ConfigBridge>(ctx_);
  modeBridge_ = std::make_unique<ModeBridge>(ctx_);
  timerBridge_ = std::make_unique<TimerBridge>(ctx_);
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
  audioBridge_->install(options_);
  displayBridge_->install(options_);
  configBridge_->install(options_);
  modeBridge_->install(options_);
  timerBridge_->install(options_);
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
  registerAnyMethod("has");
  registerAnyMethod("find");
  registerAnyMethod("trim");
  registerAnyMethod("upper");
  registerAnyMethod("lower");
  registerAnyMethod("includes");
  registerAnyMethod("startswith");
  registerAnyMethod("endswith");
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
  registerAnyMethod("foreach");
  registerAnyMethod("send");
  registerAnyMethod("pause");
  registerAnyMethod("resume");
  registerAnyMethod("stop");
  registerAnyMethod("cancel");
  registerAnyMethod("running");

  // Array methods (for any.* dispatch fallback)
  options_.host_functions["array.len"] = [this](const std::vector<BytecodeValue>& args) {
    if (args.empty() || !std::holds_alternative<ArrayRef>(args[0])) return BytecodeValue(nullptr);
    return BytecodeValue(int64_t(ctx_->vm->getHostArrayLength(std::get<ArrayRef>(args[0]))));
  };
  options_.host_functions["array.push"] = [this](const std::vector<BytecodeValue>& args) {
    if (args.size() < 2 || !std::holds_alternative<ArrayRef>(args[0])) return BytecodeValue(nullptr);
    ctx_->vm->pushHostArrayValue(std::get<ArrayRef>(args[0]), args[1]);
    return args[0];
  };
  options_.host_functions["array.pop"] = [this](const std::vector<BytecodeValue>& args) {
    if (args.empty() || !std::holds_alternative<ArrayRef>(args[0])) return BytecodeValue(nullptr);
    return ctx_->vm->popHostArrayValue(std::get<ArrayRef>(args[0]));
  };
  options_.host_functions["array.has"] = [this](const std::vector<BytecodeValue>& args) {
    if (args.size() < 2 || !std::holds_alternative<ArrayRef>(args[0])) return BytecodeValue(false);
    return BytecodeValue(ctx_->vm->arrayContains(std::get<ArrayRef>(args[0]), args[1]));
  };
  options_.host_functions["array.find"] = [this](const std::vector<BytecodeValue>& args) {
    if (args.size() < 2 || !std::holds_alternative<ArrayRef>(args[0])) return BytecodeValue(int64_t(-1));
    auto arrRef = std::get<ArrayRef>(args[0]);
    size_t len = ctx_->vm->getHostArrayLength(arrRef);
    for (size_t i = 0; i < len; i++) {
      if (valuesEqual(ctx_->vm->getHostArrayValue(arrRef, i), args[1])) {
        return BytecodeValue(int64_t(i));
      }
    }
    return BytecodeValue(int64_t(-1));
  };
  options_.host_functions["array.map"] = [this](const std::vector<BytecodeValue>& args) {
    if (args.size() < 2 || !std::holds_alternative<ArrayRef>(args[0])) return BytecodeValue(nullptr);
    auto arrRef = std::get<ArrayRef>(args[0]);
    auto resultRef = ctx_->vm->createHostArray();
    size_t len = ctx_->vm->getHostArrayLength(arrRef);
    for (size_t i = 0; i < len; i++) {
      auto elem = ctx_->vm->getHostArrayValue(arrRef, i);
      auto mapped = ctx_->vm->callFunction(args[1], {elem});
      ctx_->vm->pushHostArrayValue(resultRef, mapped);
    }
    return BytecodeValue(resultRef);
  };
  options_.host_functions["array.filter"] = [this](const std::vector<BytecodeValue>& args) {
    if (args.size() < 2 || !std::holds_alternative<ArrayRef>(args[0])) return BytecodeValue(nullptr);
    auto arrRef = std::get<ArrayRef>(args[0]);
    auto resultRef = ctx_->vm->createHostArray();
    size_t len = ctx_->vm->getHostArrayLength(arrRef);
    for (size_t i = 0; i < len; i++) {
      auto elem = ctx_->vm->getHostArrayValue(arrRef, i);
      auto keep = ctx_->vm->callFunction(args[1], {elem});
      if (std::holds_alternative<bool>(keep) && std::get<bool>(keep)) {
        ctx_->vm->pushHostArrayValue(resultRef, elem);
      }
    }
    return BytecodeValue(resultRef);
  };
  options_.host_functions["array.reduce"] = [this](const std::vector<BytecodeValue>& args) {
    if (args.size() < 3 || !std::holds_alternative<ArrayRef>(args[0])) return BytecodeValue(nullptr);
    auto arrRef = std::get<ArrayRef>(args[0]);
    BytecodeValue acc = args[2];
    size_t len = ctx_->vm->getHostArrayLength(arrRef);
    for (size_t i = 0; i < len; i++) {
      auto elem = ctx_->vm->getHostArrayValue(arrRef, i);
      acc = ctx_->vm->callFunction(args[1], {acc, elem});
    }
    return acc;
  };
  options_.host_functions["array.foreach"] = [this](const std::vector<BytecodeValue>& args) {
    if (args.size() < 2 || !std::holds_alternative<ArrayRef>(args[0])) return BytecodeValue(nullptr);
    auto arrRef = std::get<ArrayRef>(args[0]);
    size_t len = ctx_->vm->getHostArrayLength(arrRef);
    for (size_t i = 0; i < len; i++) {
      auto elem = ctx_->vm->getHostArrayValue(arrRef, i);
      ctx_->vm->callFunction(args[1], {elem});
    }
    return BytecodeValue(nullptr);
  };
  options_.host_functions["array.sort"] = [this](const std::vector<BytecodeValue>& args) {
    if (args.empty() || !std::holds_alternative<ArrayRef>(args[0])) return BytecodeValue(nullptr);
    auto arrRef = std::get<ArrayRef>(args[0]);
    size_t len = ctx_->vm->getHostArrayLength(arrRef);
    for (size_t i = 0; i < len; i++) {
      for (size_t j = 0; j < len - i - 1; j++) {
        auto a = ctx_->vm->getHostArrayValue(arrRef, j);
        auto b = ctx_->vm->getHostArrayValue(arrRef, j + 1);
        bool swap = false;
        if (args.size() >= 2) {
          auto cmp = ctx_->vm->callFunction(args[1], {a, b});
          if (std::holds_alternative<bool>(cmp)) {
            swap = std::get<bool>(cmp);
          }
        } else {
          if (std::holds_alternative<int64_t>(a) && std::holds_alternative<int64_t>(b)) {
            swap = std::get<int64_t>(a) > std::get<int64_t>(b);
          }
        }
        if (swap) {
          auto temp = a;
          ctx_->vm->setHostArrayValue(arrRef, j, b);
          ctx_->vm->setHostArrayValue(arrRef, j + 1, temp);
        }
      }
    }
    return BytecodeValue(arrRef);
  };

  // any.in(value, container) - membership test
  options_.host_functions["string.len"] = [this](const std::vector<BytecodeValue>& args) {
    if (args.empty() || !std::holds_alternative<std::string>(args[0])) return BytecodeValue(nullptr);
    return BytecodeValue(int64_t(std::get<std::string>(args[0]).length()));
  };
  options_.host_functions["string.trim"] = [this](const std::vector<BytecodeValue>& args) {
    if (args.empty() || !std::holds_alternative<std::string>(args[0])) return BytecodeValue(nullptr);
    std::string s = std::get<std::string>(args[0]);
    size_t start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return BytecodeValue(std::string(""));
    size_t end = s.find_last_not_of(" \t\n\r");
    return BytecodeValue(s.substr(start, end - start + 1));
  };
  options_.host_functions["string.upper"] = [this](const std::vector<BytecodeValue>& args) {
    if (args.empty() || !std::holds_alternative<std::string>(args[0])) return BytecodeValue(nullptr);
    std::string s = std::get<std::string>(args[0]);
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    return BytecodeValue(s);
  };
  options_.host_functions["string.lower"] = [this](const std::vector<BytecodeValue>& args) {
    if (args.empty() || !std::holds_alternative<std::string>(args[0])) return BytecodeValue(nullptr);
    std::string s = std::get<std::string>(args[0]);
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return BytecodeValue(s);
  };
  options_.host_functions["string.includes"] = [this](const std::vector<BytecodeValue>& args) {
    if (args.size() < 2 || !std::holds_alternative<std::string>(args[0]) || !std::holds_alternative<std::string>(args[1])) return BytecodeValue(false);
    const std::string& s = std::get<std::string>(args[0]);
    const std::string& sub = std::get<std::string>(args[1]);
    return BytecodeValue(s.find(sub) != std::string::npos);
  };
  options_.host_functions["string.startswith"] = [this](const std::vector<BytecodeValue>& args) {
    if (args.size() < 2 || !std::holds_alternative<std::string>(args[0]) || !std::holds_alternative<std::string>(args[1])) return BytecodeValue(false);
    const std::string& s = std::get<std::string>(args[0]);
    const std::string& pre = std::get<std::string>(args[1]);
    return BytecodeValue(s.size() >= pre.size() && s.compare(0, pre.size(), pre) == 0);
  };
  options_.host_functions["string.endswith"] = [this](const std::vector<BytecodeValue>& args) {
    if (args.size() < 2 || !std::holds_alternative<std::string>(args[0]) || !std::holds_alternative<std::string>(args[1])) return BytecodeValue(false);
    const std::string& s = std::get<std::string>(args[0]);
    const std::string& suf = std::get<std::string>(args[1]);
    return BytecodeValue(s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0);
  };

  // any.in(value, container) - membership test
  options_.host_functions["any.in"] = [this](const std::vector<BytecodeValue>& args) {
    if (args.size() < 2) {
      return BytecodeValue(false);
    }
    const auto& value = args[0];
    const auto& container = args[1];
    
    // Check based on container type
    if (std::holds_alternative<ArrayRef>(container)) {
      return BytecodeValue(ctx_->vm->arrayContains(std::get<ArrayRef>(container), value));
    } else if (std::holds_alternative<std::string>(container)) {
      const auto& str = std::get<std::string>(container);
      if (std::holds_alternative<std::string>(value)) {
        const auto& substr = std::get<std::string>(value);
        return BytecodeValue(str.find(substr) != std::string::npos);
      }
      return BytecodeValue(false);
    } else if (std::holds_alternative<ObjectRef>(container)) {
      if (std::holds_alternative<std::string>(value)) {
        const auto& key = std::get<std::string>(value);
        return BytecodeValue(ctx_->vm->objectHasKey(std::get<ObjectRef>(container), key));
      }
      return BytecodeValue(false);
    } else if (std::holds_alternative<RangeRef>(container)) {
      if (!std::holds_alternative<int64_t>(value)) {
        return BytecodeValue(false);
      }
      int64_t val = std::get<int64_t>(value);
      return BytecodeValue(ctx_->vm->isInRange(std::get<RangeRef>(container), val));
    }
    return BytecodeValue(false);
  };

  // any.not_in(value, container) - negated membership test
  options_.host_functions["any.not_in"] = [this](const std::vector<BytecodeValue>& args) {
    auto result = options_.host_functions["any.in"](args);
    if (std::holds_alternative<bool>(result)) {
      return BytecodeValue(!std::get<bool>(result));
    }
    return BytecodeValue(true);  // If in() failed, not_in is true
  };

  // any(iterable, predicate) - check if any element satisfies predicate
  options_.host_functions["any"] = [this](const std::vector<BytecodeValue>& args) {
    if (args.size() < 2) {
      return BytecodeValue(false);
    }
    
    const auto& iterable = args[0];
    const auto& predicate = args[1];
    
    // Predicate should be a function
    if (!std::holds_alternative<HostFunctionRef>(predicate)) {
      return BytecodeValue(false);
    }
    
    const std::string& fnName = std::get<HostFunctionRef>(predicate).name;
    
    // Create iterator
    IteratorRef iterRef = ctx_->vm->createIterator(iterable);
    
    // Iterate and check predicate
    while (true) {
      auto result = ctx_->vm->iteratorNext(iterRef);
      
      // Check if done using helper
      if (!std::holds_alternative<ObjectRef>(result)) {
        return BytecodeValue(false);
      }
      auto resultObjRef = std::get<ObjectRef>(result);
      
      // Get done flag
      auto doneVal = ctx_->vm->getHostObjectField(resultObjRef, "done");
      if (std::holds_alternative<bool>(doneVal) && std::get<bool>(doneVal)) {
        return BytecodeValue(false);  // Reached end, no match found
      }
      
      // Get value
      auto valueVal = ctx_->vm->getHostObjectField(resultObjRef, "value");
      if (std::holds_alternative<std::nullptr_t>(valueVal)) {
        continue;
      }
      
      // Call predicate with value
      std::vector<BytecodeValue> predArgs;
      predArgs.push_back(valueVal);
      auto predResult = ctx_->vm->callFunction(BytecodeValue(HostFunctionRef{fnName}), predArgs);
      
      if (std::holds_alternative<bool>(predResult) && std::get<bool>(predResult)) {
        return BytecodeValue(true);  // Found a match
      }
    }
  };

  // Type system functions
  options_.host_functions["type.of"] = [](const std::vector<BytecodeValue>& args) {
    if (args.empty()) {
      return BytecodeValue(std::string("null"));
    }
    const auto& val = args[0];
    if (std::holds_alternative<std::nullptr_t>(val)) return BytecodeValue(std::string("null"));
    if (std::holds_alternative<bool>(val)) return BytecodeValue(std::string("bool"));
    if (std::holds_alternative<int64_t>(val)) return BytecodeValue(std::string("int"));
    if (std::holds_alternative<double>(val)) return BytecodeValue(std::string("num"));
    if (std::holds_alternative<std::string>(val)) return BytecodeValue(std::string("string"));
    if (std::holds_alternative<ArrayRef>(val)) return BytecodeValue(std::string("array"));
    if (std::holds_alternative<ObjectRef>(val)) return BytecodeValue(std::string("object"));
    if (std::holds_alternative<RangeRef>(val)) return BytecodeValue(std::string("range"));
    return BytecodeValue(std::string("unknown"));
  };

  options_.host_functions["type.is"] = [](const std::vector<BytecodeValue>& args) {
    if (args.size() < 2) {
      return BytecodeValue(false);
    }
    const auto& val = args[0];
    if (!std::holds_alternative<std::string>(args[1])) {
      return BytecodeValue(false);
    }
    std::string typeName = std::get<std::string>(args[1]);
    if (typeName == "null") return BytecodeValue(std::holds_alternative<std::nullptr_t>(val));
    if (typeName == "bool") return BytecodeValue(std::holds_alternative<bool>(val));
    if (typeName == "int") return BytecodeValue(std::holds_alternative<int64_t>(val));
    if (typeName == "num" || typeName == "float") return BytecodeValue(std::holds_alternative<double>(val));
    if (typeName == "string") return BytecodeValue(std::holds_alternative<std::string>(val));
    if (typeName == "array") return BytecodeValue(std::holds_alternative<ArrayRef>(val));
    if (typeName == "object") return BytecodeValue(std::holds_alternative<ObjectRef>(val));
    return BytecodeValue(false);
  };

  options_.host_functions["implements"] = [](const std::vector<BytecodeValue>& args) {
    // Placeholder - full trait system requires type metadata
    // For now, return false for all checks
    (void)args;
    return BytecodeValue(false);
  };

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
