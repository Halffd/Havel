#include "BytecodePasses.hpp"
#include "DataflowAnalysis.hpp"

#include <algorithm>
#include <functional>
#include <queue>
#include <unordered_set>
#include <stack>

namespace havel::compiler {

// ===== Helper Utilities =====

static inline int64_t get_int_operand(const Instruction& inst, size_t idx = 0) {
  if (idx < inst.operands.size() && inst.operands[idx].isInt()) {
    return inst.operands[idx].asInt();
  }
  return 0;
}

static inline void set_int_operand(Instruction& inst, int64_t val, size_t idx = 0) {
  if (idx < inst.operands.size()) {
    inst.operands[idx] = Value::makeInt(val);
  }
}

static inline bool is_load_const(const Instruction& inst, int64_t* out_val = nullptr) {
  if (inst.opcode == OpCode::LOAD_CONST && !inst.operands.empty() && inst.operands[0].isInt()) {
    if (out_val) *out_val = inst.operands[0].asInt();
    return true;
  }
  return false;
}

// ===== Pass 1: SimplifyCFG =====

class SimplifyCFGPass : public BytecodePass {
public:
  PassType type() const override { return PassType::SimplifyCFG; }
  std::string name() const override { return "SimplifyCFG"; }
  std::vector<std::string> modified_state() const override { return {Analysis::kCFG}; }
  
  PassResult run(std::vector<BasicBlock>& blocks, BytecodeFunction& func, const BytecodeChunk& chunk) override {
    PassResult result;
    
    // 1. Remove unreachable blocks
    result |= remove_unreachable(blocks);
    
    // 2. Merge consecutive blocks
    result |= merge_blocks(blocks);
    
    // 3. Simplify jump chains
    result |= simplify_jump_chains(blocks);
    
    // 4. Eliminate redundant jumps
    result |= eliminate_redundant_jumps(blocks);
    
    // 5. Remove empty blocks
    result |= remove_empty_blocks(blocks);
    
    return result;
  }

private:
  PassResult remove_unreachable(std::vector<BasicBlock>& blocks) {
    PassResult result;
    std::vector<bool> reachable(blocks.size(), false);
    std::queue<uint32_t> q;
    q.push(0);
    reachable[0] = true;
    
    while (!q.empty()) {
      uint32_t id = q.front(); q.pop();
      for (uint32_t succ : blocks[id].get_targets()) {
        if (succ < blocks.size() && !reachable[succ]) {
          reachable[succ] = true;
          q.push(succ);
        }
      }
    }
    
    std::vector<BasicBlock> new_blocks;
    std::vector<uint32_t> old_to_new(blocks.size(), UINT32_MAX);
    new_blocks.reserve(blocks.size());
    
    for (size_t i = 0; i < blocks.size(); ++i) {
      if (reachable[i] || blocks[i].is_landing_pad) {
        old_to_new[i] = new_blocks.size();
        new_blocks.push_back(std::move(blocks[i]));
      }
    }
    
    if (new_blocks.size() < blocks.size()) {
      result.modified = true;
      result.messages.push_back("Removed " + std::to_string(blocks.size() - new_blocks.size()) + " unreachable blocks");
    }
    
    for (auto& block : new_blocks) {
      for (uint32_t& target : block.terminator.targets) {
        if (target < old_to_new.size()) {
          uint32_t new_target = old_to_new[target];
          if (new_target != UINT32_MAX) {
            target = new_target;
          }
        }
      }
    }
    
    blocks.swap(new_blocks);
    return result;
  }
  
  PassResult merge_blocks(std::vector<BasicBlock>& blocks) {
    PassResult result;
    bool merged = false;
    
    for (size_t i = 0; i + 1 < blocks.size(); ++i) {
      BasicBlock& a = blocks[i];
      BasicBlock& b = blocks[i + 1];
      
      if (a.terminator.kind == TerminatorKind::Jump && 
          !a.terminator.targets.empty() &&
          a.terminator.targets[0] == static_cast<uint32_t>(i + 1) &&
          b.predecessors.size() == 1 && b.predecessors[0] == i &&
          !b.is_landing_pad) {
        
        a.instructions.insert(a.instructions.end(), 
                              std::make_move_iterator(b.instructions.begin()),
                              std::make_move_iterator(b.instructions.end()));
        a.terminator = std::move(b.terminator);
        
        blocks.erase(blocks.begin() + i + 1);
        merged = true;
        result.modified = true;
        --i;
      }
    }
    
    if (merged) {
      for (size_t i = 0; i < blocks.size(); ++i) {
        blocks[i].predecessors.clear();
      }
      for (size_t i = 0; i < blocks.size(); ++i) {
        for (uint32_t target : blocks[i].get_targets()) {
          if (target < blocks.size()) {
            blocks[target].predecessors.push_back(i);
          }
        }
      }
      result.messages.push_back("Merged consecutive blocks");
    }
    
    return result;
  }
  
  PassResult simplify_jump_chains(std::vector<BasicBlock>& blocks) {
    PassResult result;
    bool modified = false;
    
    for (size_t i = 0; i < blocks.size(); ++i) {
      auto& block = blocks[i];
      if (block.terminator.kind == TerminatorKind::Jump && 
          !block.terminator.targets.empty() &&
          block.instructions.empty()) {
        
        uint32_t target = block.terminator.targets[0];
        if (target < blocks.size() && target != i) {
          if (blocks[target].terminator.kind == TerminatorKind::Jump &&
              !blocks[target].terminator.targets.empty() &&
              blocks[target].instructions.empty()) {
            
            uint32_t final_target = blocks[target].terminator.targets[0];
            blocks[i].terminator.targets[0] = final_target;
            modified = true;
            result.modified = true;
          }
        }
      }
    }
    
    if (modified) {
      result.messages.push_back("Simplified jump chains");
    }
    return result;
  }
  
