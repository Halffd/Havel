#include "BytecodeIR.hpp"

#include <algorithm>
#include <queue>
#include <unordered_set>

namespace havel::compiler {

namespace {

// Number of terminator targets required by each kind. Returns a negative value
// for kinds that must have zero targets.
int terminator_target_arity(TerminatorKind kind) {
  switch (kind) {
    case TerminatorKind::None:
    case TerminatorKind::Return:
    case TerminatorKind::Throw:
    case TerminatorKind::CallReturn:
    case TerminatorKind::Unreachable:
      return 0;
    case TerminatorKind::Jump:
    case TerminatorKind::JumpIfFalse:
    case TerminatorKind::JumpIfTrue:
    case TerminatorKind::JumpIfNull:
      return 1;
  }
  return 0;
}

}  // namespace

// ===== CFG Validation =====
//
// Verifies structural invariants of a control-flow graph:
//   - non-empty, in-range block ids and entry block
//   - every terminator has the correct number of targets
//   - every terminator target is a valid block
//   - exactly one unterminated (falls-through) block, and it is last
//   - stored predecessor/successor edges agree with the terminator-derived edges
//   - entry is not a landing pad
//   - optional: lid-of-block id consistency (block.id matches its index)
CFGValidationResult validate_cfg(const std::vector<BasicBlock>& blocks,
                                 uint32_t entry_block) {
  CFGValidationResult result;

  if (blocks.empty()) {
    result.valid = false;
    result.errors.push_back("CFG has no blocks");
    return result;
  }

  if (entry_block >= blocks.size()) {
    result.valid = false;
    result.errors.push_back("entry block index " + std::to_string(entry_block) +
                            " out of range (block count " +
                            std::to_string(blocks.size()) + ")");
    return result;
  }

  const size_t n = blocks.size();

  // Block id must match its position in the vector.
  for (size_t i = 0; i < n; ++i) {
    if (blocks[i].id != i) {
      result.valid = false;
      result.errors.push_back("block at index " + std::to_string(i) +
                              " has inconsistent id " +
                              std::to_string(blocks[i].id));
      // Prefer to keep scanning for further structural errors.
      // const_cast would be wrong; report and move on.
    }
  }

  // Recompute edges from terminators and cross-check stored edges.
  std::vector<std::vector<uint32_t>> successors(n);
  std::vector<std::vector<uint32_t>> predecessors(n);
  for (size_t i = 0; i < n; ++i) {
    const BasicBlock& b = blocks[i];
    for (uint32_t t : b.terminator.targets) {
      successors[i].push_back(t);
      if (t < n) {
        predecessors[t].push_back(static_cast<uint32_t>(i));
      }
    }
  }

  size_t unterminated_blocks = 0;
  for (size_t i = 0; i < n; ++i) {
    const BasicBlock& b = blocks[i];

    // A landing pad must not also be the entry block (it is entered only via
    // implicit exception edges).
    if (b.is_landing_pad && i == entry_block) {
      result.valid = false;
      result.errors.push_back("entry block " + std::to_string(i) +
                              " is marked as a landing pad");
    }

    const int arity = terminator_target_arity(b.terminator.kind);
    if (static_cast<int>(b.terminator.targets.size()) != arity) {
      result.valid = false;
      result.errors.push_back(
          "block " + std::to_string(i) + " terminator kind " +
          std::to_string(static_cast<int>(b.terminator.kind)) + " has " +
          std::to_string(b.terminator.targets.size()) + " targets, expected " +
          std::to_string(arity));
    }

    // Every target must be a valid block id.
    for (uint32_t t : b.terminator.targets) {
      if (t >= n) {
        result.valid = false;
        result.errors.push_back("block " + std::to_string(i) +
                                " targets invalid block id " +
                                std::to_string(t));
      }
    }

    // A block ending in None is a fall-through marker (no explicit terminator).
    // It is legal for zero blocks to end in None (every block explicitly
    // terminates), but there must never be more than one, and if one exists it
    // must be the last block so linear lowering stays unambiguous.
    if (b.terminator.kind == TerminatorKind::None) {
      ++unterminated_blocks;
      if (i != n - 1) {
        result.valid = false;
        result.errors.push_back("unterminated (fall-through) block " +
                                std::to_string(i) +
                                " is not the last block in the CFG");
      }
    }

    // Stored edges must agree with the terminator-derived edges.
    if (b.successors != successors[i]) {
      result.warnings.push_back("block " + std::to_string(i) +
                                " stored successors disagree with terminator");
    }
    if (b.predecessors != predecessors[i]) {
      result.warnings.push_back("block " + std::to_string(i) +
                                " stored predecessors disagree with terminator");
    }
  }

  if (unterminated_blocks > 1) {
    result.valid = false;
    result.errors.push_back("CFG has " + std::to_string(unterminated_blocks) +
                            " unterminated (fall-through) blocks; at most one is allowed");
  }

  // Reachability from the entry block (informational; dead code is allowed but
  // reported).
  std::vector<bool> reachable(n, false);
  {
    std::queue<uint32_t> q;
    q.push(entry_block);
    reachable[entry_block] = true;
    while (!q.empty()) {
      uint32_t id = q.front();
      q.pop();
      for (uint32_t succ : successors[id]) {
        if (succ < n && !reachable[succ]) {
          reachable[succ] = true;
          q.push(succ);
        }
      }
    }
  }
  for (size_t i = 0; i < n; ++i) {
    if (!reachable[i] && !blocks[i].is_landing_pad) {
      result.warnings.push_back("block " + std::to_string(i) +
                                " is not reachable from the entry block");
    }
  }

  return result;
}

// ===== Flatten CFG to Linear Instructions =====
//
// Orders blocks (BFS from the entry along terminator successors, then any
// leftover/unreachable blocks in their original order) and emits each block's
// instructions contiguously. Terminators lower to a single jump/return/throw
// instruction whose target is the absolute instruction index of the target
// block's first instruction (matching the VM's JUMP semantics: frame.ip =
// target). Block start indices are recorded in `block_start_ips`.
LinearFunction flatten_cfg(const std::vector<BasicBlock>& blocks,
                           uint32_t entry_block) {
  LinearFunction out;

  const size_t n = blocks.size();
  if (n == 0) return out;

  if (entry_block >= n) entry_block = 0;

  std::vector<uint32_t> order;
  order.reserve(n);
  std::vector<bool> visited(n, false);

  std::queue<uint32_t> q;
  q.push(entry_block);
  visited[entry_block] = true;
  while (!q.empty()) {
    uint32_t id = q.front();
    q.pop();
    order.push_back(id);
    const BasicBlock& b = blocks[id];
    for (uint32_t succ : b.terminator.targets) {
      if (succ < n && !visited[succ]) {
        visited[succ] = true;
        q.push(succ);
      }
    }
  }

  // Append any blocks not reached (dead code) in their original order.
  for (uint32_t i = 0; i < n; ++i) {
    if (!visited[i]) order.push_back(i);
  }

  // The single fall-through (None-terminated) block, if present, must be the
  // final block in the linear output so that "no terminator" coincides with
  // "end of the instruction stream".
  for (auto it = order.begin(); it != order.end(); ++it) {
    if (blocks[*it].terminator.kind == TerminatorKind::None && it + 1 != order.end()) {
      order.erase(it);
      order.push_back(blocks.size() - 1);
      break;
    }
  }

  // Pass 1: compute every block's absolute start index. Each block contributes
  // its instructions plus one terminator-derived instruction, so forward jump
  // targets are known before any terminator is lowered.
  out.block_start_ips.assign(n, UINT32_MAX);
  {
    uint32_t ip = 0;
    for (uint32_t id : order) {
      out.block_start_ips[id] = ip;
      ip += static_cast<uint32_t>(blocks[id].instructions.size()) + 1;
    }
  }

  // Pass 2: emit instructions, then the lowered terminator.
  for (uint32_t id : order) {
    const BasicBlock& b = blocks[id];
    out.instructions.insert(out.instructions.end(), b.instructions.begin(),
                            b.instructions.end());

    auto start_ip = [&](uint32_t target) -> uint32_t {
      if (target < n && out.block_start_ips[target] != UINT32_MAX) {
        return out.block_start_ips[target];
      }
      return static_cast<uint32_t>(out.block_start_ips[id]);  // self/guard
    };

    switch (b.terminator.kind) {
      case TerminatorKind::None:
        // End of the linear stream. Nothing to emit.
        break;
      case TerminatorKind::Jump: {
        if (!b.terminator.targets.empty()) {
          out.instructions.emplace_back(
              OpCode::JUMP, std::vector<Value>{Value::makeInt(start_ip(b.terminator.targets[0]))});
        }
        break;
      }
      case TerminatorKind::JumpIfFalse: {
        if (!b.terminator.targets.empty()) {
          out.instructions.emplace_back(
              OpCode::JUMP_IF_FALSE, std::vector<Value>{Value::makeInt(start_ip(b.terminator.targets[0]))});
        }
        break;
      }
      case TerminatorKind::JumpIfTrue: {
        if (!b.terminator.targets.empty()) {
          out.instructions.emplace_back(
              OpCode::JUMP_IF_TRUE, std::vector<Value>{Value::makeInt(start_ip(b.terminator.targets[0]))});
        }
        break;
      }
      case TerminatorKind::JumpIfNull: {
        if (!b.terminator.targets.empty()) {
          out.instructions.emplace_back(
              OpCode::JUMP_IF_NULL, std::vector<Value>{Value::makeInt(start_ip(b.terminator.targets[0]))});
        }
        break;
      }
      case TerminatorKind::Return:
        out.instructions.emplace_back(OpCode::RETURN);
        break;
      case TerminatorKind::Throw:
        out.instructions.emplace_back(OpCode::THROW, std::vector<Value>{});
        break;
      case TerminatorKind::CallReturn:
        // The call itself lives in the block; this terminator only marks that
        // the call result is returned. Tail-call lowering is a backend concern.
        out.instructions.emplace_back(OpCode::RETURN);
        break;
      case TerminatorKind::Unreachable:
        out.instructions.emplace_back(OpCode::NOP);
        break;
    }
  }

  return out;
}

// ===== Per-function validation =====
//
// Runs the CFG structural checks, then validates that instruction operands
// reference entities that exist in the owning function (locals, upvalues, and
// global names). This is the CFG/builder path: functions produced by
// FunctionBuilder carry `blocks`, `locals`, `upvalues`, and `global_names`.
namespace {

// Opcodes whose first operand is a zero-based local-slot index.
bool opc_takes_local(OpCode op) {
  switch (op) {
    case OpCode::LOAD_VAR:
    case OpCode::STORE_VAR:
    case OpCode::STORE_IMMUT_VAR:
    case OpCode::INCLOCAL:
    case OpCode::DECLOCAL:
    case OpCode::INCLOCAL_POST:
    case OpCode::DECLOCAL_POST:
      return true;
    default:
      return false;
  }
}

bool opc_takes_upvalue(OpCode op) {
  return op == OpCode::LOAD_UPVALUE || op == OpCode::STORE_UPVALUE;
}

bool opc_takes_global(OpCode op) {
  return op == OpCode::LOAD_GLOBAL || op == OpCode::STORE_GLOBAL ||
         op == OpCode::STORE_IMMUT_GLOBAL;
}

bool opc_takes_const(OpCode op) { return op == OpCode::LOAD_CONST; }

}  // namespace

CFGValidationResult validate_function(const BytecodeFunction& func,
                                      uint32_t entry_block) {
  CFGValidationResult result;

  if (func.blocks.empty()) {
    result.warnings.push_back("function '" + func.name +
                              "' has no CFG blocks; operand checks skipped");
    return result;
  }

  // Structural CFG checks.
  {
    const CFGValidationResult cfg = validate_cfg(func.blocks, entry_block);
    result.valid &= cfg.valid;
    result.errors.insert(result.errors.end(), cfg.errors.begin(), cfg.errors.end());
    result.warnings.insert(result.warnings.end(), cfg.warnings.begin(),
                           cfg.warnings.end());
  }

  // Operand reference checks.
  auto build_msg = [&](uint32_t block_id, size_t inst_idx, const std::string& what,
                       uint64_t idx, size_t bound) {
    return "function '" + func.name + "' block " + std::to_string(block_id) +
           " instruction " + std::to_string(inst_idx) + ": " + what +
           " index " + std::to_string(idx) + " out of range (count " +
           std::to_string(bound) + ")";
  };

  for (uint32_t bi = 0; bi < func.blocks.size(); ++bi) {
    const BasicBlock& b = func.blocks[bi];
    for (size_t ii = 0; ii < b.instructions.size(); ++ii) {
      const Instruction& inst = b.instructions[ii];
      if (inst.operands.empty()) continue;

      if (opc_takes_local(inst.opcode)) {
        if (!inst.operands[0].isInt()) {
          result.valid = false;
          result.errors.push_back("function '" + func.name + "' block " +
                                  std::to_string(bi) + " instruction " +
                                  std::to_string(ii) +
                                  ": local index operand is not an integer");
          continue;
        }
        const uint64_t idx = static_cast<uint64_t>(inst.operands[0].asInt());
        if (idx >= func.locals.size()) {
          result.valid = false;
          result.errors.push_back(
              build_msg(bi, ii, "local", idx, func.locals.size()));
        }
      } else if (opc_takes_upvalue(inst.opcode)) {
        const uint64_t idx = static_cast<uint64_t>(inst.operands[0].asInt());
        if (idx >= func.upvalues.size()) {
          result.valid = false;
          result.errors.push_back(
              build_msg(bi, ii, "upvalue", idx, func.upvalues.size()));
        }
      } else if (opc_takes_global(inst.opcode)) {
        // For CFG-backed functions the builder always interns globals into
        // func.global_names and emits the name index as a StringValId (low 31
        // bits, see Value::makeStringValId). An index outside that table is an
        // invalid reference.
        const uint64_t idx = inst.operands[0].asStringValId();
        if (idx >= func.global_names.size()) {
          result.valid = false;
          result.errors.push_back(
              build_msg(bi, ii, "global", idx, func.global_names.size()));
        }
      } else if (opc_takes_const(inst.opcode)) {
        // LOAD_CONST must actually carry a constant payload.
        if (!(inst.operands[0].isInt() || inst.operands[0].isNumber() ||
              inst.operands[0].isBool() || inst.operands[0].isStringId())) {
          result.valid = false;
          result.errors.push_back("function '" + func.name + "' block " +
                                  std::to_string(bi) + " instruction " +
                                  std::to_string(ii) +
                                  ": LOAD_CONST has no scalar payload");
        }
      }
    }
  }

  return result;
}

CFGValidationResult validate_module(const BytecodeChunk& chunk) {
  CFGValidationResult result;
  for (const BytecodeFunction& func : chunk.getAllFunctions()) {
    const CFGValidationResult fv = validate_function(func, func.entry_block);
    result.valid &= fv.valid;
    result.errors.insert(result.errors.end(), fv.errors.begin(), fv.errors.end());
    result.warnings.insert(result.warnings.end(), fv.warnings.begin(),
                           fv.warnings.end());
  }
  if (chunk.getFunctionCount() == 0) {
    result.warnings.push_back("module has no functions to validate");
  }
  return result;
}

}  // namespace havel::compiler
