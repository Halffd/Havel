#pragma once

#include "BytecodeIR.hpp"
#include "InstructionEffects.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

namespace havel::compiler {

// ===== Constant Propagation Analysis =====
//
// Computes, for every block, which locals hold a compile-time constant at the
// block's entry. The result feeds ConstPropagationPass, which rewrites
// LOAD_VAR -> LOAD_CONST, folds constant arithmetic/comparisons, and folds
// conditional terminators whose condition is constant.
//
// The lattice: Unknown (top) > Constant(Value). A slot only ever moves
// Constant -> Unknown (literal-derived constants are fixed; derived constants
// lose precision when their source does), so the worklist fixpoint terminates.
// The transfer walks each block with a transient abstract stack of
// optional<Value> (nullopt = could not be proven constant). The stack is what
// lets STORE_VAR record which value was stored and lets binary ops propagate
// constants; the earlier generic-DataflowAnalysis<> version lacked any
// inter-instruction stack model, so LOAD_CONST never reached its STORE_VAR
// and no local ever became constant.

namespace detail {

// Successor blocks of `bid` in the edge model, PLUS the implicit linear
// fall-through edge: JumpIf* fall through into block i+1 when the condition
// takes the non-jump arm, an Unreachable terminator lowers to a NOP (SimplifyCFG
// rewrites Jump(i+1) into it), and None is the trailing fall-through marker.
// Worklist propagation, predecessor lists, and reachability must all see that
// edge or constants/liveness do not cross those block boundaries and
// unreachable-block removal deletes live fall-through arms.
inline std::vector<uint32_t> successors_with_fallthrough(
    const std::vector<BasicBlock>& blocks, uint32_t bid) {
  std::vector<uint32_t> succ = blocks[bid].get_targets();
  const TerminatorKind tk = blocks[bid].terminator.kind;
  const bool falls_through = tk == TerminatorKind::None ||
                             tk == TerminatorKind::Unreachable ||
                             tk == TerminatorKind::JumpIfFalse ||
                             tk == TerminatorKind::JumpIfTrue ||
                             tk == TerminatorKind::JumpIfNull;
  if (falls_through && bid + 1 < blocks.size()) {
    succ.push_back(bid + 1);
  }
  return succ;
}

}  // namespace detail

enum class ConstantState : uint8_t {
  Unknown,    // May be anything (top of lattice)
  Constant,   // Known constant value
};

struct ConstantValue {
  ConstantState state = ConstantState::Unknown;
  Value value;  // Valid only if state == Constant

  ConstantValue() = default;
  explicit ConstantValue(ConstantState s) : state(s) {}
  ConstantValue(ConstantState s, Value v) : state(s), value(std::move(v)) {}

  static ConstantValue unknown() { return ConstantValue(ConstantState::Unknown); }
  static ConstantValue constant(Value v) { return ConstantValue(ConstantState::Constant, std::move(v)); }

  bool is_unknown() const { return state == ConstantState::Unknown; }
  bool is_constant() const { return state == ConstantState::Constant; }

  bool operator==(const ConstantValue& o) const {
    if (state != o.state) return false;
    return !is_constant() || value == o.value;
  }
  bool operator!=(const ConstantValue& o) const { return !(*this == o); }
};

// Per-local constant state map (indexed by local slot).
using ConstantMap = std::vector<ConstantValue>;

class ConstPropagationAnalysis {
public:
  // Intra-block abstract stack element: the proven constant, or nullopt.
  using StackVal = std::optional<Value>;
  using ConstantStack = std::vector<StackVal>;

  // Constant values that are safe to reason about at compile time: the
  // homomorphic subset of the VM value space. Heap values (strings, arrays,
  // objects, closures) are excluded.
  static bool foldable_const(const Value& v) {
    return v.isInt() || v.isDouble() || v.isBool() || v.isNull();
  }

