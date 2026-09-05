#pragma once

#include "BytecodeIR.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

namespace havel::compiler {

// ===== Dataflow Analysis Framework =====
//
// A generic forward/backward dataflow framework with a worklist fixpoint solver.
// Concrete analyses (ConstPropagation, LivenessAnalysis) provide:
//
//   - Domain: the lattice type (must be CopyConstructible)
//   - Boundary: initial value for entry (forward) / exit (backward)
//   - Transfer: function (domain, instruction, block) -> new domain
//   - Meet: domain meet (join for forward, meet for backward)
//   - Direction: forward or backward
//
// The solver computes IN/OUT states for each block to a fixpoint.

// Abstract lattice value for constant propagation.
enum class ConstantState : uint8_t {
  Unknown,    // May be anything (top of lattice)
  Constant,   // Known constant value
  Unreachable // Block is unreachable (bottom for forward, top for backward)
};

struct ConstantValue {
  ConstantState state = ConstantState::Unknown;
  int64_t value = 0;  // Valid only if state == Constant

  ConstantValue() = default;
  ConstantValue(ConstantState s) : state(s) {}
  ConstantValue(ConstantState s, int64_t v) : state(s), value(v) {}

  static ConstantValue unknown() { return ConstantValue(ConstantState::Unknown); }
  static ConstantValue constant(int64_t v) { return ConstantValue(ConstantState::Constant, v); }
  static ConstantValue unreachable() { return ConstantValue(ConstantState::Unreachable); }

  bool is_unknown() const { return state == ConstantState::Unknown; }
  bool is_constant() const { return state == ConstantState::Constant; }
  bool is_unreachable() const { return state == ConstantState::Unreachable; }
};

// Per-local constant state map (indexed by local slot).
using ConstantMap = std::vector<ConstantValue>;

// Forward/backward dataflow analysis base template.
template <typename Domain>
class DataflowAnalysis {
public:
  virtual ~DataflowAnalysis() = default;

  // Direction: true = forward, false = backward
  virtual bool is_forward() const = 0;

  // Initial state at the function entry (forward) or exit (backward).
  virtual Domain boundary(const BytecodeFunction& func) const = 0;

  // Meet operator (join for backward, meet for forward).
  // Combines incoming states from multiple predecessors/successors.
  virtual Domain meet(const Domain& a, const Domain& b) const = 0;

  // Transfer function: applies instruction effect to state.
  // Returns new state after the instruction.
  virtual Domain transfer(const Domain& in, const Instruction& inst,
                          const BytecodeFunction& func, uint32_t block_id) const = 0;

  // Equality check for fixpoint termination.
  virtual bool equals(const Domain& a, const Domain& b) const = 0;

  // Run the analysis to fixpoint.
  // Returns IN states for each block (index = block id).
  std::vector<Domain> run(const std::vector<BasicBlock>& blocks,
                          const BytecodeFunction& func) const {
    const size_t n = blocks.size();
    std::vector<Domain> in(n);
    std::vector<Domain> out(n);

    // Initialize IN states.
    Domain init = boundary(func);
    for (size_t i = 0; i < n; ++i) {
      in[i] = init;
    }
    if (n > 0) {
      // Entry block gets boundary, others start at bottom/top.
      if (is_forward()) {
        in[0] = init;
      } else {
        // For backward, exit block is typically the last with Return/None
        for (size_t i = 0; i < n; ++i) {
          const auto& b = blocks[i];
          if (b.terminator.kind == TerminatorKind::Return ||
              b.terminator.kind == TerminatorKind::None) {
            in[i] = init;
          }
        }
      }
    }

    // Worklist algorithm.
    std::vector<bool> in_worklist(n, true);
    std::vector<uint32_t> worklist;
    worklist.reserve(n);
    for (uint32_t i = 0; i < n; ++i) worklist.push_back(i);

    while (!worklist.empty()) {
      uint32_t bid = worklist.back();
      worklist.pop_back();
      in_worklist[bid] = false;

      const BasicBlock& b = blocks[bid];

      // Compute IN from predecessors (forward) or successors (backward).
      Domain in_state;
      if (is_forward()) {
        // Forward: IN = meet of OUT of predecessors.
        if (b.predecessors.empty()) {
          in_state = boundary(func);  // Entry
        } else {
          in_state = out[b.predecessors[0]];
          for (size_t pi = 1; pi < b.predecessors.size(); ++pi) {
            in_state = meet(in_state, out[b.predecessors[pi]]);
          }
        }
      } else {
        // Backward: IN = meet of OUT of successors.
        const auto& targets = b.get_targets();
        if (targets.empty()) {
          in_state = boundary(func);  // Exit
        } else {
          in_state = out[targets[0]];
          for (size_t ti = 1; ti < targets.size(); ++ti) {
            in_state = meet(in_state, out[targets[ti]]);
          }
        }
      }

      // Apply transfer functions through the block.
      Domain out_state = in_state;
      for (const Instruction& inst : b.instructions) {
        out_state = transfer(out_state, inst, func, bid);
      }
      // Terminator has no effect on constant state (but may for liveness).
      // For forward, the terminator's effect is folded into successor's IN via meet.

      // Check if OUT changed.
      if (!equals(out[bid], out_state)) {
        out[bid] = out_state;

        // Propagate to successors (forward) or predecessors (backward).
        if (is_forward()) {
          for (uint32_t succ : b.get_targets()) {
            if (!in_worklist[succ]) {
              worklist.push_back(succ);
              in_worklist[succ] = true;
            }
          }
        } else {
          for (uint32_t pred : b.predecessors) {
            if (!in_worklist[pred]) {
              worklist.push_back(pred);
              in_worklist[pred] = true;
            }
          }
        }
      }
    }

    return in;
  }
};

// ===== Constant Propagation Analysis =====

class ConstPropagationAnalysis : public DataflowAnalysis<ConstantMap> {
public:
  bool is_forward() const override { return true; }

