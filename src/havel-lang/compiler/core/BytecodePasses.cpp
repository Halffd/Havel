#include "BytecodePasses.hpp"
#include "DataflowAnalysis.hpp"

#include <algorithm>
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
  
  PassResult run(std::vector<BasicBlock>& blocks, BytecodeFunction& func) override {
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

  PassResult run(std::vector<BasicBlock>& blocks, BytecodeFunction& func) override {
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

  PassResult run(std::vector<BasicBlock>& blocks, BytecodeFunction& func) override {
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

PassResult CopyPropagationPass::run(std::vector<BasicBlock>& blocks, BytecodeFunction& func) {
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

PassResult TypePropagationPass::run(std::vector<BasicBlock>& blocks, BytecodeFunction& func) {
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

class InliningPass : public BytecodePass {
public:
  PassType type() const override { return PassType::Inlining; }
  std::string name() const override { return "Inlining"; }
  std::vector<PassType> dependencies() const override { return {PassType::SimplifyCFG, PassType::ConstPropagation}; }
  
  PassResult run(std::vector<BasicBlock>& blocks, BytecodeFunction& func) override {
    PassResult result;
    result.messages.push_back("Inlining: requires call graph + size heuristics");
    return result;
  }
};

// ===== Validation Pass =====

class ValidationPass : public BytecodePass {
public:
  PassType type() const override { return PassType::Validation; }
  std::string name() const override { return "Validation"; }
  bool requires_validation() const override { return false; }
  
  PassResult run(std::vector<BasicBlock>& blocks, BytecodeFunction& func) override {
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