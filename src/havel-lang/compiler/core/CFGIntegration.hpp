#pragma once

// ===== Linear <-> CFG integration for the optimization pipeline =====
//
// The real compiler (BootstrapByteCompiler) emits flat instruction vectors
// where LOAD_CONST operands are constant-pool indices and jump operands are
// absolute instruction indices. The optimization passes operate on the CFG
// form (BasicBlock + Terminator) with literal-value LOAD_CONST operands.
// This header provides the two directions:
//
//   reconstruct_cfg: linear instructions -> CFG blocks
//                    (LOAD_CONST index -> literal constant Value)
//   lower_cfg:       CFG blocks -> linear instructions
//                    (literal constants re-interned into the pool)
//
// Safety: functions containing opcodes whose operands reference absolute
// instruction IPs in ways the CFG model does not carry (exception handler
// entries, inline caches, coroutine suspension) are reported as unsupported
// and left untouched by the optimizer driver.

#include "BytecodeIR.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace havel::compiler {

namespace cfgintegration {

// Opcodes that make a function unsafe for CFG round-tripping:
//   - TRY_ENTER / TRY_EXIT / LOAD_EXCEPTION / THROW: exception handling is
//     driven by absolute catch/finally IPs installed on a frame-side try
//     stack; the terminator model has no exception edges, so reordering or
//     removing blocks would corrupt handler targets.
//   - YIELD / YIELD_RESUME / FIBER_AWAIT: coroutine suspension captures
//     absolute resume IPs.
//   - ARRAY_GET_FAST / ARRAY_SET_FAST: inline caches keyed by instruction IP.
inline bool opcode_unsafe_for_cfg(OpCode op) {
  switch (op) {
    case OpCode::TRY_ENTER:
    case OpCode::TRY_EXIT:
    case OpCode::LOAD_EXCEPTION:
    case OpCode::THROW:
    case OpCode::YIELD:
    case OpCode::YIELD_RESUME:
    case OpCode::FIBER_AWAIT:
    case OpCode::ARRAY_GET_FAST:
    case OpCode::ARRAY_SET_FAST:
      return true;
    default:
      return false;
  }
}

// Whether `func` can be round-tripped through the CFG form. A function is
// eligible when every opcode is modeled and every jump operand resolves in
// range. `unsafe_opcode` receives the first disqualifying opcode, if any.
inline bool function_supports_cfg(const BytecodeFunction& func,
                                  OpCode* unsafe_opcode = nullptr) {
  for (const auto& inst : func.instructions) {
    if (opcode_unsafe_for_cfg(inst.opcode)) {
      if (unsafe_opcode) *unsafe_opcode = inst.opcode;
      return false;
    }
    switch (inst.opcode) {
      case OpCode::JUMP:
      case OpCode::JUMP_IF_FALSE:
      case OpCode::JUMP_IF_TRUE:
      case OpCode::JUMP_IF_NULL: {
        if (inst.operands.empty() || !inst.operands[0].isInt()) {
          if (unsafe_opcode) *unsafe_opcode = inst.opcode;
          return false;
        }
        const int64_t target = inst.operands[0].asInt();
        if (target < 0 ||
            static_cast<size_t>(target) >= func.instructions.size()) {
          if (unsafe_opcode) *unsafe_opcode = inst.opcode;
          return false;
        }
        break;
      }
      default:
        break;
    }
  }
  return true;
}

struct ReconstructedCFG {
  std::vector<BasicBlock> blocks;
  bool ok = false;
  std::string error;
};

// Split a linear instruction stream into basic blocks.
//
// Leaders: instruction 0, jump targets, and any instruction immediately
// following a jump/return. Terminators:
//   JUMP         -> Jump(target)
//   JUMP_IF_*    -> JumpIf*(target); the fall-through arm is block i+1
//   RETURN       -> Return
//   trailing end -> None (the function may simply run off the end)
//
// LOAD_CONST operands are translated from constant-pool indices to literal
// Values so the dataflow analyses can reason about constants directly.
inline ReconstructedCFG reconstruct_cfg(const BytecodeFunction& func) {
  ReconstructedCFG out;
  const auto& code = func.instructions;
  const size_t n = code.size();
  if (n == 0) {
    out.error = "empty function";
    return out;
  }

  // Collect leaders.
  std::vector<char> leader(n, 0);
  leader[0] = 1;
  for (size_t i = 0; i < n; ++i) {
    const OpCode op = code[i].opcode;
    if (op == OpCode::JUMP || op == OpCode::JUMP_IF_FALSE ||
        op == OpCode::JUMP_IF_TRUE || op == OpCode::JUMP_IF_NULL) {
      if (code[i].operands.empty() || !code[i].operands[0].isInt()) {
        out.error = "jump without integer operand";
        return out;
      }
      const size_t t = static_cast<size_t>(code[i].operands[0].asInt());
      if (t >= n) {
        out.error = "jump target out of range";
        return out;
      }
      leader[t] = 1;
      if (i + 1 < n) leader[i + 1] = 1;
    } else if (op == OpCode::RETURN) {
      if (i + 1 < n) leader[i + 1] = 1;
    }
  }

  // Map leader instruction index -> block id.
  std::unordered_map<uint32_t, uint32_t> leader_to_block;
  uint32_t block_count = 0;
  for (size_t i = 0; i < n; ++i) {
    if (leader[i]) leader_to_block[static_cast<uint32_t>(i)] = block_count++;
  }

  // Build blocks: [start, end] inclusive, closing at a terminator opcode or
  // right before the next leader.
  out.blocks.reserve(block_count);
  {
    size_t start = 0;
    for (size_t i = 0; i < n; ++i) {
      const bool is_last = (i + 1 == n);
      const bool next_is_leader = !is_last && leader[i + 1];
      const OpCode op = code[i].opcode;
      const bool is_term = op == OpCode::JUMP || op == OpCode::JUMP_IF_FALSE ||
                           op == OpCode::JUMP_IF_TRUE || op == OpCode::JUMP_IF_NULL ||
                           op == OpCode::RETURN;
      if (is_last || next_is_leader) {
        BasicBlock b(static_cast<uint32_t>(out.blocks.size()));
        // The closing instruction is the block's terminator opcode when it
        // is a jump/return (excluded from the body; lowered terminators are
        // re-emitted by lower_cfg); a plain fall-through instruction stays
        // in the body.
        const bool closing_is_term = is_term;
        const size_t body_end = closing_is_term ? i : i + 1;
        for (size_t k = start; k < body_end; ++k) {
          Instruction inst = code[k];
          // Constant-pool index -> literal value.
          if (inst.opcode == OpCode::LOAD_CONST &&
              !inst.operands.empty() && inst.operands[0].isInt()) {
            const size_t ci = static_cast<size_t>(inst.operands[0].asInt());
            if (ci < func.constants.size()) {
              inst.operands = {func.constants[ci]};
            } else {
              out.error = "LOAD_CONST index out of constant pool";
              return out;
            }
          }
          b.instructions.push_back(std::move(inst));
        }
        switch (op) {
          case OpCode::JUMP:
            b.terminator = Terminator::jump(
                leader_to_block[static_cast<uint32_t>(
                    code[i].operands[0].asInt())],
                code[i].location);
            break;
          case OpCode::JUMP_IF_FALSE:
          case OpCode::JUMP_IF_TRUE:
          case OpCode::JUMP_IF_NULL: {
            const uint32_t t = leader_to_block[static_cast<uint32_t>(
                code[i].operands[0].asInt())];
            const auto kind = op == OpCode::JUMP_IF_FALSE
                                  ? TerminatorKind::JumpIfFalse
                              : op == OpCode::JUMP_IF_TRUE
                                  ? TerminatorKind::JumpIfTrue
                                  : TerminatorKind::JumpIfNull;
            b.terminator.kind = kind;
            b.terminator.targets = {t};
            b.terminator.location = code[i].location;
            break;
          }
          case OpCode::RETURN:
            b.terminator = Terminator::ret(code[i].location);
            break;
          default:
            // Fell into the next leader: linear fall-through. Only the
            // trailing block may keep None; intermediate ones get an
            // Unreachable (NOP) marker so the CFG stays explicitly
            // terminated and SimplifyCFG's Jump(i+1) elimination composes.
            if (is_last) {
              b.terminator.kind = TerminatorKind::None;
            } else {
              b.terminator.kind = TerminatorKind::Unreachable;
            }
            b.terminator.location = code[i].location;
            break;
        }
        out.blocks.push_back(std::move(b));
        start = i + 1;
      }
    }
  }

  // Defensive shape check: only the final block may end in None.
  for (size_t i = 0; i + 1 < out.blocks.size(); ++i) {
    if (out.blocks[i].terminator.kind == TerminatorKind::None) {
      out.error = "internal: non-trailing fall-through block";
      return out;
    }
  }

  out.ok = true;
  return out;
}

struct LoweredFunction {
  std::vector<Instruction> instructions;
  std::vector<SourceLocation> locations;
  bool ok = false;
  std::string error;
};

// Lower CFG blocks back to a linear stream. Literal LOAD_CONST values are
// re-interned into `constants` (appended; existing entries are untouched).
// The linear order is the block order (block i falls through into i+1), so
// jump operands are absolute start IPs of target blocks.
inline LoweredFunction lower_cfg(const std::vector<BasicBlock>& blocks,
                                 std::vector<Value>& constants) {
  LoweredFunction out;
  const size_t n = blocks.size();
  if (n == 0) {
    out.error = "no blocks";
    return out;
  }

  // Absolute start IP of every block. Every block contributes exactly one
  // terminator instruction (a trailing None block contributes nothing).
  std::vector<uint32_t> start(n, 0);
  {
    uint32_t ip = 0;
    for (size_t i = 0; i < n; ++i) {
      start[i] = ip;
      ip += static_cast<uint32_t>(blocks[i].instructions.size());
      if (blocks[i].terminator.kind != TerminatorKind::None) ip += 1;
    }
  }

  auto intern_const = [&](const Value& v) -> Value {
    // Exact-match dedup keeps the pool from bloating across optimizer
    // iterations.
    for (size_t i = 0; i < constants.size(); ++i) {
      if (constants[i] == v) {
        return Value::makeInt(static_cast<int64_t>(i));
      }
    }
    constants.push_back(v);
    return Value::makeInt(static_cast<int64_t>(constants.size() - 1));
  };

  for (size_t i = 0; i < n; ++i) {
    const BasicBlock& b = blocks[i];
    for (const Instruction& inst : b.instructions) {
      Instruction copy = inst;
      if (copy.opcode == OpCode::LOAD_CONST && !copy.operands.empty()) {
        // CFG form carries the literal value; re-intern into the pool.
        copy.operands = {intern_const(copy.operands[0])};
      }
      out.instructions.push_back(std::move(copy));
      out.locations.push_back(inst.location.value_or(SourceLocation{}));
    }

    auto target_ip = [&](uint32_t t) -> uint32_t {
      return t < n ? start[t] : start[static_cast<uint32_t>(i)];
    };

    switch (b.terminator.kind) {
      case TerminatorKind::None:
        break;
      case TerminatorKind::Jump: {
        if (!b.terminator.targets.empty()) {
          Instruction t(OpCode::JUMP,
                        std::vector<Value>{
                            Value::makeInt(target_ip(b.terminator.targets[0]))});
          t.location = b.terminator.location;
          out.instructions.push_back(std::move(t));
          out.locations.push_back(
              b.terminator.location.value_or(SourceLocation{}));
        }
        break;
      }
      case TerminatorKind::JumpIfFalse:
      case TerminatorKind::JumpIfTrue:
      case TerminatorKind::JumpIfNull: {
        if (!b.terminator.targets.empty()) {
          const OpCode op =
              b.terminator.kind == TerminatorKind::JumpIfFalse
                  ? OpCode::JUMP_IF_FALSE
              : b.terminator.kind == TerminatorKind::JumpIfTrue
                  ? OpCode::JUMP_IF_TRUE
                  : OpCode::JUMP_IF_NULL;
          Instruction t(op, std::vector<Value>{
                                 Value::makeInt(
                                     target_ip(b.terminator.targets[0]))});
          t.location = b.terminator.location;
          out.instructions.push_back(std::move(t));
          out.locations.push_back(
              b.terminator.location.value_or(SourceLocation{}));
        }
        break;
      }
      case TerminatorKind::Return:
      case TerminatorKind::CallReturn: {
        Instruction t(OpCode::RETURN, std::vector<Value>{});
        t.location = b.terminator.location;
        out.instructions.push_back(std::move(t));
        out.locations.push_back(
            b.terminator.location.value_or(SourceLocation{}));
        break;
      }
      case TerminatorKind::Throw: {
        Instruction t(OpCode::THROW, std::vector<Value>{});
        t.location = b.terminator.location;
        out.instructions.push_back(std::move(t));
        out.locations.push_back(
            b.terminator.location.value_or(SourceLocation{}));
        break;
      }
      case TerminatorKind::Unreachable: {
        Instruction t(OpCode::NOP, std::vector<Value>{});
        t.location = b.terminator.location;
        out.instructions.push_back(std::move(t));
        out.locations.push_back(
            b.terminator.location.value_or(SourceLocation{}));
        break;
      }
    }
  }

  out.ok = true;
  return out;
}

}  // namespace cfgintegration
}  // namespace havel::compiler