  // VM isTruthy restricted to the foldable constants. Only called on consts
  // that passed foldable_const; the fall-through is never reached.
  static bool const_truthy(const Value& v) {
    if (v.isNull()) return false;
    if (v.isBool()) return v.asBool();
    if (v.isInt()) return v.asInt() != 0;
    if (v.isDouble()) return v.asDouble() != 0.0;
    return true;
  }

  // Fold a binary op over two compile-time constants, mirroring the VM's
  // dispatch order (VMArithmetic.cpp): null special cases first, then int/int,
  // then mixed numeric; EQ/NEQ use deep numeric/boolean equality; IS is
  // identity. Returns nullopt when the result cannot be proven (div by zero,
  // unsupported operand types, heap results).
  static std::optional<Value> try_fold(OpCode op, const Value& a, const Value& b) {
    // ---- null special case (dispatch head) ----
    if (a.isNull() || b.isNull()) {
      switch (op) {
        case OpCode::EQ:
          return Value::makeBool(a.isNull() && b.isNull());
        case OpCode::NEQ:
          return Value::makeBool(!(a.isNull() && b.isNull()));
        case OpCode::IS:
          return Value::makeBool(a.isNull() && b.isNull());
        case OpCode::LT:
        case OpCode::LTE:
        case OpCode::GT:
        case OpCode::GTE:
          return Value::makeBool(false);
        case OpCode::ADD:
        case OpCode::SUB:
        case OpCode::MUL:
        case OpCode::DIV:
        case OpCode::INT_DIV:
        case OpCode::REMAINDER:
        case OpCode::MOD:
        case OpCode::POW:
        case OpCode::BIT_AND:
        case OpCode::BIT_OR:
        case OpCode::BIT_XOR:
        case OpCode::BIT_LSH:
        case OpCode::BIT_RSH:
          return Value::makeNull();  // VM: arithmetic with null yields null
        default:
          return std::nullopt;
      }
    }

    // ---- equality: deep comparison (numeric mix allowed) ----
    if (op == OpCode::EQ || op == OpCode::NEQ) {
      if ((a.isInt() || a.isDouble()) && (b.isInt() || b.isDouble())) {
        const double l = a.isInt() ? static_cast<double>(a.asInt()) : a.asDouble();
        const double r = b.isInt() ? static_cast<double>(b.asInt()) : b.asDouble();
        return Value::makeBool(op == OpCode::EQ ? (l == r) : (l != r));
      }
      if (a.isBool() && b.isBool()) {
        return Value::makeBool(op == OpCode::EQ ? (a.asBool() == b.asBool())
                                                : (a.asBool() != b.asBool()));
      }
      return std::nullopt;
    }
    if (op == OpCode::IS) {
      if (a.isInt() && b.isInt()) return Value::makeBool(a.asInt() == b.asInt());
      if (a.isDouble() && b.isDouble()) return Value::makeBool(a.asDouble() == b.asDouble());
      if (a.isBool() && b.isBool()) return Value::makeBool(a.asBool() == b.asBool());
      return std::nullopt;
    }

    // ---- int/int ----
    if (a.isInt() && b.isInt()) {
      const int64_t l = a.asInt();
      const int64_t r = b.asInt();
      switch (op) {
        case OpCode::ADD:
        case OpCode::ADD_INT:
          return Value::makeInt(l + r);
        case OpCode::SUB:
        case OpCode::SUB_INT:
          return Value::makeInt(l - r);
        case OpCode::MUL:
        case OpCode::MUL_INT:
          return Value::makeInt(l * r);
        case OpCode::DIV:
          if (r == 0) return std::nullopt;  // runtime would throw
          return Value::makeDouble(static_cast<double>(l) / static_cast<double>(r));
        case OpCode::INT_DIV:
        case OpCode::DIV_INT:
          if (r == 0) return std::nullopt;
          return Value::makeInt(l / r);
        case OpCode::REMAINDER:
          if (r == 0) return std::nullopt;
          return Value::makeInt(l % r);
        case OpCode::MOD:
        case OpCode::MOD_INT: {
          if (r == 0) return std::nullopt;
          int64_t m = l % r;
          if (m != 0 && ((m < 0) != (r < 0))) m += r;
          return Value::makeInt(m);
        }
        case OpCode::LT:
          return Value::makeBool(l < r);
        case OpCode::LTE:
          return Value::makeBool(l <= r);
        case OpCode::GT:
          return Value::makeBool(l > r);
        case OpCode::GTE:
          return Value::makeBool(l >= r);
        case OpCode::BIT_AND:
          return Value::makeInt(l & r);
        case OpCode::BIT_OR:
          return Value::makeInt(l | r);
        case OpCode::BIT_XOR:
          return Value::makeInt(l ^ r);
        case OpCode::BIT_LSH:
          return Value::makeInt(l << (static_cast<uint64_t>(r) & 63));
        case OpCode::BIT_RSH:
          return Value::makeInt(l >> (static_cast<uint64_t>(r) & 63));
        case OpCode::AND:
          return Value::makeBool(const_truthy(a) && const_truthy(b));
        case OpCode::OR:
          return Value::makeBool(const_truthy(a) || const_truthy(b));
        default:
          return std::nullopt;
      }
    }

    // ---- mixed numeric (int/double in any combination) ----
    if ((a.isInt() || a.isDouble()) && (b.isInt() || b.isDouble())) {
      const double l = a.isInt() ? static_cast<double>(a.asInt()) : a.asDouble();
      const double r = b.isInt() ? static_cast<double>(b.asInt()) : b.asDouble();
      switch (op) {
        case OpCode::ADD:
          return Value::makeDouble(l + r);
        case OpCode::SUB:
          return Value::makeDouble(l - r);
        case OpCode::MUL:
          return Value::makeDouble(l * r);
        case OpCode::DIV:
          if (r == 0.0) return std::nullopt;
          return Value::makeDouble(l / r);
        case OpCode::INT_DIV: {
          const int64_t divisor = static_cast<int64_t>(r);
          if (divisor == 0) return std::nullopt;
          return Value::makeInt(static_cast<int64_t>(l) / divisor);
        }
        case OpCode::REMAINDER: {
          const int64_t divisor = static_cast<int64_t>(r);
          if (divisor == 0) return std::nullopt;
          return Value::makeInt(static_cast<int64_t>(l) % divisor);
        }
        case OpCode::MOD: {
          if (r == 0.0) return std::nullopt;
          double m = std::fmod(l, r);
          if (m != 0.0 && ((m < 0.0) != (r < 0.0))) m += r;
          return Value::makeDouble(m);
        }
        case OpCode::LT:
          return Value::makeBool(l < r);
        case OpCode::LTE:
          return Value::makeBool(l <= r);
        case OpCode::GT:
          return Value::makeBool(l > r);
        case OpCode::GTE:
          return Value::makeBool(l >= r);
        case OpCode::AND:
          return Value::makeBool(const_truthy(a) && const_truthy(b));
        case OpCode::OR:
          return Value::makeBool(const_truthy(a) || const_truthy(b));
        default:
          return std::nullopt;
      }
    }

    // ---- bool/bool logical ----
    if (a.isBool() && b.isBool()) {
      switch (op) {
        case OpCode::AND:
          return Value::makeBool(const_truthy(a) && const_truthy(b));
        case OpCode::OR:
          return Value::makeBool(const_truthy(a) || const_truthy(b));
        default:
          return std::nullopt;
      }
    }
    return std::nullopt;
  }

