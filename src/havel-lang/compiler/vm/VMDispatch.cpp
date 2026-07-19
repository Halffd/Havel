#include "VM.hpp"
#include "VMInternals.hpp"
#include "../../../utils/Logger.hpp"
#include "../../utils/ErrorPrinter.hpp"
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
#include "../../stdlib/LogModule.hpp"

#if defined(__GNUC__) && !defined(__clang__)
#define HAVE_COMPUTED_GOTO 1
#elif defined(__clang__)
#define HAVE_COMPUTED_GOTO 1
#else
#define HAVE_COMPUTED_GOTO 0
#endif

namespace havel::compiler {

// ============================================================================
// Main executeInstruction dispatcher — switch-based (portable)
// ============================================================================

void VM::executeInstruction(const Instruction &instruction) {
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

  auto it = globals.find(name);
  if (it != globals.end()) {
        if (it->second.isObjectId()) {
            auto *obj = heap_.object(it->second.asObjectId());
            if (obj) {
                auto *lf = obj->get("__lazy__");
                if (lf && lf->isBool() && lf->asBool()) {
                    auto *modNameVal = obj->get("__module__");
                    std::string modName;
                    if (modNameVal) {
                        if (modNameVal->isStringId()) {
                            if (auto *s = heap_.string(modNameVal->asStringId())) modName = *s;
                        } else if (modNameVal->isStringValId() && current_chunk) {
                            modName = current_chunk->getString(modNameVal->asStringValId());
                        }
                    }
                    if (!modName.empty()) {
                        if (isLazyModuleRegistered(modName)) {
                            ensureModuleLoaded(modName);
                        }
                        auto git2 = globals.find(name);
                        if (git2 != globals.end()) {
                            trackGlobalAccess(name);
                            pushStack(git2->second);
                    break;
                }
            }
        }
      }
    }
    trackGlobalAccess(name);
    pushStack(it->second);
    break;
  }

auto hostIt = host_function_globals_.find(name);
  if (hostIt != host_function_globals_.end()) {
    trackGlobalAccess(name);
    pushStack(hostIt->second);
    break;
  }

  trackGlobalAccess(name);
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

  // Materialize StringValId to heap StringId so cross-chunk reads work
  if (value.isStringValId() || value.isRegexValId()) {
      const BytecodeChunk* matChunk = current_chunk ? current_chunk : (main_chunk_ ? main_chunk_.get() : nullptr);
      if (matChunk) {
          std::string s;
          if (value.isStringValId()) s = matChunk->getString(value.asStringValId());
          else if (value.isRegexValId()) s = matChunk->getString(value.asRegexValId());
          if (!s.empty()) {
              auto ref = heap_.allocateString(std::move(s));
              value = Value::makeStringId(ref.id);
          }
      }
  }

  if (immutable_globals_.count(name)) {
            auto existing = globals.find(name);
            if (existing != globals.end() && existing->second == value) {
                break;
            }
            COMPILER_THROW("Cannot reassign val global: " + name);
        }
        globals[name] = value;
        heap_.writeBarrier(Value::makeNull(), value);
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

  // Materialize StringValId to heap StringId so cross-chunk reads work
  if (value.isStringValId() || value.isRegexValId()) {
      const BytecodeChunk* matChunk = current_chunk ? current_chunk : (main_chunk_ ? main_chunk_.get() : nullptr);
      if (matChunk) {
          std::string s;
          if (value.isStringValId()) s = matChunk->getString(value.asStringValId());
          else if (value.isRegexValId()) s = matChunk->getString(value.asRegexValId());
          if (!s.empty()) {
              auto ref = heap_.allocateString(std::move(s));
              value = Value::makeStringId(ref.id);
          }
      }
  }

  // Materialize StringValId to heap StringId so cross-chunk reads work
  if (value.isStringValId() || value.isRegexValId()) {
      const BytecodeChunk* matChunk = current_chunk ? current_chunk : (main_chunk_ ? main_chunk_.get() : nullptr);
      if (matChunk) {
          std::string s;
          if (value.isStringValId()) s = matChunk->getString(value.asStringValId());
          else if (value.isRegexValId()) s = matChunk->getString(value.asRegexValId());
          if (!s.empty()) {
              auto ref = heap_.allocateString(std::move(s));
              value = Value::makeStringId(ref.id);
          }
      }
  }

        immutable_globals_.insert(name);
        globals[name] = value;
        heap_.writeBarrier(Value::makeNull(), value);
        emitVariableChanged(name);
        break;
    }

case OpCode::LOAD_VAR: {
    uint32_t var_index = instruction.operands[0].asInt();
    uint32_t abs = this->toAbsoluteLocal(var_index);
    this->ensureLocalIndex(abs);
    Value value = locals[abs];

    if (hot_func_cb_) {
      auto &frame = currentFrame();
      if (frame.ip < frame.function->type_feedback.size()) {
        auto &fb = frame.function->type_feedback[frame.ip];
        fb.execution_count++;
        fb.result_type_mask |= getFeedbackMask(value);
        if (fb.execution_count == 1000) {
          hot_func_cb_(*const_cast<BytecodeFunction*>(frame.function));
        }
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

    if (hot_func_cb_) {
      auto &frame = currentFrame();
      if (frame.ip < frame.function->type_feedback.size()) {
        auto &fb = frame.function->type_feedback[frame.ip];
        fb.execution_count++;
        fb.left_type_mask |= getFeedbackMask(value);
        if (fb.execution_count == 1000) {
          hot_func_cb_(*const_cast<BytecodeFunction*>(frame.function));
        }
      }
    }

    locals[abs] = value;
    heap_.writeBarrier(Value::makeNull(), value);
    break;
  }

case OpCode::STORE_IMMUT_VAR: {
    uint32_t var_index = instruction.operands[0].asInt();
    uint32_t abs = this->toAbsoluteLocal(var_index);
    this->ensureLocalIndex(abs);
    Value value = popStack();

    immutable_locals_.insert(abs);

    if (hot_func_cb_) {
      auto &frame = currentFrame();
      if (frame.ip < frame.function->type_feedback.size()) {
        auto &fb = frame.function->type_feedback[frame.ip];
        fb.execution_count++;
        fb.left_type_mask |= getFeedbackMask(value);
        if (fb.execution_count == 1000) {
          hot_func_cb_(*const_cast<BytecodeFunction*>(frame.function));
        }
      }
    }

    locals[abs] = value;
    heap_.writeBarrier(Value::makeNull(), value);
    break;
  }

    case OpCode::INCLOCAL: {
    uint32_t var_index = instruction.operands[0].asInt();
    uint32_t abs = this->toAbsoluteLocal(var_index);
    this->ensureLocalIndex(abs);
    Value& val = locals[abs];
    if (hot_func_cb_) {
      auto &frame = currentFrame();
      if (frame.ip < frame.function->type_feedback.size()) {
        auto &fb = frame.function->type_feedback[frame.ip];
        fb.execution_count++;
        fb.left_type_mask |= getFeedbackMask(val);
        if (fb.execution_count == 1000) {
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
    if (hot_func_cb_) {
      auto &frame = currentFrame();
      if (frame.ip < frame.function->type_feedback.size()) {
        auto &fb = frame.function->type_feedback[frame.ip];
        fb.execution_count++;
        fb.left_type_mask |= getFeedbackMask(val);
        if (fb.execution_count == 1000) {
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
    if (hot_func_cb_) {
      auto &frame = currentFrame();
      if (frame.ip < frame.function->type_feedback.size()) {
        auto &fb = frame.function->type_feedback[frame.ip];
        fb.execution_count++;
        fb.left_type_mask |= getFeedbackMask(old);
      }
    }
    pushStack(old);
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
    if (hot_func_cb_) {
      auto &frame = currentFrame();
      if (frame.ip < frame.function->type_feedback.size()) {
        auto &fb = frame.function->type_feedback[frame.ip];
        fb.execution_count++;
        fb.left_type_mask |= getFeedbackMask(old);
      }
    }
    pushStack(old);
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
        ::havel::error("[VM-DEBUG] LOAD_UPVALUE failed: closure_id=0 upvalue_idx={} frame_count={} func='{}'", 
            upvalue_index, frame_count_, 
            currentFrame().function ? currentFrame().function->name : "?");
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
    // std::cerr << "[DEBUG LOAD_UPVALUE] upvalue_index=" << upvalue_index << " cell->is_open=" << cell->is_open << " cell->open_index=" << cell->open_index << " cell->locals_base=" << cell->locals_base << std::endl;
    Value value;
    if (cell->is_open) {
        uint32_t abs_index = cell->locals_base + cell->open_index;
        // std::cerr << "  abs_index=" << abs_index << " locals_base=" << cell->locals_base << " open_index=" << cell->open_index << std::endl;
        this->ensureLocalIndex(abs_index);
        value = locals[abs_index];
        // std::cerr << "  loaded value=" << value.toString() << std::endl;
    } else {
        value = cell->closed_value;
        // std::cerr << "  closed value=" << value.toString() << std::endl;
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
      heap_.writeBarrier(Value::makeNull(), value);
    } else {
      cell->closed_value = value;
      heap_.writeBarrier(Value::makeNull(), value);
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

// ============================================================================
// Direct-threaded dispatch loop (GNU extension — GCC and Clang)
// Uses computed goto (labels-as-values) instead of switch for ~15-25%
// faster dispatch by eliminating branch prediction misses.
// ============================================================================

#if HAVE_COMPUTED_GOTO

#define DISPATCH_NEXT() do { \
    if (frame_count_ > stop_frame_depth) { \
        auto &frm = frame_arena_[frame_count_ - 1]; \
        if (frm.ip == saved_ip) frm.ip++; \
        goto *dispatch_table[static_cast<uint8_t>( \
            frm.function->instructions[frm.ip].opcode)]; \
    } \
    return; \
} while(0)

#define DISPATCH_OP(op) do { \
    if (frame_count_ > stop_frame_depth) { \
        auto &frm = frame_arena_[frame_count_ - 1]; \
        if (frm.ip == saved_ip) frm.ip++; \
        goto *dispatch_table[static_cast<uint8_t>(OpCode::op)]; \
    } \
    return; \
} while(0)

__attribute__((hot, noinline))
void VM::runDispatchFast(size_t stop_frame_depth) {
    static void* dispatch_table[256] = {
        [0 ... 255] = &&op_default,
        [static_cast<uint8_t>(OpCode::LOAD_CONST)] = &&op_LOAD_CONST,
        [static_cast<uint8_t>(OpCode::LOAD_GLOBAL)] = &&op_LOAD_GLOBAL,
        [static_cast<uint8_t>(OpCode::STORE_GLOBAL)] = &&op_STORE_GLOBAL,
        [static_cast<uint8_t>(OpCode::STORE_IMMUT_GLOBAL)] = &&op_STORE_IMMUT_GLOBAL,
        [static_cast<uint8_t>(OpCode::LOAD_VAR)] = &&op_LOAD_VAR,
        [static_cast<uint8_t>(OpCode::STORE_VAR)] = &&op_STORE_VAR,
        [static_cast<uint8_t>(OpCode::STORE_IMMUT_VAR)] = &&op_STORE_IMMUT_VAR,
        [static_cast<uint8_t>(OpCode::LOAD_UPVALUE)] = &&op_LOAD_UPVALUE,
        [static_cast<uint8_t>(OpCode::STORE_UPVALUE)] = &&op_STORE_UPVALUE,
        [static_cast<uint8_t>(OpCode::POP)] = &&op_POP,
        [static_cast<uint8_t>(OpCode::DUP)] = &&op_DUP,
        [static_cast<uint8_t>(OpCode::SWAP)] = &&op_SWAP,
        [static_cast<uint8_t>(OpCode::PUSH_NULL)] = &&op_PUSH_NULL,
        [static_cast<uint8_t>(OpCode::ADD)] = &&op_ADD,
        [static_cast<uint8_t>(OpCode::SUB)] = &&op_SUB,
        [static_cast<uint8_t>(OpCode::MUL)] = &&op_MUL,
        [static_cast<uint8_t>(OpCode::DIV)] = &&op_DIV,
        [static_cast<uint8_t>(OpCode::INT_DIV)] = &&op_INT_DIV,
        [static_cast<uint8_t>(OpCode::DIVMOD)] = &&op_DIVMOD,
        [static_cast<uint8_t>(OpCode::REMAINDER)] = &&op_REMAINDER,
        [static_cast<uint8_t>(OpCode::MOD)] = &&op_MOD,
        [static_cast<uint8_t>(OpCode::POW)] = &&op_POW,
        [static_cast<uint8_t>(OpCode::INCLOCAL)] = &&op_INCLOCAL,
        [static_cast<uint8_t>(OpCode::DECLOCAL)] = &&op_DECLOCAL,
        [static_cast<uint8_t>(OpCode::INCLOCAL_POST)] = &&op_INCLOCAL_POST,
        [static_cast<uint8_t>(OpCode::DECLOCAL_POST)] = &&op_DECLOCAL_POST,
        [static_cast<uint8_t>(OpCode::EQ)] = &&op_EQ,
        [static_cast<uint8_t>(OpCode::NEQ)] = &&op_NEQ,
        [static_cast<uint8_t>(OpCode::IS)] = &&op_IS,
        [static_cast<uint8_t>(OpCode::LT)] = &&op_LT,
        [static_cast<uint8_t>(OpCode::LTE)] = &&op_LTE,
        [static_cast<uint8_t>(OpCode::GT)] = &&op_GT,
        [static_cast<uint8_t>(OpCode::GTE)] = &&op_GTE,
        [static_cast<uint8_t>(OpCode::AND)] = &&op_AND,
        [static_cast<uint8_t>(OpCode::OR)] = &&op_OR,
        [static_cast<uint8_t>(OpCode::NOT)] = &&op_NOT,
        [static_cast<uint8_t>(OpCode::NEGATE)] = &&op_NEGATE,
        [static_cast<uint8_t>(OpCode::IS_NULL)] = &&op_IS_NULL,
        [static_cast<uint8_t>(OpCode::BIT_AND)] = &&op_BIT_AND,
        [static_cast<uint8_t>(OpCode::BIT_OR)] = &&op_BIT_OR,
        [static_cast<uint8_t>(OpCode::BIT_XOR)] = &&op_BIT_XOR,
        [static_cast<uint8_t>(OpCode::BIT_LSH)] = &&op_BIT_LSH,
        [static_cast<uint8_t>(OpCode::BIT_RSH)] = &&op_BIT_RSH,
        [static_cast<uint8_t>(OpCode::BIT_NOT)] = &&op_BIT_NOT,
        [static_cast<uint8_t>(OpCode::LENGTH)] = &&op_LENGTH,
        [static_cast<uint8_t>(OpCode::JUMP)] = &&op_JUMP,
        [static_cast<uint8_t>(OpCode::JUMP_IF_FALSE)] = &&op_JUMP_IF_FALSE,
        [static_cast<uint8_t>(OpCode::JUMP_IF_TRUE)] = &&op_JUMP_IF_TRUE,
        [static_cast<uint8_t>(OpCode::JUMP_IF_NULL)] = &&op_JUMP_IF_NULL,
        [static_cast<uint8_t>(OpCode::CALL)] = &&op_CALL,
        [static_cast<uint8_t>(OpCode::CALL_DYN)] = &&op_CALL,
        [static_cast<uint8_t>(OpCode::CALL_SPREAD)] = &&op_CALL,
        [static_cast<uint8_t>(OpCode::RETURN)] = &&op_RETURN
    };

    size_t counter = 0;

    // Fetch first instruction
    if (frame_count_ == 0 || frame_count_ <= stop_frame_depth) return;
    {
        auto &frm = frame_arena_[frame_count_ - 1];
        if (frm.ip >= frm.function->instructions.size()) {
            stack.push(nullptr);
            executeInstruction(Instruction{OpCode::RETURN});
            return;
        }
        goto *dispatch_table[static_cast<uint8_t>(frm.function->instructions[frm.ip].opcode)];
    }

    // --- Hot opcodes (most frequent in self-hosted compilation) ---

op_LOAD_CONST: {
    auto &frm = frame_arena_[frame_count_ - 1];
    const auto &inst = frm.function->instructions[frm.ip];
    uint32_t saved_ip = frm.ip;
    frm.ip++;
    pushStack(getConstant(inst.operands[0].asInt()));
    counter++;
    if ((counter & 8191) == 0) {
        if (exit_requested_.load()) return;
        maybeCollectGarbage();
        periodicYieldCheck();
        if (!pending_calls.empty()) {
            processPendingCalls();
            if (exit_requested_.load()) return;
        }
    }
    if (frame_count_ == 0 || frame_count_ <= stop_frame_depth) return;
    {
        auto &f2 = frame_arena_[frame_count_ - 1];
        if (f2.ip >= f2.function->instructions.size()) {
            stack.push(nullptr);
            Instruction retInst{OpCode::RETURN};
            try { executeInstruction(retInst); } catch (...) { throw; }
            return;
        }
        goto *dispatch_table[static_cast<uint8_t>(f2.function->instructions[f2.ip].opcode)];
    }
}

op_LOAD_VAR: {
    auto &frm = frame_arena_[frame_count_ - 1];
    const auto &inst = frm.function->instructions[frm.ip];
    uint32_t saved_ip = frm.ip;
    frm.ip++;
    uint32_t var_index = inst.operands[0].asInt();
    uint32_t abs = this->toAbsoluteLocal(var_index);
    this->ensureLocalIndex(abs);
    pushStack(locals[abs]);
    counter++;
    if ((counter & 8191) == 0) {
        if (exit_requested_.load()) return;
        maybeCollectGarbage();
        periodicYieldCheck();
    }
    if (frame_count_ == 0 || frame_count_ <= stop_frame_depth) return;
    {
        auto &f2 = frame_arena_[frame_count_ - 1];
        if (f2.ip >= f2.function->instructions.size()) {
            stack.push(nullptr);
            executeInstruction(Instruction{OpCode::RETURN});
            return;
        }
        goto *dispatch_table[static_cast<uint8_t>(f2.function->instructions[f2.ip].opcode)];
    }
}

op_STORE_VAR: {
    auto &frm = frame_arena_[frame_count_ - 1];
    const auto &inst = frm.function->instructions[frm.ip];
    uint32_t saved_ip = frm.ip;
    frm.ip++;
    uint32_t var_index = inst.operands[0].asInt();
    uint32_t abs = this->toAbsoluteLocal(var_index);
    this->ensureLocalIndex(abs);
    Value value = popStack();
    if (immutable_locals_.count(abs)) {
        COMPILER_THROW("Cannot reassign val local at index " + std::to_string(var_index));
    }
    locals[abs] = value;
    counter++;
    if ((counter & 8191) == 0) {
        if (exit_requested_.load()) return;
        maybeCollectGarbage();
        periodicYieldCheck();
    }
    if (frame_count_ == 0 || frame_count_ <= stop_frame_depth) return;
    {
        auto &f2 = frame_arena_[frame_count_ - 1];
        if (f2.ip >= f2.function->instructions.size()) {
            stack.push(nullptr);
            executeInstruction(Instruction{OpCode::RETURN});
            return;
        }
        goto *dispatch_table[static_cast<uint8_t>(f2.function->instructions[f2.ip].opcode)];
    }
}

op_POP: {
    auto &frm = frame_arena_[frame_count_ - 1];
    frm.ip++;
    popStack();
    counter++;
    if ((counter & 8191) == 0) {
        if (exit_requested_.load()) return;
        maybeCollectGarbage();
        periodicYieldCheck();
    }
    if (frame_count_ == 0 || frame_count_ <= stop_frame_depth) return;
    {
        auto &f2 = frame_arena_[frame_count_ - 1];
        if (f2.ip >= f2.function->instructions.size()) {
            stack.push(nullptr);
            executeInstruction(Instruction{OpCode::RETURN});
            return;
        }
        goto *dispatch_table[static_cast<uint8_t>(f2.function->instructions[f2.ip].opcode)];
    }
}

op_PUSH_NULL: {
    auto &frm = frame_arena_[frame_count_ - 1];
    frm.ip++;
    pushStack(Value::makeNull());
    if (frame_count_ == 0 || frame_count_ <= stop_frame_depth) return;
    {
        auto &f2 = frame_arena_[frame_count_ - 1];
        if (f2.ip >= f2.function->instructions.size()) {
            stack.push(nullptr);
            executeInstruction(Instruction{OpCode::RETURN});
            return;
        }
        goto *dispatch_table[static_cast<uint8_t>(f2.function->instructions[f2.ip].opcode)];
    }
}

op_CALL: {
    auto &frm = frame_arena_[frame_count_ - 1];
    const auto &inst = frm.function->instructions[frm.ip];
    frm.ip++;
    try {
        executeInstruction(inst);
    } catch (const ScriptThrow &thrown) {
        ::havel::stdlib::notifyRuntimeError(thrown.value.toString());
        if (!handleScriptThrow(thrown.value)) {
            throw ScriptError(thrown.value, "Uncaught exception", "", 0, 0);
        }
    } catch (const std::runtime_error &e) {
        Value exceptionValue = Value::makeStringId(heap_.allocateString(e.what()).id);
        ::havel::stdlib::notifyRuntimeError(e.what());
        if (handleScriptThrow(exceptionValue)) {
            // caught
        } else {
            throw std::runtime_error(e.what());
        }
    }
    counter++;
    if ((counter & 8191) == 0) {
        if (exit_requested_.load()) return;
        maybeCollectGarbage();
        periodicYieldCheck();
        if (!pending_calls.empty()) {
            processPendingCalls();
            if (exit_requested_.load()) return;
        }
    }
    if (suspension_requested_) goto slow_dispatch_fallback;
    if (frame_count_ == 0 || frame_count_ <= stop_frame_depth) return;
    {
        auto &f2 = frame_arena_[frame_count_ - 1];
        if (f2.ip >= f2.function->instructions.size()) {
            return;
        }
        goto *dispatch_table[static_cast<uint8_t>(f2.function->instructions[f2.ip].opcode)];
    }
}

op_RETURN: {
    auto &frm = frame_arena_[frame_count_ - 1];
    const auto &inst = frm.function->instructions[frm.ip];
    try {
        executeInstruction(inst);
    } catch (const ScriptThrow &thrown) {
        ::havel::stdlib::notifyRuntimeError(thrown.value.toString());
        if (!handleScriptThrow(thrown.value)) {
            throw ScriptError(thrown.value, "Uncaught exception", "", 0, 0);
        }
    } catch (const std::runtime_error &e) {
        ::havel::stdlib::notifyRuntimeError(e.what());
        throw std::runtime_error(e.what());
    }
    if (frame_count_ == 0 || frame_count_ <= stop_frame_depth) return;
    {
        auto &f2 = frame_arena_[frame_count_ - 1];
        if (f2.ip >= f2.function->instructions.size()) {
            stack.push(nullptr);
            executeInstruction(Instruction{OpCode::RETURN});
            return;
        }
        goto *dispatch_table[static_cast<uint8_t>(f2.function->instructions[f2.ip].opcode)];
    }
}

    // --- Remaining opcodes: delegate to executeInstruction ---

op_LOAD_GLOBAL: {
    auto &frm = frame_arena_[frame_count_ - 1];
    const auto &inst = frm.function->instructions[frm.ip];
    frm.ip++;
    try {
        executeInstruction(inst);
    } catch (const ScriptThrow &thrown) {
        ::havel::stdlib::notifyRuntimeError(thrown.value.toString());
        if (!handleScriptThrow(thrown.value)) {
            throw ScriptError(thrown.value, "Uncaught exception", "", 0, 0);
        }
    } catch (const std::runtime_error &e) {
        Value exceptionValue = Value::makeStringId(heap_.allocateString(e.what()).id);
        ::havel::stdlib::notifyRuntimeError(e.what());
        if (handleScriptThrow(exceptionValue)) {
            // caught - continue
        } else {
            throw std::runtime_error(e.what());
        }
    }
    counter++;
    if ((counter & 8191) == 0) {
        if (exit_requested_.load()) return;
        maybeCollectGarbage();
        periodicYieldCheck();
    }
    if (frame_count_ == 0 || frame_count_ <= stop_frame_depth) return;
    {
        auto &f2 = frame_arena_[frame_count_ - 1];
        if (f2.ip >= f2.function->instructions.size()) {
            stack.push(nullptr);
            executeInstruction(Instruction{OpCode::RETURN});
            return;
        }
        goto *dispatch_table[static_cast<uint8_t>(f2.function->instructions[f2.ip].opcode)];
    }
}

op_STORE_GLOBAL: {
    auto &frm = frame_arena_[frame_count_ - 1];
    const auto &inst = frm.function->instructions[frm.ip];
    frm.ip++;
    try {
        executeInstruction(inst);
    } catch (const std::runtime_error &e) {
        Value exceptionValue = Value::makeStringId(heap_.allocateString(e.what()).id);
        ::havel::stdlib::notifyRuntimeError(e.what());
        if (handleScriptThrow(exceptionValue)) {
            // caught
        } else {
            throw std::runtime_error(e.what());
        }
    }
    counter++;
    if ((counter & 8191) == 0) {
        if (exit_requested_.load()) return;
        maybeCollectGarbage();
        periodicYieldCheck();
    }
    if (frame_count_ == 0 || frame_count_ <= stop_frame_depth) return;
    {
        auto &f2 = frame_arena_[frame_count_ - 1];
        if (f2.ip >= f2.function->instructions.size()) {
            stack.push(nullptr);
            executeInstruction(Instruction{OpCode::RETURN});
            return;
        }
        goto *dispatch_table[static_cast<uint8_t>(f2.function->instructions[f2.ip].opcode)];
    }
}

op_STORE_IMMUT_GLOBAL: {
    auto &frm = frame_arena_[frame_count_ - 1];
    const auto &inst = frm.function->instructions[frm.ip];
    frm.ip++;
    try {
        executeInstruction(inst);
    } catch (const std::runtime_error &e) {
        Value exceptionValue = Value::makeStringId(heap_.allocateString(e.what()).id);
        ::havel::stdlib::notifyRuntimeError(e.what());
        if (handleScriptThrow(exceptionValue)) {
        } else {
            throw std::runtime_error(e.what());
        }
    }
    counter++;
    if ((counter & 8191) == 0) {
        if (exit_requested_.load()) return;
        maybeCollectGarbage();
        periodicYieldCheck();
    }
    if (frame_count_ == 0 || frame_count_ <= stop_frame_depth) return;
    {
        auto &f2 = frame_arena_[frame_count_ - 1];
        if (f2.ip >= f2.function->instructions.size()) {
            stack.push(nullptr);
            executeInstruction(Instruction{OpCode::RETURN});
            return;
        }
        goto *dispatch_table[static_cast<uint8_t>(f2.function->instructions[f2.ip].opcode)];
    }
}

op_STORE_IMMUT_VAR: {
    auto &frm = frame_arena_[frame_count_ - 1];
    const auto &inst = frm.function->instructions[frm.ip];
    frm.ip++;
    try {
        executeInstruction(inst);
    } catch (const std::runtime_error &e) {
        throw std::runtime_error(e.what());
    }
    counter++;
    if ((counter & 8191) == 0) {
        if (exit_requested_.load()) return;
        maybeCollectGarbage();
        periodicYieldCheck();
    }
    if (frame_count_ == 0 || frame_count_ <= stop_frame_depth) return;
    {
        auto &f2 = frame_arena_[frame_count_ - 1];
        if (f2.ip >= f2.function->instructions.size()) {
            stack.push(nullptr);
            executeInstruction(Instruction{OpCode::RETURN});
            return;
        }
        goto *dispatch_table[static_cast<uint8_t>(f2.function->instructions[f2.ip].opcode)];
    }
}

op_LOAD_UPVALUE: {
    auto &frm = frame_arena_[frame_count_ - 1];
    frm.ip++;
    try { executeInstruction(frm.function->instructions[frm.ip - 1]); }
    catch (const std::runtime_error &e) { throw std::runtime_error(e.what()); }
    counter++;
    if ((counter & 8191) == 0) {
        if (exit_requested_.load()) return;
        maybeCollectGarbage();
        periodicYieldCheck();
    }
    if (frame_count_ == 0 || frame_count_ <= stop_frame_depth) return;
    {
        auto &f2 = frame_arena_[frame_count_ - 1];
        if (f2.ip >= f2.function->instructions.size()) {
            stack.push(nullptr);
            executeInstruction(Instruction{OpCode::RETURN});
            return;
        }
        goto *dispatch_table[static_cast<uint8_t>(f2.function->instructions[f2.ip].opcode)];
    }
}

op_STORE_UPVALUE: {
    auto &frm = frame_arena_[frame_count_ - 1];
    frm.ip++;
    try { executeInstruction(frm.function->instructions[frm.ip - 1]); }
    catch (const std::runtime_error &e) { throw std::runtime_error(e.what()); }
    counter++;
    if ((counter & 8191) == 0) {
        if (exit_requested_.load()) return;
        maybeCollectGarbage();
        periodicYieldCheck();
    }
    if (frame_count_ == 0 || frame_count_ <= stop_frame_depth) return;
    {
        auto &f2 = frame_arena_[frame_count_ - 1];
        if (f2.ip >= f2.function->instructions.size()) {
            stack.push(nullptr);
            executeInstruction(Instruction{OpCode::RETURN});
            return;
        }
        goto *dispatch_table[static_cast<uint8_t>(f2.function->instructions[f2.ip].opcode)];
    }
}

op_DUP: {
    auto &frm = frame_arena_[frame_count_ - 1];
    frm.ip++;
    Value value = popStack();
    pushStack(value);
    pushStack(value);
    if (frame_count_ == 0 || frame_count_ <= stop_frame_depth) return;
    {
        auto &f2 = frame_arena_[frame_count_ - 1];
        if (f2.ip >= f2.function->instructions.size()) {
            stack.push(nullptr);
            executeInstruction(Instruction{OpCode::RETURN});
            return;
        }
        goto *dispatch_table[static_cast<uint8_t>(f2.function->instructions[f2.ip].opcode)];
    }
}

op_SWAP: {
    auto &frm = frame_arena_[frame_count_ - 1];
    frm.ip++;
    Value top = popStack();
    Value next = popStack();
    pushStack(top);
    pushStack(next);
    if (frame_count_ == 0 || frame_count_ <= stop_frame_depth) return;
    {
        auto &f2 = frame_arena_[frame_count_ - 1];
        if (f2.ip >= f2.function->instructions.size()) {
            stack.push(nullptr);
            executeInstruction(Instruction{OpCode::RETURN});
            return;
        }
        goto *dispatch_table[static_cast<uint8_t>(f2.function->instructions[f2.ip].opcode)];
    }
}

op_INCLOCAL: {
    auto &frm = frame_arena_[frame_count_ - 1];
    const auto &inst = frm.function->instructions[frm.ip];
    frm.ip++;
    uint32_t var_index = inst.operands[0].asInt();
    uint32_t abs = this->toAbsoluteLocal(var_index);
    this->ensureLocalIndex(abs);
    Value& val = locals[abs];
    if (val.isInt()) {
        val = Value::makeInt(val.asInt() + 1);
        pushStack(val);
    } else if (val.isDouble()) {
        val = Value::makeDouble(val.asDouble() + 1.0);
        pushStack(val);
    } else {
        COMPILER_THROW("Cannot increment non-numeric value");
    }
    counter++;
    if ((counter & 8191) == 0) {
        if (exit_requested_.load()) return;
        maybeCollectGarbage();
        periodicYieldCheck();
    }
    if (frame_count_ == 0 || frame_count_ <= stop_frame_depth) return;
    {
        auto &f2 = frame_arena_[frame_count_ - 1];
        if (f2.ip >= f2.function->instructions.size()) {
            stack.push(nullptr);
            executeInstruction(Instruction{OpCode::RETURN});
            return;
        }
        goto *dispatch_table[static_cast<uint8_t>(f2.function->instructions[f2.ip].opcode)];
    }
}

op_DECLOCAL: {
    auto &frm = frame_arena_[frame_count_ - 1];
    const auto &inst = frm.function->instructions[frm.ip];
    frm.ip++;
    uint32_t var_index = inst.operands[0].asInt();
    uint32_t abs = this->toAbsoluteLocal(var_index);
    this->ensureLocalIndex(abs);
    Value& val = locals[abs];
    if (val.isInt()) {
        val = Value::makeInt(val.asInt() - 1);
        pushStack(val);
    } else if (val.isDouble()) {
        val = Value::makeDouble(val.asDouble() - 1.0);
        pushStack(val);
    } else {
        COMPILER_THROW("Cannot decrement non-numeric value");
    }
    counter++;
    if ((counter & 8191) == 0) {
        if (exit_requested_.load()) return;
        maybeCollectGarbage();
        periodicYieldCheck();
    }
    if (frame_count_ == 0 || frame_count_ <= stop_frame_depth) return;
    {
        auto &f2 = frame_arena_[frame_count_ - 1];
        if (f2.ip >= f2.function->instructions.size()) {
            stack.push(nullptr);
            executeInstruction(Instruction{OpCode::RETURN});
            return;
        }
        goto *dispatch_table[static_cast<uint8_t>(f2.function->instructions[f2.ip].opcode)];
    }
}

op_INCLOCAL_POST: {
    auto &frm = frame_arena_[frame_count_ - 1];
    const auto &inst = frm.function->instructions[frm.ip];
    frm.ip++;
    uint32_t var_index = inst.operands[0].asInt();
    uint32_t abs = this->toAbsoluteLocal(var_index);
    this->ensureLocalIndex(abs);
    Value old = locals[abs];
    pushStack(old);
    if (old.isInt()) {
        locals[abs] = Value::makeInt(old.asInt() + 1);
    } else if (old.isDouble()) {
        locals[abs] = Value::makeDouble(old.asDouble() + 1.0);
    }
    counter++;
    if ((counter & 8191) == 0) {
        if (exit_requested_.load()) return;
        maybeCollectGarbage();
        periodicYieldCheck();
    }
    if (frame_count_ == 0 || frame_count_ <= stop_frame_depth) return;
    {
        auto &f2 = frame_arena_[frame_count_ - 1];
        if (f2.ip >= f2.function->instructions.size()) {
            stack.push(nullptr);
            executeInstruction(Instruction{OpCode::RETURN});
            return;
        }
        goto *dispatch_table[static_cast<uint8_t>(f2.function->instructions[f2.ip].opcode)];
    }
}

op_DECLOCAL_POST: {
    auto &frm = frame_arena_[frame_count_ - 1];
    const auto &inst = frm.function->instructions[frm.ip];
    frm.ip++;
    uint32_t var_index = inst.operands[0].asInt();
    uint32_t abs = this->toAbsoluteLocal(var_index);
    this->ensureLocalIndex(abs);
    Value old = locals[abs];
    pushStack(old);
    if (old.isInt()) {
        locals[abs] = Value::makeInt(old.asInt() - 1);
    } else if (old.isDouble()) {
        locals[abs] = Value::makeDouble(old.asDouble() - 1.0);
    }
    counter++;
    if ((counter & 8191) == 0) {
        if (exit_requested_.load()) return;
        maybeCollectGarbage();
        periodicYieldCheck();
    }
    if (frame_count_ == 0 || frame_count_ <= stop_frame_depth) return;
    {
        auto &f2 = frame_arena_[frame_count_ - 1];
        if (f2.ip >= f2.function->instructions.size()) {
            stack.push(nullptr);
            executeInstruction(Instruction{OpCode::RETURN});
            return;
        }
        goto *dispatch_table[static_cast<uint8_t>(f2.function->instructions[f2.ip].opcode)];
    }
}

op_ADD: op_SUB: op_MUL: op_DIV:
op_INT_DIV: op_MOD: op_DIVMOD:
op_REMAINDER: op_POW: op_EQ:
op_NEQ: op_IS: op_LT:
op_LTE: op_GT: op_GTE:
op_BIT_AND: op_BIT_OR: op_BIT_XOR:
op_BIT_LSH: op_BIT_RSH: {
    auto &frm = frame_arena_[frame_count_ - 1];
    const auto &inst = frm.function->instructions[frm.ip];
    frm.ip++;
    execBinaryOp(inst);
    counter++;
    if ((counter & 8191) == 0) {
        if (exit_requested_.load()) return;
        maybeCollectGarbage();
        periodicYieldCheck();
    }
    if (frame_count_ == 0 || frame_count_ <= stop_frame_depth) return;
    {
        auto &f2 = frame_arena_[frame_count_ - 1];
        if (f2.ip >= f2.function->instructions.size()) {
            stack.push(nullptr);
            executeInstruction(Instruction{OpCode::RETURN});
            return;
        }
        goto *dispatch_table[static_cast<uint8_t>(f2.function->instructions[f2.ip].opcode)];
    }
}

op_AND: op_OR: {
    auto &frm = frame_arena_[frame_count_ - 1];
    const auto &inst = frm.function->instructions[frm.ip];
    frm.ip++;
    execLogicalOp(inst.opcode);
    if (frame_count_ == 0 || frame_count_ <= stop_frame_depth) return;
    {
        auto &f2 = frame_arena_[frame_count_ - 1];
        if (f2.ip >= f2.function->instructions.size()) {
            stack.push(nullptr);
            executeInstruction(Instruction{OpCode::RETURN});
            return;
        }
        goto *dispatch_table[static_cast<uint8_t>(f2.function->instructions[f2.ip].opcode)];
    }
}

op_NOT: {
    auto &frm = frame_arena_[frame_count_ - 1];
    frm.ip++;
    Value v = popStack();
    pushStack(!isTruthy(v));
    if (frame_count_ == 0 || frame_count_ <= stop_frame_depth) return;
    {
        auto &f2 = frame_arena_[frame_count_ - 1];
        if (f2.ip >= f2.function->instructions.size()) {
            stack.push(nullptr);
            executeInstruction(Instruction{OpCode::RETURN});
            return;
        }
        goto *dispatch_table[static_cast<uint8_t>(f2.function->instructions[f2.ip].opcode)];
    }
}

op_BIT_NOT: {
    auto &frm = frame_arena_[frame_count_ - 1];
    frm.ip++;
    Value v = popStack();
    if (v.isInt()) pushStack(~v.asInt());
    else if (v.isDouble()) pushStack(~static_cast<int64_t>(v.asDouble()));
    else COMPILER_THROW("Bitwise NOT requires integer operand");
    if (frame_count_ == 0 || frame_count_ <= stop_frame_depth) return;
    {
        auto &f2 = frame_arena_[frame_count_ - 1];
        if (f2.ip >= f2.function->instructions.size()) {
            stack.push(nullptr);
            executeInstruction(Instruction{OpCode::RETURN});
            return;
        }
        goto *dispatch_table[static_cast<uint8_t>(f2.function->instructions[f2.ip].opcode)];
    }
}

op_NEGATE: {
    auto &frm = frame_arena_[frame_count_ - 1];
    frm.ip++;
    execNegate();
    if (frame_count_ == 0 || frame_count_ <= stop_frame_depth) return;
    {
        auto &f2 = frame_arena_[frame_count_ - 1];
        if (f2.ip >= f2.function->instructions.size()) {
            stack.push(nullptr);
            executeInstruction(Instruction{OpCode::RETURN});
            return;
        }
        goto *dispatch_table[static_cast<uint8_t>(f2.function->instructions[f2.ip].opcode)];
    }
}

op_LENGTH: {
    auto &frm = frame_arena_[frame_count_ - 1];
    frm.ip++;
    pushStack(execLengthOp(popStack()));
    if (frame_count_ == 0 || frame_count_ <= stop_frame_depth) return;
    {
        auto &f2 = frame_arena_[frame_count_ - 1];
        if (f2.ip >= f2.function->instructions.size()) {
            stack.push(nullptr);
            executeInstruction(Instruction{OpCode::RETURN});
            return;
        }
        goto *dispatch_table[static_cast<uint8_t>(f2.function->instructions[f2.ip].opcode)];
    }
}

op_JUMP: {
    auto &frm = frame_arena_[frame_count_ - 1];
    const auto &inst = frm.function->instructions[frm.ip];
    execJump(inst);
    if (frame_count_ == 0 || frame_count_ <= stop_frame_depth) return;
    {
        auto &f2 = frame_arena_[frame_count_ - 1];
        if (f2.ip >= f2.function->instructions.size()) {
            stack.push(nullptr);
            executeInstruction(Instruction{OpCode::RETURN});
            return;
        }
        goto *dispatch_table[static_cast<uint8_t>(f2.function->instructions[f2.ip].opcode)];
    }
}

op_JUMP_IF_FALSE: {
    auto &frm = frame_arena_[frame_count_ - 1];
    const auto &inst = frm.function->instructions[frm.ip];
    execJumpIfFalse(inst);
    if (frame_count_ == 0 || frame_count_ <= stop_frame_depth) return;
    {
        auto &f2 = frame_arena_[frame_count_ - 1];
        if (f2.ip >= f2.function->instructions.size()) {
            stack.push(nullptr);
            executeInstruction(Instruction{OpCode::RETURN});
            return;
        }
        goto *dispatch_table[static_cast<uint8_t>(f2.function->instructions[f2.ip].opcode)];
    }
}

op_JUMP_IF_TRUE: {
    auto &frm = frame_arena_[frame_count_ - 1];
    const auto &inst = frm.function->instructions[frm.ip];
    execJumpIfTrue(inst);
    if (frame_count_ == 0 || frame_count_ <= stop_frame_depth) return;
    {
        auto &f2 = frame_arena_[frame_count_ - 1];
        if (f2.ip >= f2.function->instructions.size()) {
            stack.push(nullptr);
            executeInstruction(Instruction{OpCode::RETURN});
            return;
        }
        goto *dispatch_table[static_cast<uint8_t>(f2.function->instructions[f2.ip].opcode)];
    }
}

op_IS_NULL: {
    auto &frm = frame_arena_[frame_count_ - 1];
    frm.ip++;
    Value value = popStack();
    pushStack(Value::makeBool(value.isNull()));
    if (frame_count_ == 0 || frame_count_ <= stop_frame_depth) return;
    {
        auto &f2 = frame_arena_[frame_count_ - 1];
        if (f2.ip >= f2.function->instructions.size()) {
            stack.push(nullptr);
            executeInstruction(Instruction{OpCode::RETURN});
            return;
        }
        goto *dispatch_table[static_cast<uint8_t>(f2.function->instructions[f2.ip].opcode)];
    }
}

op_JUMP_IF_NULL: {
    auto &frm = frame_arena_[frame_count_ - 1];
    const auto &inst = frm.function->instructions[frm.ip];
    uint32_t target = inst.operands[0].asInt();
    Value value = popStack();
    if (value.isNull()) {
        frm.ip = target;
    } else {
        frm.ip++;
    }
    if (frame_count_ == 0 || frame_count_ <= stop_frame_depth) return;
    {
        auto &f2 = frame_arena_[frame_count_ - 1];
        if (f2.ip >= f2.function->instructions.size()) {
            stack.push(nullptr);
            executeInstruction(Instruction{OpCode::RETURN});
            return;
        }
        goto *dispatch_table[static_cast<uint8_t>(f2.function->instructions[f2.ip].opcode)];
    }
}

slow_dispatch_fallback:
    // Suspension or complex opcode encountered — return to caller's slow path
    return;

op_default: {
    auto &frm = frame_arena_[frame_count_ - 1];
    const auto &inst = frm.function->instructions[frm.ip];
    frm.ip++;
    try {
        executeInstruction(inst);
    } catch (const ScriptThrow &thrown) {
        ::havel::stdlib::notifyRuntimeError(thrown.value.toString());
        if (!handleScriptThrow(thrown.value)) {
            throw ScriptError(thrown.value, "Uncaught exception", "", 0, 0);
        }
    } catch (const std::runtime_error &e) {
        Value exceptionValue = Value::makeStringId(heap_.allocateString(e.what()).id);
        ::havel::stdlib::notifyRuntimeError(e.what());
        if (handleScriptThrow(exceptionValue)) {
        } else {
            throw std::runtime_error(e.what());
        }
    }
    if (suspension_requested_) goto slow_dispatch_fallback;
    counter++;
    if ((counter & 8191) == 0) {
        if (exit_requested_.load()) return;
        maybeCollectGarbage();
        periodicYieldCheck();
        if (!pending_calls.empty()) {
            processPendingCalls();
            if (exit_requested_.load()) return;
        }
    }
    if (frame_count_ == 0 || frame_count_ <= stop_frame_depth) return;
    {
        auto &f2 = frame_arena_[frame_count_ - 1];
        if (f2.ip >= f2.function->instructions.size()) {
            stack.push(nullptr);
            executeInstruction(Instruction{OpCode::RETURN});
            return;
        }
        goto *dispatch_table[static_cast<uint8_t>(f2.function->instructions[f2.ip].opcode)];
    }
}

#undef DISPATCH_NEXT
#undef DISPATCH_OP
}

#endif // HAVE_COMPUTED_GOTO


} // namespace havel::compiler
