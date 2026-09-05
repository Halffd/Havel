// CFG/IR unit tests for the BytecodeIR stabilization (TODO.md Phase 1).
//
// Exercises the FunctionBuilder, CFGValidationResult/validate_cfg, and
// LinearFunction/flatten_cfg against concrete control-flow shapes (if/else,
// loops, switch) plus the typed-local/parameter metadata round-trip.
//
// One test = one concern; every test has assertions; no dependency on external
// files or scripts.

#include "BytecodeIR.hpp"
#include "BytecodePasses.hpp"
#include "DataflowAnalysis.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace havel::compiler {
namespace {

using havel::core::Value;

class CFGPipelineTest : public ::testing::Test {};

TEST_F(CFGPipelineTest, RetOnlyFunctionValidates) {
  FunctionBuilder fb("f", 0, 0);
  fb.ret();
  fb.create_block();  // trailing fall-through (end of stream)

  BytecodeFunction f = fb.build();
  const auto v = validate_cfg(f.blocks, f.entry_block);
  ASSERT_TRUE(v.errors.empty()) << v.errors[0];
  EXPECT_TRUE(v.valid);
  EXPECT_EQ(f.blocks.size(), 2u);
}

TEST_F(CFGPipelineTest, IfElseBranchValidatesAndFlattens) {
  FunctionBuilder fb("ifelse", 1, 0);
  // entry (block 0): param[0] == 0
  fb.load_var(0);
  fb.load_const(Value::makeInt(0));
  fb.eq();

  uint32_t then_b = fb.create_block();
  uint32_t exit_b = fb.create_block();
  fb.set_current_block(0);
  auto ab = fb.branch(0, then_b, exit_b);
  EXPECT_EQ(ab.first, then_b);
  EXPECT_EQ(ab.second, exit_b);

  fb.set_current_block(then_b);
  fb.load_const(Value::makeInt(42));
  fb.ret();

  fb.set_current_block(exit_b);
  fb.load_const(Value::makeInt(7));
  fb.ret();

  BytecodeFunction f = fb.build();
  const auto v = validate_cfg(f.blocks, f.entry_block);
  ASSERT_TRUE(v.errors.empty()) << v.errors[0];
  EXPECT_TRUE(v.valid);
  EXPECT_EQ(f.blocks.size(), 3u);

  // Every flattened jump target must resolve within the linear stream.
  LinearFunction lin = flatten_cfg(f.blocks, f.entry_block);
  for (const Instruction& inst : lin.instructions) {
    if (inst.opcode == OpCode::JUMP_IF_FALSE || inst.opcode == OpCode::JUMP ||
        inst.opcode == OpCode::JUMP_IF_TRUE || inst.opcode == OpCode::JUMP_IF_NULL) {
      ASSERT_FALSE(inst.operands.empty());
      ASSERT_TRUE(inst.operands[0].isInt());
      const int64_t t = inst.operands[0].asInt();
      EXPECT_GE(t, 0);
      EXPECT_LT(static_cast<size_t>(t), lin.instructions.size());
    }
  }
}

TEST_F(CFGPipelineTest, LoopHelperValidates) {
  FunctionBuilder fb("loopb", 1, 0);

  uint32_t header, body, exit;
  std::tie(header, body, exit) = fb.loop(
      [&](uint32_t) {
        fb.load_var(0);
        fb.load_const(Value::makeInt(0));
        fb.gt();  // loop while param[0] > 0
      },
      [&](uint32_t) { fb.push(OpCode::PRINT); });

  fb.set_current_block(0);
  fb.jump(header, {});
  fb.set_current_block(exit);
  fb.load_const(Value::makeInt(99));
  fb.ret();

  BytecodeFunction f = fb.build();
  const auto v = validate_cfg(f.blocks, f.entry_block);
  ASSERT_TRUE(v.errors.empty()) << v.errors[0];
  EXPECT_TRUE(v.valid);

  LinearFunction lin = flatten_cfg(f.blocks, f.entry_block);
  for (const Instruction& inst : lin.instructions) {
    if (inst.opcode == OpCode::JUMP || inst.opcode == OpCode::JUMP_IF_FALSE) {
      ASSERT_FALSE(inst.operands.empty());
      const int64_t t = inst.operands[0].asInt();
      EXPECT_GE(t, 0);
      EXPECT_LT(static_cast<size_t>(t), lin.instructions.size());
    }
  }
}

TEST_F(CFGPipelineTest, OutOfRangeJumpTargetRejected) {
  std::vector<BasicBlock> blocks;
  BasicBlock b0(0);
  b0.terminator = Terminator::jumpIfFalse(9);  // only 2 blocks -> invalid
  blocks.push_back(b0);
  BasicBlock b1(1);
  b1.terminator = Terminator::jump(0);
  blocks.push_back(b1);

  const auto v = validate_cfg(blocks, 0);
  EXPECT_FALSE(v.valid);
  ASSERT_FALSE(v.errors.empty());
  EXPECT_NE(v.errors[0].find("invalid block id"), std::string::npos);
}

TEST_F(CFGPipelineTest, MultipleFallThroughBlocksRejected) {
  std::vector<BasicBlock> blocks;
  blocks.emplace_back(0);  // None, not last
  blocks.emplace_back(1);  // None, last

  const auto v = validate_cfg(blocks, 0);
  EXPECT_FALSE(v.valid);
}

TEST_F(CFGPipelineTest, TypedLocalsAndParamsRoundTrip) {
  FunctionBuilder fb("typed", 2, 3);
  fb.set_local_type(0, TYPE_HINT_INT);
  fb.set_local_type(1, TYPE_HINT_STRING);
  fb.add_upvalue(7);

  BytecodeFunction f = fb.build();
  EXPECT_EQ(f.param_count, 2u);
  EXPECT_EQ(f.local_count, 5u);  // 2 params + 3 temps
  ASSERT_EQ(f.locals.size(), 5u);
  EXPECT_EQ(f.locals[0].type_hint, TYPE_HINT_INT);
  EXPECT_EQ(f.locals[1].type_hint, TYPE_HINT_STRING);
  EXPECT_TRUE(f.locals[0].is_param);
  EXPECT_TRUE(f.locals[1].is_param);
  ASSERT_EQ(f.upvalues.size(), 1u);
  EXPECT_EQ(f.upvalues[0].index, 7u);
}

TEST_F(CFGPipelineTest, SwitchHelperBuildsDispatchChain) {
  FunctionBuilder fb("sw", 1, 0);
  fb.load_var(0);  // selector on the eval stack in entry (block 0)

  std::vector<uint64_t> cases = {1, 2, 3};
  uint32_t dispatch, def = UINT32_MAX;
  std::vector<uint32_t> targets;
  std::tie(dispatch, targets, def) = fb.switch_(0, cases, UINT32_MAX);
  (void)dispatch;

  ASSERT_EQ(targets.size(), 3u);
  // Default target placeholder must be rewired to a real block by the caller.
  std::vector<BasicBlock>& b = fb.blocks();
  bool patched = false;
  for (auto& blk : b) {
    if (blk.terminator.kind == TerminatorKind::Jump && !blk.terminator.targets.empty() &&
        blk.terminator.targets[0] == UINT32_MAX) {
      // Point the default jump at the function's real exit (block 0 was not
      // terminated here; this merely proves the dispatch chain is wired).
      blk.terminator.targets[0] = 0;
      patched = true;
    }
  }
  EXPECT_TRUE(patched);
  // Selector local was added via add_local (beyond the 2 builder temps).
  EXPECT_GE(fb.build().local_count, 1u);
}

TEST_F(CFGPipelineTest, ValidateFunctionRejectsBadLocalIndex) {
  FunctionBuilder fb("badlocal", 0, 1);
  fb.load_var(0);  // valid; slot 0 exists
  fb.store_var(3); // invalid; only slots 0 exists after +temps
  fb.create_block();
  BytecodeFunction f = fb.build();

  const auto v = validate_function(f, f.entry_block);
  EXPECT_FALSE(v.valid);
  bool caught = false;
  for (const auto& e : v.errors) {
    if (e.find("local index 3 out of range") != std::string::npos) caught = true;
  }
  EXPECT_TRUE(caught);
}

TEST_F(CFGPipelineTest, ValidateFunctionPassesValidLocalsAndGlobals) {
  FunctionBuilder fb("ok", 2, 1);
  fb.load_var(0);
  fb.load_var(1);
  fb.load_global("config");
  fb.store_global("config");
  fb.add_upvalue(0);
  fb.load_upvalue(0);
  fb.ret();
  fb.create_block();

  BytecodeFunction f = fb.build();
  const auto v = validate_function(f, f.entry_block);
  EXPECT_TRUE(v.valid) << (v.errors.empty() ? "" : v.errors[0]);
}

TEST_F(CFGPipelineTest, ValidateFunctionRejectsGlobalOutOfRange) {
  FunctionBuilder fb("badglobal", 0, 0);
  // Refer a global name table index that does not exist. The builder interns
  // names internally, so fabricate the operand by poking the instruction directly.
  fb.push(Instruction(OpCode::LOAD_GLOBAL,
                      std::vector<Value>{Value::makeStringValId(7)}));
  fb.ret();
  fb.create_block();

  BytecodeFunction f = fb.build();
  ASSERT_TRUE(f.global_names.empty());  // never interned -> table is empty

  const auto v = validate_function(f, f.entry_block);
  EXPECT_FALSE(v.valid);
}

TEST_F(CFGPipelineTest, ValidateModuleChecksAllFunctions) {
  BytecodeChunk chunk;
  {
    FunctionBuilder fb("good", 0, 0);
    fb.ret();
    fb.create_block();
    chunk.addFunction(fb.build());
  }
  {
    FunctionBuilder fb("bad", 0, 1);
    fb.load_var(5);  // out of range
    fb.create_block();
    chunk.addFunction(fb.build());
  }

  const auto v = validate_module(chunk);
  EXPECT_FALSE(v.valid);  // the bad function must fail the module
  bool caught = false;
  for (const auto& e : v.errors) {
    if (e.find("function 'bad'") != std::string::npos) caught = true;
  }
  EXPECT_TRUE(caught);
}

TEST_F(CFGPipelineTest, StandardPipelineKeepsCfgValidAfterEachPass) {
  // Feed a real CFG-backed function through the standard optimization pipeline.
  // PassManager::run_all validates (structurally + operand references) after
  // every pass; this asserts the function survives optimization as a valid CFG.
  FunctionBuilder fb("pipe", 1, 1);
  fb.load_var(0);
  fb.load_const(Value::makeInt(1));
  fb.add_int();
  fb.store_var(1);
  fb.ret();
  fb.create_block();  // trailing, unreachable fall-through

  BytecodeFunction f = fb.build();
  {
    const auto v = validate_function(f, f.entry_block);
    ASSERT_TRUE(v.valid) << (v.errors.empty() ? "" : v.errors[0]);
  }

  auto pm = create_standard_pipeline();
  const PassResult res = pm->run_all(f.blocks, f);
  EXPECT_TRUE(res.valid);  // no validation failure surfaced after any pass
}

TEST_F(CFGPipelineTest, ConstPropagationFoldsBinaryOps) {
  FunctionBuilder fb("cp1", 0, 0);
  fb.load_const(Value::makeInt(10));
  fb.load_const(Value::makeInt(5));
  fb.add_int();
  fb.ret();
  fb.create_block();

  BytecodeFunction f = fb.build();
  auto pm = create_standard_pipeline();  // includes ConstPropagation
  const PassResult res = pm->run_all(f.blocks, f);

  // Should have folded 10 + 5 -> 15
  bool found_folded = false;
  for (const auto& inst : f.blocks[0].instructions) {
    if (inst.opcode == OpCode::LOAD_CONST && inst.operands[0].isInt() &&
        inst.operands[0].asInt() == 15) {
      found_folded = true;
    }
  }
  EXPECT_TRUE(found_folded) << "Constant folding of 10 + 5 -> 15";
  EXPECT_TRUE(res.valid);
}

TEST_F(CFGPipelineTest, ConstPropagationPropagatesLocals) {
  FunctionBuilder fb("cp2", 0, 1);
  fb.load_const(Value::makeInt(42));
  fb.store_var(0);  // local 0 = 42
  fb.load_var(0);
  fb.ret();
  fb.create_block();

  BytecodeFunction f = fb.build();
  auto pm = create_standard_pipeline();
  const PassResult res = pm->run_all(f.blocks, f);

  // Should have replaced LOAD_VAR 0 with LOAD_CONST 42
  bool found_const = false;
  for (const auto& inst : f.blocks[0].instructions) {
    if (inst.opcode == OpCode::LOAD_CONST && inst.operands[0].isInt() &&
        inst.operands[0].asInt() == 42) {
      found_const = true;
    }
  }
  EXPECT_TRUE(found_const) << "Constant propagation into local load";
  EXPECT_TRUE(res.valid);
}

TEST_F(CFGPipelineTest, DceRemovesDeadStoresAndLoads) {
  FunctionBuilder fb("dce1", 0, 2);
  // dead store
  fb.load_const(Value::makeInt(10));
  fb.store_var(0);
  // dead load
  fb.load_var(1);
  fb.pop();
  // live store + load
  fb.load_const(Value::makeInt(7));
  fb.store_var(1);
  fb.load_var(1);
  fb.ret();
  fb.create_block();

  BytecodeFunction f = fb.build();
  auto pm = create_standard_pipeline();
  const PassResult res = pm->run_all(f.blocks, f);

  EXPECT_TRUE(res.valid);

  // Count remaining instructions - dead store/load should be gone
  size_t inst_count = 0;
  for (const auto& inst : f.blocks[0].instructions) {
    if (inst.opcode != OpCode::RETURN) ++inst_count;
  }
  // Should have: LOAD_CONST 7, STORE_VAR 1, LOAD_VAR 1 (3 inst) + RET
  // Dead STORE_VAR 0, LOAD_VAR 1, POP should be removed
  EXPECT_LE(inst_count, 3);
}

TEST_F(CFGPipelineTest, DceRemovesDeadIncDec) {
  FunctionBuilder fb("dce2", 0, 1);
  // dead increment
  fb.inc_local(0);
  // live
  fb.load_const(Value::makeInt(1));
  fb.store_var(0);
  fb.load_var(0);
  fb.ret();
  fb.create_block();

  BytecodeFunction f = fb.build();
  auto pm = create_standard_pipeline();
  const PassResult res = pm->run_all(f.blocks, f);

  EXPECT_TRUE(res.valid);
  // INCLOCAL of dead local should be removed
  bool has_inc = false;
  for (const auto& inst : f.blocks[0].instructions) {
    if (inst.opcode == OpCode::INCLOCAL) has_inc = true;
  }
  EXPECT_FALSE(has_inc) << "Dead INCRELOCAL should be eliminated";
}

TEST_F(CFGPipelineTest, CopyPropagationReplacesLocalCopies) {
  FunctionBuilder fb("copy1", 0, 2);
  // local 1 = local 0 (copy)
  fb.load_var(0);
  fb.store_var(1);
  // later load local 1 -> should become load local 0
  fb.load_var(1);
  fb.ret();
  fb.create_block();

  BytecodeFunction f = fb.build();
  auto pm = std::make_unique<PassManager>();
  pm->add_pass(std::make_unique<CopyPropagationPass>());
  const PassResult res = pm->run_all(f.blocks, f);

  EXPECT_TRUE(res.valid);
  // Count LOAD_VAR 0 occurrences - should be 2 (original + 1 replaced)
  int load_var_0 = 0;
  for (const auto& inst : f.blocks[0].instructions) {
    if (inst.opcode == OpCode::LOAD_VAR && inst.operands[0].isInt() &&
        inst.operands[0].asInt() == 0) {
      ++load_var_0;
    }
  }
  EXPECT_EQ(load_var_0, 2) << "Copy propagation should replace local 1 with local 0";
}

TEST_F(CFGPipelineTest, TypePropagationInfersLocalHints) {
  FunctionBuilder fb("t1", 0, 3);
  fb.load_const(Value::makeInt(42));
  fb.store_var(0);
  fb.load_const(Value::makeDouble(2.5));
  fb.store_var(1);
  fb.load_const(Value::makeBool(true));
  fb.store_var(2);
  fb.ret();
  fb.create_block();

  BytecodeFunction f = fb.build();
  PassManager pm;
  pm.add_pass(std::make_unique<TypePropagationPass>());
  const PassResult res = pm.run_all(f.blocks, f);

  EXPECT_TRUE(res.valid);
  EXPECT_EQ(f.locals[0].type_hint, TYPE_HINT_INT);
  EXPECT_EQ(f.locals[1].type_hint, TYPE_HINT_NUMBER);
  EXPECT_EQ(f.locals[2].type_hint, TYPE_HINT_BOOL);
}

TEST_F(CFGPipelineTest, TypePropagationMergesMultipleTypes) {
  FunctionBuilder fb("t2", 0, 1);
  fb.load_const(Value::makeInt(1));
  fb.store_var(0);
  fb.load_const(Value::makeDouble(3.0));
  fb.store_var(0);
  fb.ret();
  fb.create_block();

  BytecodeFunction f = fb.build();
  PassManager pm;
  pm.add_pass(std::make_unique<TypePropagationPass>());
  const PassResult res = pm.run_all(f.blocks, f);

  EXPECT_TRUE(res.valid);
  EXPECT_EQ(f.locals[0].type_hint, TYPE_HINT_INT | TYPE_HINT_NUMBER);
}

TEST_F(CFGPipelineTest, TypePropagationTreatsUnknownParamsAsAny) {
  FunctionBuilder fb("t3", 1, 2);
  // local 1 = param 0 (unknown type) -> must be conservative ALL_TYPES
  fb.load_var(0);
  fb.store_var(1);
  fb.ret();
  fb.create_block();

  BytecodeFunction f = fb.build();
  PassManager pm;
  pm.add_pass(std::make_unique<TypePropagationPass>());
  const PassResult res = pm.run_all(f.blocks, f);

  EXPECT_TRUE(res.valid);
  EXPECT_EQ(f.locals[1].type_hint, TypePropagationAnalysis::ALL_TYPES);
}

// A pass that requires an analysis no earlier pass provides.
class RequiresAnalysisPass : public BytecodePass {
public:
  PassType type() const override { return PassType::LivenessAnalysis; }
  std::string name() const override { return "RequiresAnalysis"; }
  std::vector<std::string> required_analyses() const override { return {"never_provided"}; }
  bool requires_validation() const override { return false; }
  PassResult run(std::vector<BasicBlock>&, BytecodeFunction&,
                 const BytecodeChunk&) override { return {}; }
};

TEST_F(CFGPipelineTest, PassSkippedWhenAnalysisInvalidated) {
  FunctionBuilder fb("t4", 0, 0);
  fb.load_const(Value::makeInt(1));
  fb.pop();
  fb.ret();
  fb.create_block();
  BytecodeFunction f = fb.build();

  PassManager pm;
  const auto contract_pass = std::make_unique<TypePropagationPass>();
  const std::string hinted_analysis = contract_pass->modified_state().empty()
                                          ? std::string("???")
                                          : contract_pass->modified_state().front();
  pm.add_pass(std::make_unique<TypePropagationPass>());
  pm.add_pass(std::make_unique<RequiresAnalysisPass>());
  const PassResult res = pm.run_all(f.blocks, f);

  // Run all: TypePropagation executes, the requires-analysis pass is skipped.
  bool saw_skip = false;
  for (const auto& m : res.messages) {
    if (m.find("Skipping RequiresAnalysis") != std::string::npos) saw_skip = true;
  }
  EXPECT_TRUE(saw_skip) << "PassManager should skip a pass with an unavailable analysis";
  EXPECT_EQ(hinted_analysis, Analysis::kLocals);  // sanity: contract wired
}

TEST_F(CFGPipelineTest, InliningReplacesCallSiteWithCalleeBody) {
  // A small pure callee `add1(x) = x + x` inlined into `main` at
  // `LOAD_GLOBAL add1; CALL 1`. Expects: no CALL left anywhere, callee locals
  // shifted into fresh caller slots (delta = caller local count), continuations
  // rewired so the result still flows into local 0, and a valid CFG.
  BytecodeChunk chunk;
  {
    FunctionBuilder callee("add1", 1, 0);
    callee.load_var(0);
    callee.load_var(0);
    callee.add();
    callee.ret();
    callee.create_block();
    chunk.addFunction(callee.build());
  }

  FunctionBuilder caller("main", 0, 1);
  caller.load_const(Value::makeInt(5));
  caller.load_global("add1");
  caller.call(1);
  caller.store_var(0);
  caller.ret();
  caller.create_block();

  BytecodeFunction f = caller.build();
  const size_t blocks_before = f.blocks.size();
  const size_t insts_before = [&] {
    size_t n = 0;
    for (const auto& b : f.blocks) n += b.instructions.size();
    return n;
  }();

  PassManager pm;
  pm.add_pass(std::make_unique<InliningPass>());
  const PassResult res = pm.run_all(f.blocks, f, chunk);

  EXPECT_TRUE(res.valid);
  EXPECT_TRUE(res.modified);
  EXPECT_GT(f.blocks.size(), blocks_before);
  EXPECT_GT([&] {
    size_t n = 0;
    for (const auto& b : f.blocks) n += b.instructions.size();
    return n;
  }(), insts_before);

  bool has_call = false;
  for (const auto& b : f.blocks) {
    for (const auto& inst : b.instructions) {
      if (inst.opcode == OpCode::CALL) has_call = true;
    }
  }
  EXPECT_FALSE(has_call) << "Inlined call site must not leave a CALL behind";
}

TEST_F(CFGPipelineTest, LicmHoistsInvariantStorePairOutOfLoop) {
  // Natural loop: block 0 entry jumps to header 1; header 1 holds the invariant
  // pair `LOAD_CONST 7; STORE_VAR 1` and jumps to latch 2; latch 2 jumps back
  // to header 1; block 3 (exit, ret) + trailing fall-through close the function.
  // LICM must move the pair into a new preheader right before the header (new
  // block 1), leaving the header (new block 2) with no instructions.
  FunctionBuilder fb("licm", 0, 2);
  fb.load_const(Value::makeInt(0));
  fb.store_var(0);

  uint32_t header = fb.create_block();
  uint32_t latch = fb.create_block();
  uint32_t exit = fb.create_block();

  fb.set_current_block(0);
  fb.jump(header, {});

  fb.set_current_block(header);
  fb.load_const(Value::makeInt(7));
  fb.store_var(1);
  fb.jump(latch, {});

  fb.set_current_block(latch);
  fb.jump(header, {});

  fb.set_current_block(exit);
  fb.load_const(Value::makeInt(99));
  fb.ret();
  fb.create_block();  // trailing fall-through (end of stream)

  BytecodeFunction f = fb.build();
  {
    const auto v = validate_cfg(f.blocks, f.entry_block);
    ASSERT_TRUE(v.errors.empty()) << v.errors[0];
    EXPECT_TRUE(v.valid);
  }
  EXPECT_EQ(f.blocks.size(), 5u);

  PassManager pm;
  pm.add_pass(std::make_unique<LICMPass>());
  const PassResult res = pm.run_all(f.blocks, f);

  EXPECT_TRUE(res.valid);
  EXPECT_TRUE(res.modified);

  // Block 1 must now be the preheader holding the hoisted pair.
  ASSERT_GE(f.blocks.size(), 3u);
  EXPECT_EQ(f.blocks[1].id, 1u);
  bool pair_in_preheader = false;
  for (size_t i = 0; i + 1 < f.blocks[1].instructions.size(); ++i) {
    if (f.blocks[1].instructions[i].opcode == OpCode::LOAD_CONST &&
        f.blocks[1].instructions[i].operands[0].isInt() &&
        f.blocks[1].instructions[i].operands[0].asInt() == 7 &&
        f.blocks[1].instructions[i + 1].opcode == OpCode::STORE_VAR) {
      pair_in_preheader = true;
    }
  }
  EXPECT_TRUE(pair_in_preheader) << "Invariant pair must land in the preheader";

  // The old header (block 2 after the shift) must hold no instructions.
  bool header_touches_local1 = false;
  for (const auto& inst : f.blocks[2].instructions) {
    if (inst.opcode == OpCode::LOAD_CONST || inst.opcode == OpCode::STORE_VAR) {
      header_touches_local1 = true;
    }
  }
  EXPECT_FALSE(header_touches_local1) << "Hoisted pair must be removed from header";
}

}  // namespace
}  // namespace havel::compiler