  // Fold a unary op over a compile-time constant.
  static std::optional<Value> try_fold_unary(OpCode op, const Value& v) {
    switch (op) {
      case OpCode::NEGATE:
        if (v.isInt()) return Value::makeInt(-v.asInt());
        if (v.isDouble()) return Value::makeDouble(-v.asDouble());
        return std::nullopt;
      case OpCode::NOT:
        if (foldable_const(v)) return Value::makeBool(!const_truthy(v));
        return std::nullopt;
      case OpCode::IS_NULL:
        if (foldable_const(v)) return Value::makeBool(v.isNull());
        return std::nullopt;
      default:
        return std::nullopt;
    }
  }

  // Per-block IN constant maps (index = block id). Size per block is
  // local_count + param_count, matching LOAD_VAR/STORE_VAR slot indices.
  std::vector<ConstantMap> run(const std::vector<BasicBlock>& blocks,
                               const BytecodeFunction& func) const {
    const size_t num_slots = static_cast<size_t>(func.local_count) + func.param_count;
    const size_t n = blocks.size();
    if (n == 0) return {};

    std::vector<ConstantMap> in(n, ConstantMap(num_slots, ConstantValue::unknown()));
    std::vector<ConstantMap> out(n, ConstantMap(num_slots, ConstantValue::unknown()));

    std::vector<uint32_t> worklist(n);
    for (uint32_t i = 0; i < n; ++i) worklist[i] = i;
    std::vector<char> in_worklist(n, 1);

    while (!worklist.empty()) {
      uint32_t bid = worklist.back();
      worklist.pop_back();
      in_worklist[bid] = 0;

      ConstantMap in_state(num_slots, ConstantValue::unknown());
      bool have_pred = false;
      for (uint32_t pred : blocks[bid].predecessors) {
        if (pred >= n) continue;
        // meet_into with an all-Unknown destination stays all-Unknown (a
        // meeting with Unknown wipes precision), so seed the accumulator with
        // the first predecessor's OUT verbatim.
        if (!have_pred) {
          in_state = out[pred];
          have_pred = true;
        } else {
          meet_into(in_state, out[pred]);
        }
      }
      in[bid] = in_state;

      ConstantMap cur = in_state;
      ConstantStack stack;
      walk_block(blocks[bid], cur, stack);

      if (cur != out[bid]) {
        out[bid] = std::move(cur);
        for (uint32_t succ : detail::successors_with_fallthrough(blocks, bid)) {
          if (succ < n && !in_worklist[succ]) {
            in_worklist[succ] = 1;
            worklist.push_back(succ);
          }
        }
      }
    }
    return in;
  }

