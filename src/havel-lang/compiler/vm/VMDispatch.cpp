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

namespace havel::compiler {

// ============================================================================
// Main executeInstruction dispatcher
// (Extracted arithmetic/logic/jump handlers moved to VMArithmetic.cpp)
// ============================================================================

void VM::executeInstruction(const Instruction &instruction) {
// Sync current_chunk with the current frame's chunk so string resolution
// is always correct for the executing function (fixes cross-chunk bugs
// when closures from one chunk call host functions that modify current_chunk).
if (frame_count_ > 0 && frame_arena_[frame_count_ - 1].chunk) {
        current_chunk = frame_arena_[frame_count_ - 1].chunk;
    }
    switch (instruction.opcode) {
  case OpCode::LOAD_CONST: {
    uint32_t const_index = instruction.operands[0].asInt();
    pushStack(getConstant(const_index));
    break;
  }

case OpCode::LOAD_GLOBAL: {
            if (instruction.operands.empty() ||
                !instruction.operands[0].isStringValId()) {
                COMPILER_THROW("LOAD_GLOBAL expects string operand");
            }
            // Get the string from the function's own chunk's string table
            uint32_t strIndex = instruction.operands[0].asStringValId();
            const auto& cf = currentFrame();
            const auto* func = cf.function;
            const BytecodeChunk* resolveChunk = cf.chunk ? cf.chunk : current_chunk;
            std::string name;
            if (resolveChunk) {
                name = resolveChunk->getString(strIndex);
            } else {
                name = "<unknown:" + std::to_string(strIndex) + ">";
            }

  // First check regular globals (user variables shadow host functions)
  auto it = globals.find(name);
  if (it != globals.end()) {
    trackGlobalAccess(name);
    pushStack(it->second);
    break;
  }

  // Then check host function globals (fallback for built-in functions)
  auto hostIt = host_function_globals_.find(name);
  if (hostIt != host_function_globals_.end()) {
    trackGlobalAccess(name);
    pushStack(hostIt->second);
    break;
  }

    COMPILER_THROW("Undefined variable: '" + name + "'");
    break;
  }

        case OpCode::STORE_GLOBAL: {
            if (instruction.operands.empty() ||
                !instruction.operands[0].isStringValId()) {
                COMPILER_THROW("STORE_GLOBAL expects string operand");
            }
            uint32_t strIndex = instruction.operands[0].asStringValId();
            const auto& cf_store = currentFrame();
            const BytecodeChunk* resolveChunkStore = cf_store.chunk ? cf_store.chunk : current_chunk;
            std::string name;
            if (resolveChunkStore) {
                name = resolveChunkStore->getString(strIndex);
            } else {
                name = "<unknown:" + std::to_string(strIndex) + ">";
            }
            Value value = popStack();

    if (immutable_globals_.count(name)) {
            auto existing = globals.find(name);
            if (existing != globals.end() && existing->second == value) {
                break;
            }
            COMPILER_THROW("Cannot reassign val global: " + name);
        }
        globals[name] = value;
        emitVariableChanged(name);
        break;
    }

        case OpCode::STORE_IMMUT_GLOBAL: {
            if (instruction.operands.empty() ||
                !instruction.operands[0].isStringValId()) {
                COMPILER_THROW("STORE_IMMUT_GLOBAL expects string operand");
            }
            uint32_t strIndex = instruction.operands[0].asStringValId();
            const auto& cf_imut = currentFrame();
            const BytecodeChunk* resolveChunkImut = cf_imut.chunk ? cf_imut.chunk : current_chunk;
            std::string name;
            if (resolveChunkImut) {
                name = resolveChunkImut->getString(strIndex);
            } else {
                name = "<unknown:" + std::to_string(strIndex) + ">";
            }
        Value value = popStack();

        immutable_globals_.insert(name);
        globals[name] = value;
        emitVariableChanged(name);
        break;
    }

case OpCode::LOAD_VAR: {
            uint32_t var_index = instruction.operands[0].asInt();
            uint32_t abs = this->toAbsoluteLocal(var_index);
            this->ensureLocalIndex(abs);
Value value = locals[abs];

    // Record feedback
    auto &frame = currentFrame();
    if (frame.ip < frame.function->type_feedback.size()) {
      auto &fb = frame.function->type_feedback[frame.ip];
      fb.execution_count++;
      fb.result_type_mask |= getFeedbackMask(value);
      if (fb.execution_count == 1000 && hot_func_cb_) {
        hot_func_cb_(*const_cast<BytecodeFunction*>(frame.function));
      }
    }

    pushStack(value);
    break;
    }

    case OpCode::STORE_VAR: {
            uint32_t var_index = instruction.operands[0].asInt();
            uint32_t abs = this->toAbsoluteLocal(var_index);
            this->ensureLocalIndex(abs);
            Value value = popStack();

        if (immutable_locals_.count(abs)) {
            COMPILER_THROW("Cannot reassign val local at index " + std::to_string(var_index));
        }

        // Record feedback
        auto &frame = currentFrame();
        if (frame.ip < frame.function->type_feedback.size()) {
            auto &fb = frame.function->type_feedback[frame.ip];
            fb.execution_count++;
            fb.left_type_mask |= getFeedbackMask(value);
            if (fb.execution_count == 1000 && hot_func_cb_) {
                hot_func_cb_(*const_cast<BytecodeFunction*>(frame.function));
            }
        }

        locals[abs] = value;
        break;
    }

    case OpCode::STORE_IMMUT_VAR: {
        uint32_t var_index = instruction.operands[0].asInt();
        uint32_t abs = this->toAbsoluteLocal(var_index);
        this->ensureLocalIndex(abs);
        Value value = popStack();

        immutable_locals_.insert(abs);

        // Record feedback
        auto &frame = currentFrame();
        if (frame.ip < frame.function->type_feedback.size()) {
            auto &fb = frame.function->type_feedback[frame.ip];
            fb.execution_count++;
            fb.left_type_mask |= getFeedbackMask(value);
            if (fb.execution_count == 1000 && hot_func_cb_) {
                hot_func_cb_(*const_cast<BytecodeFunction*>(frame.function));
            }
        }

        locals[abs] = value;
        break;
    }

    // Increment/Decrement local variable optimization
    case OpCode::INCLOCAL: {
        uint32_t var_index = instruction.operands[0].asInt();
        uint32_t abs = this->toAbsoluteLocal(var_index);
        this->ensureLocalIndex(abs);
        Value& val = locals[abs];
        // Record feedback - INCLOCAL is a loop counter hot spot
        {
            auto &frame = currentFrame();
            if (frame.ip < frame.function->type_feedback.size()) {
                auto &fb = frame.function->type_feedback[frame.ip];
                fb.execution_count++;
                fb.left_type_mask |= getFeedbackMask(val);
                if (fb.execution_count == 1000 && hot_func_cb_) {
                    hot_func_cb_(*const_cast<BytecodeFunction*>(frame.function));
                }
            }
        }
        if (val.isInt()) {
            val = Value::makeInt(val.asInt() + 1);
            pushStack(val);
        } else if (val.isDouble()) {
            val = Value::makeDouble(val.asDouble() + 1.0);
            pushStack(val);
        } else {
            COMPILER_THROW("Cannot increment non-numeric value");
        }
        break;
    }

    case OpCode::DECLOCAL: {
        uint32_t var_index = instruction.operands[0].asInt();
        uint32_t abs = this->toAbsoluteLocal(var_index);
        this->ensureLocalIndex(abs);
        Value& val = locals[abs];
        {
            auto &frame = currentFrame();
            if (frame.ip < frame.function->type_feedback.size()) {
                auto &fb = frame.function->type_feedback[frame.ip];
                fb.execution_count++;
                fb.left_type_mask |= getFeedbackMask(val);
                if (fb.execution_count == 1000 && hot_func_cb_) {
                    hot_func_cb_(*const_cast<BytecodeFunction*>(frame.function));
                }
            }
        }
        if (val.isInt()) {
            val = Value::makeInt(val.asInt() - 1);
            pushStack(val);
        } else if (val.isDouble()) {
            val = Value::makeDouble(val.asDouble() - 1.0);
            pushStack(val);
        } else {
            COMPILER_THROW("Cannot decrement non-numeric value");
        }
        break;
    }

    case OpCode::INCLOCAL_POST: {
        uint32_t var_index = instruction.operands[0].asInt();
        uint32_t abs = this->toAbsoluteLocal(var_index);
        this->ensureLocalIndex(abs);
        Value old = locals[abs];
        {
            auto &frame = currentFrame();
            if (frame.ip < frame.function->type_feedback.size()) {
                auto &fb = frame.function->type_feedback[frame.ip];
                fb.execution_count++;
                fb.left_type_mask |= getFeedbackMask(old);
            }
        }
        pushStack(old);  // Push old value first
        if (old.isInt()) {
            locals[abs] = Value::makeInt(old.asInt() + 1);
        } else if (old.isDouble()) {
            locals[abs] = Value::makeDouble(old.asDouble() + 1.0);
        } else {
            COMPILER_THROW("Cannot increment non-numeric value");
        }
        break;
    }

    case OpCode::DECLOCAL_POST: {
        uint32_t var_index = instruction.operands[0].asInt();
        uint32_t abs = this->toAbsoluteLocal(var_index);
        this->ensureLocalIndex(abs);
        Value old = locals[abs];
        {
            auto &frame = currentFrame();
            if (frame.ip < frame.function->type_feedback.size()) {
                auto &fb = frame.function->type_feedback[frame.ip];
                fb.execution_count++;
                fb.left_type_mask |= getFeedbackMask(old);
            }
        }
        pushStack(old);  // Push old value first
        if (old.isInt()) {
            locals[abs] = Value::makeInt(old.asInt() - 1);
        } else if (old.isDouble()) {
            locals[abs] = Value::makeDouble(old.asDouble() - 1.0);
        } else {
            COMPILER_THROW("Cannot decrement non-numeric value");
        }
        break;
    }

case OpCode::LOAD_UPVALUE: {
uint32_t upvalue_index = instruction.operands[0].asInt();
uint32_t closure_id = currentFrame().closure_id;
if (closure_id == 0) {
COMPILER_THROW("LOAD_UPVALUE used without active closure");
}
auto *closure = heap_.closure(closure_id);
if (!closure) {
COMPILER_THROW("Closure not found for LOAD_UPVALUE");
}
if (upvalue_index >= closure->upvalues.size() ||
!closure->upvalues[upvalue_index]) {
COMPILER_THROW("LOAD_UPVALUE index out of range");
}
        const auto &cell = closure->upvalues[upvalue_index];
  Value value;
  if (cell->is_open) {
    uint32_t abs_index = cell->locals_base + cell->open_index;
    this->ensureLocalIndex(abs_index);
    value = locals[abs_index];
  } else {
    value = cell->closed_value;
  }
    pushStack(value);
    break;
}

case OpCode::STORE_UPVALUE: {
uint32_t upvalue_index = instruction.operands[0].asInt();
uint32_t closure_id = currentFrame().closure_id;
if (closure_id == 0) {
COMPILER_THROW("STORE_UPVALUE used without active closure");
}
auto *closure = heap_.closure(closure_id);
if (!closure) {
COMPILER_THROW("Closure not found for STORE_UPVALUE");
}
if (upvalue_index >= closure->upvalues.size() ||
!closure->upvalues[upvalue_index]) {
COMPILER_THROW("STORE_UPVALUE index out of range");
}
auto &cell = closure->upvalues[upvalue_index];
Value value = popStack();

if (cell->is_open) {
uint32_t abs_index = cell->locals_base + cell->open_index;
this->ensureLocalIndex(abs_index);
locals[abs_index] = value;
} else {
cell->closed_value = value;
}
break;
}

  case OpCode::POP: {
    popStack();
    break;
  }

  case OpCode::DUP: {
    Value value = popStack();
    pushStack(value);
    pushStack(value);
    break;
  }

  case OpCode::SWAP: {
    Value top = popStack();
    Value next = popStack();
    pushStack(top);
    pushStack(next);
    break;
  }

  case OpCode::PUSH_NULL: {
    pushStack(Value::makeNull());
    break;
  }

    case OpCode::ADD:
    case OpCode::SUB:
    case OpCode::MUL:
    case OpCode::DIV:
    case OpCode::INT_DIV:
    case OpCode::MOD:
    case OpCode::DIVMOD:
    case OpCode::REMAINDER:
    case OpCode::POW:
    case OpCode::EQ:
    case OpCode::NEQ:
    case OpCode::IS:
    case OpCode::LT:
    case OpCode::LTE:
    case OpCode::GT:
    case OpCode::GTE:
    case OpCode::BIT_AND:
    case OpCode::BIT_OR:
    case OpCode::BIT_XOR:
    case OpCode::BIT_LSH:
    case OpCode::BIT_RSH:
      execBinaryOp(instruction);
      break;

  case OpCode::AND:
  case OpCode::OR:
    execLogicalOp(instruction.opcode);
    break;

        case OpCode::NOT: {
            Value v = popStack();
            if (v.isObjectId()) {
                Value opMethod = getHostObjectField(ObjectRef{v.asObjectId(), true}, "op_not");
                if (!opMethod.isNull() && (opMethod.isFunctionObjId() || opMethod.isClosureId() || opMethod.isHostFuncId())) {
                    pushStack(callFunction(opMethod, {v}));
                } else {
                    pushStack(!isTruthy(v));
                }
            } else {
                pushStack(!isTruthy(v));
            }
            break;
        }

	case OpCode::BIT_NOT: {
		Value v = popStack();
		if (v.isInt()) {
			pushStack(~v.asInt());
		} else if (v.isDouble()) {
			pushStack(~static_cast<int64_t>(v.asDouble()));
		} else if (v.isObjectId()) {
			Value opMethod = getHostObjectField(ObjectRef{v.asObjectId(), true}, "op_bit_not");
			if (!opMethod.isNull() && (opMethod.isFunctionObjId() || opMethod.isClosureId() || opMethod.isHostFuncId())) {
				pushStack(callFunction(opMethod, {v}));
			} else {
				COMPILER_THROW("Bitwise NOT requires integer operand or op_bit_not method");
			}
		} else {
			COMPILER_THROW("Bitwise NOT requires integer operand");
		}
		break;
	}

	case OpCode::NEGATE:
		execNegate();
		break;

case OpCode::LENGTH: {
    Value v = popStack();
    pushStack(execLengthOp(v));
    break;
}

	case OpCode::JUMP:
    execJump(instruction);
    break;

  case OpCode::JUMP_IF_FALSE:
    execJumpIfFalse(instruction);
    break;

  case OpCode::JUMP_IF_TRUE:
    execJumpIfTrue(instruction);
    break;

  case OpCode::IS_NULL: {
    Value value = popStack();
    bool isNullVal = value.isNull();
    pushStack(Value::makeBool(isNullVal));
    break;
  }

  case OpCode::JUMP_IF_NULL: {
    uint32_t target = instruction.operands[0].asInt();
    Value value = popStack();
    // Only jump on null/undefined, not on all falsy values
    if (value.isNull()) {
      currentFrame().ip = target;
    }
	break;
}

default:
	if (execCollectionOp(instruction)) break;
	if (execControlFlowOp(instruction)) break;
	if (execConcurrencyOp(instruction)) break;
	if (execBuiltinOp(instruction)) break;
	COMPILER_THROW(
        "Unknown opcode: " +
        std::to_string(static_cast<int>(instruction.opcode)));
  }
}


} // namespace havel::compiler
