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
        &&op_LOAD_CONST,        // 0
        &&op_LOAD_GLOBAL,       // 1  (delegates to executeInstruction)
        &&op_STORE_GLOBAL,      // 2  (delegates to executeInstruction)
        &&op_STORE_IMMUT_GLOBAL,// 3  (delegates to executeInstruction)
        &&op_LOAD_VAR,          // 4
        &&op_STORE_VAR,         // 5
        &&op_STORE_IMMUT_VAR,   // 6  (delegates to executeInstruction)
        &&op_LOAD_UPVALUE,      // 7  (delegates to executeInstruction)
        &&op_STORE_UPVALUE,     // 8  (delegates to executeInstruction)
        &&op_POP,               // 9
        &&op_DUP,               // 10
        &&op_SWAP,              // 11
        &&op_PUSH_NULL,         // 12
        &&op_ADD,               // 13 (binary ops group)
        &&op_SUB,               // 14 (binary ops group)
        &&op_MUL,               // 15 (binary ops group)
        &&op_DIV,               // 16 (binary ops group)
        &&op_INT_DIV,           // 17 (binary ops group)
        &&op_DIVMOD,            // 18 (binary ops group)
        &&op_REMAINDER,         // 19 (binary ops group)
        &&op_MOD,               // 20 (binary ops group)
        &&op_POW,               // 21 (binary ops group)
        &&op_default,           // 22  ADD_ASSIGN
        &&op_default,           // 23  SUB_ASSIGN
        &&op_default,           // 24  MUL_ASSIGN
        &&op_default,           // 25  DIV_ASSIGN
        &&op_default,           // 26  INT_DIV_ASSIGN
        &&op_default,           // 27  REMAINDER_ASSIGN
        &&op_default,           // 28  MOD_ASSIGN
        &&op_default,           // 29  POW_ASSIGN
        &&op_default,           // 30  BITWISE_AND_ASSIGN
        &&op_default,           // 31  BITWISE_OR_ASSIGN
        &&op_default,           // 32  BITWISE_XOR_ASSIGN
        &&op_default,           // 33  SHIFT_LEFT_ASSIGN
        &&op_default,           // 34  SHIFT_RIGHT_ASSIGN
        &&op_INCLOCAL,          // 35
        &&op_DECLOCAL,          // 36
        &&op_INCLOCAL_POST,     // 37
        &&op_DECLOCAL_POST,     // 38
        &&op_EQ,                // 39 (binary ops group)
        &&op_NEQ,               // 40 (binary ops group)
        &&op_IS,                // 41 (binary ops group)
        &&op_LT,                // 42 (binary ops group)
        &&op_LTE,               // 43 (binary ops group)
        &&op_GT,                // 44 (binary ops group)
        &&op_GTE,               // 45 (binary ops group)
        &&op_AND,               // 46 (logical ops group)
        &&op_OR,                // 47 (logical ops group)
        &&op_NOT,               // 48
        &&op_NEGATE,            // 49
        &&op_IS_NULL,           // 50
        &&op_BIT_AND,           // 51 (binary ops group)
        &&op_BIT_OR,            // 52 (binary ops group)
        &&op_BIT_XOR,           // 53 (binary ops group)
        &&op_BIT_LSH,           // 54 (binary ops group)
        &&op_BIT_RSH,           // 55 (binary ops group)
        &&op_BIT_NOT,           // 56
        &&op_LENGTH,            // 57
        &&op_JUMP,              // 58
        &&op_JUMP_IF_FALSE,     // 59
        &&op_JUMP_IF_TRUE,      // 60
        &&op_JUMP_IF_NULL,      // 61
        &&op_CALL,              // 62
        &&op_default,           // 63  TAIL_CALL
        &&op_default,           // 64  CALL_METHOD
        &&op_RETURN,            // 65
        &&op_default,           // 66  TRY_ENTER
        &&op_default,           // 67  TRY_EXIT
        &&op_default,           // 68  LOAD_EXCEPTION
        &&op_default,           // 69  THROW
        &&op_default,           // 70  DEFINE_FUNC
        &&op_default,           // 71  CLOSURE
        &&op_default,           // 72  ARRAY_NEW
        &&op_default,           // 73  ARRAY_GET
        &&op_default,           // 74  ARRAY_SET
        &&op_default,           // 75  ARRAY_DEL
        &&op_default,           // 76  ARRAY_PUSH
        &&op_default,           // 77  ARRAY_LEN
        &&op_default,           // 78  ARRAY_FREEZE
        &&op_default,           // 79  SET_SET
        &&op_default,           // 80  SET_DEL
        &&op_default,           // 81  SET_HAS
        &&op_default,           // 82  SET_NEW
        &&op_default,           // 83  OBJECT_NEW
        &&op_default,           // 84  OBJECT_GET
        &&op_default,           // 85  OBJECT_SET
        &&op_default,           // 86  OBJECT_DEL
        &&op_default,           // 87  OBJECT_HAS
        &&op_default,           // 88  STRING_NEW
        &&op_default,           // 89  STRING_GET
        &&op_default,           // 90  STRING_LEN
        &&op_default,           // 91  STRING_CAT
        &&op_default,           // 92  STRING_CMP
        &&op_default,           // 93  STRING_FORMAT
        &&op_default,           // 94  IMPORT
        &&op_default,           // 95  IMPORT_WILDCARD
        &&op_default,           // 96  HALT
        &&op_default,           // 97  NOP
        &&op_default,           // 98  DUP_N
        &&op_default,           // 99  INCGLOBAL
        &&op_default,           // 100 DECGLOBAL
        &&op_default,           // 101 INCGLOBAL_POST
        &&op_default,           // 102 DECGLOBAL_POST
        &&op_default,           // 103 CALL_HOST
        &&op_default,           // 104 CALL_HOST_NAMED
        &&op_default,           // 105 MAKE_REGEX
        &&op_default,           // 106 MAKE_ITERATOR
        &&op_default,           // 107 ITER_NEXT
        &&op_default,           // 108 STRING_BEGIN_INTERP
        &&op_default,           // 109 STRING_END_INTERP
        &&op_default,           // 110 YIELD
        &&op_default,           // 111 RESUME
        &&op_default,           // 112 GO
        &&op_default,           // 113 CHANNEL_SEND
        &&op_default,           // 114 CHANNEL_RECV
        &&op_default,           // 115 CHANNEL_CLOSE
        &&op_default,           // 116 WAITGROUP_ADD
        &&op_default,           // 117 WAITGROUP_DONE
        &&op_default,           // 118 WAITGROUP_WAIT
        &&op_default,           // 119 LOCK
        &&op_default,           // 120 UNLOCK
        &&op_default,           // 121 SYNC
        &&op_default,           // 122 ASYNC
        &&op_default,           // 123 AWAIT
        &&op_default,           // 124 SELECT
        &&op_default,           // 125 DEFER
        &&op_default,           // 126 RANGE_NEW
        &&op_default,           // 127 RANGE_ITER
        &&op_default,           // 128 TYPE_CHECK
        &&op_default,           // 129 TYPE_CAST
        &&op_default,           // 130 ISINSTANCE
        &&op_default,           // 131 SUPER_CALL
        &&op_default,           // 132 SPREAD
        &&op_default,           // 133 REST
        &&op_default,           // 134 DEFAULT_VALUE
        &&op_default,           // 135 TUPLE_NEW
        &&op_default,           // 136 MAKE_TUPLE
        &&op_default,           // 137 TUPLE_GET
        &&op_default,           // 138 TUPLE_SET
        &&op_default,           // 139 TUPLE_LEN
        &&op_default,           // 140 FPRINTF
        &&op_default,           // 141 MAKE_SET
        &&op_default,           // 142 STRING_UPPER
        &&op_default,           // 143 STRING_LOWER
        &&op_default,           // 144 STRING_TRIM
        &&op_default,           // 145 STRING_SPLIT
        &&op_default,           // 146 STRING_FIND
        &&op_default,           // 147 STRING_SLICE
        &&op_default,           // 148 STRING_REPEAT
        &&op_default,           // 149 STRING_REPLACE
        &&op_default,           // 150 STRING_STARTS
        &&op_default,           // 151 STRING_ENDS
        &&op_default,           // 152 STRING_CHARS
        &&op_default,           // 153 ARRAY_SORT
        &&op_default,           // 154 ARRAY_MAP
        &&op_default,           // 155 ARRAY_FILTER
        &&op_default,           // 156 ARRAY_REDUCE
        &&op_default,           // 157 ARRAY_FIND
        &&op_default,           // 158 ARRAY_ANY
        &&op_default,           // 159 ARRAY_ALL
        &&op_default,           // 160 ARRAY_FLAT
        &&op_default,           // 161 ARRAY_FLATMAP
        &&op_default,           // 162 ARRAY_REVERSE
        &&op_default,           // 163 ARRAY_JOIN
        &&op_default,           // 164 ARRAY_INCLUDES
        &&op_default,           // 165 ARRAY_UNIQUE
        &&op_default,           // 166 BIT_CLZ
        &&op_default,           // 167 BIT_CTZ
        &&op_default,           // 168 BIT_POPCNT
        &&op_default,           // 169 BIT_BSWAP
        &&op_default,           // 170 BIT_ROTL
        &&op_default,           // 171 BIT_ROTR
        &&op_default,           // 172 TIME_NOW
        &&op_default,           // 173 FORMAT_HEX
        &&op_default,           // 174 FORMAT_UNHEX
        &&op_default,           // 175 FORMAT_BASE64_ENCODE
        &&op_default,           // 176 FORMAT_BASE64_DECODE
        &&op_default            // 177+ catch-all
    };
    // Pad remaining entries (178-255) with default handler
    for (int i = 178; i < 256; ++i) dispatch_table[i] = &&op_default;

    size_t counter = 0;

    // Fetch first instruction
    if (frame_count_ == 0) return;
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
        if (!pending_calls.empty()) {
            processPendingCalls();
            if (exit_requested_.load()) return;
        }
    }
    if (frame_count_ == 0) return;
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
    }
    if (frame_count_ == 0) return;
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
    }
    if (frame_count_ == 0) return;
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
    }
    if (frame_count_ == 0) return;
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
    if (frame_count_ == 0) return;
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
    fprintf(stderr, "DBG op_CALL: ip=%u arg_count=%u frame_count=%zu stack_size=%zu\n", frm.ip-1, inst.operands.empty() ? 999u : inst.operands[0].asInt(), frame_count_, stack.size());
    try {
        executeInstruction(inst);
    } catch (const ScriptThrow &thrown) {
        if (!handleScriptThrow(thrown.value)) {
            throw ScriptError(thrown.value, "Uncaught exception", "", 0, 0);
        }
    } catch (const std::runtime_error &e) {
        Value exceptionValue = Value::makeStringId(heap_.allocateString(e.what()).id);
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
        if (!pending_calls.empty()) {
            processPendingCalls();
            if (exit_requested_.load()) return;
        }
    }
    if (suspension_requested_) goto slow_dispatch_fallback;
    if (frame_count_ == 0) return;
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
        if (!handleScriptThrow(thrown.value)) {
            throw ScriptError(thrown.value, "Uncaught exception", "", 0, 0);
        }
    } catch (const std::runtime_error &e) {
        throw std::runtime_error(e.what());
    }
    if (frame_count_ == 0) return;
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
        if (!handleScriptThrow(thrown.value)) {
            throw ScriptError(thrown.value, "Uncaught exception", "", 0, 0);
        }
    } catch (const std::runtime_error &e) {
        Value exceptionValue = Value::makeStringId(heap_.allocateString(e.what()).id);
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
    }
    if (frame_count_ == 0) return;
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
    }
    if (frame_count_ == 0) return;
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
        if (handleScriptThrow(exceptionValue)) {
        } else {
            throw std::runtime_error(e.what());
        }
    }
    counter++;
    if ((counter & 8191) == 0) {
        if (exit_requested_.load()) return;
        maybeCollectGarbage();
    }
    if (frame_count_ == 0) return;
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
    }
    if (frame_count_ == 0) return;
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
    }
    if (frame_count_ == 0) return;
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
    }
    if (frame_count_ == 0) return;
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
    if (frame_count_ == 0) return;
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
    if (frame_count_ == 0) return;
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
    }
    if (frame_count_ == 0) return;
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
    }
    if (frame_count_ == 0) return;
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
    }
    if (frame_count_ == 0) return;
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
    }
    if (frame_count_ == 0) return;
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
    }
    if (frame_count_ == 0) return;
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
    if (frame_count_ == 0) return;
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
    if (frame_count_ == 0) return;
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
    if (frame_count_ == 0) return;
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
    if (frame_count_ == 0) return;
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
    if (frame_count_ == 0) return;
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
    if (frame_count_ == 0) return;
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
    if (frame_count_ == 0) return;
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
    if (frame_count_ == 0) return;
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
    if (frame_count_ == 0) return;
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
    if (frame_count_ == 0) return;
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
    if (inst.opcode == OpCode::ARRAY_MAP || inst.opcode == OpCode::CALL_METHOD || inst.opcode == OpCode::ARRAY_GET || inst.opcode == OpCode::DEFINE_FUNC || inst.opcode == OpCode::CLOSURE || inst.opcode == OpCode::CALL) {
        fprintf(stderr, "DBG op_default: ip=%u op=%d frame_count=%zu stack_size=%zu\n", frm.ip-1, static_cast<int>(inst.opcode), frame_count_, stack.size());
    }
    try {
        if (inst.opcode == OpCode::ARRAY_GET) {
            auto container = stack.top();
            fprintf(stderr, "DBG ARRAY_GET: container_type=%s, isArrayId=%d, bits=%lu, stack_depth=%zu\n", getTypeName(container).c_str(), container.isArrayId(), container.getTagBits(), stack.size());
        }
        executeInstruction(inst);
    } catch (const ScriptThrow &thrown) {
        if (!handleScriptThrow(thrown.value)) {
            throw ScriptError(thrown.value, "Uncaught exception", "", 0, 0);
        }
    } catch (const std::runtime_error &e) {
        Value exceptionValue = Value::makeStringId(heap_.allocateString(e.what()).id);
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
        if (!pending_calls.empty()) {
            processPendingCalls();
            if (exit_requested_.load()) return;
        }
    }
    if (frame_count_ == 0) return;
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