  // Blocks that create or invoke closures may observe and mutate frame
  // locals (captured as upvalues), so local constants must be forgotten.
  static bool may_escape_to_closure(OpCode op) {
    switch (op) {
      case OpCode::CLOSURE:
      case OpCode::DEFINE_FUNC:
      case OpCode::CALL:
      case OpCode::CALL_DYN:
      case OpCode::CALL_SPREAD:
      case OpCode::TAIL_CALL:
      case OpCode::CALL_METHOD:
      case OpCode::CALL_METHOD_SPREAD:
      case OpCode::FFI_CALL:
      case OpCode::SPREAD_CALL:
      case OpCode::CALL_IF_FUNCTION:
      case OpCode::ARRAY_MAP:
      case OpCode::ARRAY_FILTER:
      case OpCode::ARRAY_REDUCE:
      case OpCode::ARRAY_FOREACH:
        return true;
      default:
        return false;
    }
  }

  // Walk one block's instructions, updating the per-local constant map and the
  // transient abstract stack. Used by the fixpoint above; the same rules drive
  // ConstPropagationPass's rewrite walk.
  static void walk_block(const BasicBlock& block, ConstantMap& locals,
                         ConstantStack& stack) {
    for (const Instruction& inst : block.instructions) {
      switch (inst.opcode) {
        case OpCode::LOAD_CONST:
          if (!inst.operands.empty()) stack.push_back(inst.operands[0]);
          else stack.push_back(std::nullopt);
          break;
        case OpCode::PUSH_NULL:
          stack.push_back(Value::makeNull());
          break;
        case OpCode::LOAD_VAR: {
          uint64_t idx = local_operand(inst);
          stack.push_back(idx < locals.size() && locals[idx].is_constant()
                              ? StackVal(locals[idx].value)
                              : StackVal());
          break;
        }
        case OpCode::LOAD_UPVALUE:
          stack.push_back(std::nullopt);  // captured value, unknown
          break;
        case OpCode::STORE_VAR:
        case OpCode::STORE_IMMUT_VAR: {
          uint64_t idx = local_operand(inst);
          StackVal v = pop_stack(stack);
          if (idx < locals.size()) {
            locals[idx] = v.has_value() ? ConstantValue::constant(*v)
                                        : ConstantValue::unknown();
          }
          break;
        }
        case OpCode::STORE_UPVALUE:
          (void)pop_stack(stack);
          break;
        case OpCode::POP:
          (void)pop_stack(stack);
          break;
        case OpCode::DUP: {
          StackVal v = pop_stack(stack);
          stack.push_back(v);
          stack.push_back(v);
          break;
        }
        case OpCode::SWAP: {
          StackVal a = pop_stack(stack);
          StackVal b = pop_stack(stack);
          stack.push_back(a);
          stack.push_back(b);
          break;
        }
        case OpCode::INCLOCAL:
        case OpCode::DECLOCAL:
        case OpCode::INCLOCAL_POST:
        case OpCode::DECLOCAL_POST: {
          uint64_t idx = local_operand(inst);
          if (idx < locals.size()) locals[idx] = ConstantValue::unknown();
          stack.push_back(std::nullopt);  // pushes the new/old value
          break;
        }
        case OpCode::NEGATE:
        case OpCode::NOT:
        case OpCode::IS_NULL: {
          StackVal v = pop_stack(stack);
          StackVal folded;
          if (v.has_value() && foldable_const(*v)) {
            folded = try_fold_unary(inst.opcode, *v);
          }
          stack.push_back(folded);
          break;
        }
        case OpCode::ADD:
        case OpCode::SUB:
        case OpCode::MUL:
        case OpCode::DIV:
        case OpCode::INT_DIV:
        case OpCode::REMAINDER:
        case OpCode::MOD:
        case OpCode::POW:
        case OpCode::ADD_INT:
        case OpCode::SUB_INT:
        case OpCode::MUL_INT:
        case OpCode::DIV_INT:
        case OpCode::MOD_INT:
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
        case OpCode::AND:
        case OpCode::OR: {
          StackVal r = pop_stack(stack);
          StackVal l = pop_stack(stack);
          StackVal folded;
          if (l.has_value() && r.has_value() && foldable_const(*l) && foldable_const(*r)) {
            folded = try_fold(inst.opcode, *l, *r);
          }
          stack.push_back(folded);
          break;
        }
        default:
          if (may_escape_to_closure(inst.opcode)) {
            // A closure may capture and mutate frame locals.
            std::fill(locals.begin(), locals.end(), ConstantValue::unknown());
            stack.clear();
            stack.push_back(std::nullopt);  // CALL/CLOSURE result is unknown
            break;
          }
          // Conservative effect fallback from the shared classification.
          {
            const auto e = instruction_effect(inst.opcode);
            if (e.pops < 0) {
              stack.clear();
            } else {
              for (int32_t i = 0; i < e.pops; ++i) (void)pop_stack(stack);
            }
            if (e.pushes < 0) {
              stack.clear();
            } else {
              for (int32_t i = 0; i < e.pushes; ++i) stack.push_back(std::nullopt);
            }
          }
          break;
      }
    }
  }

private:
  static uint64_t local_operand(const Instruction& inst) {
    if (!inst.operands.empty() && inst.operands[0].isInt()) {
      return static_cast<uint64_t>(inst.operands[0].asInt());
    }
    return UINT64_MAX;
  }

