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
#include "../../../modules/window/WindowMonitorModule.hpp"
#include "../../../core/HotkeyManager.hpp"
#include "havel-lang/compiler/vm/VMApi.hpp"
#include "havel-lang/parser/Parser.h"
#include "havel-lang/compiler/core/ByteCompiler.hpp"
#include "havel-lang/lexer/Lexer.hpp"

#include "../../../host/app/AppService.hpp"
#include "../../../host/media/MediaService.hpp"
#include "../../../host/network/NetworkService.hpp"

namespace havel::compiler {

// Helper: convert OpCode to string name (simplified subset for bytecode() display)
static std::string opcodeNameStr(OpCode opcode) {
  switch (opcode) {
    case OpCode::LOAD_CONST: return "LOAD_CONST";
    case OpCode::LOAD_GLOBAL: return "LOAD_GLOBAL";
    case OpCode::STORE_GLOBAL: return "STORE_GLOBAL";
    case OpCode::LOAD_VAR: return "LOAD_VAR";
    case OpCode::STORE_VAR: return "STORE_VAR";
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
  return "unknown";
}

// Helper function to compare two Values for equality
static bool valuesEqual(const Value &a, const Value &b) {
  return a == b; // Value has operator==
}

HostBridge::HostBridge(const ::havel::HostContext &ctx)
    : ctx_(&ctx), policy_(ExecutionPolicy::DefaultPolicy()),
      moduleLoader_(*ctx_) {
  extensionLoader_ = std::make_unique<ExtensionLoader>();
  initBridges();
}

HostBridge::HostBridge(const ::havel::HostContext &ctx,
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
                                       CallbackId conditionId,
                                       CallbackId enterId, CallbackId exitId) {
  // Store the mode binding with callback IDs
  ModeBinding binding;
  binding.modeName = modeName;
  binding.condition_id = conditionId;
  binding.enter_id = enterId;
  binding.exit_id = exitId;
  mode_bindings_[modeName] = std::move(binding);
  mode_definition_order_.push_back(modeName);
}

void HostBridge::initBridges() {
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
  mpvBridge_->install(options_);
  displayBridge_->install(options_);
  configBridge_->install(options_);
  modeBridge_->install(options_);
  timerBridge_->install(options_);
  appBridge_->install(options_);
  concurrencyBridge_->install(options_);
  automationBridge_->install(options_);
  browserBridge_->install(options_);
  toolsBridge_->install(options_);

  // Setup dynamic window globals using existing WindowMonitor from
  // HotkeyManager This integrates window monitoring with bytecode VM without
  // creating duplicate instances
  if (ctx_->windowMonitor && ctx_->vm) {
    VM *vm = static_cast<VM *>(ctx_->vm);
    VMApi api(*vm);
    ::havel::modules::setupDynamicWindowGlobals(api, ctx_->windowMonitor);
  }

  // Create hotkey global object with list method (after InputBridge installs hotkey.list)
  addVmSetup([this](VM &vm) {
    auto hotkeyObj = vm.createHostObject();
    if (vm.getHostFunctionIndex("hotkey.list") >= 0) {
      vm.setHostObjectField(
          hotkeyObj, "list",
          Value::makeHostFuncId(vm.getHostFunctionIndex("hotkey.list")));
    }
    vm.setGlobal("hotkey", Value::makeObjectId(hotkeyObj.id));
  });

  // Install extension loading functions
  // Use raw 'this' pointer to avoid circular reference (HostBridge outlives VM
  // usage)
  options_.host_functions["extension.load"] =
      [this](const std::vector<Value> &args) {
        if (args.empty() || !args[0].isStringValId()) {
          return Value::makeBool(false);
        }
        std::string name = args[0].toString();
        return Value(extensionLoader_->loadExtensionByName(name));
      };
  options_.host_functions["extension.isLoaded"] =
      [this](const std::vector<Value> &args) {
        if (args.empty() || !args[0].isStringValId()) {
          return Value::makeBool(false);
        }
        std::string name = args[0].toString();
        return Value(extensionLoader_->isLoaded(name));
      };
  options_.host_functions["extension.list"] =
      [this](const std::vector<Value> &args) {
        (void)args;
        auto names = extensionLoader_->getLoadedExtensions();
        auto *vm = static_cast<VM *>(ctx_->vm);
        if (!vm) {
          return Value::makeNull();
        }
        auto arr = vm->createHostArray();
        for (const auto &name : names) {
          // TODO: string pool integration - for now push null as placeholder
          vm->pushHostArrayValue(arr, Value::makeNull());
        }
        return Value::makeArrayId(arr.id);
      };
  options_.host_functions["extension.addSearchPath"] =
      [this](const std::vector<Value> &args) {
        if (args.empty() || !args[0].isStringValId()) {
          return Value::makeBool(false);
        }
        std::string path = args[0].toString();
        extensionLoader_->addSearchPath(path);
        return Value::makeBool(true);
      };


 // Global type() function - returns type name string for any value
  options_.host_functions["type"] =
      [this](const std::vector<Value> &args) {
        if (args.empty()) return Value::makeNull();
        std::string typeName = getTypeName(args[0]);
        auto ref = ctx_->vm->getHeap().allocateString(std::move(typeName));
        return Value::makeStringId(ref.id);
      };

  // Global eval() function - compile and execute code at runtime
  // Note: eval is limited - it cannot call print() or other host functions
  // that depend on VM state. Simple expressions like "2+3" work fine.
  options_.host_functions["eval"] =
      [this](const std::vector<Value> &args) {
        if (args.empty() || !ctx_ || !ctx_->vm) return Value::makeNull();

        std::string code;
        if (args[0].isStringValId() && ctx_->vm->getCurrentChunk()) {
          code = ctx_->vm->getCurrentChunk()->getString(args[0].asStringValId());
        } else if (args[0].isStringId() && ctx_->vm->getHeap().string(args[0].asStringId())) {
          code = *ctx_->vm->getHeap().string(args[0].asStringId());
        } else {
          code = args[0].toString();
        }

        if (code.empty()) return Value::makeNull();

        // Compile and execute the code using the existing VM
        try {
          parser::Parser parser;
          auto program = parser.produceAST(code);
          if (!program || parser.hasErrors()) {
            std::string errMsg = "eval: parse error";
            if (!parser.getErrors().empty()) {
              errMsg += ": " + parser.getErrors()[0].message;
            }
            auto ref = ctx_->vm->getHeap().allocateString(std::move(errMsg));
            return Value::makeStringId(ref.id);
          }

	ByteCompiler byteCompiler;
	auto chunk = byteCompiler.compile(*program);
	if (!chunk) {
		auto ref = ctx_->vm->getHeap().allocateString("eval: compilation failed");
		return Value::makeStringId(ref.id);
          }

          // Execute using the VM's execute() which handles state properly
          // Note: this resets the heap, so eval should be used carefully
          auto result = ctx_->vm->execute(*chunk, "__main__");
          return result;
        } catch (const std::exception &e) {
          std::string errMsg = "eval: " + std::string(e.what());
          auto ref = ctx_->vm->getHeap().allocateString(std::move(errMsg));
          return Value::makeStringId(ref.id);
        }
      };

  // Global inspect() function - detailed string representation of any value
  options_.host_functions["inspect"] =
      [this](const std::vector<Value> &args) {
        if (args.empty() || !ctx_ || !ctx_->vm) return Value::makeNull();
        const Value& val = args[0];
        std::string result;

        // Helper lambda to format a value recursively
        std::function<std::string(const Value&, int)> format;
        format = [this, &format](const Value& v, int depth) -> std::string {
          std::string indent(depth * 2, ' ');
          std::string childIndent = std::string((depth + 1) * 2, ' ');

          if (v.isNull()) return "null";
          if (v.isBool()) return v.asBool() ? "true" : "false";
          if (v.isInt()) return std::to_string(v.asInt());
          if (v.isDouble()) return std::to_string(v.asDouble());

          if (v.isStringValId() || v.isStringId()) {
            std::string s;
            if (v.isStringValId() && ctx_->vm->getCurrentChunk()) {
              s = ctx_->vm->getCurrentChunk()->getString(v.asStringValId());
            } else if (v.isStringId() && ctx_->vm->getHeap().string(v.asStringId())) {
              s = *ctx_->vm->getHeap().string(v.asStringId());
            }
            return "\"" + s + "\"";
          }

          if (v.isArrayId()) {
            auto* arr = ctx_->vm->getHeap().array(v.asArrayId());
            if (!arr) return "[]";
            std::string out = "[\n";
            for (size_t i = 0; i < arr->size(); ++i) {
              out += childIndent + std::to_string(i) + ": " + format((*arr)[i], depth + 1) + "\n";
            }
            out += indent + "]";
            return out;
          }

          if (v.isObjectId()) {
            auto* obj = ctx_->vm->getHeap().object(v.asObjectId());
            if (!obj) return "{}";
            std::string out = "{\n";
            auto keys = obj->getKeys();
            for (const auto& key : keys) {
              if (key == "__class" || key == "__parent" || key == "__struct" ||
                  key == "__is_class" || key == "__is_struct" || key == "__name" || key == "__fields") {
                continue; // Skip internal markers
              }
              auto* val = obj->get(key);
              if (val) {
                out += childIndent + key + ": " + format(*val, depth + 1) + "\n";
              }
            }
            out += indent + "}";
            return out;
          }

          if (v.isFunctionObjId()) {
            if (ctx_->vm->getCurrentChunk()) {
              const auto* func = ctx_->vm->getCurrentChunk()->getFunction(v.asFunctionObjId());
              if (func) return "fn " + func->name + "(" + std::to_string(func->param_count) + " params)";
            }
            return "fn(...)";
          }

          return getTypeName(v);
        };

        result = format(val, 0);
        auto ref = ctx_->vm->getHeap().allocateString(std::move(result));
        return Value::makeStringId(ref.id);
      };

  // Global prototypes() function - get prototype chain of an object
  // Follows: __proto → __class → __parent → __struct
  options_.host_functions["prototypes"] =
      [this](const std::vector<Value> &args) {
        if (args.empty() || !ctx_ || !ctx_->vm) return Value::makeNull();
        const Value& val = args[0];

        auto arrRef = ctx_->vm->getHeap().allocateArray();
        auto* arr = ctx_->vm->getHeap().array(arrRef.id);

        if (!val.isObjectId()) {
          return Value::makeArrayId(arrRef.id);
        }

        auto* obj = ctx_->vm->getHeap().object(val.asObjectId());
        while (obj) {
          // Check if this object has a name
          auto* nameVal = obj->get("__name");
          if (nameVal && nameVal->isStringId()) {
            arr->push_back(*nameVal);
          } else {
            arr->push_back(Value::makeStringId(ctx_->vm->getHeap().allocateString("<anonymous>").id));
          }

          // Follow prototype chain: __proto first, then __class, __parent, __struct
          GCHeap::ObjectEntry* next = nullptr;
          auto* protoVal = obj->get("__proto");
          if (protoVal && protoVal->isObjectId()) {
            next = ctx_->vm->getHeap().object(protoVal->asObjectId());
          }
          if (!next) {
            auto* parentVal = obj->get("__class");
            if (!parentVal) parentVal = obj->get("__parent");
            if (!parentVal) parentVal = obj->get("__struct");
            if (parentVal && parentVal->isObjectId()) {
              next = ctx_->vm->getHeap().object(parentVal->asObjectId());
            }
          }
          obj = next;
        }

        return Value::makeArrayId(arrRef.id);
      };

  // ========================================================================
  // PROTOTYPE OOP PRIMITIVES (Lua-style)
  // ========================================================================

  // proto(obj, parent) - Set obj's prototype to parent, return obj
  // Usage: proto(obj, prototype)  -- chains obj → prototype
  options_.host_functions["proto"] =
      [this](const std::vector<Value> &args) {
        if (args.size() < 2 || !ctx_ || !ctx_->vm) return Value::makeNull();
        if (!args[0].isObjectId()) return args[0];
        // Set __proto field to parent (can be null to remove)
        ctx_->vm->getHeap().object(args[0].asObjectId())->set("__proto", args[1]);
        return args[0];
      };

  // getproto(obj) - Get obj's prototype
  // Usage: getproto(obj)  -- returns prototype object or null
  options_.host_functions["getproto"] =
      [this](const std::vector<Value> &args) {
        if (args.empty() || !ctx_ || !ctx_->vm) return Value::makeNull();
        if (!args[0].isObjectId()) return Value::makeNull();
        auto* obj = ctx_->vm->getHeap().object(args[0].asObjectId());
        if (!obj) return Value::makeNull();
        auto* protoVal = obj->get("__proto");
        return protoVal ? *protoVal : Value::makeNull();
      };

  // setproto(obj, parent) - Change obj's prototype, return obj
  // Usage: setproto(obj, newParent)  -- like proto() but returns obj for chaining
  options_.host_functions["setproto"] =
      [this](const std::vector<Value> &args) {
        if (args.size() < 2 || !ctx_ || !ctx_->vm) return Value::makeNull();
        if (!args[0].isObjectId()) return args[0];
        ctx_->vm->getHeap().object(args[0].asObjectId())->set("__proto", args[1]);
        return args[0];
      };

  // Global caller() function - returns caller info object
  // Usage: caller().function, caller().line, caller().file
  // Also supports depth: caller(1) for grandparent caller
  options_.host_functions["caller"] =
      [this](const std::vector<Value> &args) {
        if (!ctx_ || !ctx_->vm) return Value::makeNull();
        int depth = 0;
        if (!args.empty() && args[0].isInt()) depth = static_cast<int>(args[0].asInt());
        // Add 0 because caller() is a host function - no extra bytecode frame is pushed
        auto info = ctx_->vm->getCallerInfo(depth);
        auto objRef = ctx_->vm->getHeap().allocateObject();
        auto* obj = ctx_->vm->getHeap().object(objRef.id);
        obj->set("function", Value::makeStringId(ctx_->vm->getHeap().allocateString(info.function).id));
        obj->set("line", Value::makeInt(static_cast<int64_t>(info.line)));
        obj->set("column", Value::makeInt(static_cast<int64_t>(info.column)));
        obj->set("file", Value::makeStringId(ctx_->vm->getHeap().allocateString(info.file).id));
        return Value::makeObjectId(objRef.id);
      };

  // Global defun() function - define a method on a prototype at runtime
  // Usage: defun("string", "reverse", fn() { ... })
  // or: defun(string, "reverse", fn() { ... })
  options_.host_functions["defun"] =
      [this](const std::vector<Value> &args) {
        if (args.size() < 3 || !ctx_ || !ctx_->vm) return Value::makeNull();

        std::string typeName;
        // First arg: either a string type name or a prototype object
        if (args[0].isStringValId() || args[0].isStringId()) {
          if (args[0].isStringValId() && ctx_->vm->getCurrentChunk()) {
            typeName = ctx_->vm->getCurrentChunk()->getString(args[0].asStringValId());
          } else if (args[0].isStringId() && ctx_->vm->getHeap().string(args[0].asStringId())) {
            typeName = *ctx_->vm->getHeap().string(args[0].asStringId());
          }
        }

        std::string methodName;
        if (args[1].isStringValId() && ctx_->vm->getCurrentChunk()) {
          methodName = ctx_->vm->getCurrentChunk()->getString(args[1].asStringValId());
        } else if (args[1].isStringId() && ctx_->vm->getHeap().string(args[1].asStringId())) {
          methodName = *ctx_->vm->getHeap().string(args[1].asStringId());
        }

        if (typeName.empty() || methodName.empty()) return Value::makeNull();

        // Store the function value and the chunk it came from so we can call it later
        Value fn = args[2];
        const BytecodeChunk* fnChunk = ctx_->vm->getCurrentChunk();

        // Register the method as a host function on the prototype
        // Format: typeName.methodName
        std::string fullName = typeName + "." + methodName;
        uint32_t funcIdx = ctx_->vm->getHostFunctionIndex(fullName);
        options_.host_functions[fullName] =
            [this, fn, fnChunk](const std::vector<Value> &callArgs) {
              if (!ctx_ || !ctx_->vm) return Value::makeNull();
              // Call the function with the provided arguments
              try {
                // Save and restore the current chunk so function lookup works
                const auto* savedChunk = ctx_->vm->getCurrentChunk();
                ctx_->vm->setCurrentChunk(fnChunk);
                auto result = ctx_->vm->call(fn, callArgs);
                ctx_->vm->setCurrentChunk(savedChunk);
                return result;
              } catch (...) {
                return Value::makeNull();
              }
            };

        // Also register it in the VM's prototype method table for CALL_METHOD dispatch
        ctx_->vm->registerPrototypeMethod(typeName, methodName, funcIdx);

        return Value::makeBool(true);
      };

  // Global del() function - delete a field from an object
  // Usage: del obj.field or del(obj, "field")
  options_.host_functions["del"] =
      [this](const std::vector<Value> &args) {
        if (args.size() < 2 || !ctx_ || !ctx_->vm) return Value::makeNull();

        if (args[0].isObjectId() && args[1].isStringValId()) {
          auto* obj = ctx_->vm->getHeap().object(args[0].asObjectId());
          if (obj) {
            std::string key = ctx_->vm->getCurrentChunk() ?
                ctx_->vm->getCurrentChunk()->getString(args[1].asStringValId()) : "";
            if (!key.empty()) {
              obj->erase(key);
              return Value::makeBool(true);
            }
          }
        }
        return Value::makeBool(false);
      };

  // Global describe() function - structural introspection
  options_.host_functions["describe"] =
      [this](const std::vector<Value> &args) {
        if (args.empty() || !ctx_ || !ctx_->vm) return Value::makeNull();
        const Value& val = args[0];

        std::string desc;
        std::string typeName = getTypeName(val);
        desc += "type: " + typeName;

        if (val.isInt()) {
          desc += ", value: " + std::to_string(val.asInt());
        } else if (val.isDouble()) {
          desc += ", value: " + std::to_string(val.asDouble());
        } else if (val.isBool()) {
          desc += ", value: " + std::string(val.asBool() ? "true" : "false");
        } else if (val.isNull()) {
          // nothing more
        } else if (val.isStringValId() || val.isStringId()) {
          std::string s;
          if (val.isStringValId() && ctx_->vm->getCurrentChunk()) {
            s = ctx_->vm->getCurrentChunk()->getString(val.asStringValId());
          } else if (val.isStringId() && ctx_->vm->getHeap().string(val.asStringId())) {
            s = *ctx_->vm->getHeap().string(val.asStringId());
          }
          desc += ", length: " + std::to_string(s.size());
        } else if (val.isArrayId()) {
          auto* arr = ctx_->vm->getHeap().array(val.asArrayId());
          desc += ", length: " + std::to_string(arr ? arr->size() : 0);
        } else if (val.isObjectId()) {
          auto* obj = ctx_->vm->getHeap().object(val.asObjectId());
          desc += ", keys: " + std::to_string(obj ? obj->size() : 0);
        }

        auto ref = ctx_->vm->getHeap().allocateString(std::move(desc));
        return Value::makeStringId(ref.id);
      };

  // Global bytecode() function - compile source to bytecode representation
  options_.host_functions["bytecode"] =
      [this](const std::vector<Value> &args) {
        if (args.empty() || !ctx_ || !ctx_->vm) return Value::makeNull();

        std::string source;
        if (args[0].isStringValId() && ctx_->vm->getCurrentChunk()) {
          source = ctx_->vm->getCurrentChunk()->getString(args[0].asStringValId());
        } else if (args[0].isStringId() && ctx_->vm->getHeap().string(args[0].asStringId())) {
          source = *ctx_->vm->getHeap().string(args[0].asStringId());
        } else {
          source = args[0].toString();
        }

        if (source.empty()) return Value::makeNull();

        try {
          parser::Parser parser;
          auto program = parser.produceAST(source);
          if (!program || parser.hasErrors()) {
            std::string errMsg = "bytecode: parse error";
            if (!parser.getErrors().empty()) errMsg += ": " + parser.getErrors()[0].message;
            auto ref = ctx_->vm->getHeap().allocateString(std::move(errMsg));
            return Value::makeStringId(ref.id);
          }

	ByteCompiler byteCompiler;
	auto chunk = byteCompiler.compile(*program);
          if (!chunk) {
            auto ref = ctx_->vm->getHeap().allocateString("bytecode: compilation failed");
            return Value::makeStringId(ref.id);
          }

          // Format bytecode to string
          std::string result;
          for (size_t i = 0; i < chunk->getFunctionCount(); ++i) {
            const auto* func = chunk->getFunction(i);
            if (!func) continue;
            result += "fn " + func->name + "(";
            for (size_t p = 0; p < func->param_names.size(); ++p) {
              if (p > 0) result += ", ";
              result += func->param_names[p];
            }
            result += ")\n";
            for (size_t j = 0; j < func->instructions.size(); ++j) {
              const auto& instr = func->instructions[j];
              result += "  " + std::to_string(j) + ": " + opcodeNameStr(instr.opcode);
              for (const auto& op : instr.operands) {
                result += " " + op.toString();
              }
              result += "\n";
            }
          }

          auto ref = ctx_->vm->getHeap().allocateString(std::move(result));
          return Value::makeStringId(ref.id);
        } catch (const std::exception &e) {
          std::string errMsg = "bytecode: " + std::string(e.what());
          auto ref = ctx_->vm->getHeap().allocateString(std::move(errMsg));
          return Value::makeStringId(ref.id);
        }
      };

  // Global tokenize() function - lex source code and return token info
  options_.host_functions["tokenize"] =
      [this](const std::vector<Value> &args) {
        if (args.empty() || !ctx_ || !ctx_->vm) return Value::makeNull();

        std::string source;
        if (args[0].isStringValId() && ctx_->vm->getCurrentChunk()) {
          source = ctx_->vm->getCurrentChunk()->getString(args[0].asStringValId());
        } else if (args[0].isStringId() && ctx_->vm->getHeap().string(args[0].asStringId())) {
          source = *ctx_->vm->getHeap().string(args[0].asStringId());
        } else {
          source = args[0].toString();
        }

        if (source.empty()) return Value::makeNull();

        try {
          Lexer lexer(source);
          auto tokens = lexer.tokenize();

          auto arrRef = ctx_->vm->getHeap().allocateArray();
          auto* arr = ctx_->vm->getHeap().array(arrRef.id);
          for (const auto& tok : tokens) {
            // Skip EOF token
            if (tok.value == "EndOfFile") continue;
            // Create token object: {raw, value, line, column}
            auto objRef = ctx_->vm->getHeap().allocateObject();
            auto* obj = ctx_->vm->getHeap().object(objRef.id);
            obj->set("raw", Value::makeStringId(ctx_->vm->getHeap().allocateString(tok.raw).id));
            obj->set("value", Value::makeStringId(ctx_->vm->getHeap().allocateString(tok.value).id));
            obj->set("line", Value::makeInt(static_cast<int64_t>(tok.line)));
            obj->set("column", Value::makeInt(static_cast<int64_t>(tok.column)));
            arr->push_back(Value::makeObjectId(objRef.id));
          }
          return Value::makeArrayId(arrRef.id);
        } catch (const std::exception &e) {
          std::string errMsg = "tokenize: " + std::string(e.what());
          auto ref = ctx_->vm->getHeap().allocateString(std::move(errMsg));
          return Value::makeStringId(ref.id);
        }
      };

  // Global print() function - resolves strings and outputs
  options_.host_functions["print"] =
      [this](const std::vector<Value> &args) {
        // Check if last arg is kwargs object (has end= or delim=)
        std::string delim = " ";
        std::string end = "\n";
        size_t argCount = args.size();

        // Check for kwargs object as last argument
        bool hasKwargs = false;
        if (!args.empty() && args.back().isObjectId()) {
          auto *kwargsObj = ctx_->vm->getHeap().object(args.back().asObjectId());
          if (kwargsObj) {
            auto itEnd = kwargsObj->find("end");
            bool foundEnd = itEnd != kwargsObj->end();
            auto itDelim = kwargsObj->find("delim");
            bool foundDelim = itDelim != kwargsObj->end();
            if (foundEnd) {
              end = ctx_->vm->resolveStringKey(itEnd->second);
            }
            if (foundDelim) {
              delim = ctx_->vm->resolveStringKey(itDelim->second);
            }
            // Only treat as kwargs if it has at least one of end/delim
            if (foundEnd || foundDelim) {
              hasKwargs = true;
            }
          }
        }
        if (hasKwargs) {
          argCount--; // Don't count kwargs as a value to print
        }

        if (!ctx_ || !ctx_->vm) {
          for (size_t i = 0; i < argCount; ++i) {
            if (i > 0) std::cout << delim;
            std::cout << args[i].toString();
          }
          std::cout << end << std::flush;
          return Value::makeNull();
        }
        for (size_t i = 0; i < argCount; ++i) {
          if (i > 0) std::cout << delim;
          if (args[i].isStringValId() || args[i].isStringId()) {
            std::cout << ctx_->vm->resolveStringKey(args[i]);
          } else {
            std::string s = ctx_->vm->toString(args[i]);
            std::cout << s;
          }
        }
        std::cout << end << std::flush;
        return Value::makeNull();
      };

  // Global println() function - like print but adds newline
  options_.host_functions["println"] =
      [this](const std::vector<Value> &args) {
        if (!ctx_ || !ctx_->vm) {
          for (const auto &arg : args) {
            std::cout << arg.toString();
          }
          std::cout << std::endl;
          return Value::makeNull();
        }
        for (const auto &arg : args) {
          if (arg.isStringValId() || arg.isStringId()) {
            std::cout << ctx_->vm->resolveStringKey(arg);
          } else {
            std::cout << ctx_->vm->toString(arg);
          }
        }
        std::cout << std::endl;
 return Value::makeNull();
 };

 // File system methods (dispatch to fs.* functions)

  // System methods (dispatch to system.* functions)

  // Process methods (dispatch to process.* functions)


  // Stdlib module methods (http, io, json, fs, net, time, math, os, env)

  // Chain method aliases (LINQ-style naming)
  auto truthy = [](const Value &value) -> bool {
    if (value.isNull())
      return false;
    if (value.isBool())
      return value.asBool();
    if (value.isInt())
      return value.asInt() != 0;
    if (value.isDouble())
      return value.asDouble() != 0.0;
    if (value.isStringValId())
      return !value.toString().empty();
    return true;
  };

 // Array methods (for namespace-style calls like array.map(arr, fn))
  options_.host_functions["array.len"] =
      [this](const std::vector<Value> &args) {
        if (args.empty() || !args[0].isArrayId())
          return Value::makeNull();
        return Value::makeInt(
            static_cast<int64_t>(ctx_->vm->getHostArrayLength(ArrayRef{args[0].asArrayId()})));
      };
  options_.host_functions["array.push"] =
      [this](const std::vector<Value> &args) {
        if (args.size() < 2 || !args[0].isArrayId())
          return Value::makeNull();
        ctx_->vm->pushHostArrayValue(ArrayRef{args[0].asArrayId()}, args[1]);
        return args[0];
      };
  options_.host_functions["array.pop"] =
      [this](const std::vector<Value> &args) {
        if (args.empty() || !args[0].isArrayId())
          return Value::makeNull();
        return ctx_->vm->popHostArrayValue(ArrayRef{args[0].asArrayId()});
      };
  options_.host_functions["array.has"] =
      [this](const std::vector<Value> &args) {
        if (args.size() < 2 || !args[0].isArrayId())
          return Value::makeBool(false);
        return Value(
            ctx_->vm->arrayContains(ArrayRef{args[0].asArrayId()}, args[1]));
      };
  options_.host_functions["array.find"] =
      [this](const std::vector<Value> &args) {
        if (args.size() < 2 || !args[0].isArrayId())
          return Value::makeInt(static_cast<int64_t>(-1));
        auto arrRef = ArrayRef{args[0].asArrayId()};
        size_t len = ctx_->vm->getHostArrayLength(arrRef);
        for (size_t i = 0; i < len; i++) {
          if (valuesEqual(ctx_->vm->getHostArrayValue(arrRef, i), args[1])) {
            return Value::makeInt(static_cast<int64_t>(i));
          }
        }
        return Value::makeInt(static_cast<int64_t>(-1));
      };
  options_.host_functions["array.map"] =
      [this](const std::vector<Value> &args) {
        if (args.size() < 2 || !args[0].isArrayId())
          return Value::makeNull();
        auto arrRef = ArrayRef{args[0].asArrayId()};
        auto resultRef = ctx_->vm->createHostArray();
        size_t len = ctx_->vm->getHostArrayLength(arrRef);
        for (size_t i = 0; i < len; i++) {
          auto elem = ctx_->vm->getHostArrayValue(arrRef, i);
          auto mapped = ctx_->vm->callFunction(args[1], {elem});
          ctx_->vm->pushHostArrayValue(resultRef, mapped);
        }
        return Value::makeArrayId(resultRef.id);
      };
  options_.host_functions["array.filter"] =
      [this](const std::vector<Value> &args) {
        if (args.size() < 2 || !args[0].isArrayId())
          return Value::makeNull();
        auto arrRef = ArrayRef{args[0].asArrayId()};
        auto resultRef = ctx_->vm->createHostArray();
        size_t len = ctx_->vm->getHostArrayLength(arrRef);
        for (size_t i = 0; i < len; i++) {
          auto elem = ctx_->vm->getHostArrayValue(arrRef, i);
          auto keep = ctx_->vm->callFunction(args[1], {elem});
          if (ctx_->vm->isTruthy(keep)) {
            ctx_->vm->pushHostArrayValue(resultRef, elem);
          }
        }
        return Value::makeArrayId(resultRef.id);
      };
  options_.host_functions["array.reduce"] =
      [this](const std::vector<Value> &args) {
        if (args.size() < 3 || !args[0].isArrayId())
          return Value::makeNull();
        auto arrRef = ArrayRef{args[0].asArrayId()};
        Value acc = args[2];
        size_t len = ctx_->vm->getHostArrayLength(arrRef);
        for (size_t i = 0; i < len; i++) {
          auto elem = ctx_->vm->getHostArrayValue(arrRef, i);
          acc = ctx_->vm->callFunction(args[1], {acc, elem});
        }
        return acc;
      };
  options_.host_functions["array.foreach"] =
      [this](const std::vector<Value> &args) {
        if (args.size() < 2 || !args[0].isArrayId())
          return Value::makeNull();
        auto arrRef = ArrayRef{args[0].asArrayId()};
        size_t len = ctx_->vm->getHostArrayLength(arrRef);
        for (size_t i = 0; i < len; i++) {
          auto elem = ctx_->vm->getHostArrayValue(arrRef, i);
          ctx_->vm->callFunction(args[1], {elem});
        }
        return Value::makeNull();
      };
  options_.host_functions["array.sort"] =
      [this](const std::vector<Value> &args) {
        if (args.empty() || !args[0].isArrayId())
          return Value::makeNull();
        auto arrRef = ArrayRef{args[0].asArrayId()};
        size_t len = ctx_->vm->getHostArrayLength(arrRef);
        for (size_t i = 0; i < len; i++) {
          for (size_t j = 0; j < len - i - 1; j++) {
            auto a = ctx_->vm->getHostArrayValue(arrRef, j);
            auto b = ctx_->vm->getHostArrayValue(arrRef, j + 1);
            bool swap = false;
            if (args.size() >= 2) {
              auto cmp = ctx_->vm->callFunction(args[1], {a, b});
              if (cmp.isBool()) {
                swap = cmp.asBool();
              }
            } else {
              if (a.isInt() &&
                  b.isInt()) {
                swap = a.asInt() > b.asInt();
              }
            }
            if (swap) {
              auto temp = a;
              ctx_->vm->setHostArrayValue(arrRef, j, b);
              ctx_->vm->setHostArrayValue(arrRef, j + 1, temp);
            }
          }
        }
        return Value::makeArrayId(arrRef.id);
      };

  // any.in(value, container) - membership test
  options_.host_functions["string.len"] =
      [this](const std::vector<Value> &args) {
        if (args.empty() ||
            (!args[0].isStringValId() && !args[0].isStringId()))
          return Value::makeNull();
        return Value(int64_t(args[0].toString().length()));
      };
  options_.host_functions["string.trim"] =
      [this](const std::vector<Value> &args) {
        if (args.empty() ||
            (!args[0].isStringValId() && !args[0].isStringId()))
          return Value::makeNull();
        std::string s = args[0].toString();
        size_t start = s.find_first_not_of(" \t\n\r");
        if (start == std::string::npos)
          return Value::makeNull(); // TODO: string pool integration
        size_t end = s.find_last_not_of(" \t\n\r");
        // TODO: string pool integration - for now return original
        (void)end;
        return args[0];
      };
  options_.host_functions["string.upper"] =
      [this](const std::vector<Value> &args) {
        if (args.empty() ||
            (!args[0].isStringValId() && !args[0].isStringId()))
          return Value::makeNull();
        std::string s = args[0].toString();
        std::transform(s.begin(), s.end(), s.begin(), ::toupper);
        // TODO: string pool integration - for now return original
        return args[0];
      };
  options_.host_functions["string.lower"] =
      [this](const std::vector<Value> &args) {
        if (args.empty() ||
            (!args[0].isStringValId() && !args[0].isStringId()))
          return Value::makeNull();
        std::string s = args[0].toString();
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        // TODO: string pool integration - for now return original
        return args[0];
      };
  options_.host_functions["string.includes"] =
      [this](const std::vector<Value> &args) {
        if (args.size() < 2 ||
            (!args[0].isStringValId() && !args[0].isStringId()) ||
            (!args[1].isStringValId() && !args[1].isStringId()))
          return Value::makeBool(false);
        const std::string &s = args[0].toString();
        const std::string &sub = args[1].toString();
        return Value(s.find(sub) != std::string::npos);
      };
  options_.host_functions["string.startswith"] =
      [this](const std::vector<Value> &args) {
        if (args.size() < 2 ||
            (!args[0].isStringValId() && !args[0].isStringId()) ||
            (!args[1].isStringValId() && !args[1].isStringId()))
          return Value::makeBool(false);
        const std::string &s = args[0].toString();
        const std::string &pre = args[1].toString();
        return Value(s.size() >= pre.size() &&
                             s.compare(0, pre.size(), pre) == 0);
      };
  options_.host_functions["string.endswith"] =
      [this](const std::vector<Value> &args) {
        if (args.size() < 2 ||
            (!args[0].isStringValId() && !args[0].isStringId()) ||
            (!args[1].isStringValId() && !args[1].isStringId()))
          return Value::makeBool(false);
        const std::string &s = args[0].toString();
        const std::string &suf = args[1].toString();
        return Value(
            s.size() >= suf.size() &&
            s.compare(s.size() - suf.size(), suf.size(), suf) == 0);
      };

  // String print - just output the string value
  options_.host_functions["string.print"] =
      [this](const std::vector<Value> &args) {
        if (args.empty()) return Value::makeNull();
        std::string s = ctx_->vm->resolveStringKey(args[0]);
        std::cout << s << std::flush;
        return Value::makeNull();
      };

  // Object print - output object representation
  options_.host_functions["object.print"] =
      [this](const std::vector<Value> &args) {
        if (args.empty()) return Value::makeNull();
        std::cout << args[0].toString() << std::flush;
        return Value::makeNull();
      };

  // Struct field access
  // TODO: Implement field name to index lookup in VM
  // For now, these are disabled since getStructField/setStructField expect
  // numeric indices, not string field names
  /*
  options_.host_functions["struct.get"] =
      [this](const std::vector<Value> &args) {
        if (args.size() < 2 || !args[0].isStructId() ||
            !args[1].isStringValId())
          return Value::makeNull();
        auto structRef = StructRef{args[0].asStructId(), 0};
        const std::string &fieldName = args[1].toString();
        // Need VM method to lookup field index by name
        return ctx_->vm->getStructField(structRef, 0); // placeholder
      };
  options_.host_functions["struct.set"] =
      [this](const std::vector<Value> &args) {
        if (args.size() < 3 || !args[0].isStructId() ||
            !args[1].isStringValId())
          return Value::makeNull();
        auto structRef = StructRef{args[0].asStructId(), 0};
        const std::string &fieldName = args[1].toString();
        // Need VM method to lookup field index by name
        ctx_->vm->setStructField(structRef, 0, args[2]); // placeholder
        return args[2];
      };
  */

  // Object methods (for any.* dispatch)
  options_.host_functions["object.len"] =
      [this](const std::vector<Value> &args) {
        if (args.empty() || !args[0].isObjectId())
          return Value::makeNull();
        int64_t count = 0;
        auto iterRef = ctx_->vm->createIterator(args[0]);
        while (true) {
          auto step = ctx_->vm->iteratorNext(iterRef);
          if (!step.isObjectId())
            break;
          auto stepObj = ObjectRef{step.asObjectId(), true};
          auto done = ctx_->vm->getHostObjectField(stepObj, "done");
          if (done.isBool() && done.asBool())
            break;
          count++;
        }
        return Value(count);
      };
  options_.host_functions["object.has"] =
      [this](const std::vector<Value> &args) {
        if (args.size() < 2 || !args[0].isObjectId() ||
            !args[1].isStringValId())
          return Value::makeBool(false);
        return Value(ctx_->vm->objectHasKey(
            ObjectRef{args[0].asObjectId(), true}, args[1].toString()));
      };
  options_.host_functions["object.get"] =
      [this](const std::vector<Value> &args) {
        if (args.size() < 2 || !args[0].isObjectId())
          return Value::makeNull();
        std::string key = ctx_->vm->resolveStringKey(args[1]);
        auto result = ctx_->vm->getHostObjectField(ObjectRef{args[0].asObjectId(), true}, key);
        return result;
      };
  options_.host_functions["object.set"] =
      [this](const std::vector<Value> &args) {
        if (args.size() < 3 || !args[0].isObjectId())
          return Value::makeNull();
        std::string key = ctx_->vm->resolveStringKey(args[1]);
        ctx_->vm->setHostObjectField(ObjectRef{args[0].asObjectId(), true},
                                     key, args[2]);
        return args[2];
      };
  options_.host_functions["object.keys"] =
      [this](const std::vector<Value> &args) {
        if (args.empty() || !args[0].isObjectId())
          return Value::makeNull();
        auto result = ctx_->vm->createHostArray();
        auto iterRef = ctx_->vm->createIterator(args[0]);
        while (true) {
          auto step = ctx_->vm->iteratorNext(iterRef);
          if (!step.isObjectId())
            break;
          auto stepObj = ObjectRef{step.asObjectId(), true};
          auto done = ctx_->vm->getHostObjectField(stepObj, "done");
          if (done.isBool() && done.asBool())
            break;
          auto key = ctx_->vm->getHostObjectField(stepObj, "key");
          if (key.isStringValId()) {
            ctx_->vm->pushHostArrayValue(result, key);
          }
        }
        return Value::makeObjectId(result.id);
      };
  options_.host_functions["object.values"] =
      [this](const std::vector<Value> &args) {
        if (args.empty() || !args[0].isObjectId())
          return Value::makeNull();
        auto result = ctx_->vm->createHostArray();
        auto iterRef = ctx_->vm->createIterator(args[0]);
        while (true) {
          auto step = ctx_->vm->iteratorNext(iterRef);
          if (!step.isObjectId())
            break;
          auto stepObj = ObjectRef{step.asObjectId(), true};
          auto done = ctx_->vm->getHostObjectField(stepObj, "done");
          if (done.isBool() && done.asBool())
            break;
          auto val = ctx_->vm->getHostObjectField(stepObj, "value");
          ctx_->vm->pushHostArrayValue(result, val);
        }
        return Value::makeObjectId(result.id);
      };

  // LINQ-style filter and map functions for query expressions
  options_.host_functions["filter"] =
      [this](const std::vector<Value> &args) {
        if (args.size() < 2 ||
            !args[1].isHostFuncId()) {
          return Value::makeNull();
        }
        const auto &iterable = args[0];
        const auto &predicate = args[1];
        const std::string &fnName = HostFunctionRef{predicate.toString()}.name;

        ArrayRef result = ctx_->vm->createHostArray();

        // Create iterator
        IteratorRef iterRef = ctx_->vm->createIterator(iterable);

        while (true) {
          auto iterResult = ctx_->vm->iteratorNext(iterRef);
          if (!iterResult.isObjectId())
            break;
          auto resultObjRef = ObjectRef{iterResult.asObjectId(), true};

          auto doneVal = ctx_->vm->getHostObjectField(resultObjRef, "done");
          if (doneVal.isBool() && doneVal.asBool())
            break;

          auto valueVal = ctx_->vm->getHostObjectField(resultObjRef, "value");
          if (valueVal.isNull())
            continue;

          // Call predicate
          std::vector<Value> predArgs{valueVal};
          auto predResult = ctx_->vm->callFunction(predicate, predArgs);

          if (predResult.isBool() &&
              predResult.asBool()) {
            ctx_->vm->pushHostArrayValue(result, valueVal);
          }
        }
        return Value::makeObjectId(result.id);
      };

  options_.host_functions["map"] = [this](
                                       const std::vector<Value> &args) {
    if (args.size() < 2 || !args[1].isHostFuncId()) {
      return Value::makeNull();
    }
    const auto &iterable = args[0];
    const auto &transform = args[1];
    const std::string &fnName = HostFunctionRef{transform.toString()}.name;
    (void)fnName;

    ArrayRef result = ctx_->vm->createHostArray();

    // Create iterator
    IteratorRef iterRef = ctx_->vm->createIterator(iterable);

    while (true) {
      auto iterResult = ctx_->vm->iteratorNext(iterRef);
      if (!iterResult.isObjectId())
        break;
      auto resultObjRef = ObjectRef{iterResult.asObjectId(), true};

      auto doneVal = ctx_->vm->getHostObjectField(resultObjRef, "done");
      if (doneVal.isBool() && doneVal.asBool())
        break;

      auto valueVal = ctx_->vm->getHostObjectField(resultObjRef, "value");
      if (valueVal.isNull())
        continue;

      // Call transform
      std::vector<Value> transArgs{valueVal};
      auto transResult = ctx_->vm->callFunction(transform, transArgs);

      ctx_->vm->pushHostArrayValue(result, transResult);
    }
    return Value::makeObjectId(result.id);
  };

  // Terminal operation - convert to set
  options_.host_functions["set"] = [this](
                                       const std::vector<Value> &args) {
    if (args.empty()) {
      return Value::makeNull();
    }
    const auto &iterable = args[0];

    // Create set and add unique elements
    ObjectRef result = ctx_->vm->createHostObject();
    ctx_->vm->setHostObjectField(result, "__set_marker__", Value::makeBool(true));
    IteratorRef iterRef = ctx_->vm->createIterator(iterable);

    while (true) {
      auto iterResult = ctx_->vm->iteratorNext(iterRef);
      if (!iterResult.isObjectId())
        break;
      auto resultObjRef = ObjectRef{iterResult.asObjectId(), true};

      auto doneVal = ctx_->vm->getHostObjectField(resultObjRef, "done");
      if (doneVal.isBool() && doneVal.asBool())
        break;

      auto valueVal = ctx_->vm->getHostObjectField(resultObjRef, "value");
      if (!valueVal.isNull()) {
        ctx_->vm->setHostObjectField(result, std::to_string(0),
                                     valueVal);
      }
    }
    return Value::makeObjectId(result.id);
  };

  // Terminal operation - convert to object with key-value pairs
  options_.host_functions["object"] =
      [this](const std::vector<Value> &args) {
        if (args.empty()) {
          return Value::makeNull();
        }
        const auto &iterable = args[0];

        ObjectRef result = ctx_->vm->createHostObject();
        IteratorRef iterRef = ctx_->vm->createIterator(iterable);

        while (true) {
          auto iterResult = ctx_->vm->iteratorNext(iterRef);
          if (!iterResult.isObjectId())
            break;
          auto resultObjRef = ObjectRef{iterResult.asObjectId(), true};

          auto doneVal = ctx_->vm->getHostObjectField(resultObjRef, "done");
          if (doneVal.isBool() && doneVal.asBool())
            break;

          auto pairVal = ctx_->vm->getHostObjectField(resultObjRef, "value");
          // Expect pairs like [key, value] or {key: ..., value: ...}
          if (pairVal.isArrayId()) {
            auto arr = ArrayRef{pairVal.asArrayId()};
            auto key = ctx_->vm->getHostArrayValue(arr, 0);
            auto val = ctx_->vm->getHostArrayValue(arr, 1);
            if (key.isStringValId()) {
              ctx_->vm->setHostObjectField(result, key.toString(),
                                           val);
            }
          }
        }
        return Value::makeObjectId(result.id);
      };

  // Aggregation - sum all values
  options_.host_functions["sum"] =
      [this](const std::vector<Value> &args) {
        if (args.empty()) {
          return Value::makeNull();
        }
        const auto &iterable = args[0];

        double total = 0;
        IteratorRef iterRef = ctx_->vm->createIterator(iterable);

        while (true) {
          auto iterResult = ctx_->vm->iteratorNext(iterRef);
          if (!iterResult.isObjectId())
            break;
          auto resultObjRef = ObjectRef{iterResult.asObjectId(), true};

          auto doneVal = ctx_->vm->getHostObjectField(resultObjRef, "done");
          if (doneVal.isBool() && doneVal.asBool())
            break;

          auto valueVal = ctx_->vm->getHostObjectField(resultObjRef, "value");
          if (valueVal.isInt()) {
            total += valueVal.asInt();
          } else if (valueVal.isDouble()) {
            total += valueVal.asDouble();
          }
        }
        return Value(total);
      };

  // Aggregation - find max value
  options_.host_functions["max"] =
      [this](const std::vector<Value> &args) {
        if (args.empty()) {
          return Value::makeNull();
        }
        const auto &iterable = args[0];

        double maxVal = std::numeric_limits<double>::lowest();
        bool hasValue = false;

        IteratorRef iterRef = ctx_->vm->createIterator(iterable);

        while (true) {
          auto iterResult = ctx_->vm->iteratorNext(iterRef);
          if (!iterResult.isObjectId())
            break;
          auto resultObjRef = ObjectRef{iterResult.asObjectId(), true};

          auto doneVal = ctx_->vm->getHostObjectField(resultObjRef, "done");
          if (doneVal.isBool() && doneVal.asBool())
            break;

          auto valueVal = ctx_->vm->getHostObjectField(resultObjRef, "value");
          double num = 0;
          if (valueVal.isInt()) {
            num = valueVal.asInt();
          } else if (valueVal.isDouble()) {
            num = valueVal.asDouble();
          } else {
            continue;
          }

          if (!hasValue || num > maxVal) {
            maxVal = num;
            hasValue = true;
          }
        }
        return hasValue ? Value(maxVal) : Value::makeNull();
      };

  // Aggregation - find min value
  options_.host_functions["min"] =
      [this](const std::vector<Value> &args) {
        if (args.empty()) {
          return Value::makeNull();
        }
        const auto &iterable = args[0];

        double minVal = std::numeric_limits<double>::max();
        bool hasValue = false;

        IteratorRef iterRef = ctx_->vm->createIterator(iterable);

        while (true) {
          auto iterResult = ctx_->vm->iteratorNext(iterRef);
          if (!iterResult.isObjectId())
            break;
          auto resultObjRef = ObjectRef{iterResult.asObjectId(), true};

          auto doneVal = ctx_->vm->getHostObjectField(resultObjRef, "done");
          if (doneVal.isBool() && doneVal.asBool())
            break;

          auto valueVal = ctx_->vm->getHostObjectField(resultObjRef, "value");
          double num = 0;
          if (valueVal.isInt()) {
            num = valueVal.asInt();
          } else if (valueVal.isDouble()) {
            num = valueVal.asDouble();
          } else {
            continue;
          }

          if (!hasValue || num < minVal) {
            minVal = num;
            hasValue = true;
          }
        }
        return hasValue ? Value(minVal) : Value::makeNull();
      };

  // Advanced LINQ methods - orderby (sort with key selector)
  options_.host_functions["orderby"] =
      [this](const std::vector<Value> &args) {
        if (args.empty()) {
          return Value::makeNull();
        }
        const auto &iterable = args[0];

        // Collect all elements first
        std::vector<Value> elements;
        IteratorRef iterRef = ctx_->vm->createIterator(iterable);

        while (true) {
          auto iterResult = ctx_->vm->iteratorNext(iterRef);
          if (!iterResult.isObjectId())
            break;
          auto resultObjRef = ObjectRef{iterResult.asObjectId(), true};

          auto doneVal = ctx_->vm->getHostObjectField(resultObjRef, "done");
          if (doneVal.isBool() && doneVal.asBool())
            break;

          auto valueVal = ctx_->vm->getHostObjectField(resultObjRef, "value");
          if (!valueVal.isNull()) {
            elements.push_back(valueVal);
          }
        }

        // Sort elements
        std::sort(elements.begin(), elements.end(),
                  [](const Value &a, const Value &b) {
                    // Numeric comparison
                    if (a.isInt() &&
                        b.isInt()) {
                      return a.asInt() < b.asInt();
                    }
                    if (a.isDouble() &&
                        b.isDouble()) {
                      return a.asDouble() < b.asDouble();
                    }
                    if (a.isInt() &&
                        b.isDouble()) {
                      return a.asInt() < b.asDouble();
                    }
                    if (a.isDouble() &&
                        b.isInt()) {
                      return a.asDouble() < b.asInt();
                    }
                    // String comparison
                    if (a.isStringValId() &&
                        b.isStringValId()) {
                      return a.toString() <
                             b.toString();
                    }
                    return false;
                  });

        // Create result array
        ArrayRef result = ctx_->vm->createHostArray();
        for (const auto &elem : elements) {
          ctx_->vm->pushHostArrayValue(result, elem);
        }
        return Value::makeObjectId(result.id);
      };

  // groupby - group elements by key selector
  options_.host_functions["groupby"] =
      [this](const std::vector<Value> &args) {
        if (args.size() < 2) {
          return Value::makeNull();
        }
        const auto &iterable = args[0];
        const auto &keySelector = args[1];

        // Group elements by key
        std::unordered_map<std::string, std::vector<Value>> groups;

        IteratorRef iterRef = ctx_->vm->createIterator(iterable);
        while (true) {
          auto iterResult = ctx_->vm->iteratorNext(iterRef);
          if (!iterResult.isObjectId())
            break;
          auto resultObjRef = ObjectRef{iterResult.asObjectId(), true};

          auto doneVal = ctx_->vm->getHostObjectField(resultObjRef, "done");
          if (doneVal.isBool() && doneVal.asBool())
            break;

          auto valueVal = ctx_->vm->getHostObjectField(resultObjRef, "value");
          if (valueVal.isNull())
            continue;

          // Get key from selector
          std::vector<Value> selectorArgs{valueVal};
          auto keyVal = ctx_->vm->callFunction(keySelector, selectorArgs);

          std::string keyStr;
          if (keyVal.isStringValId()) {
            keyStr = keyVal.toString();
          } else if (keyVal.isInt()) {
            keyStr = std::to_string(keyVal.asInt());
          } else if (keyVal.isDouble()) {
            keyStr = std::to_string(keyVal.asDouble());
          } else {
            keyStr = "null";
          }

          groups[keyStr].push_back(valueVal);
        }

        // Create result object with groups
        ObjectRef result = ctx_->vm->createHostObject();
        for (const auto &pair : groups) {
          ArrayRef groupArray = ctx_->vm->createHostArray();
          for (const auto &elem : pair.second) {
            ctx_->vm->pushHostArrayValue(groupArray, elem);
          }
          ctx_->vm->setHostObjectField(result, pair.first,
                                       Value::makeArrayId(groupArray.id));
        }
        return Value::makeObjectId(result.id);
      };

  // concat - concatenate two iterables
  options_.host_functions["concat"] =
      [this](const std::vector<Value> &args) {
        if (args.size() < 2) {
          return Value::makeNull();
        }

        ArrayRef result = ctx_->vm->createHostArray();

        // Add elements from first iterable
        IteratorRef iterRef1 = ctx_->vm->createIterator(args[0]);
        while (true) {
          auto iterResult = ctx_->vm->iteratorNext(iterRef1);
          if (!iterResult.isObjectId())
            break;
          auto resultObjRef = ObjectRef{iterResult.asObjectId(), true};

          auto doneVal = ctx_->vm->getHostObjectField(resultObjRef, "done");
          if (doneVal.isBool() && doneVal.asBool())
            break;

          auto valueVal = ctx_->vm->getHostObjectField(resultObjRef, "value");
          if (!valueVal.isNull()) {
            ctx_->vm->pushHostArrayValue(result, valueVal);
          }
        }

        // Add elements from second iterable
        IteratorRef iterRef2 = ctx_->vm->createIterator(args[1]);
        while (true) {
          auto iterResult = ctx_->vm->iteratorNext(iterRef2);
          if (!iterResult.isObjectId())
            break;
          auto resultObjRef = ObjectRef{iterResult.asObjectId(), true};

          auto doneVal = ctx_->vm->getHostObjectField(resultObjRef, "done");
          if (doneVal.isBool() && doneVal.asBool())
            break;

          auto valueVal = ctx_->vm->getHostObjectField(resultObjRef, "value");
          if (!valueVal.isNull()) {
            ctx_->vm->pushHostArrayValue(result, valueVal);
          }
        }

        return Value::makeObjectId(result.id);
      };

  // merge - merge two objects
  options_.host_functions["merge"] =
      [this](const std::vector<Value> &args) {
        if (args.size() < 2) {
          return Value::makeNull();
        }

        ObjectRef result = ctx_->vm->createHostObject();

        // Copy properties from first object
        IteratorRef iterRef1 = ctx_->vm->createIterator(args[0]);
        while (true) {
          auto iterResult = ctx_->vm->iteratorNext(iterRef1);
          if (!iterResult.isObjectId())
            break;
          auto resultObjRef = ObjectRef{iterResult.asObjectId(), true};

          auto doneVal = ctx_->vm->getHostObjectField(resultObjRef, "done");
          if (doneVal.isBool() && doneVal.asBool())
            break;

          auto keyVal = ctx_->vm->getHostObjectField(resultObjRef, "key");
          auto valueVal = ctx_->vm->getHostObjectField(resultObjRef, "value");
          if (!keyVal.isNull() &&
              !valueVal.isNull()) {
            ctx_->vm->setHostObjectField(result, keyVal.toString(),
                                         valueVal);
          }
        }

        // Copy properties from second object
        IteratorRef iterRef2 = ctx_->vm->createIterator(args[1]);
        while (true) {
          auto iterResult = ctx_->vm->iteratorNext(iterRef2);
          if (!iterResult.isObjectId())
            break;
          auto resultObjRef = ObjectRef{iterResult.asObjectId(), true};

          auto doneVal = ctx_->vm->getHostObjectField(resultObjRef, "done");
          if (doneVal.isBool() && doneVal.asBool())
            break;

          auto keyVal = ctx_->vm->getHostObjectField(resultObjRef, "key");
          auto valueVal = ctx_->vm->getHostObjectField(resultObjRef, "value");
          if (!keyVal.isNull() &&
              !valueVal.isNull()) {
            ctx_->vm->setHostObjectField(result, keyVal.toString(),
                                         valueVal);
          }
        }

        return Value::makeObjectId(result.id);
      };

  // join - join two iterables on matching keys
  options_.host_functions["join"] =
      [this](const std::vector<Value> &args) {
        if (args.size() < 4) {
          return Value::makeNull();
        }
        // args: inner, outerKeySelector, innerKeySelector, resultSelector
        const auto &inner = args[0];
        const auto &outerKeySelector = args[1];
        const auto &innerKeySelector = args[2];
        const auto &resultSelector = args[3];

        // For now, return a simplified join (inner join)
        // This is a placeholder - full join requires more context
        ArrayRef result = ctx_->vm->createHostArray();

        // Create map of inner elements by key
        std::unordered_map<std::string, std::vector<Value>> innerMap;
        IteratorRef innerIterRef = ctx_->vm->createIterator(inner);
        while (true) {
          auto innerIterResult = ctx_->vm->iteratorNext(innerIterRef);
          if (!innerIterResult.isObjectId())
            break;
          auto innerResultObjRef = ObjectRef{innerIterResult.asObjectId(), true};

          auto innerDoneVal =
              ctx_->vm->getHostObjectField(innerResultObjRef, "done");
          if (innerDoneVal.isBool() &&
              innerDoneVal.asBool())
            break;

          auto innerValueVal =
              ctx_->vm->getHostObjectField(innerResultObjRef, "value");
          if (innerValueVal.isNull())
            continue;

          // Get key from selector
          std::vector<Value> innerSelectorArgs{innerValueVal};
          auto innerKeyVal =
              ctx_->vm->callFunction(innerKeySelector, innerSelectorArgs);

          std::string innerKeyStr;
          if (innerKeyVal.isStringValId()) {
            innerKeyStr = innerKeyVal.toString();
          } else if (innerKeyVal.isInt()) {
            innerKeyStr = std::to_string(innerKeyVal.asInt());
          } else if (innerKeyVal.isDouble()) {
            innerKeyStr = std::to_string(innerKeyVal.asDouble());
          } else {
            innerKeyStr = "null";
          }

          innerMap[innerKeyStr].push_back(innerValueVal);
        }

        // Iterate over outer elements and join
        IteratorRef outerIterRef = ctx_->vm->createIterator(args[0]);
        while (true) {
          auto outerIterResult = ctx_->vm->iteratorNext(outerIterRef);
          if (!outerIterResult.isObjectId())
            break;
          auto outerResultObjRef = ObjectRef{outerIterResult.asObjectId(), true};

          auto outerDoneVal =
              ctx_->vm->getHostObjectField(outerResultObjRef, "done");
          if (outerDoneVal.isBool() &&
              outerDoneVal.asBool())
            break;

          auto outerValueVal =
              ctx_->vm->getHostObjectField(outerResultObjRef, "value");
          if (outerValueVal.isNull())
            continue;

          // Get key from selector
          std::vector<Value> outerSelectorArgs{outerValueVal};
          auto outerKeyVal =
              ctx_->vm->callFunction(outerKeySelector, outerSelectorArgs);

          std::string outerKeyStr;
          if (outerKeyVal.isStringValId()) {
            outerKeyStr = outerKeyVal.toString();
          } else if (outerKeyVal.isInt()) {
            outerKeyStr = std::to_string(outerKeyVal.asInt());
          } else if (outerKeyVal.isDouble()) {
            outerKeyStr = std::to_string(outerKeyVal.asDouble());
          } else {
            outerKeyStr = "null";
          }

          // Join with inner elements
          if (innerMap.find(outerKeyStr) != innerMap.end()) {
            for (const auto &innerValue : innerMap[outerKeyStr]) {
              // Call result selector
              std::vector<Value> resultArgs{outerValueVal, innerValue};
              auto resultVal =
                  ctx_->vm->callFunction(resultSelector, resultArgs);
              ctx_->vm->pushHostArrayValue(result, resultVal);
            }
          }
        }

        return Value::makeObjectId(result.id);
      };

  // Standalone aliases for pipeline support (e.g., clipboard.get | upper |
  // trim)
  options_.host_functions["upper"] =
      [this](const std::vector<Value> &args) {
        if (args.empty() ||
            (!args[0].isStringValId() && !args[0].isStringId()))
          return Value::makeNull();
        std::string s = args[0].toString();
        std::transform(s.begin(), s.end(), s.begin(), ::toupper);
        // TODO: string pool integration - for now return original
        return args[0];
      };
  options_.host_functions["lower"] =
      [this](const std::vector<Value> &args) {
        if (args.empty() ||
            (!args[0].isStringValId() && !args[0].isStringId()))
          return Value::makeNull();
        std::string s = args[0].toString();
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        // TODO: string pool integration - for now return original
        return args[0];
      };
  options_.host_functions["trim"] =
      [this](const std::vector<Value> &args) {
        if (args.empty() ||
            (!args[0].isStringValId() && !args[0].isStringId()))
          return Value::makeNull();
        std::string s = args[0].toString();
        size_t start = s.find_first_not_of(" \t\n\r");
        if (start == std::string::npos)
          return Value::makeNull(); // TODO: return empty string
        size_t end = s.find_last_not_of(" \t\n\r");
        // TODO: string pool integration - for now return original
        (void)start; (void)end;
        return args[0];
};

 // Helper: check if a value is callable (host func, closure, or function object)
  auto isCallable = [](const Value &v) {
    return v.isHostFuncId() || v.isClosureId() || v.isFunctionObjId();
  };

  // any(iterable, predicate) - check if any element satisfies predicate
  options_.host_functions["any"] = [this, isCallable](
                                       const std::vector<Value> &args) {
    if (args.size() < 2 || !isCallable(args[1])) {
      return Value::makeBool(false);
    }

    const auto &iterable = args[0];
    const auto &predicate = args[1];

    // Create iterator
    IteratorRef iterRef = ctx_->vm->createIterator(iterable);

    // Iterate and check predicate
    while (true) {
      auto result = ctx_->vm->iteratorNext(iterRef);

      // Check if done
      if (!result.isObjectId()) {
        return Value::makeBool(false);
      }
      auto resultObjRef = ObjectRef{result.asObjectId(), true};

      // Get done flag
      auto doneVal = ctx_->vm->getHostObjectField(resultObjRef, "done");
      if (doneVal.isBool() && doneVal.asBool()) {
        return Value::makeBool(false); // Reached end, no match found
      }

      // Get value
      auto valueVal = ctx_->vm->getHostObjectField(resultObjRef, "value");

      // Call predicate with value
      std::vector<Value> predArgs;
      predArgs.push_back(valueVal);
      auto predResult = ctx_->vm->callFunction(predicate, predArgs);

      if (predResult.isBool() && predResult.asBool()) {
        return Value::makeBool(true); // Found a match
      }
      // For truthiness check (non-bool values that are truthy)
      if (!predResult.isNull() && !predResult.isBool()) {
        if (predResult.isInt() && predResult.asInt() != 0) {
          return Value::makeBool(true);
        }
      }
    }
  };

  // all(iterable, predicate) - check if all elements satisfy predicate
  options_.host_functions["all"] = [this, isCallable](
                                       const std::vector<Value> &args) {
    if (args.size() < 2 || !isCallable(args[1])) {
      return Value::makeBool(false);
    }

    const auto &iterable = args[0];
    const auto &predicate = args[1];

    // Create iterator
    IteratorRef iterRef = ctx_->vm->createIterator(iterable);

    // Iterate and check predicate - ALL must match
    bool found_any = false;
    while (true) {
      auto result = ctx_->vm->iteratorNext(iterRef);

      // Check if done
      if (!result.isObjectId()) {
        break;
      }
      auto resultObjRef = ObjectRef{result.asObjectId(), true};

      // Get done flag
      auto doneVal = ctx_->vm->getHostObjectField(resultObjRef, "done");
      if (doneVal.isBool() && doneVal.asBool()) {
        break; // Reached end
      }

      // Get value
      auto valueVal = ctx_->vm->getHostObjectField(resultObjRef, "value");
      found_any = true;

      // Call predicate with value
      std::vector<Value> predArgs;
      predArgs.push_back(valueVal);
      auto predResult = ctx_->vm->callFunction(predicate, predArgs);

      bool isTruthy = false;
      if (predResult.isBool() && predResult.asBool()) {
        isTruthy = true;
      } else if (!predResult.isNull() && !predResult.isBool()) {
        if (predResult.isInt() && predResult.asInt() != 0) {
          isTruthy = true;
        }
      }

      if (!isTruthy) {
        return Value::makeBool(false); // Found element that doesn't match
      }
    }
    return Value::makeBool(found_any);
  };

  // Type system functions - the VM's registerDefaultHostFunctions() already
  // registers a working "type" function, so we don't need to register it here.
  // Register type.of as alias for type
  options_.host_functions["type.of"] =
      [this](const std::vector<Value> &args) {
        return ctx_->vm->callFunction(Value::makeHostFuncId(ctx_->vm->getHostFunctionIndex("type")), args);
      };

  options_.host_functions["type.is"] =
      [](const std::vector<Value> &args) {
        if (args.size() < 2) {
          return Value::makeBool(false);
        }
        const auto &val = args[0];
        if (!args[1].isStringValId()) {
          return Value::makeBool(false);
        }
        std::string typeName = args[1].toString();
        if (typeName == "null")
          return Value(val.isNull());
        if (typeName == "bool")
          return Value(val.isBool());
        if (typeName == "int")
          return Value(val.isInt());
        if (typeName == "num" || typeName == "float")
          return Value(val.isDouble());
        if (typeName == "string")
          return Value(val.isStringValId());
        if (typeName == "array")
          return Value(val.isArrayId());
        if (typeName == "object")
          return Value(val.isObjectId());
        return Value::makeBool(false);
      };

  options_.host_functions["implements"] =
      [](const std::vector<Value> &args) {
        // Placeholder - full trait system requires type metadata
        // For now, return false for all checks
        (void)args;
        return Value::makeBool(false);
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
    extensionLoader_->loadExtensionByName(name);
  }
}

void HostBridge::checkTimers() {
  if (concurrencyBridge_) {
    concurrencyBridge_->checkTimers();
  }
}

} // namespace havel::compiler
