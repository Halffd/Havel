#pragma once

// ===== Instruction Effects Model (TODO.md #13) =====
//
// Explicit, per-opcode instruction properties used by optimizing passes:
//
//   Pure          - no observable effect; removable when its result is unused
//   ReadOnly      - reads state (locals/globals/upvalues/heap) but writes
//                   nothing observable
//   MayThrow      - can raise a runtime error / trap for some operand values
//   HasSideEffects- writes observable state (locals, globals, heap, IO, ...)
//   Terminates    - ends the current linear instruction stream (terminator)
//   Allocates     - allocates GC-managed storage
//   Calls         - may invoke arbitrary user code
//
// `Pure` is encoded as the absence of every other flag (effects == None), so
// an opcode is removable iff `instruction_effect(op).effects == Effects::None`.
//
// The single switch in `instruction_effect` is the source of truth. It is
// exhaustive (no default): adding an OpCode forces a compiler warning here,
// which is the drift check that keeps the model in sync with the enum.
//
// Stack effects: `pops`/`pushes` count values consumed/produced. -1 means
// "unknown / operand-dependent"; optimizations must treat unknown stack
// effects as a barrier (never model the stack across them).

#include "BytecodeIR.hpp"

#include <cstdint>

namespace havel::compiler {

enum class Effects : uint16_t {
  None = 0,
  ReadOnly = 1 << 0,
  MayThrow = 1 << 1,
  HasSideEffects = 1 << 2,
  Terminates = 1 << 3,
  Allocates = 1 << 4,
  Calls = 1 << 5,
};

inline constexpr Effects operator|(Effects a, Effects b) {
  return static_cast<Effects>(static_cast<uint16_t>(a) | static_cast<uint16_t>(b));
}

inline constexpr bool has_flag(Effects e, Effects flag) {
  return (static_cast<uint16_t>(e) & static_cast<uint16_t>(flag)) != 0;
}

struct InstructionEffect {
  Effects effects = Effects::None;
  int32_t pops = 0;    // values consumed from the operand stack; -1 = unknown
  int32_t pushes = 0;  // values produced; -1 = unknown
};

// Classification of every OpCode. Conservative by design: an opcode is marked
// MayThrow/HasSideEffects unless the interpreter path for it was verified to
// be total and side-effect-free. Only the ops classified `None` become DCE
// removal candidates (see DeadCodeEliminationPass).
inline InstructionEffect instruction_effect(OpCode op) {
  switch (op) {
    // ---- Pure: total, no writes, no allocation, removable when dead ----
    case OpCode::LOAD_CONST:
      return {Effects::None, 0, 1};
    case OpCode::LOAD_VAR:  // local slot index always valid post-compile
      return {Effects::None, 0, 1};
    case OpCode::LOAD_UPVALUE:  // upvalue index always valid post-compile
      return {Effects::None, 0, 1};
    case OpCode::POP:
      return {Effects::None, 1, 0};
    case OpCode::DUP:
      return {Effects::None, 1, 2};
    case OpCode::SWAP:
      return {Effects::None, 2, 2};
    case OpCode::PUSH_NULL:
      return {Effects::None, 0, 1};
    case OpCode::IS_NULL:  // pops value, pushes bool; isNull is total
      return {Effects::None, 1, 1};
    case OpCode::NOT:  // pushStack(!isTruthy(v)); isTruthy is total
      return {Effects::None, 1, 1};
    case OpCode::AND:  // pushStack(isTruthy(l) && isTruthy(r)); total
      return {Effects::None, 2, 1};
    case OpCode::OR:  // pushStack(isTruthy(l) || isTruthy(r)); total
      return {Effects::None, 2, 1};
    case OpCode::NOP:
      return {Effects::None, 0, 0};

    // ---- Reads: safe to hoist, not removable when result unused if MayThrow ----
    case OpCode::LOAD_GLOBAL:  // missing global -> runtime error
      return {Effects::ReadOnly | Effects::MayThrow, 0, 1};
    case OpCode::TYPE_OF:  // total per value kind; keep ReadOnly (not removable)
      return {Effects::ReadOnly, 1, 1};
    case OpCode::TIME_NOW:  // observable (clock), but no writes
      return {Effects::ReadOnly, 0, 1};

    // ---- Writes to locals: barrier for stack DCE, removable only via
    //      liveness-driven dead-store elimination (see DCE) ----
    case OpCode::STORE_VAR:
    case OpCode::STORE_IMMUT_VAR:
      return {Effects::HasSideEffects, 1, 0};
    case OpCode::STORE_UPVALUE:
      return {Effects::HasSideEffects, 1, 0};
    case OpCode::STORE_GLOBAL:
    case OpCode::STORE_IMMUT_GLOBAL:
      return {Effects::HasSideEffects | Effects::MayThrow, 1, 0};

    // ---- Arithmetic ----
    case OpCode::ADD:
    case OpCode::SUB:
    case OpCode::MUL:
      // int/double paths are total; mixed/unsupported types raise.
      return {Effects::MayThrow, 2, 1};
    case OpCode::DIV:
    case OpCode::INT_DIV:
    case OpCode::REMAINDER:
    case OpCode::MOD:
      return {Effects::MayThrow, 2, 1};  // division by zero traps
    case OpCode::DIVMOD:
      return {Effects::MayThrow | Effects::Allocates, 2, 1};  // result array
    case OpCode::POW:
      return {Effects::MayThrow, 2, 1};
    case OpCode::ADD_INT:
    case OpCode::SUB_INT:
    case OpCode::MUL_INT:
      // Fast int path has no checks; signed overflow is UB (traps under
      // UBSAN), so conservatively MayThrow.
      return {Effects::MayThrow, 2, 1};
    case OpCode::DIV_INT:
    case OpCode::MOD_INT:
      return {Effects::MayThrow, 2, 1};  // division by zero traps
    case OpCode::NEGATE:  // throws on non-numeric
      return {Effects::MayThrow, 1, 1};
    case OpCode::INCLOCAL:
    case OpCode::DECLOCAL:
    case OpCode::INCLOCAL_POST:
    case OpCode::DECLOCAL_POST:
      // pops 0 (local index is an operand), pushes 1; throws on non-numeric.
      return {Effects::HasSideEffects | Effects::MayThrow, 0, 1};

    // ---- Compound assignments: lowered before reaching the bytecode VM;
    //      stack effect not verified -> unknown ----
    case OpCode::ADD_ASSIGN:
    case OpCode::SUB_ASSIGN:
    case OpCode::MUL_ASSIGN:
    case OpCode::DIV_ASSIGN:
    case OpCode::INT_DIV_ASSIGN:
    case OpCode::REMAINDER_ASSIGN:
    case OpCode::MOD_ASSIGN:
    case OpCode::POW_ASSIGN:
    case OpCode::BITWISE_AND_ASSIGN:
    case OpCode::BITWISE_OR_ASSIGN:
    case OpCode::BITWISE_XOR_ASSIGN:
    case OpCode::SHIFT_LEFT_ASSIGN:
    case OpCode::SHIFT_RIGHT_ASSIGN:
      return {Effects::HasSideEffects | Effects::MayThrow, -1, -1};

    // ---- Comparisons: int path total; incomparable types raise ----
    case OpCode::EQ:
    case OpCode::NEQ:
    case OpCode::LT:
    case OpCode::LTE:
    case OpCode::GT:
    case OpCode::GTE:
      return {Effects::MayThrow, 2, 1};
    case OpCode::IS:  // identity/equality check, no ordering semantics
      return {Effects::ReadOnly, 2, 1};

    // ---- Bitwise: int-only, throws otherwise ----
    case OpCode::BIT_AND:
    case OpCode::BIT_OR:
    case OpCode::BIT_XOR:
    case OpCode::BIT_LSH:
    case OpCode::BIT_RSH:
    case OpCode::BIT_NOT:
    case OpCode::BIT_POPCOUNT:
    case OpCode::BIT_CTZ:
    case OpCode::BIT_CLZ:
    case OpCode::BIT_BSWAP:
    case OpCode::BIT_ROTL:
    case OpCode::BIT_ROTR:
      return {Effects::MayThrow, op == OpCode::BIT_NOT ? 1 : 2,
              op == OpCode::BIT_NOT ? 1 : 1};

    // ---- Control flow (terminators) ----
    case OpCode::JUMP:
      return {Effects::Terminates, 0, 0};
    case OpCode::JUMP_IF_FALSE:
    case OpCode::JUMP_IF_TRUE:
    case OpCode::JUMP_IF_NULL:
      return {Effects::Terminates, 1, 0};
    case OpCode::RETURN:
      return {Effects::Terminates, 1, 0};
    case OpCode::TAIL_CALL:
      return {Effects::Terminates | Effects::MayThrow | Effects::Calls, -1, 0};
    case OpCode::THROW:
      return {Effects::Terminates | Effects::MayThrow, 1, 0};

    // ---- Calls ----
    case OpCode::CALL:
      // operand = arg count; pops callee + args, pushes result
      return {Effects::Calls | Effects::MayThrow | Effects::Allocates, -1, 1};
    case OpCode::CALL_DYN:
    case OpCode::CALL_SPREAD:
    case OpCode::CALL_METHOD:
    case OpCode::CALL_METHOD_SPREAD:
    case OpCode::FFI_CALL:
    case OpCode::SPREAD_CALL:
    case OpCode::CALL_IF_FUNCTION:
      return {Effects::Calls | Effects::MayThrow | Effects::Allocates, -1, 1};

    // ---- Exception handling ----
    case OpCode::TRY_ENTER:
    case OpCode::TRY_EXIT:
      return {Effects::HasSideEffects, 0, 0};
    case OpCode::LOAD_EXCEPTION:
      return {Effects::ReadOnly, 0, 1};

    // ---- Function ops ----
    case OpCode::DEFINE_FUNC:
    case OpCode::CLOSURE:
      return {Effects::Allocates | Effects::MayThrow, 0, 1};

    // ---- Arrays ----
    case OpCode::ARRAY_NEW:
      return {Effects::Allocates | Effects::MayThrow, 1, 1};
    case OpCode::ARRAY_GET:
    case OpCode::ARRAY_GET_FAST:
    case OpCode::ARRAY_LEN:
    case OpCode::ARRAY_HAS:
    case OpCode::ARRAY_FIND:
      return {Effects::ReadOnly | Effects::MayThrow, 2, 1};
    case OpCode::ARRAY_SET:
    case OpCode::ARRAY_SET_FAST:
    case OpCode::ARRAY_DEL:
    case OpCode::ARRAY_POP:
      return {Effects::HasSideEffects | Effects::MayThrow, 2, 1};
    case OpCode::ARRAY_PUSH:
      return {Effects::HasSideEffects | Effects::MayThrow, 2, 1};
    case OpCode::ARRAY_FREEZE:
      return {Effects::HasSideEffects | Effects::MayThrow, 1, 1};
    case OpCode::ARRAY_MAP:
    case OpCode::ARRAY_FILTER:
    case OpCode::ARRAY_REDUCE:
    case OpCode::ARRAY_FOREACH:
      // Function applied to elements: may run arbitrary user code.
      return {Effects::Calls | Effects::MayThrow | Effects::Allocates |
                  Effects::HasSideEffects,
              -1, -1};

    // ---- Sets / ranges / enums ----
    case OpCode::SET_SET:
    case OpCode::SET_DEL:
      return {Effects::HasSideEffects | Effects::MayThrow, -1, -1};
    case OpCode::SET_NEW:
      return {Effects::Allocates | Effects::MayThrow, 0, 1};
    case OpCode::RANGE_NEW:
    case OpCode::RANGE_STEP_NEW:
      return {Effects::Allocates | Effects::MayThrow, 2, 1};
    case OpCode::ENUM_NEW:
      return {Effects::Allocates | Effects::MayThrow, -1, 1};
    case OpCode::ENUM_TAG:
    case OpCode::ENUM_PAYLOAD:
      return {Effects::ReadOnly | Effects::MayThrow, 1, 1};
    case OpCode::ENUM_MATCH:
      return {Effects::MayThrow, -1, -1};

    // ---- Objects ----
    case OpCode::OBJECT_NEW:
    case OpCode::OBJECT_NEW_UNSORTED:
      return {Effects::Allocates | Effects::MayThrow, 0, 1};
    case OpCode::OBJECT_GET:
    case OpCode::OBJECT_GET_RAW:
    case OpCode::OBJECT_HAS:
    case OpCode::OBJECT_KEYS:
    case OpCode::OBJECT_VALUES:
    case OpCode::OBJECT_ENTRIES:
    case OpCode::OBJECT_SIZE:
    case OpCode::OBJECT_IS_FROZEN:
    case OpCode::OBJECT_IS_SEALED:
      return {Effects::ReadOnly | Effects::MayThrow, 2, 1};
    case OpCode::OBJECT_SET:
    case OpCode::OBJECT_DELETE:
    case OpCode::OBJECT_FREEZE:
    case OpCode::OBJECT_SEAL:
    case OpCode::OBJECT_ASSIGN:
      return {Effects::HasSideEffects | Effects::MayThrow, 2, 1};

    // ---- Strings ----
    case OpCode::STRING_CONCAT:
      return {Effects::Allocates | Effects::MayThrow, 2, 1};
    case OpCode::STRING_LEN:
    case OpCode::STRING_FIND:
    case OpCode::STRING_HAS:
    case OpCode::STRING_STARTS:
    case OpCode::STRING_ENDS:
    case OpCode::STRING_INCLUDES:
      return {Effects::ReadOnly | Effects::MayThrow, 2, 1};
    case OpCode::STRING_UPPER:
    case OpCode::STRING_LOWER:
    case OpCode::STRING_TRIM:
    case OpCode::STRING_TRIM_START:
    case OpCode::STRING_TRIM_END:
    case OpCode::STRING_SUB:
    case OpCode::STRING_SPLIT:
    case OpCode::STRING_REPLACE:
    case OpCode::STRING_REVERSE:
    case OpCode::STRING_REPEAT:
    case OpCode::STRING_PAD_START:
    case OpCode::STRING_PAD_END:
      return {Effects::Allocates | Effects::MayThrow, 2, 1};
    case OpCode::STRING_PROMOTE:
      return {Effects::ReadOnly | Effects::MayThrow, 1, 1};
    case OpCode::STRING_GET_FAST:
    case OpCode::STRING_GET_FAST_IP:
      return {Effects::ReadOnly | Effects::MayThrow, 2, 1};
    case OpCode::STRING_SET_FAST:
    case OpCode::STRING_SET_FAST_IP:
      return {Effects::HasSideEffects | Effects::MayThrow, 2, 1};
    case OpCode::STRING_CURSOR_NEW:
      return {Effects::Allocates | Effects::MayThrow, 1, 1};
    case OpCode::STRING_CURSOR_CURRENT:
    case OpCode::STRING_CURSOR_PEEK:
    case OpCode::STRING_CURSOR_GET_POS:
      return {Effects::ReadOnly | Effects::MayThrow, 1, 1};
    case OpCode::STRING_CURSOR_ADVANCE:
    case OpCode::STRING_CURSOR_RESET:
    case OpCode::STRING_CURSOR_SET_POS:
      return {Effects::HasSideEffects | Effects::MayThrow, -1, -1};

    // ---- Iteration ----
    case OpCode::ITER_NEW:
      return {Effects::Allocates | Effects::MayThrow, 1, 1};
    case OpCode::ITER_NEXT:
      return {Effects::MayThrow | Effects::Allocates, 1, 1};

    // ---- Conversion / reflection ----
    case OpCode::AS_TYPE:
    case OpCode::TO_INT:
    case OpCode::TO_FLOAT:
    case OpCode::TO_STRING:
    case OpCode::TO_BOOL:
      return {Effects::MayThrow, 1, 1};
    case OpCode::LENGTH:
      return {Effects::ReadOnly | Effects::MayThrow, 1, 1};

    // ---- Class / struct / protocol ----
    case OpCode::CLASS_NEW:
      return {Effects::Allocates | Effects::MayThrow, -1, 1};
    case OpCode::CLASS_GET_FIELD:
    case OpCode::LOAD_CLASS_PROTO:
      return {Effects::ReadOnly | Effects::MayThrow, 2, 1};
    case OpCode::CLASS_SET_FIELD:
      return {Effects::HasSideEffects | Effects::MayThrow, 2, 1};
    case OpCode::CALL_SUPER:
      return {Effects::Calls | Effects::MayThrow | Effects::Allocates, -1, 1};
    case OpCode::STRUCT_NEW:
      return {Effects::Allocates | Effects::MayThrow, -1, 1};
    case OpCode::STRUCT_GET:
      return {Effects::ReadOnly | Effects::MayThrow, 2, 1};
    case OpCode::STRUCT_SET:
      return {Effects::HasSideEffects | Effects::MayThrow, 2, 1};
    case OpCode::PROT_CHECK:
      return {Effects::ReadOnly | Effects::MayThrow, 1, 1};
    case OpCode::PROT_CAST:
      return {Effects::MayThrow, 1, 1};

    // ---- Modules / IO ----
    case OpCode::IMPORT:
    case OpCode::IMPORT_WILDCARD:
      return {Effects::HasSideEffects | Effects::MayThrow | Effects::Calls, -1, 1};
    case OpCode::PRINT:
    case OpCode::DEBUG:
      return {Effects::HasSideEffects, 1, 0};

    // ---- Concurrency / async ----
    case OpCode::THREAD_SPAWN:
    case OpCode::THREAD_JOIN:
    case OpCode::THREAD_SEND:
    case OpCode::THREAD_RECEIVE:
    case OpCode::INTERVAL_START:
    case OpCode::INTERVAL_STOP:
    case OpCode::TIMEOUT_START:
    case OpCode::TIMEOUT_CANCEL:
    case OpCode::YIELD:
    case OpCode::YIELD_RESUME:
    case OpCode::GO_ASYNC:
    case OpCode::FIBER_AWAIT:
    case OpCode::FIBER_SLEEP:
    case OpCode::CHANNEL_NEW:
    case OpCode::CHANNEL_SEND:
    case OpCode::CHANNEL_RECEIVE:
    case OpCode::CHANNEL_CLOSE:
    case OpCode::DEFER_PUSH:
    case OpCode::WAITGROUP_NEW:
    case OpCode::WAITGROUP_ADD:
    case OpCode::WAITGROUP_DONE:
    case OpCode::WAITGROUP_WAIT:
      return {Effects::HasSideEffects | Effects::MayThrow, -1, -1};

    // ---- Module context ----
    case OpCode::BEGIN_MODULE:
    case OpCode::END_MODULE:
      return {Effects::HasSideEffects | Effects::MayThrow, -1, -1};

    // ---- Spread ----
    case OpCode::SPREAD:
      return {Effects::Allocates | Effects::MayThrow, 1, -1};

    // ---- Math intrinsics (no allocation, no throw within domain) ----
    case OpCode::MATH_SIN:
    case OpCode::MATH_COS:
    case OpCode::MATH_TAN:
    case OpCode::MATH_ASIN:
    case OpCode::MATH_ACOS:
    case OpCode::MATH_ATAN:
    case OpCode::MATH_SINH:
    case OpCode::MATH_COSH:
    case OpCode::MATH_TANH:
    case OpCode::MATH_SQRT:
    case OpCode::MATH_LOG:
    case OpCode::MATH_LOG2:
    case OpCode::MATH_LOG10:
    case OpCode::MATH_EXP:
    case OpCode::MATH_CEIL:
    case OpCode::MATH_FLOOR:
    case OpCode::MATH_ROUND:
    case OpCode::MATH_ABS:
      return {Effects::ReadOnly | Effects::MayThrow, 1, 1};
    case OpCode::MATH_ATAN2:  // binary: atan2(l, r)
      return {Effects::ReadOnly | Effects::MayThrow, 2, 1};

    // ---- Format intrinsics (allocate strings) ----
    case OpCode::FORMAT_HEX:
    case OpCode::FORMAT_UNHEX:
    case OpCode::FORMAT_BASE64_ENCODE:
    case OpCode::FORMAT_BASE64_DECODE:
      return {Effects::Allocates | Effects::MayThrow, 1, 1};
  }
  // No default: every OpCode must be classified here. Reaching this line is a
  // compiler error (unhandled enum case) when a new opcode is added.
  return {Effects::HasSideEffects, -1, -1};
}

inline bool is_pure(OpCode op) {
  return instruction_effect(op).effects == Effects::None;
}

inline bool may_throw(OpCode op) {
  return has_flag(instruction_effect(op).effects, Effects::MayThrow);
}

inline bool has_side_effects(OpCode op) {
  return has_flag(instruction_effect(op).effects, Effects::HasSideEffects);
}

}  // namespace havel::compiler