  // Pop the abstract stack; an underflow models a value pushed by a
  // predecessor block (unknown), which we never need to distinguish.
  static StackVal pop_stack(ConstantStack& stack) {
    if (stack.empty()) return std::nullopt;
    StackVal v = std::move(stack.back());
    stack.pop_back();
    return v;
  }

  // Join `src` into `dst` (meet: a slot keeps its constant only if both
  // sides agree on the same value).
  static void meet_into(ConstantMap& dst, const ConstantMap& src) {
    const size_t n = std::min(dst.size(), src.size());
    for (size_t i = 0; i < n; ++i) {
      if (dst[i].is_unknown() || src[i].is_unknown()) {
        dst[i] = ConstantValue::unknown();
      } else if (dst[i].is_constant() && src[i].is_constant()) {
        if (!(dst[i].value == src[i].value)) dst[i] = ConstantValue::unknown();
      } else {
        dst[i] = ConstantValue::unknown();
      }
    }
  }
};

// ===== Liveness Analysis (backward) =====

struct LiveSet {
  std::vector<bool> live;  // per local slot

  LiveSet() = default;
  explicit LiveSet(size_t n) : live(n, false) {}

  bool operator==(const LiveSet& other) const { return live == other.live; }
  bool operator!=(const LiveSet& other) const { return !(*this == other); }