  PassResult eliminate_redundant_jumps(std::vector<BasicBlock>& blocks) {
    PassResult result;
    bool modified = false;
    
    for (size_t i = 0; i < blocks.size(); ++i) {
      auto& block = blocks[i];
      if (block.terminator.kind == TerminatorKind::Jump && 
          !block.terminator.targets.empty() &&
          block.terminator.targets[0] == static_cast<uint32_t>(i + 1)) {
        
        if (i + 1 < blocks.size()) {
          block.terminator = Terminator::unreachable();
          modified = true;
          result.modified = true;
        }
      }
    }
    
    if (modified) {
      result.messages.push_back("Eliminated redundant jumps to next block");
    }
    return result;
  }
  
  PassResult remove_empty_blocks(std::vector<BasicBlock>& blocks) {
    PassResult result;
    
    for (size_t i = 0; i < blocks.size(); ++i) {
      auto& block = blocks[i];
      if (block.instructions.empty() && 
          !block.is_landing_pad &&
          block.terminator.kind == TerminatorKind::Unreachable &&
          i != 0) {
        
        bool has_pred = false;
        for (auto& b : blocks) {
          for (uint32_t t : b.get_targets()) {
            if (t == i) { has_pred = true; break; }
          }
          if (has_pred) break;
        }
        
        if (!has_pred) {
          blocks.erase(blocks.begin() + i);
          result.modified = true;
          result.messages.push_back("Removed empty unreachable block");
          --i;
        }
      }
    }
    return result;
  }
};

// ===== Pass 2: ConstPropagation =====

class ConstPropagationPass : public BytecodePass {
public:
  PassType type() const override { return PassType::ConstPropagation; }
  std::string name() const override { return "ConstPropagation"; }
  std::vector<PassType> dependencies() const override { return {PassType::SimplifyCFG}; }
  std::vector<std::string> modified_state() const override { return {Analysis::kConstantState}; }

  PassResult run(std::vector<BasicBlock>& blocks, BytecodeFunction& func, const BytecodeChunk& chunk) override {
    PassResult result;

    // ---- 1. Cross-block constant propagation for locals using dataflow ----
    ConstPropagationAnalysis analysis;
    auto in_states = analysis.run(blocks, func);

    // For each block, propagate constants through the block using the IN state.
    for (size_t bi = 0; bi < blocks.size(); ++bi) {
      const BasicBlock& block = blocks[bi];
      ConstantMap local_consts = in_states[bi];

      for (size_t ii = 0; ii < block.instructions.size(); ++ii) {
        Instruction& inst = const_cast<Instruction&>(block.instructions[ii]);

        // Propagate known constant into instructions that use local indices
        switch (inst.opcode) {
          case OpCode::LOAD_VAR: {
            if (!inst.operands.empty() && inst.operands[0].isInt()) {
              uint64_t idx = static_cast<uint64_t>(inst.operands[0].asInt());
              if (idx < local_consts.size() && local_consts[idx].is_constant()) {
                // Replace LOAD_VAR with LOAD_CONST of the known value
                inst.opcode = OpCode::LOAD_CONST;
                inst.operands[0] = Value::makeInt(local_consts[idx].value);
                result.modified = true;
              }
            }
            break;
          }
          case OpCode::STORE_VAR:
          case OpCode::STORE_IMMUT_VAR: {
            if (!inst.operands.empty() && inst.operands[0].isInt()) {
              uint64_t idx = static_cast<uint64_t>(inst.operands[0].asInt());
              if (idx < local_consts.size()) {
                local_consts[idx] = ConstantValue::unknown();
              }
            }
            break;
          }
          default:
            break;
        }
      }
    }

    // ---- 2. Intra-block peephole constant folding ----
    // Scan each block for LOAD_CONST + binary op patterns and fold them.
    for (auto& block : blocks) {
      std::vector<Instruction> new_insts;
      new_insts.reserve(block.instructions.size());

      for (size_t ii = 0; ii < block.instructions.size(); ++ii) {
        const Instruction& inst = block.instructions[ii];

        // Look ahead for LOAD_CONST + binary op where both operands are const
        if (ii + 2 < block.instructions.size()) {
          const Instruction& inst1 = block.instructions[ii + 1];
          const Instruction& inst2 = block.instructions[ii + 2];

          if (inst.opcode == OpCode::LOAD_CONST && inst.operands[0].isInt() &&
              inst1.opcode == OpCode::LOAD_CONST && inst1.operands[0].isInt()) {
            int64_t a = inst.operands[0].asInt();
            int64_t b = inst1.operands[0].asInt();
            int64_t result_val = 0;
            bool folded = false;

            if (inst2.opcode == OpCode::ADD || inst2.opcode == OpCode::ADD_INT) {
              result_val = a + b;
              folded = true;
            } else if (inst2.opcode == OpCode::SUB || inst2.opcode == OpCode::SUB_INT) {
              result_val = a - b;
              folded = true;
            } else if (inst2.opcode == OpCode::MUL || inst2.opcode == OpCode::MUL_INT) {
              result_val = a * b;
              folded = true;
            } else if (inst2.opcode == OpCode::DIV || inst2.opcode == OpCode::DIV_INT) {
              if (b != 0) { result_val = a / b; folded = true; }
            } else if (inst2.opcode == OpCode::MOD || inst2.opcode == OpCode::MOD_INT) {
              if (b != 0) { result_val = a % b; folded = true; }
            }

            if (folded) {
              // Replace the three instructions with a single LOAD_CONST
              new_insts.push_back(Instruction(OpCode::LOAD_CONST, {Value::makeInt(result_val)}));
              ii += 2;  // Skip the next two instructions we folded
              result.modified = true;
              result.messages.push_back("ConstPropagation: folded constant binary op");
              continue;
            }
          }
        }

        // Also fold unary negation
        if (ii + 1 < block.instructions.size()) {
          const Instruction& next = block.instructions[ii + 1];
          if (inst.opcode == OpCode::LOAD_CONST && inst.operands[0].isInt() &&
              next.opcode == OpCode::NEGATE) {
            int64_t val = inst.operands[0].asInt();
            new_insts.push_back(Instruction(OpCode::LOAD_CONST, {Value::makeInt(-val)}));
            ++ii;  // Skip NEGATE
            result.modified = true;
            result.messages.push_back("ConstPropagation: folded NEGATE");
            continue;
          }
        }

        new_insts.push_back(inst);
      }

      if (new_insts.size() != block.instructions.size()) {
        block.instructions.swap(new_insts);
        result.modified = true;
      }
    }

    // ---- 3. Remove redundant LOAD_CONST + POP pairs ----
    for (auto& block : blocks) {
      std::vector<Instruction> new_insts;
      new_insts.reserve(block.instructions.size());

      for (size_t i = 0; i < block.instructions.size(); ++i) {
        const Instruction& inst = block.instructions[i];
        bool skip = false;

        if (i + 1 < block.instructions.size()) {
          const Instruction& next = block.instructions[i + 1];
          if (inst.opcode == OpCode::LOAD_CONST && next.opcode == OpCode::POP) {
            result.modified = true;
            skip = true;
            result.messages.push_back("ConstPropagation: removed dead LOAD_CONST");
          }
        }

        if (!skip) new_insts.push_back(inst);
      }

      if (new_insts.size() != block.instructions.size()) {
        block.instructions.swap(new_insts);
        result.modified = true;
      }
    }

    if (!result.modified) {
      result.messages.push_back("ConstPropagation: no changes");
    }
    return result;
  }
};

// ===== Pass 3: DeadCodeElimination =====

class DeadCodeEliminationPass : public BytecodePass {
public:
  PassType type() const override { return PassType::DeadCodeElimination; }
  std::string name() const override { return "DeadCodeElimination"; }
  std::vector<PassType> dependencies() const override { return {PassType::ConstPropagation}; }
  std::vector<std::string> modified_state() const override { return {Analysis::kLiveness}; }

