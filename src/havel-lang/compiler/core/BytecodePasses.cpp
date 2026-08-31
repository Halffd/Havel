#include "BytecodePasses.hpp"
#include <algorithm>
#include <queue>
#include <unordered_set>

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
    
    // Track known constant values for locals
    std::vector<std::optional<int64_t>> local_values(func.local_count + func.param_count);
    std::vector<bool> is_constant(func.local_count + func.param_count, false);
    
    // Forward propagation - simplified implementation
    for (size_t bi = 0; bi < blocks.size(); ++bi) {
      auto& block = blocks[bi];
      for (size_t ii = 0; ii < block.instructions.size(); ++ii) {
        Instruction& inst = block.instructions[ii];
        
        switch (inst.opcode) {
          case OpCode::LOAD_CONST: {
            if (!inst.operands.empty() && inst.operands[0].isInt()) {
              int64_t val = inst.operands[0].asInt();
              // Next instruction should be STORE_VAR - track in real impl
            }
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
            // Fold constants if both operands known - simplified
            break;
          }
          case OpCode::STORE_VAR: {
            if (!inst.operands.empty() && inst.operands[0].isInt()) {
              // Track constant value for this local
            }
            break;
          }
          default:
            break;
        }
      }
    }
    
    result.messages.push_back("ConstPropagation: stack-based analysis needed");
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
    
    for (auto& block : blocks) {
      if (block.instructions.empty()) continue;
      
      std::vector<Instruction> new_insts;
      new_insts.reserve(block.instructions.size());
      
      for (size_t i = 0; i < block.instructions.size(); ++i) {
        const auto& inst = block.instructions[i];
        bool skip = false;
        
        if (i + 1 < block.instructions.size()) {
          const auto& next = block.instructions[i + 1];
          
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
        }
        
        if (!skip) {
          new_insts.push_back(inst);
        }
      }
      
      if (new_insts.size() != block.instructions.size()) {
        block.instructions.swap(new_insts);
        result.modified = true;
      }
    }
    
    return result;
  }
};

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
    auto validation = validate_cfg(blocks);
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