  LiveSet join(const LiveSet& other) const {
    LiveSet r(std::max(live.size(), other.live.size()));
    for (size_t i = 0; i < r.live.size(); ++i) {
      bool a = i < live.size() ? live[i] : false;
      bool b = i < other.live.size() ? other.live[i] : false;
      r.live[i] = a || b;  // Union for backward (join)
    }
    return r;
  }
};

class LivenessAnalysis {
public:
  // Returns, for each block, the set of locals LIVE AT THE BLOCK'S EXIT:
  // locals that any successor may read. This is what a backward dead-store
  // walk needs to start from.
  //
  // Classic backward liveness: exit[b] = join of entry[s] over successors s;
  // entry[b] = exit[b] gen/killed walking the block's instructions in
  // reverse. Iterated to a fixpoint over the CFG. Backward transfer does not
  // fit the forward-only framework, so this is self-owned like the other
  // analyses in this header.
  std::vector<LiveSet> run(const std::vector<BasicBlock>& blocks,
                           const BytecodeFunction& func) const {
    const size_t num_locals = static_cast<size_t>(func.local_count) + func.param_count;
    const size_t n = blocks.size();
    if (n == 0) return {};

    std::vector<LiveSet> entry(n, LiveSet(num_locals));
    std::vector<LiveSet> exit(n, LiveSet(num_locals));

    bool changed = true;
    while (changed) {
      changed = false;
      for (uint32_t b = 0; b < n; ++b) {
        LiveSet ex(num_locals);
        // Successors include the implicit linear fall-through carried by an
        // Unreachable terminator (SimplifyCFG rewrites Jump(i+1) to it), or a
        // dead store here would be treated as unused while the next block
        // still reads the local.
        for (uint32_t s : detail::successors_with_fallthrough(blocks, b)) {
          if (s < n) ex = ex.join(entry[s]);
        }
        LiveSet en = ex;
        transfer_backward(blocks[b], en);
        if (!(en == entry[b]) || !(ex == exit[b])) {
          entry[b] = std::move(en);
          exit[b] = std::move(ex);
          changed = true;
        }
      }
    }
    return exit;
  }

private:
  void transfer_backward(const BasicBlock& block, LiveSet& live) const {
    for (int ii = static_cast<int>(block.instructions.size()) - 1; ii >= 0; --ii) {
      const Instruction& inst = block.instructions[ii];
      uint64_t idx = UINT64_MAX;
      if (!inst.operands.empty() && inst.operands[0].isInt()) {
        idx = static_cast<uint64_t>(inst.operands[0].asInt());
      }
      switch (inst.opcode) {
        case OpCode::LOAD_VAR:
          if (idx < live.live.size()) live.live[idx] = true;  // gen
          break;
        case OpCode::STORE_VAR:
        case OpCode::STORE_IMMUT_VAR:
          if (idx < live.live.size()) live.live[idx] = false;  // kill
          break;
        case OpCode::INCLOCAL:
        case OpCode::DECLOCAL:
        case OpCode::INCLOCAL_POST:
        case OpCode::DECLOCAL_POST:
          // Reads the old value, then writes the new one: the old value must
          // be live before the instruction.
          if (idx < live.live.size()) live.live[idx] = true;
          break;
        default:
          break;
      }
    }
  }
};

// ===== Type Propagation Analysis =====
//
// Forward may-analysis over value types. Computes, for each local slot, the
// union (bitwise OR) of the TYPE_HINT_* bits of every value that can flow into
// it along any path. The result feeds LocalInfo::type_hint (and, downstream,
// the JIT's AOT type feedback).
//
// Unlike ConstPropagation/Liveness this analysis is not expressed through the
// generic DataflowAnalysis<> base: its transfer function needs the operand
// stack, which the base does not model between instructions. Instead it runs
// its own worklist fixpoint; the per-block transfer walks the block with a
// transient abstract stack and only per-local masks persist.
//
// Lattice: bottom = 0 (no information), join = OR, top = ALL_TYPES. Monotone
// (masks only accrue bits), so the worklist terminates.

class TypePropagationAnalysis {
public:
  using TypeMask = uint64_t;