  PassResult run(std::vector<BasicBlock>& blocks, BytecodeFunction& func, const BytecodeChunk& chunk) override {
    PassResult result;

    // ---- 1. Liveness analysis for local variables ----
    // Compute which locals are live at each program point.
    LivenessAnalysis analysis;
    auto in_live = analysis.run(blocks, func);

    // For each block, we track liveness backwards through instructions.
    // An instruction is dead if:
    // - It writes a local that is not live after the instruction
    // - It is a LOAD_VAR of a local not live after, and the value is not used
    // For now we do the simple local-dead-store elimination.

    for (size_t bi = 0; bi < blocks.size(); ++bi) {
      auto& block = blocks[bi];
      if (block.instructions.empty()) continue;

      // Start with OUT live set from dataflow (what's live at block exit)
      LiveSet live = in_live[bi];

      // Process instructions backwards
      std::vector<Instruction> new_insts;
      new_insts.reserve(block.instructions.size());

      for (int ii = static_cast<int>(block.instructions.size()) - 1; ii >= 0; --ii) {
        const Instruction& inst = block.instructions[ii];
        bool keep = true;

        switch (inst.opcode) {
          case OpCode::STORE_VAR:
          case OpCode::STORE_IMMUT_VAR: {
            if (!inst.operands.empty() && inst.operands[0].isInt()) {
              uint64_t idx = static_cast<uint64_t>(inst.operands[0].asInt());
              if (idx < live.live.size() && !live.live[idx]) {
                // Stored value is never read - dead store
                keep = false;
                result.modified = true;
                result.messages.push_back("DCE: removed dead store to local " + std::to_string(idx));
              } else {
                // This local is now live (its value is needed)
                if (idx < live.live.size()) live.live[idx] = true;
              }
            }
            break;
          }
          case OpCode::LOAD_VAR: {
            if (!inst.operands.empty() && inst.operands[0].isInt()) {
              uint64_t idx = static_cast<uint64_t>(inst.operands[0].asInt());
              if (idx < live.live.size() && !live.live[idx]) {
                // Loaded value is never used - dead load
                keep = false;
                result.modified = true;
                result.messages.push_back("DCE: removed dead load of local " + std::to_string(idx));
              } else {
                // This local is live (its value is needed)
                if (idx < live.live.size()) live.live[idx] = true;
              }
            }
            break;
          }
          case OpCode::INCLOCAL:
          case OpCode::DECLOCAL:
          case OpCode::INCLOCAL_POST:
          case OpCode::DECLOCAL_POST: {
            if (!inst.operands.empty() && inst.operands[0].isInt()) {
              uint64_t idx = static_cast<uint64_t>(inst.operands[0].asInt());
              if (idx < live.live.size() && !live.live[idx]) {
                // Increment/decrement of dead local
                keep = false;
                result.modified = true;
                result.messages.push_back("DCE: removed dead inc/dec of local " + std::to_string(idx));
              } else {
                if (idx < live.live.size()) live.live[idx] = true;
              }
            }
            break;
          }
          default:
            // For other instructions, we conservatively keep them.
            // A full DCE would track stack liveness.
            break;
        }

        if (keep) {
          new_insts.push_back(inst);
        }
      }

      // Reverse back to forward order
      std::reverse(new_insts.begin(), new_insts.end());
      if (new_insts.size() != block.instructions.size()) {
        block.instructions.swap(new_insts);
        result.modified = true;
      }
    }

    // ---- 2. Peephole dead code elimination (stack patterns) ----
    for (auto& block : blocks) {
      std::vector<Instruction> new_insts;
      new_insts.reserve(block.instructions.size());

      for (size_t i = 0; i < block.instructions.size(); ++i) {
        const Instruction& inst = block.instructions[i];
        bool skip = false;

        if (i + 1 < block.instructions.size()) {
          const Instruction& next = block.instructions[i + 1];

          // LOAD_CONST -> POP
          if (inst.opcode == OpCode::LOAD_CONST && next.opcode == OpCode::POP) {
            result.modified = true;
            skip = true;
            result.messages.push_back("DCE: removed LOAD_CONST + POP");
          }
          // DUP -> POP
          else if (inst.opcode == OpCode::DUP && next.opcode == OpCode::POP) {
            result.modified = true;
            skip = true;
            result.messages.push_back("DCE: removed DUP + POP");
          }
          // LOAD_VAR -> POP (dead load)
          else if (inst.opcode == OpCode::LOAD_VAR && next.opcode == OpCode::POP) {
            result.modified = true;
            skip = true;
            result.messages.push_back("DCE: removed LOAD_VAR + POP");
          }
        }

        if (!skip) new_insts.push_back(inst);
      }

      if (new_insts.size() != block.instructions.size()) {
        block.instructions.swap(new_insts);
        result.modified = true;
      }
    }

    return result;
  }
};

// ===== Copy Propagation Pass =====
//
// Simple intra-block copy propagation: tracks local-to-local copies
// (STORE_VAR x from LOAD_VAR y) and replaces subsequent LOAD_VAR x
// with LOAD_VAR y within the same block.

PassResult CopyPropagationPass::run(std::vector<BasicBlock>& blocks, BytecodeFunction& func, const BytecodeChunk& chunk) {
  PassResult result;

  for (auto& block : blocks) {
    std::vector<Instruction> new_insts;
    new_insts.reserve(block.instructions.size());
    bool changed = false;

    for (size_t ii = 0; ii < block.instructions.size(); ++ii) {
      const Instruction& inst = block.instructions[ii];
      bool replaced = false;

      if (inst.opcode == OpCode::LOAD_VAR && !inst.operands.empty() &&
          inst.operands[0].isInt()) {
        uint64_t idx = static_cast<uint64_t>(inst.operands[0].asInt());

        // Walk backwards to find if this local was copied from another
        for (int jj = static_cast<int>(ii) - 1; jj >= 0; --jj) {
          const Instruction& prev = block.instructions[jj];
          if (prev.opcode == OpCode::STORE_VAR ||
              prev.opcode == OpCode::STORE_IMMUT_VAR) {
            if (!prev.operands.empty() && prev.operands[0].isInt()) {
              uint64_t dst = static_cast<uint64_t>(prev.operands[0].asInt());
              if (dst == idx && jj > 0) {
                // Check if the value stored came from LOAD_VAR of another local
                const Instruction& src_inst = block.instructions[jj - 1];
                if (src_inst.opcode == OpCode::LOAD_VAR &&
                    !src_inst.operands.empty() &&
                    src_inst.operands[0].isInt()) {
                  uint64_t src_idx = static_cast<uint64_t>(src_inst.operands[0].asInt());
                  if (src_idx != idx) {
                    // Replace LOAD_VAR idx with LOAD_VAR src_idx
                    new_insts.push_back(
                        Instruction(OpCode::LOAD_VAR, {Value::makeInt(src_idx)}));
                    result.modified = true;
                    result.messages.push_back("CopyPropagation: replaced local " +
                                              std::to_string(idx) + " with " +
                                              std::to_string(src_idx));
                    replaced = true;
                    changed = true;
                  }
                }
                break;  // Stop at first store to this local
              }
            }
          }
        }
      }

      if (!replaced) {
        new_insts.push_back(inst);
      }
    }

    if (changed) {
      block.instructions.swap(new_insts);
      result.modified = true;
    }
  }

  return result;
}

// ===== Type Propagation Pass =====
//
// Runs the forward type analysis and writes the resulting per-local masks into
// func.locals[i].type_hint. Masks are OR-ed in (never removing existing hints
// such as frontend annotations). The result feeds JIT AOT type feedback.

PassResult TypePropagationPass::run(std::vector<BasicBlock>& blocks, BytecodeFunction& func, const BytecodeChunk& chunk) {
  PassResult result;
  if (func.locals.empty() || !func.has_cfg()) {
    return result;
  }

  TypePropagationAnalysis analysis;
  auto masks = analysis.run(blocks, func);
  if (masks.size() != func.locals.size()) {
    return result;
  }

  bool changed = false;
  for (size_t i = 0; i < masks.size(); ++i) {
    TypePropagationAnalysis::TypeMask inferred = masks[i];
    if (inferred != 0 && (func.locals[i].type_hint & inferred) != inferred) {
      func.locals[i].type_hint |= inferred;
      changed = true;
    }
  }
  if (changed) {
    result.modified = true;
    result.messages.push_back("TypePropagation: updated local type hints");
  }
  return result;
}

// ===== Pass 4: Inlining =====
//
// Conservative direct-call inliner. Inlines calls of the form
//
//   LOAD_GLOBAL <fn>
//   CALL n
//
// when the callee is small, non-recursive, non-variadic, has no upvalues, and
// contains no nested calls of its own (so no recursive/cross call-graph state
// needs tracking). The callee's locals are shifted into fresh caller slots,
// its globals merged into the caller's name table, and its Return/None
// terminators rewired to jump to the continuation after the call site.

namespace {

constexpr size_t kInlineMaxBody = 24;

bool has_nested_call(const BytecodeFunction& f) {
  for (const auto& b : f.blocks) {
    for (const auto& inst : b.instructions) {
      switch (inst.opcode) {
        case OpCode::CALL:
        case OpCode::CALL_DYN:
        case OpCode::CALL_SPREAD:
        case OpCode::TAIL_CALL:
        case OpCode::FFI_CALL:
          return true;
        default:
          break;
      }
    }
    if (b.terminator.kind == TerminatorKind::CallReturn ||
        b.terminator.kind == TerminatorKind::Throw) {
      return true;
    }
  }
  return false;
}

size_t count_instructions(const BytecodeFunction& f) {
  size_t n = 0;
  for (const auto& b : f.blocks) n += b.instructions.size();
  return n;
}

// Defined below; declared here so InliningPass::run can call it.
PassResult inline_call_at(std::vector<BasicBlock>& blocks, BytecodeFunction& func,
                          size_t bi, size_t ii, const BytecodeFunction& callee,
                          std::unordered_map<std::string, uint32_t>& name_to_idx);

}  // namespace

PassResult InliningPass::run(std::vector<BasicBlock>& blocks, BytecodeFunction& func,
                             const BytecodeChunk& chunk) {
    PassResult result;
    if (!func.has_cfg() || blocks.empty() || chunk.getFunctionCount() == 0) {
      return result;
    }

    // Caller's global names keep their indices; callee names merge in on top.
    std::unordered_map<std::string, uint32_t> name_to_idx;
    for (size_t i = 0; i < func.global_names.size(); ++i) {
      name_to_idx.emplace(func.global_names[i], static_cast<uint32_t>(i));
    }

    // One call site per iteration; iterations are bounded by the number of
    // call sites that get inlined (each iteration removes one).
    for (int iter = 0; iter < 32; ++iter) {
      bool found = false;
      for (size_t bi = 0; bi < blocks.size() && !found; ++bi) {
        const BasicBlock& block = blocks[bi];
        for (size_t ii = 0; ii + 1 < block.instructions.size(); ++ii) {
          const Instruction& ig = block.instructions[ii];
          const Instruction& ic = block.instructions[ii + 1];
          if (ig.opcode != OpCode::LOAD_GLOBAL || ic.opcode != OpCode::CALL ||
              ig.operands.empty() || !ig.operands[0].isStringValId() ||
              ic.operands.empty() || !ic.operands[0].isInt()) {
            continue;
          }
          uint64_t name_idx = ig.operands[0].asStringValId();
          if (name_idx >= func.global_names.size()) continue;
          const BytecodeFunction* callee = chunk.getFunction(func.global_names[name_idx]);
          const uint32_t arg_count = static_cast<uint32_t>(ic.operands[0].asInt());
          if (!callee || callee == &func || !callee->has_cfg() || callee->blocks.empty() ||
              callee->entry_block != 0 || callee->param_count != arg_count ||
              callee->variadic_param_index != UINT32_MAX || callee->is_generator ||
              !callee->upvalues.empty() || count_instructions(*callee) > kInlineMaxBody ||
              has_nested_call(*callee)) {
            continue;
          }

          const PassResult sub = inline_call_at(blocks, func, bi, ii, *callee, name_to_idx);
          if (sub.modified) {
            result |= sub;
            found = true;
            // `block`, `ig`, `ic` reference the pre-inline buffer, which
            // inline_call_at swapped away; leave the scan immediately.
            break;
          }
        }
      }
      if (!found) break;
    }
    return result;
  }

namespace {

PassResult inline_call_at(std::vector<BasicBlock>& blocks, BytecodeFunction& func,
                          size_t bi, size_t ii, const BytecodeFunction& callee,
                          std::unordered_map<std::string, uint32_t>& name_to_idx) {
    PassResult result;
    const uint32_t arg_count = callee.param_count;
    const uint32_t delta = static_cast<uint32_t>(func.locals.size());

    // Extend the caller's local table with renamed callee slots.
    func.locals.reserve(delta + callee.locals.size());
    for (const LocalInfo& li : callee.locals) {
      func.locals.push_back(li);
    }

    auto intern_name = [&](const std::string& name) -> uint32_t {
      auto it = name_to_idx.find(name);
      if (it != name_to_idx.end()) return it->second;
      uint32_t idx = static_cast<uint32_t>(func.global_names.size());
      func.global_names.push_back(name);
      name_to_idx.emplace(name, idx);
      return idx;
    };

    // Clone a callee block, shifting local indices by `delta` and interning
    // global names into the caller's table.
    auto clone_renamed = [&](const BasicBlock& src) -> BasicBlock {
      BasicBlock out(src.id);
      out.instructions.reserve(src.instructions.size());
      for (const Instruction& inst : src.instructions) {
        Instruction c = inst;
        switch (c.opcode) {
          case OpCode::LOAD_VAR:
          case OpCode::STORE_VAR:
          case OpCode::STORE_IMMUT_VAR:
          case OpCode::INCLOCAL:
          case OpCode::DECLOCAL:
          case OpCode::INCLOCAL_POST:
          case OpCode::DECLOCAL_POST:
            if (!c.operands.empty() && c.operands[0].isInt()) {
              c.operands[0] =
                  Value::makeInt(c.operands[0].asInt() + static_cast<int64_t>(delta));
            }
            break;
          case OpCode::LOAD_GLOBAL:
          case OpCode::STORE_GLOBAL:
          case OpCode::STORE_IMMUT_GLOBAL:
            if (!c.operands.empty() && c.operands[0].isStringValId()) {
              uint64_t gi = c.operands[0].asStringValId();
              if (gi < callee.global_names.size()) {
                c.operands[0] = Value::makeStringValId(intern_name(callee.global_names[gi]));
              }
            }
            break;
          default:
            break;
        }
        out.instructions.push_back(std::move(c));
      }
      return out;
    };

    // ---- Layout: assign new ids to every block ----
    std::vector<uint32_t> new_id(blocks.size());
    const uint32_t prelude_id = (arg_count > 0) ? static_cast<uint32_t>(bi) + 1 : UINT32_MAX;
    const uint32_t callee_start = (arg_count > 0) ? prelude_id + 1 : static_cast<uint32_t>(bi) + 1;
    std::vector<uint32_t> callee_new_id(callee.blocks.size());
    for (size_t c = 0; c < callee.blocks.size(); ++c) {
      callee_new_id[c] = callee_start + static_cast<uint32_t>(c);
    }
    const uint32_t b2_id = callee_start + static_cast<uint32_t>(callee.blocks.size());
    const uint32_t extra = (arg_count > 0 ? 1u : 0u) + static_cast<uint32_t>(callee.blocks.size()) + 1u;
    for (size_t o = 0; o < blocks.size(); ++o) {
      // The split block keeps its position (and becomes B1); every later block
      // shifts by the number of inserted blocks so ids stay position-aligned.
      new_id[o] = (o > bi) ? static_cast<uint32_t>(o) + extra : static_cast<uint32_t>(o);
    }

    auto map_targets = [&](Terminator& t) {
      for (auto& tgt : t.targets) {
        if (tgt < new_id.size()) tgt = new_id[tgt];
      }
    };

    // ---- Materialize the new block list ----
    std::vector<BasicBlock> out;
    out.reserve(blocks.size() + extra);
    for (size_t o = 0; o < blocks.size(); ++o) {
      if (o != bi) {
        out.push_back(blocks[o]);
        map_targets(out.back().terminator);
        continue;
      }

      // B1: instructions before the call; jumps into the callee prelude/entry.
      BasicBlock b1(new_id[bi]);
      b1.instructions.assign(blocks[bi].instructions.begin(),
                             blocks[bi].instructions.begin() + static_cast<ptrdiff_t>(ii));
      b1.terminator =
          Terminator::jump(arg_count > 0 ? prelude_id : callee_new_id[callee.entry_block],
                           blocks[bi].terminator.location);
      out.push_back(std::move(b1));

      // Prelude: pop args into renamed param slots (top of stack is last arg).
      if (arg_count > 0) {
        BasicBlock pre(prelude_id);
        pre.instructions.reserve(arg_count);
        for (uint32_t a = arg_count; a-- > 0;) {
          pre.instructions.emplace_back(
              OpCode::STORE_VAR, std::vector<Value>{Value::makeInt(static_cast<int64_t>(delta) + a)});
        }
        pre.terminator = Terminator::jump(callee_new_id[callee.entry_block]);
        out.push_back(std::move(pre));
      }

      // Callee body, renamed; Return/None become jumps to the continuation.
      auto map_callee_targets = [&](Terminator& t) {
        for (auto& tgt : t.targets) {
          if (tgt < callee_new_id.size()) tgt = callee_new_id[tgt];
        }
      };
      for (size_t c = 0; c < callee.blocks.size(); ++c) {
        BasicBlock cb = clone_renamed(callee.blocks[c]);
        cb.id = callee_new_id[c];
        if (cb.terminator.kind == TerminatorKind::Return ||
            cb.terminator.kind == TerminatorKind::None) {
          cb.terminator = Terminator::jump(b2_id, cb.terminator.location);
        } else {
          map_callee_targets(cb.terminator);
        }
        out.push_back(std::move(cb));
      }

      // B2: continuation after the call site, keeps the original terminator.
      BasicBlock b2(b2_id);
      const size_t from = ii + 2;
      b2.instructions.assign(blocks[bi].instructions.begin() + static_cast<ptrdiff_t>(from),
                             blocks[bi].instructions.end());
      b2.terminator = blocks[bi].terminator;
      map_targets(b2.terminator);
      out.push_back(std::move(b2));
    }

    // Recompute predecessor lists from the new terminators.
    for (auto& b : out) b.predecessors.clear();
    for (uint32_t i = 0; i < out.size(); ++i) {
      for (uint32_t t : out[i].get_targets()) {
        if (t != UINT32_MAX && t < out.size()) out[t].predecessors.push_back(i);
      }
    }

    blocks.swap(out);
    result.modified = true;
    result.messages.push_back("Inlining: inlined " + callee.name + " (" +
                              std::to_string(arg_count) + " args)");
    return result;
}

}  // namespace

// ===== Pass 5: LICM =====
//
// Loop-invariant code motion on the stack-based CFG. Detects natural loops via
// DFS back edges, then hoists invariant `LOAD_CONST/LOAD_VAR <inv>; STORE_VAR
// X` pairs out of the loop header (which runs every iteration) into a
// preheader. The pair is net stack-neutral (push + pop), so moving it keeps
// stack discipline in both places. Conditions:
//   - X (and the loaded local, if any) is written nowhere else in the loop;
//   - the header has exactly one non-loop predecessor (the preheader);
//   - the preheader terminates with a plain Jump to the header.

namespace {
// Defined below; declared here so LICMPass::run can call them.
bool hoist_once(std::vector<BasicBlock>& blocks, PassResult& result);
bool is_written_in_loop(uint64_t slot, const std::vector<BasicBlock>& blocks,
                        const std::vector<char>& in_loop, uint32_t header,
                        size_t skip_a, size_t skip_b);
void hoist_pair(std::vector<BasicBlock>& blocks, uint32_t header, size_t k,
                uint32_t preheader, const Instruction& a, const Instruction& b);
}  // namespace

PassResult LICMPass::run(std::vector<BasicBlock>& blocks, BytecodeFunction& /*func*/,
                         const BytecodeChunk& /*chunk*/) {
    PassResult result;
    if (blocks.size() < 2) return result;
    for (int iter = 0; iter < 32; ++iter) {
      if (!hoist_once(blocks, result)) break;
    }
    return result;
  }

namespace {

std::vector<std::vector<uint32_t>> compute_preds(const std::vector<BasicBlock>& blocks) {
    std::vector<std::vector<uint32_t>> preds(blocks.size());
    for (uint32_t i = 0; i < blocks.size(); ++i) {
      for (uint32_t t : blocks[i].get_targets()) {
        if (t < blocks.size()) preds[t].push_back(i);
      }
    }
    return preds;
  }

