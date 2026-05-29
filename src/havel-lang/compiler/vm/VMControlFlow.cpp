#include "VM.hpp"
#include "VMInternals.hpp"
#include "../../../utils/Logger.hpp"
#include "../../utils/ErrorPrinter.hpp"
#include "../runtime/HostBridge.hpp"
#include "../runtime/RuntimeSupport.hpp"
#include "../../runtime/concurrency/DependencyTracker.hpp"
#include "../../runtime/concurrency/WatcherRegistry.hpp"
#include "../../runtime/concurrency/Fiber.hpp"
#include "../prototypes/PrototypeRegistry.hpp"
#include "core/config/ConfigManager.hpp"
#include <cmath>
#include <iostream>
#include <set>
#include <sstream>
#include <algorithm>
#include <numeric>
#include <regex>

namespace havel::compiler {

bool VM::execControlFlowOp(const Instruction &instruction) {
	switch (instruction.opcode) {
        case OpCode::CALL: {
            uint32_t arg_count = instruction.operands[0].asInt();
            if (stack.size() < static_cast<size_t>(arg_count) + 1) {
                COMPILER_THROW("Stack underflow during CALL");
            }

            std::vector<Value> args(arg_count);
            for (uint32_t i = 0; i < arg_count; ++i) {
                args[arg_count - 1 - i] = popStack();
  }
  Value callee_value = popStack();

  // Handle callable objects (Lua-style __call metamethod, or op_call operator)
  if (callee_value.isObjectId()) {
    auto *obj = heap_.object(callee_value.asObjectId());
    if (obj) {
      // Look up __call or op_call in the object and its prototype chain
      GCHeap::ObjectEntry* search = obj;
      Value callFn = Value::makeNull();
      while (search) {
				auto* val = search->get("__call");
				if (!val) val = search->get("op_call");
				if (val) {
					callFn = *val;
					break;
				}
				auto* parentVal = search->get("__proto");
				if (!parentVal) parentVal = search->get("__class");
				if (!parentVal) parentVal = search->get("__parent");
				if (parentVal && parentVal->isObjectId()) {
					search = heap_.object(parentVal->asObjectId());
				} else {
					break;
				}
			}
        if (!callFn.isNull() && (callFn.isFunctionObjId() || callFn.isClosureId() || callFn.isHostFuncId())) {
          // Call __call with self as first arg
          std::vector<Value> callArgs;
          callArgs.push_back(callee_value);
          callArgs.insert(callArgs.end(), args.begin(), args.end());
          doCall(callFn, std::move(callArgs));
          break;
        }
      }
    }

    // Handle bound methods (lightweight BoundMethod struct)
    if (callee_value.isBoundMethodId()) {
        auto *bm = heap_.boundMethod(callee_value.asBoundMethodId());
        if (bm && (bm->fn.isHostFuncId() || bm->fn.isFunctionObjId() || bm->fn.isClosureId())) {
            std::vector<Value> boundArgs;
            boundArgs.push_back(bm->self);
            boundArgs.insert(boundArgs.end(), args.begin(), args.end());
            doCall(bm->fn, std::move(boundArgs));
            break;
        }
    }

    // Handle bound method objects (from runtime member lookup)
    if (callee_value.isObjectId()) {
      auto *obj = heap_.object(callee_value.asObjectId());
      if (obj) {
        auto fnIt = obj->find("fn");
        auto selfIt = obj->find("self");
        if (fnIt != obj->end() && selfIt != obj->end() &&
            (fnIt->second.isHostFuncId() || fnIt->second.isFunctionObjId() ||
             fnIt->second.isClosureId())) {
          // Prepend self to args
          std::vector<Value> boundArgs;
          boundArgs.push_back(selfIt->second);
          boundArgs.insert(boundArgs.end(), args.begin(), args.end());
          doCall(fnIt->second, std::move(boundArgs));
          break;
        }
      }
    }

    doCall(callee_value, std::move(args));
    break;
  }

case OpCode::TAIL_CALL: {
  // Tail call optimization: reuse current frame instead of pushing new one
  uint32_t arg_count = instruction.operands[0].asInt();
  if (stack.size() < static_cast<size_t>(arg_count) + 1) {
    COMPILER_THROW("Stack underflow during TAIL_CALL");
  }

  std::vector<Value> args(arg_count);
  for (uint32_t i = 0; i < arg_count; ++i) {
    args[arg_count - 1 - i] = popStack();
  }
  Value callee_value = popStack();
  doTailCall(callee_value, std::move(args));
  break;
  }

  case OpCode::CALL_METHOD: {
    // Dispatches based on receiver type without boxing.
    if (instruction.operands.size() != 2 ||
        !instruction.operands[0].isStringValId() ||
        !instruction.operands[1].isInt()) {
      COMPILER_THROW("CALL_METHOD expects operands: <string method_name, uint32 arg_count>");
    }

            uint32_t strIndex = instruction.operands[0].asStringValId();
            const auto& cf_cm = currentFrame();
            const BytecodeChunk* resolveChunkCM = cf_cm.chunk ? cf_cm.chunk : current_chunk;
            std::string method_name;
            if (resolveChunkCM) {
                method_name = resolveChunkCM->getString(strIndex);
            }
            method_name = operatorSymbolToMethodName(method_name);
    uint32_t arg_count = instruction.operands[1].asInt();

    // Receiver is at stack top - arg_count positions down
    if (stack.size() < static_cast<size_t>(arg_count) + 1) {
      COMPILER_THROW("Stack underflow during CALL_METHOD (stack=" + std::to_string(stack.size()) + " need=" + std::to_string(arg_count + 1) + " method=" + method_name + ")");
    }

    // Peek at receiver (don't pop yet)
    std::vector<Value> temp_args;
    temp_args.reserve(arg_count);
    for (uint32_t i = 0; i < arg_count; ++i) {
      temp_args.push_back(stack.top());
      stack.pop();
    }
    Value receiver = stack.top();
    // Push args back in reverse order
    for (auto it = temp_args.rbegin(); it != temp_args.rend(); ++it) {
      pushStack(*it);
    }

    // Determine type name for dispatch
    std::string type_name;
    uint32_t host_func_idx = 0;
    bool found_host = false;
    bool found_via_module = false;
    bool isInstanceFunc = false;
    Value vm_func = Value::makeNull();
    if (receiver.isStringValId() || receiver.isStringId()) {
      type_name = "string";
    } else if (receiver.isInt()) {
      type_name = "int";
    } else if (receiver.isDouble()) {
      type_name = "float";
    } else if (receiver.isBool()) {
      type_name = "bool";
    } else if (receiver.isArrayId()) {
      type_name = "array";
    } else if (receiver.isObjectId()) {
      type_name = "object";
    } else if (receiver.isSetId()) {
      type_name = "set";
    } else if (receiver.isThreadId()) {
      type_name = "thread";
    } else if (receiver.isIntervalId()) {
      type_name = "interval";
    } else if (receiver.isTimeoutId()) {
      type_name = "timeout";
} else if (receiver.isRangeId()) {
        type_name = "range";
    } else if (receiver.isHostFuncId()) {
        // Dotted host function call: e.g. interval.start(100, fn)
        // Resolve "interval.start" by concatenating receiver name + "." + method_name
        std::string receiver_name;
        if (receiver.asHostFuncId() < host_function_names_.size()) {
            receiver_name = host_function_names_[receiver.asHostFuncId()];
        }
        std::string dotted_name = receiver_name + "." + method_name;
        for (size_t i = 0; i < host_function_names_.size(); ++i) {
            if (host_function_names_[i] == dotted_name) {
                host_func_idx = static_cast<uint32_t>(i);
                found_host = true;
                found_via_module = true; // Don't pass receiver as self
                break;
            }
        }
        if (!found_host) {
            for (uint32_t i = 0; i < arg_count; ++i) popStack();
            popStack();
            pushStack(Value::makeNull());
            break;
        }
    } else {
        for (uint32_t i = 0; i < arg_count; ++i) popStack();
        popStack(); // receiver
        pushStack(Value::makeNull());
        break;
    }

// Look up method: 0. Object instance field, 1. Host prototype, 2. Module monkey-patch, 3. Class prototype chain

    // 0. If receiver is an object, check for direct callable field FIRST.
    // Namespace objects like `process` have host function fields (e.g. `find`)
    // that must take priority over prototype methods (e.g. `object.find`).
if (receiver.isObjectId()) {
    auto *instanceObj = heap_.object(receiver.asObjectId());
    if (instanceObj) {
        auto it = instanceObj->find(method_name);
        if (it != instanceObj->end()) {
          if (it->second.isHostFuncId()) {
            host_func_idx = it->second.asHostFuncId();
            found_host = true;
              // Set found_via_module if the object is in globals (likely a module/namespace)
              for (const auto &g : globals) {
                if (g.second.isObjectId() && g.second.asObjectId() == receiver.asObjectId()) {
                  found_via_module = true;
                  break;
                }
              }
          } else if (it->second.isFunctionObjId() || it->second.isClosureId()) {
            vm_func = it->second;
            isInstanceFunc = true;
          }
        }
      }
    }

    // 0.5 Check __class/__struct prototype for class/struct methods first
    if (!found_host && vm_func.isNull() && receiver.isObjectId()) {
      auto *classProto = heap_.object(receiver.asObjectId());
      if (classProto) {
        auto *classVal = classProto->get("__class");
        if (!classVal) classVal = classProto->get("__struct");
        if (classVal && classVal->isObjectId()) {
          classProto = heap_.object(classVal->asObjectId());
        } else {
          classProto = nullptr;
        }
      }

      while (classProto) {
        auto *methodVal = classProto->get(method_name);
        if (methodVal) {
          if (methodVal->isHostFuncId()) {
            host_func_idx = methodVal->asHostFuncId();
            found_host = true;
            break;
          } else if (methodVal->isFunctionObjId() || methodVal->isClosureId()) {
            vm_func = *methodVal;
            break;
          }
        }
        auto *parentVal = classProto->get("__parent");
        if (parentVal && parentVal->isObjectId()) {
          classProto = heap_.object(parentVal->asObjectId());
        } else {
          break;
        }
      }
    }

    // 1. Try host prototype (for primitives and built-in object methods)
    if (!found_host && vm_func.isNull()) {
        auto typeIt = prototypes_.find(type_name);
        if (typeIt != prototypes_.end()) {
            auto methodIt = typeIt->second.find(method_name);
            if (methodIt != typeIt->second.end()) {
                host_func_idx = methodIt->second;
                found_host = true;
            }
        }
    }

    // 1.5 Try module object for monkey-patched methods
    if (!found_host && vm_func.isNull()) {
      // Generate capitalized version (e.g., "string" -> "String")
      std::string capName = type_name;
      if (!capName.empty()) capName[0] = static_cast<char>(std::toupper(capName[0]));

      for (const auto &modName : {type_name, capName}) {
        auto modIt = globals.find(modName);
        if (modIt != globals.end() && modIt->second.isObjectId()) {
          auto *modObj = heap_.object(modIt->second.asObjectId());
          if (modObj) {
            auto *val = modObj->get(method_name);
            if (val) {
              if (val->isHostFuncId()) {
                host_func_idx = val->asHostFuncId();
                found_host = true;
                // Monkey-patched methods in prototypes SHOULD receive 'self', so found_via_module stays false.
                break;
              } else if (val->isFunctionObjId() || val->isClosureId()) {
                vm_func = *val;
                break;
              }
            }
          }
        }
      }
    }

    if (!found_host && vm_func.isNull()) {
      // Pop args and receiver before pushing null result
      for (uint32_t i = 0; i < arg_count; ++i) popStack();
      popStack(); // receiver
      pushStack(Value::makeNull());
      break;
    }

  // Pop args and receiver
  std::vector<Value> args2(arg_count);
  for (uint32_t i = 0; i < arg_count; ++i) {
    args2[arg_count - 1 - i] = popStack();
  }
  Value recv = popStack();

  // Prepare args
  std::vector<Value> all_args;
  if (isInstanceFunc || found_via_module) {
all_args = args2;
} else {
all_args.reserve(arg_count + 1);
all_args.push_back(recv);
all_args.insert(all_args.end(), args2.begin(), args2.end());
}

if (found_host) {
      if (host_func_idx < host_function_names_.size()) {
        std::string resolved_name = host_function_names_[host_func_idx];
        auto fnIt = host_functions.find(resolved_name);
        if (fnIt != host_functions.end()) {
          Value result = fnIt->second(all_args);
          pushStack(result);
          if (currentFrame().ip < currentFrame().function->type_feedback.size()) {
            currentFrame().function->type_feedback[currentFrame().ip].result_type_mask |= getFeedbackMask(result);
          }
        } else {
          pushStack(Value::makeNull());
        }
      } else {
        pushStack(Value::makeNull());
      }
    } else {
      // Call VM function
      doCall(vm_func, all_args, true);
    }
    break;
  }

  case OpCode::CALL_SUPER: {
    // CALL_SUPER: operands are [method_name, arg_count]
    // Pops args from stack, looks up parent class method, calls it
    if (instruction.operands.size() != 2 ||
        !instruction.operands[0].isStringValId() ||
        !instruction.operands[1].isInt()) {
      COMPILER_THROW("CALL_SUPER expects operands: <string "
                               "method_name, uint32 arg_count>");
    }

    const std::string &method_name =
        current_chunk ? current_chunk->getString(instruction.operands[0].asStringValId()) : instruction.operands[0].toString();
    uint32_t arg_count = instruction.operands[1].asInt();

    if (stack.size() < static_cast<size_t>(arg_count)) {
      COMPILER_THROW("Stack underflow during CALL_SUPER");
    }

    // Pop arguments from stack
    std::vector<Value> args(arg_count);
    for (uint32_t i = 0; i < arg_count; ++i) {
      args[arg_count - 1 - i] = popStack();
    }

    // Get current 'this' from local scope (slot 0 typically)
    size_t base = currentFrame().locals_base;
    if (base >= locals.size()) {
      COMPILER_THROW("CALL_SUPER: locals_base out of range");
    }
    Value this_value = locals[base];

    // Find the parent class method using the prototype chain
    // For now, emit as a host function call with special prefix
    // Full implementation needs parent method lookup via heap_.findClassMethod
    std::string super_method_name = "super." + method_name;

    // Prepend 'this' to args
    args.insert(args.begin(), this_value);

    // Call as host function - runtime will need to resolve via parent class
    pushStack(invokeHostFunction(super_method_name,
                            static_cast<uint32_t>(args.size())));
    break;
  }

  case OpCode::STRUCT_NEW: {
    if (instruction.operands.size() != 2 ||
        !instruction.operands[0].isStringValId() ||
        !instruction.operands[1].isInt()) {
      COMPILER_THROW("STRUCT_NEW expects operands: <string type, uint32 argc>");
    }
    const uint32_t argc = instruction.operands[1].asInt();
    if (stack.size() < argc) {
      COMPILER_THROW("STRUCT_NEW stack underflow");
    }
    std::vector<Value> args(argc + 1);
    args[0] = Value::makeStringValId(instruction.operands[0].asStringValId());
    for (uint32_t i = 0; i < argc; ++i) {
      args[argc - i] = popStack();
    }
    pushStack(invokeHostFunctionDirect("struct.new", args));
    break;
  }

  case OpCode::STRUCT_GET: {
    if (instruction.operands.size() != 1 || !instruction.operands[0].isStringValId()) {
      COMPILER_THROW("STRUCT_GET expects operand: <string field>");
    }
    if (!current_chunk) {
      COMPILER_THROW("STRUCT_GET requires active chunk");
    }
    Value instance = popStack();
    Value field = Value::makeStringValId(instruction.operands[0].asStringValId());
    pushStack(invokeHostFunctionDirect("struct.get", {instance, field}));
    break;
  }

  case OpCode::STRUCT_SET: {
    if (instruction.operands.size() != 1 || !instruction.operands[0].isStringValId()) {
      COMPILER_THROW("STRUCT_SET expects operand: <string field>");
    }
    Value value = popStack();
    Value instance = popStack();
    Value field = Value::makeStringValId(instruction.operands[0].asStringValId());
    pushStack(invokeHostFunctionDirect("struct.set", {instance, field, value}));
    break;
  }

    case OpCode::PROT_CHECK: {
      if (instruction.operands.size() != 1 || !instruction.operands[0].isStringValId()) {
        COMPILER_THROW("PROT_CHECK expects operand: <string protocol>");
      }
      if (!current_chunk) {
        COMPILER_THROW("PROT_CHECK requires active chunk");
      }
    Value value = popStack();
    const std::string proto =
        current_chunk->getString(instruction.operands[0].asStringValId());
    std::string typeName = getTypeName(value);
    if (typeImplementsProtocol(typeName, proto)) {
      pushStack(Value::makeBool(true));
    } else {
      pushStack(Value::makeBool(false));
    }
    break;
  }

  case OpCode::PROT_CAST: {
    if (instruction.operands.size() != 1 || !instruction.operands[0].isStringValId()) {
      COMPILER_THROW("PROT_CAST expects operand: <string protocol>");
    }
    if (!current_chunk) {
      COMPILER_THROW("PROT_CAST requires active chunk");
    }
    Value value = popStack();
    const std::string proto =
        current_chunk->getString(instruction.operands[0].asStringValId());
    std::string typeName = getTypeName(value);
    pushStack(typeImplementsProtocol(typeName, proto) ? value : Value::makeNull());
    break;
  }

        case OpCode::RETURN: {
            this->doReturn();
            break;
        }

  case OpCode::TRY_ENTER: {
    if (instruction.operands.size() < 1 ||
        !instruction.operands[0].isInt()) {
      COMPILER_THROW("TRY_ENTER expects catch ip operand");
    }
    const uint32_t catch_ip = instruction.operands[0].asInt();
    uint32_t finally_ip = 0;
    if (instruction.operands.size() >= 2 &&
        instruction.operands[1].isInt()) {
      finally_ip = instruction.operands[1].asInt();
    }
    currentFrame().try_stack.push_back(TryHandler{.catch_ip = catch_ip,
                                                  .finally_ip = finally_ip,
                                                  .finally_return_ip = 0,
                                                  .stack_depth = stack.size()});
    break;
  }

  case OpCode::TRY_EXIT: {
    if (!currentFrame().try_stack.empty()) {
      currentFrame().try_stack.pop_back();
    }
    break;
  }

  case OpCode::LOAD_EXCEPTION: {
    if (has_current_exception_) {
      pushStack(current_exception_);
    } else {
      pushStack(nullptr);
    }
    break;
  }

  case OpCode::THROW: {
    Value thrown = popStack();
    throw ScriptThrow{std::move(thrown)};
  }

case OpCode::CLOSURE: {
    if (instruction.operands.empty()) COMPILER_THROW("CLOSURE: no operands");
    uint32_t function_index = instruction.operands[0].asInt();
    const auto *target = current_chunk->getFunction(function_index);
    if (!target) {
      COMPILER_THROW("CLOSURE references unknown function index");
    }

    GCHeap::RuntimeClosure closure;
    closure.function_index = function_index;
    closure.upvalues.reserve(target->upvalues.size());
    for (const auto &descriptor : target->upvalues) {
        if (descriptor.captures_local) {
          uint32_t abs = this->toAbsoluteLocal(descriptor.index);
          this->ensureLocalIndex(abs);
          auto open_it = open_upvalues.find(abs);
          if (open_it == open_upvalues.end()) {
      auto cell = std::make_shared<GCHeap::UpvalueCell>();
        cell->is_open = true;
        cell->open_index = descriptor.index;
        cell->locals_base = currentFrame().locals_base;
        open_upvalues.emplace(abs, cell);
            closure.upvalues.push_back(std::move(cell));
} else {
closure.upvalues.push_back(open_it->second);
}
} else {
uint32_t parent_closure_id = currentFrame().closure_id;
if (parent_closure_id == 0) {
COMPILER_THROW(
"CLOSURE tried to capture upvalue without parent closure");
}
auto *parent_closure = heap_.closure(parent_closure_id);
        if (!parent_closure) {
          COMPILER_THROW("Parent closure not found for CLOSURE");
        }
        if (descriptor.index >= parent_closure->upvalues.size()) {
          COMPILER_THROW("CLOSURE upvalue index out of range");
        }
        closure.upvalues.push_back(parent_closure->upvalues[descriptor.index]);
      }
    }

  std::shared_ptr<std::unordered_map<std::string, Value>> closure_globals;
  if (current_chunk != main_chunk_.get()) {
    closure_globals = std::make_shared<std::unordered_map<std::string, Value>>(globals);
  }
            pushStack(Value::makeClosureId(heap_.allocateClosure(
                GCHeap::RuntimeClosure{.function_index = closure.function_index,
                                        .chunk_index = 0,
                                        .chunk = current_chunk,
.module_globals = std::move(closure_globals),
    .upvalues = std::move(closure.upvalues)}).id));
    break;
  }


	if (execCollectionOp(instruction)) break;



	default:
		return false;
	}
	return true;
}

} // namespace havel::compiler