  // Union of every TYPE_HINT_* bit. Used for values whose type cannot be
  // determined statically.
  static constexpr TypeMask ALL_TYPES = 0xFF;

  // Map a constant Value to its type hint bit.
  static TypeMask hint_for_const(const Value& v) {
    if (v.isInt()) return TYPE_HINT_INT;
    if (v.isDouble()) return TYPE_HINT_NUMBER;
    if (v.isBool()) return TYPE_HINT_BOOL;
    if (v.isNull()) return TYPE_HINT_NULL;
    if (v.isStringId()) return TYPE_HINT_STRING;
    if (v.isArrayId()) return TYPE_HINT_ARRAY;
    if (v.isObjectId()) return TYPE_HINT_OBJECT;
    if (v.isClosureId()) return TYPE_HINT_FUNCTION;
    return ALL_TYPES;
  }

  // Per-local type masks at fixpoint, unioned across all blocks. Indexed like
  // LOAD_VAR/STORE_VAR operands. Empty if there are no blocks.
  std::vector<TypeMask> run(const std::vector<BasicBlock>& blocks,
                            const BytecodeFunction& func) const {
    const size_t num_locals = func.local_count > 0
                                  ? static_cast<size_t>(func.local_count)
                                  : func.locals.size();
    const size_t n = blocks.size();
    std::vector<std::vector<TypeMask>> out(n, std::vector<TypeMask>(num_locals, 0));

    if (n == 0) return {};

    // Worklist of blocks to (re)process.
    std::vector<uint32_t> worklist(n);
    for (uint32_t i = 0; i < n; ++i) worklist[i] = i;
    std::vector<char> in_worklist(n, 1);

    while (!worklist.empty()) {
      uint32_t bid = worklist.back();
      worklist.pop_back();
      in_worklist[bid] = 0;

      // IN = join of predecessor OUT masks (entry has no predecessors -> 0).
      std::vector<TypeMask> in(num_locals, 0);
      for (uint32_t pred : blocks[bid].predecessors) {
        if (pred < n) {
          for (size_t i = 0; i < num_locals; ++i) {
            in[i] |= out[pred][i];
          }
        }
      }

      std::vector<TypeMask> cur = in;
      transfer_block(blocks[bid], cur);

      if (cur != out[bid]) {
        out[bid] = std::move(cur);
        for (uint32_t succ : blocks[bid].get_targets()) {
          if (succ < n && !in_worklist[succ]) {
            in_worklist[succ] = 1;
            worklist.push_back(succ);
          }
        }
      }
    }

    // Union the fixpoint masks across all blocks (any block may be reached by
    // some path; the entry block's effects appear via its successors).
    std::vector<TypeMask> result(num_locals, 0);
    for (const auto& b : out) {
      for (size_t i = 0; i < num_locals; ++i) {
        result[i] |= b[i];
      }
    }
    return result;
  }

private:
  static TypeMask normalize(TypeMask m) { return m != 0 ? m : ALL_TYPES; }