  // DFS in/out times; edge (i -> h) is a back edge iff h is an ancestor of i.
  bool find_back_edge(const std::vector<BasicBlock>& blocks,
                             uint32_t* out_header, uint32_t* out_latch,
                             std::vector<std::vector<uint32_t>>* out_preds) {
    const size_t n = blocks.size();
    std::vector<std::vector<uint32_t>> preds = compute_preds(blocks);
    std::vector<uint32_t> in_t(n, 0), out_t(n, 0);
    std::vector<char> seen(n, 0);
    uint32_t timer = 0;
    std::function<void(uint32_t)> dfs = [&](uint32_t b) {
      seen[b] = 1;
      in_t[b] = timer++;
      for (uint32_t s : blocks[b].get_targets()) {
        if (s < n && !seen[s]) dfs(s);
      }
      out_t[b] = timer++;
    };
    dfs(0);
    for (size_t i = 0; i < n; ++i) {
      for (uint32_t h : blocks[i].get_targets()) {
        if (h < n && seen[h] && in_t[h] <= in_t[i] && out_t[i] <= out_t[h] && h != i) {
          *out_header = h;
          *out_latch = static_cast<uint32_t>(i);
          *out_preds = std::move(preds);
          return true;
        }
      }
    }
    return false;
  }

  bool hoist_once(std::vector<BasicBlock>& blocks, PassResult& result) {
    uint32_t header = 0, latch = 0;
    std::vector<std::vector<uint32_t>> preds;
    if (!find_back_edge(blocks, &header, &latch, &preds)) return false;
    if (header == 0) return false;  // Entry cannot be a loop header here.

    const size_t n = blocks.size();
    // Loop body = header + every node that can reach `latch` without passing
    // through `header` (reverse BFS over predecessors).
    std::vector<char> in_loop(n, 0);
    in_loop[header] = 1;
    std::queue<uint32_t> q;
    q.push(latch);
    while (!q.empty()) {
      uint32_t cur = q.front();
      q.pop();
      if (cur == header || in_loop[cur]) continue;
      in_loop[cur] = 1;
      for (uint32_t p : preds[cur]) {
        if (p < n && !in_loop[p] && p != header) q.push(p);
      }
    }

    // Exactly one non-loop predecessor of the header = the preheader.
    uint32_t preheader = UINT32_MAX;
    for (uint32_t p : preds[header]) {
      if (p < n && !in_loop[p]) {
        if (preheader != UINT32_MAX) return false;  // Too many entries
        preheader = p;
      }
    }
    if (preheader == UINT32_MAX) return false;
    if (blocks[preheader].terminator.kind != TerminatorKind::Jump ||
        blocks[preheader].terminator.targets.size() != 1 ||
        blocks[preheader].terminator.targets[0] != header) {
      return false;  // Preheader must be a plain jump into the header
    }

    // Find a hoistable invariant store pair in the header block.
    std::vector<Instruction>& hi = blocks[header].instructions;
    for (size_t k = 0; k + 1 < hi.size(); ++k) {
      const Instruction& a = hi[k];
      const Instruction& b = hi[k + 1];
      if (b.opcode != OpCode::STORE_VAR && b.opcode != OpCode::STORE_IMMUT_VAR) continue;
      if (b.operands.empty() || !b.operands[0].isInt()) continue;
      const uint64_t dst = static_cast<uint64_t>(b.operands[0].asInt());

      bool invariant = false;
      if (a.opcode == OpCode::LOAD_CONST && !a.operands.empty()) {
        invariant = true;
      } else if (a.opcode == OpCode::LOAD_VAR && !a.operands.empty() && a.operands[0].isInt()) {
        const uint64_t src = static_cast<uint64_t>(a.operands[0].asInt());
        if (src != dst &&
            !is_written_in_loop(src, blocks, in_loop, header, k, k + 1)) {
          invariant = true;
        }
      }
      if (!invariant) continue;
      if (is_written_in_loop(dst, blocks, in_loop, header, k, k + 1)) continue;

      hoist_pair(blocks, header, k, preheader, a, b);
      result.modified = true;
      result.messages.push_back("LICM: hoisted invariant store pair (local " +
                                std::to_string(dst) + ") to preheader block " +
                                std::to_string(preheader));
      return true;
    }
    return false;
  }