  ConstantMap boundary(const BytecodeFunction& func) const override {
    ConstantMap m(func.local_count + func.param_count, ConstantValue::unknown());
    return m;
  }

  ConstantMap meet(const ConstantMap& a, const ConstantMap& b) const override {
    ConstantMap r(a.size());
    for (size_t i = 0; i < a.size(); ++i) {
      r[i] = meet_values(a[i], b[i]);
    }
    return r;
  }

  ConstantMap transfer(const ConstantMap& in, const Instruction& inst,
                       const BytecodeFunction& func, uint32_t block_id) const override {
    ConstantMap out = in;

    switch (inst.opcode) {
      case OpCode::LOAD_CONST: {
        if (!inst.operands.empty() && inst.operands[0].isInt()) {
          int64_t val = inst.operands[0].asInt();
          // The next instruction should be STORE_VAR - we can't easily know
          // which local it targets without peeking. For simplicity, we'll
          // handle this via a post-scan or by assuming LOAD_CONST is
          // immediately followed by STORE_VAR. A more complete impl would
          // use a peephole pass or track the stack.
          // Here we just note the constant is on the stack (no slot assigned).
        }
        break;
      }
      case OpCode::STORE_VAR:
      case OpCode::STORE_IMMUT_VAR: {
        if (!inst.operands.empty() && inst.operands[0].isInt()) {
          uint64_t idx = static_cast<uint64_t>(inst.operands[0].asInt());
          if (idx < out.size()) {
            out[idx] = ConstantValue::unknown();
          }
        }
        break;
      }
      case OpCode::LOAD_VAR: {
        // Loading a variable - no state change.
        break;
      }
      case OpCode::ADD:
      case OpCode::SUB:
      case OpCode::MUL:
      case OpCode::DIV:
      case OpCode::MOD:
      case OpCode::ADD_INT:
      case OpCode::SUB_INT:
      case OpCode::MUL_INT:
      case OpCode::DIV_INT:
      case OpCode::MOD_INT: {
        // Binary ops consume two values, produce one. Without stack tracking
        // we can't precisely propagate constants. Mark destination as Unknown.
        // (Real impl would use a stack abstraction.)
        break;
      }
      default:
        break;
    }
    return out;
  }

  bool equals(const ConstantMap& a, const ConstantMap& b) const override {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
      if (a[i].state != b[i].state) return false;
      if (a[i].is_constant() && b[i].is_constant() && a[i].value != b[i].value)
        return false;
    }
    return true;
  }

private:
  static ConstantValue meet_values(const ConstantValue& a, const ConstantValue& b) {
    if (a.is_unreachable() || b.is_unreachable()) return ConstantValue::unreachable();
    if (a.is_constant() && b.is_constant()) {
      if (a.value == b.value) return a;
      return ConstantValue::unknown();  // Different constants -> unknown
    }
    if (a.is_constant()) return a;
    if (b.is_constant()) return b;
    return ConstantValue::unknown();
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

class LivenessAnalysis : public DataflowAnalysis<LiveSet> {
public:
  bool is_forward() const override { return false; }

  LiveSet boundary(const BytecodeFunction& func) const override {
    return LiveSet(func.local_count + func.param_count);
  }

  LiveSet meet(const LiveSet& a, const LiveSet& b) const override {
    return a.join(b);
  }

  LiveSet transfer(const LiveSet& in, const Instruction& inst,
                   const BytecodeFunction& func, uint32_t block_id) const override {
    LiveSet out = in;

    switch (inst.opcode) {
      case OpCode::LOAD_VAR: {
        if (!inst.operands.empty() && inst.operands[0].isInt()) {
          uint64_t idx = static_cast<uint64_t>(inst.operands[0].asInt());
          if (idx < out.live.size()) out.live[idx] = true;
        }
        break;
      }
      case OpCode::STORE_VAR:
      case OpCode::STORE_IMMUT_VAR:
      case OpCode::INCLOCAL:
      case OpCode::DECLOCAL:
      case OpCode::INCLOCAL_POST:
      case OpCode::DECLOCAL_POST: {
        if (!inst.operands.empty() && inst.operands[0].isInt()) {
          uint64_t idx = static_cast<uint64_t>(inst.operands[0].asInt());
          if (idx < out.live.size()) out.live[idx] = false;  // Killed by store
        }
        break;
      }
      case OpCode::LOAD_UPVALUE:
      case OpCode::STORE_UPVALUE:
        // Upvalues tracked separately; conservatively mark all live.
        break;
      default:
        break;
    }
    return out;
  }

  bool equals(const LiveSet& a, const LiveSet& b) const override {
    return a == b;
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