  // Walk one block with a transient abstract stack. `masks` holds the current
  // per-local masks (starting from the block's IN state).
  void transfer_block(const BasicBlock& block, std::vector<TypeMask>& masks) const {
    std::vector<TypeMask> stack;

    auto pop = [&]() -> TypeMask {
      if (stack.empty()) return ALL_TYPES;  // Unknown or unbalanced stack
      TypeMask m = stack.back();
      stack.pop_back();
      return normalize(m);
    };
    auto push = [&](TypeMask m) { stack.push_back(normalize(m)); };

    auto operand = [](const Instruction& inst) -> uint64_t {
      if (!inst.operands.empty() && inst.operands[0].isInt()) {
        return static_cast<uint64_t>(inst.operands[0].asInt());
      }
      return UINT64_MAX;
    };

    for (const Instruction& inst : block.instructions) {
      switch (inst.opcode) {
        case OpCode::LOAD_CONST:
          if (!inst.operands.empty()) {
            push(hint_for_const(inst.operands[0]));
          } else {
            push(ALL_TYPES);
          }
          break;
        case OpCode::PUSH_NULL:
          push(TYPE_HINT_NULL);
          break;
        case OpCode::LOAD_VAR: {
          uint64_t idx = operand(inst);
          push(idx < masks.size() ? masks[idx] : ALL_TYPES);
          break;
        }
        case OpCode::STORE_VAR:
        case OpCode::STORE_IMMUT_VAR: {
          uint64_t idx = operand(inst);
          TypeMask t = pop();
          if (idx < masks.size()) masks[idx] |= t;
          break;
        }
        case OpCode::POP:
          (void)pop();
          break;
        case OpCode::DUP: {
          TypeMask t = pop();
          push(t);
          push(t);
          break;
        }
        case OpCode::SWAP: {
          TypeMask a = pop();
          TypeMask b = pop();
          push(a);
          push(b);
          break;
        }
        case OpCode::LOAD_GLOBAL:
        case OpCode::LOAD_UPVALUE:
        case OpCode::CLOSURE:
          push(ALL_TYPES);
          break;
        case OpCode::STORE_GLOBAL:
        case OpCode::STORE_IMMUT_GLOBAL:
        case OpCode::STORE_UPVALUE:
          (void)pop();
          break;
        case OpCode::ADD:
        case OpCode::SUB:
        case OpCode::MUL:
        case OpCode::DIV:
        case OpCode::INT_DIV:
        case OpCode::DIVMOD:
        case OpCode::MOD:
        case OpCode::ADD_INT:
        case OpCode::SUB_INT:
        case OpCode::MUL_INT:
        case OpCode::DIV_INT:
        case OpCode::MOD_INT: {
          // Numeric op: INT op INT yields INT; anything else is unknown.
          TypeMask r = pop();
          TypeMask l = pop();
          if (l == TYPE_HINT_INT && r == TYPE_HINT_INT) {
            push(TYPE_HINT_INT);
          } else {
            push(ALL_TYPES);
          }
          break;
        }
        case OpCode::NEGATE: {
          TypeMask t = pop();
          push(t == TYPE_HINT_INT ? TYPE_HINT_INT : ALL_TYPES);
          break;
        }
        case OpCode::CALL:
        case OpCode::TAIL_CALL:
          // Unmodeled stack effect (pops callee + args, pushes result).
          stack.clear();
          push(ALL_TYPES);
          break;
        case OpCode::RETURN:
          (void)pop();  // Return value popped
          break;
        default:
          // Conservative: one unknown result pushed.
          push(ALL_TYPES);
          break;
      }
    }
  }
};

}  // namespace havel::compiler