  bool is_written_in_loop(uint64_t slot, const std::vector<BasicBlock>& blocks,
                                 const std::vector<char>& in_loop, uint32_t header,
                                 size_t skip_a, size_t skip_b) {
    const size_t n = blocks.size();
    for (size_t b = 0; b < n; ++b) {
      if (!in_loop[b]) continue;
      const auto& insts = blocks[b].instructions;
      for (size_t x = 0; x < insts.size(); ++x) {
        if (b == header && (x == skip_a || x == skip_b)) continue;
        const Instruction& inst = insts[x];
        switch (inst.opcode) {
          case OpCode::STORE_VAR:
          case OpCode::STORE_IMMUT_VAR:
          case OpCode::INCLOCAL:
          case OpCode::DECLOCAL:
          case OpCode::INCLOCAL_POST:
          case OpCode::DECLOCAL_POST:
            if (!inst.operands.empty() && inst.operands[0].isInt() &&
                static_cast<uint64_t>(inst.operands[0].asInt()) == slot) {
              return true;
            }
            break;
          default:
            break;
        }
      }
    }
    return false;
  }

void hoist_pair(std::vector<BasicBlock>& blocks, uint32_t header, size_t k,
                uint32_t preheader, const Instruction& a, const Instruction& b) {
    // Build the new list without mutating the originals in place: the
    // preheader is inserted right before the header (ids >= header shift by
    // one so ids stay position-aligned), and the header copy drops the pair.
    std::vector<BasicBlock> out;
    out.reserve(blocks.size() + 1);
    auto map = [&](uint32_t old) { return old >= header ? old + 1 : old; };
    for (size_t o = 0; o < blocks.size(); ++o) {
      if (o == static_cast<size_t>(header)) {
        BasicBlock pre(header);
        pre.instructions.push_back(a);  // copies; source stays alive
        pre.instructions.push_back(b);
        pre.terminator = Terminator::jump(map(header));
        out.push_back(std::move(pre));
      }
      BasicBlock nb = blocks[o];
      nb.id = map(static_cast<uint32_t>(o));
      if (o == static_cast<size_t>(header)) {
        nb.instructions.erase(nb.instructions.begin() + static_cast<ptrdiff_t>(k),
                              nb.instructions.begin() + static_cast<ptrdiff_t>(k) + 2);
      }
      for (auto& tgt : nb.terminator.targets) tgt = map(tgt);
      out.push_back(std::move(nb));
    }

    // Recompute predecessors from the new terminators.
    for (auto& x : out) x.predecessors.clear();
    for (uint32_t i = 0; i < out.size(); ++i) {
      for (uint32_t t : out[i].get_targets()) {
        if (t != UINT32_MAX && t < out.size()) out[t].predecessors.push_back(i);
      }
    }
    blocks.swap(out);
  }

}  // namespace

// ===== Validation Pass =====

class ValidationPass : public BytecodePass {
public:
  PassType type() const override { return PassType::Validation; }
  std::string name() const override { return "Validation"; }
  bool requires_validation() const override { return false; }
  std::vector<std::string> preserved_analyses() const override {
    return {Analysis::kCFG, Analysis::kLocals, Analysis::kConstantState,
            Analysis::kLiveness, Analysis::kTypeState, Analysis::kCopyState};
  }
  
