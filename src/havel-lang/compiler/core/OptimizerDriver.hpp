#pragma once

// ===== Optimizer driver for real compiled functions =====
//
// Bridges the linear bytecode produced by BootstrapByteCompiler and the
// CFG-based optimization pipeline:
//
//   1. reconstruct_cfg:  linear -> blocks (LOAD_CONST pool index -> literal)
//   2. PassManager::run_all over the blocks
//   3. lower_cfg:        blocks -> linear (literals re-interned into the pool)
//   4. rebuild per-IP arrays (instruction_locations, type_feedback)
//
// Functions containing opcodes the CFG model cannot faithfully carry
// (exception handlers, coroutine suspension, inline caches) are skipped
// untouched — never partially optimized.

#include "CFGIntegration.hpp"
#include "BytecodePasses.hpp"
#include "DataflowAnalysis.hpp"

#include <cstdint>
#include <string>

namespace havel::compiler {
namespace cfgintegration {

struct OptimizeStats {
  uint32_t functions_total = 0;
  uint32_t functions_optimized = 0;
  uint32_t functions_skipped_unsafe = 0;
  uint32_t functions_skipped_error = 0;
  uint32_t blocks_removed = 0;
  uint32_t instructions_removed = 0;
  // First failure detail per class, for diagnostics.
  std::string last_reconstruct_error;
  std::string last_validation_error;
};

// Round-trip one function through the CFG pipeline. Returns true when the
// function's linear form was rewritten (func.instructions replaced).
inline bool optimize_function_cfg(BytecodeFunction& func,
                                  const BytecodeChunk& chunk,
                                  OptimizeStats* stats) {
  ++stats->functions_total;

  if (!function_supports_cfg(func)) {
    ++stats->functions_skipped_unsafe;
    return false;
  }

  auto rc = reconstruct_cfg(func);
  if (!rc.ok) {
    if (stats->last_reconstruct_error.empty()) {
      stats->last_reconstruct_error = func.name + ": " + rc.error;
    }
    ++stats->functions_skipped_error;
    return false;
  }

  const size_t blocks_before = rc.blocks.size();
  size_t instructions_before = 0;
  for (const auto& b : rc.blocks) instructions_before += b.instructions.size();

  func.blocks = std::move(rc.blocks);
  func.entry_block = 0;

  // BootstrapByteCompiler emits slot indices only (local_count/param_count);
  // populate the LocalInfo table so validate_function and TypePropagation
  // can resolve every LOAD_VAR/STORE_VAR operand.
  const size_t slot_count = static_cast<size_t>(func.local_count) +
                            static_cast<size_t>(func.param_count);
  if (func.locals.size() < slot_count) {
    func.locals.resize(slot_count);
    for (size_t i = 0; i < slot_count; ++i) {
      func.locals[i].is_param = i < func.param_count;
      func.locals[i].is_mutable = true;
    }
  }

  // Global operands are chunk-wide StringValIds, but validate_function
  // bounds-checks them against func.global_names. BootstrapByteCompiler never
  // fills that table, so mirror the owning chunk's string table; indices then
  // resolve for any name the function can reference. Nothing consumes
  // global_names for real functions at runtime (the VM resolves against the
  // chunk), so this is purely to satisfy the verifier.
  if (func.global_names.empty()) {
    const auto& strings = chunk.getAllStrings();
    func.global_names.assign(strings.begin(), strings.end());
  }

  // Rebuild stored edges so analyses see the real CFG.
  for (auto& b : func.blocks) {
    b.predecessors.clear();
    b.successors.clear();
  }
  for (uint32_t i = 0; i < func.blocks.size(); ++i) {
    func.blocks[i].successors =
        havel::compiler::detail::successors_with_fallthrough(
            func.blocks, i);
    for (uint32_t t : func.blocks[i].successors) {
      if (t < func.blocks.size()) {
        func.blocks[t].predecessors.push_back(i);
      }
    }
  }

  const auto validation = validate_function(func, func.entry_block);
  if (!validation.valid) {
    // Reconstruction produced something malformed; abandon without touching
    // the linear stream (blocks are also dropped to avoid inconsistent state).
    if (stats->last_validation_error.empty() && !validation.errors.empty()) {
      stats->last_validation_error =
          func.name + ": " + validation.errors.front();
    }
    func.blocks.clear();
    ++stats->functions_skipped_error;
    return false;
  }

  auto pm = create_standard_pipeline();
  const auto res = pm->run_all(func.blocks, func, chunk);
  (void)res;

  const auto validation_after = validate_function(func, func.entry_block);
  if (!validation_after.valid) {
    if (stats->last_validation_error.empty() &&
        !validation_after.errors.empty()) {
      stats->last_validation_error =
          func.name + " (post-pass): " + validation_after.errors.front();
    }
    func.blocks.clear();
    ++stats->functions_skipped_error;
    return false;
  }

  size_t instructions_after = 0;
  for (const auto& b : func.blocks) instructions_after += b.instructions.size();
  stats->blocks_removed +=
      static_cast<uint32_t>(blocks_before > func.blocks.size()
                                ? blocks_before - func.blocks.size()
                                : 0);
  stats->instructions_removed +=
      static_cast<uint32_t>(instructions_before > instructions_after
                                ? instructions_before - instructions_after
                                : 0);

  // Re-derive AOT type hints for the optimized stream. The optimizer rewrote
  // instruction identities, so original per-IP feedback would be stale; the
  // lowering below clears it. The JIT's Phase-4 specialization consumes
  // aot_type_hint/has_aot_hint (int-only hints enable the specialized int
  // binop and comparison paths), so recompute them from the TypePropagation
  // fixpoint instead of losing them: walk each block with an abstract type
  // stack (same model as FastIntegerLoweringPass) and mark generic
  // arithmetic/comparison sites whose operands are provably int.
  std::vector<uint64_t> block_hints;  // flat per-instruction, block order.
  {
    TypePropagationAnalysis analysis;
    const auto local_masks = analysis.run(func.blocks, func);
    using TypeMask = TypePropagationAnalysis::TypeMask;
    const TypeMask INT = TYPE_HINT_INT;
    const TypeMask UNKNOWN = TypePropagationAnalysis::ALL_TYPES;

    block_hints.reserve(instructions_after);
    for (const auto& block : func.blocks) {
      std::vector<TypeMask> stack;
      auto pop = [&]() -> TypeMask {
        if (stack.empty()) return UNKNOWN;
        TypeMask m = stack.back();
        stack.pop_back();
        return m == 0 ? UNKNOWN : m;
      };
      for (const auto& inst : block.instructions) {
        uint64_t hint = 0;
        switch (inst.opcode) {
          case OpCode::LOAD_CONST:
            if (!inst.operands.empty()) {
              stack.push_back(
                  TypePropagationAnalysis::hint_for_const(inst.operands[0]));
            } else {
              stack.push_back(UNKNOWN);
            }
            break;
          case OpCode::PUSH_NULL:
            stack.push_back(TYPE_HINT_NULL);
            break;
          case OpCode::LOAD_VAR: {
            uint64_t idx = UINT64_MAX;
            if (!inst.operands.empty() && inst.operands[0].isInt()) {
              idx = static_cast<uint64_t>(inst.operands[0].asInt());
            }
            stack.push_back(idx < local_masks.size() && local_masks[idx] != 0
                               ? local_masks[idx]
                               : UNKNOWN);
            break;
          }
          case OpCode::STORE_VAR:
          case OpCode::STORE_IMMUT_VAR:
          case OpCode::POP:
            (void)pop();
            break;
          case OpCode::DUP: {
            TypeMask t = pop();
            stack.push_back(t);
            stack.push_back(t);
            break;
          }
          case OpCode::SWAP: {
            TypeMask a = pop();
            TypeMask b = pop();
            stack.push_back(a);
            stack.push_back(b);
            break;
          }
          case OpCode::LOAD_GLOBAL:
          case OpCode::LOAD_UPVALUE:
          case OpCode::CLOSURE:
            stack.push_back(UNKNOWN);
            break;
          case OpCode::ADD:
          case OpCode::SUB:
          case OpCode::MUL:
          case OpCode::EQ:
          case OpCode::NEQ:
          case OpCode::LT:
          case OpCode::LTE:
          case OpCode::GT:
          case OpCode::GTE: {
            TypeMask r = pop();
            TypeMask l = pop();
            if (l == INT && r == INT) {
              hint = TYPE_HINT_INT;
              stack.push_back(
                  (inst.opcode == OpCode::EQ || inst.opcode == OpCode::NEQ ||
                   inst.opcode == OpCode::LT || inst.opcode == OpCode::LTE ||
                   inst.opcode == OpCode::GT || inst.opcode == OpCode::GTE)
                      ? static_cast<TypeMask>(TYPE_HINT_BOOL)
                      : INT);
            } else {
              stack.push_back(UNKNOWN);
            }
            break;
          }
          case OpCode::ADD_INT:
          case OpCode::SUB_INT:
          case OpCode::MUL_INT:
            (void)pop();
            (void)pop();
            stack.push_back(UNKNOWN);
            break;
          case OpCode::CALL:
          case OpCode::TAIL_CALL:
            stack.clear();
            stack.push_back(UNKNOWN);
            break;
          default:
            stack.push_back(UNKNOWN);
            break;
        }
        block_hints.push_back(hint);
      }
    }
  }

  // Lower back to the linear stream.
  auto lowered = lower_cfg(func.blocks, func.constants);
  if (!lowered.ok) {
    func.blocks.clear();
    ++stats->functions_skipped_error;
    return false;
  }

  func.instructions = std::move(lowered.instructions);
  func.instruction_locations = std::move(lowered.locations);
  func.type_feedback.clear();
  func.type_feedback.resize(func.instructions.size());
  // Write the recomputed hints: lower_cfg emits each block's body
  // contiguously in block order and appends exactly one terminator
  // instruction per block (a trailing None block appends none). Mirror that
  // emission order to map flat body hints to linear IPs; never pattern-match
  // opcodes, which would desync on a legitimate body NOP.
  {
    size_t src = 0;
    size_t ip = 0;
    for (size_t b = 0; b < func.blocks.size() && src < block_hints.size(); ++b) {
      for (size_t k = 0; k < func.blocks[b].instructions.size(); ++k, ++ip, ++src) {
        if (block_hints[src] != 0 && ip < func.type_feedback.size()) {
          func.type_feedback[ip].aot_type_hint = block_hints[src];
          func.type_feedback[ip].has_aot_hint = true;
        }
      }
      if (func.blocks[b].terminator.kind != TerminatorKind::None) ++ip;
    }
  }
  // Keep the CFG attached: downstream consumers can rely on either form, and
  // has_cfg() stays truthful.
  ++stats->functions_optimized;
  return true;
}

}  // namespace cfgintegration
}  // namespace havel::compiler
