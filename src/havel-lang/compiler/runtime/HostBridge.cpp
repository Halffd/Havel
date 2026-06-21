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
#include "ConcurrencyBridge.hpp"
#include <iostream>
#include <functional>
#include "../../../host/module/ModularHostBridges.hpp"
#include "../../../host/ui/UIManager.hpp"
#include "core/hotkey/HotkeyManager.hpp"
#include "havel-lang/compiler/vm/VMApi.hpp"
#include "havel-lang/parser/Parser.h"
#include "havel-lang/compiler/core/ByteCompiler.hpp"
#include "havel-lang/compiler/runtime/RuntimeSupport.hpp"
#include "havel-lang/lexer/Lexer.hpp"

#include <fstream>
#include "../../../host/app/AppService.hpp"
#include "../../../host/media/MediaService.hpp"

extern "C" {

HavelAPI *getHavelAPI(void) {
    static HavelAPI api = {};
    static int initialized = 0;
    if (!initialized) {
        initialized = 1;
        api.version = HAVEL_API_VERSION;
    }
    return &api;
}

}

namespace havel::compiler {

#ifdef HAVEL_CORE_PROFILE
HostBridge::HostBridge(const ::havel::HostContext &ctx)
    : ctx_(&ctx), policy_(ExecutionPolicy::DefaultPolicy()), moduleLoader_(*ctx_) {
  extensionLoader_ = std::make_unique<Loader>();
}

HostBridge::HostBridge(const ::havel::HostContext &ctx,
                       const ExecutionPolicy &policy)
    : ctx_(&ctx), policy_(policy), moduleLoader_(*ctx_) {
  extensionLoader_ = std::make_unique<Loader>();
  moduleLoader_.setExecutionPolicy(policy);
}

HostBridge::~HostBridge() = default;
void HostBridge::shutdown() {}
void HostBridge::clear() {}
void HostBridge::registerModeCallbacks(const std::string &, CallbackId, CallbackId) {}
void HostBridge::initBridges() {}
void HostBridge::install(InstallProfile, bool) {}
void HostBridge::runVmSetup() {}
void HostBridge::registerModule(const HostModule &module) { modules_.push_back(module); }
void HostBridge::addVmSetup(std::function<void(VM &)> setupFn) { vm_setup_callbacks_.push_back(std::move(setupFn)); }
void HostBridge::registerExtensionFunction(const std::string &fullName, BytecodeHostFunction fn) { options_.host_functions[fullName] = std::move(fn); }
std::shared_ptr<HostBridge> createHostBridge(const ::havel::HostContext &ctx) { return std::make_shared<HostBridge>(ctx); }
std::shared_ptr<HostBridge> createHostBridge(const ::havel::HostContext &ctx, const ExecutionPolicy &policy) { return std::make_shared<HostBridge>(ctx, policy); }
bool HostBridge::import(const std::string &) { return false; }
void HostBridge::loadExtension(const std::string &) {}
void HostBridge::checkTimers() {}

#else

// Helper: convert OpCode to string name (simplified subset for bytecode() display)
[[maybe_unused]] static std::string opcodeNameStr(OpCode opcode) {
  switch (opcode) {
    case OpCode::LOAD_CONST: return "LOAD_CONST";
    case OpCode::LOAD_GLOBAL: return "LOAD_GLOBAL";
    case OpCode::STORE_GLOBAL: return "STORE_GLOBAL";
    case OpCode::STORE_IMMUT_GLOBAL: return "STORE_IMMUT_GLOBAL";
    case OpCode::LOAD_VAR: return "LOAD_VAR";
    case OpCode::STORE_VAR: return "STORE_VAR";
    case OpCode::STORE_IMMUT_VAR: return "STORE_IMMUT_VAR";
    case OpCode::POP: return "POP";
    case OpCode::DUP: return "DUP";
    case OpCode::ADD: return "ADD";
    case OpCode::SUB: return "SUB";
    case OpCode::MUL: return "MUL";
    case OpCode::DIV: return "DIV";
    case OpCode::INT_DIV: return "INT_DIV";
  case OpCode::DIVMOD: return "DIVMOD";
  case OpCode::REMAINDER: return "REMAINDER";
    case OpCode::RETURN: return "RETURN";
	case OpCode::CALL: return "CALL";
	case OpCode::JUMP: return "JUMP";
    case OpCode::JUMP_IF_FALSE: return "JUMP_IF_FALSE";
    case OpCode::OBJECT_NEW: return "OBJECT_NEW";
    case OpCode::OBJECT_SET: return "OBJECT_SET";
    case OpCode::OBJECT_GET: return "OBJECT_GET";
    case OpCode::ARRAY_NEW: return "ARRAY_NEW";
    case OpCode::ARRAY_SET: return "ARRAY_SET";
    case OpCode::ARRAY_GET: return "ARRAY_GET";
    default: return std::to_string(static_cast<int>(opcode));
  }
}

// Helper function to get type name from Value
static std::string getTypeName(const Value &value) {
  if (value.isNull())
    return "null";
  if (value.isBool())
    return "bool";
  if (value.isInt())
    return "int";
  if (value.isDouble())
    return "float";
  if (value.isStringValId() || value.isStringId())
    return "string";
  if (value.isArrayId())
    return "array";
  if (value.isObjectId())
    return "object";
  if (value.isSetId())
    return "set";
  if (value.isEnumId())
    return "enum";
  if (value.isRangeId())
    return "range";
  if (value.isThreadId())
    return "thread";
  if (value.isIntervalId())
    return "interval";
  if (value.isTimeoutId())
    return "timeout";
  if (value.isErrorId())
    return "error";
  if (value.isFunctionObjId())
    return "function";
  if (value.isClosureId())
    return "closure";
  if (value.isHostFuncId())
    return "function";
  if (value.isBoundMethodId())
    return "function";
  return "unknown";
}

// Helper function to compare two Values for equality
[[maybe_unused]] static bool valuesEqual(const Value &a, const Value &b) {
  return a == b; // Value has operator==
}

HostBridge::HostBridge(const ::havel::HostContext &ctx)
    : ctx_(&ctx), policy_(ExecutionPolicy::DefaultPolicy()),
      moduleLoader_(*ctx_) {
  extensionLoader_ = std::make_unique<Loader>();
  initBridges();
}

HostBridge::HostBridge(const ::havel::HostContext &ctx,
                       const ExecutionPolicy &policy)
    : ctx_(&ctx), policy_(policy), moduleLoader_(*ctx_) {
  extensionLoader_ = std::make_unique<Loader>();
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
  concurrencyBridge_.reset();
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

void HostBridge::registerModeCallbacks(const std::string &modeName,
                                           CallbackId enterId, CallbackId exitId) {
    ModeBinding binding;
    binding.modeName = modeName;
    binding.enter_id = enterId;
    binding.exit_id = exitId;
    mode_bindings_[modeName] = std::move(binding);
    mode_definition_order_.push_back(modeName);
}

void HostBridge::initBridges() {
#ifdef HAVEL_CORE_PROFILE
    extensionLoader_ = std::make_unique<Loader>();
#else
  ioBridge_ = std::make_unique<IOBridge>(ctx_);
  systemBridge_ = std::make_unique<SystemBridge>(ctx_);
  uiBridge_ = std::make_unique<UIBridge>(ctx_);
  inputBridge_ = std::make_unique<InputBridge>(ctx_);
  mediaBridge_ = std::make_unique<MediaBridge>(ctx_);
  networkBridge_ = std::make_unique<NetworkBridge>(ctx_);
  audioBridge_ = std::make_unique<AudioBridge>(ctx_);
  mpvBridge_ = std::make_unique<MPVBridge>(ctx_);
  displayBridge_ = std::make_unique<DisplayBridge>(ctx_);
  configBridge_ = std::make_unique<ConfigBridge>(ctx_);
  modeBridge_ = std::make_unique<ModeBridge>(ctx_);
  timerBridge_ = std::make_unique<TimerBridge>(ctx_);
  appBridge_ = std::make_unique<AppBridge>(ctx_);
  concurrencyBridge_ = std::make_unique<ConcurrencyBridge>(*ctx_);
  // Share event queue with HostContext and HotkeyManager for thread-safe dispatch
  const_cast<HostContext &>(*ctx_).eventQueue = concurrencyBridge_->eventQueue();
  if (ctx_->hotkeyManager) {
    ctx_->hotkeyManager->setEventQueue(concurrencyBridge_->eventQueue());
  }
    automationBridge_ = std::make_unique<AutomationBridge>(ctx_);
    browserBridge_ = std::make_unique<BrowserBridge>(ctx_);
    toolsBridge_ = std::make_unique<ToolsBridge>(ctx_);
}
#endif

void HostBridge::install(InstallProfile profile, bool eagerBridgeInstall) {
  const bool coreProfile = (profile == InstallProfile::Core);
  options_.host_functions.reserve(64);
  vm_setup_callbacks_.reserve(16);

  options_.host_functions["type"] = [this](const std::vector<Value> &args) {
    if (args.empty()) return Value::makeNull();
    auto ref = ctx_->vm->getHeap().allocateString(getTypeName(args[0]));
    return Value::makeStringId(ref.id);
  };

  options_.host_functions["len"] = [this](const std::vector<Value> &args) {
    if (args.empty()) return Value::makeInt(0);
    auto *vm = static_cast<VM *>(ctx_->vm);
    return vm->execLengthOp(args[0]);
  };

#ifdef HAVEL_CORE_PROFILE
  if (coreProfile) {
    return;
  }
#endif

  if (eagerBridgeInstall && !coreProfile) {
    ioBridge_->install(options_);
    systemBridge_->install(options_);
    uiBridge_->install(options_);
    inputBridge_->install(options_);
    mediaBridge_->install(options_);
    networkBridge_->install(options_);
    audioBridge_->install(options_);
    mpvBridge_->install(options_);
    displayBridge_->install(options_);
    modeBridge_->install(options_);
    timerBridge_->install(options_);
    appBridge_->install(options_);
    concurrencyBridge_->install(options_);
    automationBridge_->install(options_);
    browserBridge_->install(options_);
    toolsBridge_->install(options_);
  }

  addVmSetup([](VM &vm) {
    auto hotkeyObj = vm.createHostObject();
    for (const auto &name : {"register", "trigger", "list"}) {
      std::string fn = std::string("hotkey.") + name;
      int idx = vm.getHostFunctionIndex(fn);
      if (idx >= 0) {
        vm.setHostObjectField(hotkeyObj, name, Value::makeHostFuncId(static_cast<uint32_t>(idx)));
      }
    }
vm.setGlobal("hotkey", Value::makeObjectId(hotkeyObj.id));
  });

  options_.host_functions["extension.load"] =
[this](const std::vector<Value> &args) {
  if (args.empty() || !args[0].isStringValId()) return Value::makeBool(false);
  extensionLoader_->loadExtensionWithInit(args[0].toString(), getHavelAPI());
  return Value(extensionLoader_->isLoaded(args[0].toString()));
};
  options_.host_functions["extension.isLoaded"] =
      [this](const std::vector<Value> &args) {
        if (args.empty() || !args[0].isStringValId()) return Value::makeBool(false);
        return Value(extensionLoader_->isLoaded(args[0].toString()));
      };
  options_.host_functions["extension.list"] =
      [this](const std::vector<Value> &args) {
        (void)args;
        auto names = extensionLoader_->getLoadedExtensions();
        auto *vm = static_cast<VM *>(ctx_->vm);
        if (!vm) return Value::makeNull();
        auto arr = vm->createHostArray();
        for (const auto &name : names) {
          (void)name;
          vm->pushHostArrayValue(arr, Value::makeNull());
        }
        return Value::makeArrayId(arr.id);
      };
  options_.host_functions["extension.addSearchPath"] =
      [this](const std::vector<Value> &args) {
        if (args.empty() || !args[0].isStringValId()) return Value::makeBool(false);
        extensionLoader_->addSearchPath(args[0].toString());
        return Value::makeBool(true);
      };

  auto isCallable = [](const Value &v) {
    return v.isHostFuncId() || v.isClosureId() || v.isFunctionObjId();
  };

  options_.host_functions["any"] = [this, isCallable](const std::vector<Value> &args) {
    if (args.size() < 2 || !isCallable(args[1])) return Value::makeBool(false);
    IteratorRef iterRef = ctx_->vm->createIterator(args[0]);
    while (true) {
      auto result = ctx_->vm->iteratorNext(iterRef);
      if (!result.isObjectId()) return Value::makeBool(false);
      auto resultObjRef = ObjectRef{result.asObjectId(), true};
      auto doneVal = ctx_->vm->getHostObjectField(resultObjRef, "done");
      if (doneVal.isBool() && doneVal.asBool()) return Value::makeBool(false);
      auto valueVal = ctx_->vm->getHostObjectField(resultObjRef, "second");
      std::vector<Value> predArgs{valueVal};
      auto predResult = ctx_->vm->callFunction(args[1], predArgs);
      if (predResult.isBool() && predResult.asBool()) return Value::makeBool(true);
      if (predResult.isInt() && predResult.asInt() != 0) return Value::makeBool(true);
    }
  };

  options_.host_functions["all"] = [this, isCallable](const std::vector<Value> &args) {
    if (args.size() < 2 || !isCallable(args[1])) return Value::makeBool(false);
    IteratorRef iterRef = ctx_->vm->createIterator(args[0]);
    bool found_any = false;
    while (true) {
      auto result = ctx_->vm->iteratorNext(iterRef);
      if (!result.isObjectId()) break;
      auto resultObjRef = ObjectRef{result.asObjectId(), true};
      auto doneVal = ctx_->vm->getHostObjectField(resultObjRef, "done");
      if (doneVal.isBool() && doneVal.asBool()) break;
      auto valueVal = ctx_->vm->getHostObjectField(resultObjRef, "second");
      found_any = true;
      std::vector<Value> predArgs{valueVal};
      auto predResult = ctx_->vm->callFunction(args[1], predArgs);
      bool isTruthy = (predResult.isBool() && predResult.asBool()) ||
                      (predResult.isInt() && predResult.asInt() != 0);
      if (!isTruthy) return Value::makeBool(false);
    }
    return Value::makeBool(found_any);
  };

  options_.host_functions["eval"] = [this](const std::vector<Value> &args) {
    if (args.empty() || !ctx_ || !ctx_->vm) return Value::makeNull();
    std::string code = args[0].toString();
    if (code.empty()) return Value::makeNull();
    try {
      parser::Parser parser;
      auto program = parser.produceAST(code);
      if (!program || parser.hasErrors()) return Value::makeNull();
      ByteCompiler byteCompiler;
      auto chunk = byteCompiler.compile(*program);
      if (!chunk) return Value::makeNull();
      return ctx_->vm->execute(*chunk, "__main__");
    } catch (...) {
      return Value::makeNull();
    }
  };

  options_.host_functions["inspect"] = [this](const std::vector<Value> &args) {
    if (args.empty() || !ctx_ || !ctx_->vm) return Value::makeNull();
    auto ref = ctx_->vm->getHeap().allocateString(getTypeName(args[0]));
    return Value::makeStringId(ref.id);
  };

  options_.host_functions["prototypes"] = [this](const std::vector<Value> &args) {
    if (args.empty() || !ctx_ || !ctx_->vm) return Value::makeNull();
    auto arrRef = ctx_->vm->getHeap().allocateArray();
    auto *arr = ctx_->vm->getHeap().array(arrRef.id);
    if (!args[0].isObjectId()) return Value::makeArrayId(arrRef.id);
    auto *obj = ctx_->vm->getHeap().object(args[0].asObjectId());
    while (obj) {
      auto *nameVal = obj->get("__name");
      if (nameVal && nameVal->isStringId()) {
        arr->push_back(*nameVal);
      } else {
        arr->push_back(Value::makeStringId(ctx_->vm->getHeap().allocateString("<anonymous>").id));
      }
      GCHeap::ObjectEntry *next = nullptr;
      auto *protoVal = obj->get("__proto");
      if (protoVal && protoVal->isObjectId()) next = ctx_->vm->getHeap().object(protoVal->asObjectId());
      if (!next) {
        auto *parentVal = obj->get("__class");
        if (!parentVal) parentVal = obj->get("__parent");
        if (!parentVal) parentVal = obj->get("__struct");
        if (parentVal && parentVal->isObjectId()) next = ctx_->vm->getHeap().object(parentVal->asObjectId());
      }
      obj = next;
    }
    return Value::makeArrayId(arrRef.id);
  };

  options_.host_functions["proto"] = [this](const std::vector<Value> &args) {
    if (args.size() < 2 || !ctx_ || !ctx_->vm) return Value::makeNull();
    if (!args[0].isObjectId()) return args[0];
    ctx_->vm->getHeap().object(args[0].asObjectId())->set("__proto", args[1]);
    return args[0];
  };
  options_.host_functions["getproto"] = [this](const std::vector<Value> &args) {
    if (args.empty() || !ctx_ || !ctx_->vm || !args[0].isObjectId()) return Value::makeNull();
    auto *obj = ctx_->vm->getHeap().object(args[0].asObjectId());
    if (!obj) return Value::makeNull();
    auto *protoVal = obj->get("__proto");
    return protoVal ? *protoVal : Value::makeNull();
  };
  options_.host_functions["setproto"] = [this](const std::vector<Value> &args) {
    if (args.size() < 2 || !ctx_ || !ctx_->vm) return Value::makeNull();
    if (!args[0].isObjectId()) return args[0];
    ctx_->vm->getHeap().object(args[0].asObjectId())->set("__proto", args[1]);
    return args[0];
  };

  options_.host_functions["caller"] = [this](const std::vector<Value> &args) {
    if (!ctx_ || !ctx_->vm) return Value::makeNull();
    int depth = (!args.empty() && args[0].isInt()) ? static_cast<int>(args[0].asInt()) : 0;
    auto info = ctx_->vm->getCallerInfo(depth);
    auto objRef = ctx_->vm->getHeap().allocateObject();
    auto *obj = ctx_->vm->getHeap().object(objRef.id);
    obj->set("function", Value::makeStringId(ctx_->vm->getHeap().allocateString(info.function).id));
    obj->set("line", Value::makeInt(static_cast<int64_t>(info.line)));
    obj->set("column", Value::makeInt(static_cast<int64_t>(info.column)));
    obj->set("file", Value::makeStringId(ctx_->vm->getHeap().allocateString(info.file).id));
    return Value::makeObjectId(objRef.id);
  };

  options_.host_functions["defun"] = [this](const std::vector<Value> &args) {
    if (args.size() < 3 || !ctx_ || !ctx_->vm) return Value::makeNull();
    std::string typeName = args[0].toString();
    std::string methodName = args[1].toString();
    if (typeName.empty() || methodName.empty()) return Value::makeNull();
    Value fn = args[2];
    const BytecodeChunk *fnChunk = ctx_->vm->getCurrentChunk();
    std::string fullName = typeName + "." + methodName;
    uint32_t funcIdx = ctx_->vm->getHostFunctionIndex(fullName);
    options_.host_functions[fullName] = [this, fn, fnChunk](const std::vector<Value> &callArgs) {
      if (!ctx_ || !ctx_->vm) return Value::makeNull();
      try {
        const auto *savedChunk = ctx_->vm->getCurrentChunk();
        ctx_->vm->setCurrentChunk(fnChunk);
        auto result = ctx_->vm->call(fn, callArgs);
        ctx_->vm->setCurrentChunk(savedChunk);
        return result;
      } catch (...) {
        return Value::makeNull();
      }
    };
    ctx_->vm->registerPrototypeMethod(typeName, methodName, funcIdx);
    return Value::makeBool(true);
  };

  options_.host_functions["del"] = [this](const std::vector<Value> &args) {
    if (args.size() < 2 || !ctx_ || !ctx_->vm) return Value::makeNull();
    if (args[0].isObjectId() && args[1].isStringValId()) {
      auto *obj = ctx_->vm->getHeap().object(args[0].asObjectId());
      if (obj) {
        std::string key = args[1].toString();
        if (!key.empty()) {
          obj->erase(key);
          return Value::makeBool(true);
        }
      }
    }
    return Value::makeBool(false);
  };

  options_.host_functions["implements"] = [](const std::vector<Value> &args) {
    (void)args;
    return Value::makeBool(false);
  };
}
void HostBridge::runVmSetup() {
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

void HostBridge::registerExtensionFunction(const std::string &fullName,
                                           BytecodeHostFunction fn) {
  options_.host_functions[fullName] = std::move(fn);
}

std::shared_ptr<HostBridge> createHostBridge(const ::havel::HostContext &ctx) {
  return std::make_shared<HostBridge>(ctx);
}

std::shared_ptr<HostBridge> createHostBridge(const ::havel::HostContext &ctx,
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
    extensionLoader_->loadExtensionWithInit(name, getHavelAPI());
  }
}

void HostBridge::checkTimers() {
  if (concurrencyBridge_) {
    concurrencyBridge_->checkTimers();
  }
}

#endif

} // namespace havel::compiler