  PassResult run(std::vector<BasicBlock>& blocks, BytecodeFunction& func, const BytecodeChunk& chunk) override {
    auto validation = validate_function(func, func.entry_block);
    PassResult result;
    result.valid = validation.valid;
    result.messages = validation.errors;
    if (!validation.warnings.empty()) {
      result.messages.insert(result.messages.end(), validation.warnings.begin(), validation.warnings.end());
    }
    if (!validation.valid) {
      result.modified = true;
    }
    return result;
  }
};

// ===== Pass Factory =====

std::unique_ptr<BytecodePass> create_pass(PassType type) {
  switch (type) {
    case PassType::SimplifyCFG:
      return std::make_unique<SimplifyCFGPass>();
    case PassType::ConstPropagation:
      return std::make_unique<ConstPropagationPass>();
    case PassType::CopyPropagation:
      return std::make_unique<CopyPropagationPass>();
    case PassType::TypePropagation:
      return std::make_unique<TypePropagationPass>();
    case PassType::DeadCodeElimination:
      return std::make_unique<DeadCodeEliminationPass>();
    case PassType::Inlining:
      return std::make_unique<InliningPass>();
    case PassType::LICM:
      return std::make_unique<LICMPass>();
    case PassType::Validation:
      return std::make_unique<ValidationPass>();
    default:
      return nullptr;
  }
}

// ===== Pipeline Creation =====

std::unique_ptr<PassManager> create_standard_pipeline() {
  auto pm = std::make_unique<PassManager>();
  pm->add_pass(std::make_unique<SimplifyCFGPass>());
  pm->add_pass(std::make_unique<ValidationPass>());
  pm->add_pass(std::make_unique<ConstPropagationPass>());
  pm->add_pass(std::make_unique<ValidationPass>());
  pm->add_pass(std::make_unique<CopyPropagationPass>());
  pm->add_pass(std::make_unique<ValidationPass>());
  pm->add_pass(std::make_unique<TypePropagationPass>());
  pm->add_pass(std::make_unique<ValidationPass>());
  pm->add_pass(std::make_unique<DeadCodeEliminationPass>());
  pm->add_pass(std::make_unique<ValidationPass>());
  pm->add_pass(std::make_unique<InliningPass>());
  pm->add_pass(std::make_unique<ValidationPass>());
  pm->add_pass(std::make_unique<LICMPass>());
  pm->add_pass(std::make_unique<ValidationPass>());
  return pm;
}

std::unique_ptr<PassManager> create_fast_pipeline() {
  auto pm = std::make_unique<PassManager>();
  pm->add_pass(std::make_unique<SimplifyCFGPass>());
  pm->add_pass(std::make_unique<DeadCodeEliminationPass>());
  return pm;
}

std::unique_ptr<PassManager> create_optimizing_pipeline() {
  return create_standard_pipeline();
}

} // namespace havel::compiler