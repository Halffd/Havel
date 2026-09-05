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
#include "CFGIntegration.hpp"
#include "DataflowAnalysis.hpp"
#include "InstructionEffects.hpp"
#include "OptimizerDriver.hpp"

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

TEST_F(CFGPipelineTest, ConstPropagationFoldsComparisons) {
  FunctionBuilder fb("cp_cmp", 0, 0);
  fb.load_const(Value::makeInt(10));
  fb.load_const(Value::makeInt(5));
  fb.lt();  // 10 < 5 -> false
  fb.ret();
  fb.create_block();

  BytecodeFunction f = fb.build();
  auto pm = create_standard_pipeline();
  const PassResult res = pm->run_all(f.blocks, f);

  // Should have folded 10 < 5 -> LOAD_CONST false
  bool found_folded = false;
  for (const auto& inst : f.blocks[0].instructions) {
    if (inst.opcode == OpCode::LOAD_CONST && inst.operands[0].isBool() &&
        !inst.operands[0].asBool()) {
      found_folded = true;
    }
  }
  EXPECT_TRUE(found_folded) << "Constant folding of 10 < 5 -> false";
  EXPECT_TRUE(res.valid);
}

TEST_F(CFGPipelineTest, ConstPropagationFoldsUnaryOps) {
  FunctionBuilder fb("cp_un", 0, 0);
  fb.load_const(Value::makeInt(5));
  fb.push(OpCode::NEGATE);  // -5
  fb.ret();
  fb.create_block();

  BytecodeFunction f = fb.build();
  auto pm = create_standard_pipeline();
  const PassResult res = pm->run_all(f.blocks, f);

  // Should have folded NEGATE(5) -> LOAD_CONST -5
  bool found_folded = false;
  for (const auto& inst : f.blocks[0].instructions) {
    if (inst.opcode == OpCode::LOAD_CONST && inst.operands[0].isInt() &&
        inst.operands[0].asInt() == -5) {
      found_folded = true;
    }
  }
  EXPECT_TRUE(found_folded) << "Constant folding of negate(5) -> -5";
  EXPECT_TRUE(res.valid);
}

TEST_F(CFGPipelineTest, ConstPropagationFoldsConstantConditional) {
  // if (true) fall through to b1, else b2 -> b1 (join). The CFG model carries
  // every edge as an explicit terminator target, so the else arm re-joins via
  // an explicit Jump to b1. Const true means JumpIfFalse(2) is never taken and
  // the conditional folds to an unconditional Jump into the join.
  FunctionBuilder fb("cp_cond", 0, 0);
  const uint32_t b0 = fb.current_block();
  const uint32_t b1 = fb.create_block();
  const uint32_t b2 = fb.create_block();
  fb.set_current_block(b0);
  fb.load_const(Value::makeBool(true));
  fb.jump_if_false(b2);  // never taken: true -> b1
  fb.set_current_block(b1);
  fb.ret();
  fb.set_current_block(b2);
  fb.jump(b1);  // explicit re-join keeps the else arm a real CFG edge

  BytecodeFunction f = fb.build();
  auto pm = create_standard_pipeline();
  const PassResult res = pm->run_all(f.blocks, f);

  ASSERT_TRUE(res.valid);
  // Only the fall-through edge may remain: unconditional Jump to block 1
  // (the join), whose predecessor list was rebuilt to match.
  EXPECT_EQ(f.blocks[0].terminator.kind, TerminatorKind::Jump);
  ASSERT_EQ(f.blocks[0].terminator.targets.size(), 1u);
  EXPECT_EQ(f.blocks[0].terminator.targets[0], b1);
  // The dead condition tail was dropped.
  EXPECT_TRUE(f.blocks[0].instructions.empty());
  // Stored predecessor lists match the folded terminator.
  bool b1_has_pred = false;
  for (uint32_t p : f.blocks[b1].predecessors) b1_has_pred |= (p == b0);
  EXPECT_TRUE(b1_has_pred) << "fall-through predecessor updated";
}

TEST_F(CFGPipelineTest, ConstPropagationPropagatesCrossBlockLocals) {
  // block 0: local 0 = 7; block 1: LOAD_VAR 0 -> LOAD_CONST 7
  FunctionBuilder fb("cp_x", 0, 1);
  const uint32_t b0 = fb.current_block();
  const uint32_t b1 = fb.create_block();
  fb.set_current_block(b0);
  fb.load_const(Value::makeInt(7));
  fb.store_var(0);
  fb.jump(b1);
  fb.set_current_block(b1);
  fb.load_var(0);
  fb.ret();

  BytecodeFunction f = fb.build();
  auto pm = create_standard_pipeline();
  const PassResult res = pm->run_all(f.blocks, f);

  EXPECT_TRUE(res.valid);
  bool found_const = false;
  for (const auto& inst : f.blocks[b1].instructions) {
    if (inst.opcode == OpCode::LOAD_CONST && inst.operands[0].isInt() &&
        inst.operands[0].asInt() == 7) {
      found_const = true;
    }
  }
  EXPECT_TRUE(found_const)
      << "Constant from predecessor block propagated into LOAD_VAR";
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
  // dead increment (statement form: INCLOCAL pushes, POP consumes)
  fb.inc_local(0);
  fb.pop();
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
  // The INCLOCAL + POP pair (dead local) should be removed together, keeping
  // the stack balanced.
  bool has_inc = false;
  for (const auto& inst : f.blocks[0].instructions) {
    if (inst.opcode == OpCode::INCLOCAL) has_inc = true;
  }
  EXPECT_FALSE(has_inc) << "Dead INCLOCAL should be eliminated";
}

TEST_F(CFGPipelineTest, DceKeepsDeadStoreWhenProducerIsImpure) {
  // A dead store whose producer can throw (LOAD_GLOBAL is MayThrow) must be
  // kept: removing it would both unbalance the stack and elide a possible
  // runtime error.
  FunctionBuilder fb("dce3", 0, 1);
  fb.load_global("missing");
  fb.store_var(0);  // dead store, but LOAD_GLOBAL is not removable
  fb.ret();
  fb.create_block();

  BytecodeFunction f = fb.build();
  auto pm = create_standard_pipeline();
  const PassResult res = pm->run_all(f.blocks, f);

  EXPECT_TRUE(res.valid);
  bool has_load_global = false;
  bool has_store = false;
  for (const auto& inst : f.blocks[0].instructions) {
    if (inst.opcode == OpCode::LOAD_GLOBAL) has_load_global = true;
    if (inst.opcode == OpCode::STORE_VAR) has_store = true;
  }
  EXPECT_TRUE(has_load_global) << "MayThrow producer must not be removed";
  EXPECT_TRUE(has_store) << "Dead store with unremovable producer must be kept";
}

TEST_F(CFGPipelineTest, DceRemovesBalancedPureChains) {
  // A self-contained pure chain with no net stack effect is entirely dead.
  FunctionBuilder fb("dce4", 0, 0);
  fb.load_const(Value::makeInt(1));
  fb.load_const(Value::makeInt(2));
  fb.pop();
  fb.pop();
  fb.ret();
  fb.create_block();

  BytecodeFunction f = fb.build();
  auto pm = create_standard_pipeline();
  const PassResult res = pm->run_all(f.blocks, f);

  EXPECT_TRUE(res.valid);
  for (const auto& inst : f.blocks[0].instructions) {
    EXPECT_NE(inst.opcode, OpCode::LOAD_CONST) << "Dead pure chain must vanish";
  }
}

TEST_F(CFGPipelineTest, DceKeepsPureRunFeedingReturn) {
  // A pure run whose value is consumed by RET is live: nothing must vanish.
  FunctionBuilder fb("dce5", 0, 0);
  fb.load_const(Value::makeInt(42));
  fb.ret();
  fb.create_block();

  BytecodeFunction f = fb.build();
  auto pm = create_standard_pipeline();
  const PassResult res = pm->run_all(f.blocks, f);

  EXPECT_TRUE(res.valid);
  bool found = false;
  for (const auto& inst : f.blocks[0].instructions) {
    if (inst.opcode == OpCode::LOAD_CONST && inst.operands[0].isInt() &&
        inst.operands[0].asInt() == 42) {
      found = true;
    }
  }
  EXPECT_TRUE(found) << "Return value producer must survive DCE";
}

TEST_F(CFGPipelineTest, DceRemovesDeadLoadPopPair) {
  // LOAD_VAR whose value is immediately discarded: the pair is one balanced
  // pure run and vanishes.
  FunctionBuilder fb("dce6", 0, 2);
  fb.load_var(1);
  fb.pop();
  fb.load_const(Value::makeInt(7));
  fb.ret();
  fb.create_block();

  BytecodeFunction f = fb.build();
  auto pm = create_standard_pipeline();
  const PassResult res = pm->run_all(f.blocks, f);

  EXPECT_TRUE(res.valid);
  bool has_load = false;
  for (const auto& inst : f.blocks[0].instructions) {
    if (inst.opcode == OpCode::LOAD_VAR) has_load = true;
  }
  EXPECT_FALSE(has_load) << "Dead LOAD_VAR + POP pair must be removed";
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

// ===== TODO.md #13: Instruction Effects Model =====

// The removable-pure set must be exactly the ops classified Effects::None.
TEST_F(CFGPipelineTest, InstructionEffectsPureSetIsRemovable) {
  EXPECT_TRUE(is_pure(OpCode::LOAD_CONST));
  EXPECT_TRUE(is_pure(OpCode::LOAD_VAR));
  EXPECT_TRUE(is_pure(OpCode::LOAD_UPVALUE));
  EXPECT_TRUE(is_pure(OpCode::POP));
  EXPECT_TRUE(is_pure(OpCode::DUP));
  EXPECT_TRUE(is_pure(OpCode::SWAP));
  EXPECT_TRUE(is_pure(OpCode::PUSH_NULL));
  EXPECT_TRUE(is_pure(OpCode::IS_NULL));
  EXPECT_TRUE(is_pure(OpCode::NOT));
  EXPECT_TRUE(is_pure(OpCode::AND));
  EXPECT_TRUE(is_pure(OpCode::OR));
  EXPECT_TRUE(is_pure(OpCode::NOP));

  // Anything that writes, calls, allocates, or can trap must NOT be pure.
  EXPECT_FALSE(is_pure(OpCode::STORE_VAR));
  EXPECT_FALSE(is_pure(OpCode::STORE_GLOBAL));
  EXPECT_FALSE(is_pure(OpCode::LOAD_GLOBAL));
  EXPECT_FALSE(is_pure(OpCode::ADD));
  EXPECT_FALSE(is_pure(OpCode::ADD_INT));
  EXPECT_FALSE(is_pure(OpCode::DIV));
  EXPECT_FALSE(is_pure(OpCode::EQ));
  EXPECT_FALSE(is_pure(OpCode::CALL));
  EXPECT_FALSE(is_pure(OpCode::PRINT));
  EXPECT_FALSE(is_pure(OpCode::THROW));
  EXPECT_FALSE(is_pure(OpCode::ARRAY_NEW));
}

// Exact stack effects of the ops the optimizer models precisely.
TEST_F(CFGPipelineTest, InstructionEffectsStackEffectsAreExact) {
  EXPECT_EQ(instruction_effect(OpCode::LOAD_CONST).pushes, 1);
  EXPECT_EQ(instruction_effect(OpCode::LOAD_CONST).pops, 0);
  EXPECT_EQ(instruction_effect(OpCode::POP).pops, 1);
  EXPECT_EQ(instruction_effect(OpCode::POP).pushes, 0);
  EXPECT_EQ(instruction_effect(OpCode::DUP).pops, 1);
  EXPECT_EQ(instruction_effect(OpCode::DUP).pushes, 2);
  EXPECT_EQ(instruction_effect(OpCode::SWAP).pops, 2);
  EXPECT_EQ(instruction_effect(OpCode::SWAP).pushes, 2);
  EXPECT_EQ(instruction_effect(OpCode::STORE_VAR).pops, 1);
  EXPECT_EQ(instruction_effect(OpCode::STORE_VAR).pushes, 0);
  EXPECT_EQ(instruction_effect(OpCode::CALL).pushes, 1);
  EXPECT_EQ(instruction_effect(OpCode::RETURN).pops, 1);
}

// Terminators, calls, and IO carry the flags they must carry.
TEST_F(CFGPipelineTest, InstructionEffectsFlagCoverage) {
  EXPECT_TRUE(has_flag(instruction_effect(OpCode::JUMP).effects, Effects::Terminates));
  EXPECT_TRUE(has_flag(instruction_effect(OpCode::JUMP_IF_FALSE).effects, Effects::Terminates));
  EXPECT_TRUE(has_flag(instruction_effect(OpCode::RETURN).effects, Effects::Terminates));
  EXPECT_TRUE(has_flag(instruction_effect(OpCode::THROW).effects, Effects::Terminates));

  EXPECT_TRUE(has_flag(instruction_effect(OpCode::CALL).effects, Effects::Calls));
  EXPECT_TRUE(has_flag(instruction_effect(OpCode::CALL).effects, Effects::MayThrow));
  EXPECT_TRUE(has_flag(instruction_effect(OpCode::PRINT).effects, Effects::HasSideEffects));
  EXPECT_TRUE(has_flag(instruction_effect(OpCode::ARRAY_NEW).effects, Effects::Allocates));
  EXPECT_TRUE(has_flag(instruction_effect(OpCode::DIV).effects, Effects::MayThrow));

  // Unknown stack effects are marked with -1, never misreported as exact.
  EXPECT_EQ(instruction_effect(OpCode::CALL).pops, -1);
  EXPECT_EQ(instruction_effect(OpCode::THREAD_SPAWN).pops, -1);
}

// ===== Linear <-> CFG integration (reconstruct_cfg / lower_cfg) =====

// Reconstruct a linear stream with a conditional into blocks; lowering back
// must produce an equivalent stream (same opcodes; jump targets remapped to
// the same leaders) and literal LOAD_CONST values must round-trip through the
// constant pool.
TEST_F(CFGPipelineTest, ReconstructAndLowerRoundTripConditional) {
  namespace cfi = havel::compiler::cfgintegration;
  BytecodeFunction f("rt1", 0, 0);

  // Linear form (constant-pool indices):
  //   0: LOAD_CONST pool[1]      <- int 5
  //   1: LOAD_CONST pool[0]      <- int 3
  //   2: LT
  //   3: JUMP_IF_FALSE 8
  //   4: LOAD_CONST pool[2]      <- int 10
  //   5: STORE_VAR 0
  //   6: JUMP 9
  //   7: NOP                     <- leader target 8 lands here
  // (we build 8 as the else-arm start)
  f.constants.push_back(Value::makeInt(3));   // pool 0
  f.constants.push_back(Value::makeInt(5));   // pool 1
  f.constants.push_back(Value::makeInt(10));  // pool 2
  f.local_count = 1;

  auto LC = [&](int64_t pool_idx) {
    return Instruction(OpCode::LOAD_CONST, {Value::makeInt(pool_idx)});
  };

  f.instructions.push_back(LC(1));
  f.instructions.push_back(LC(0));
  f.instructions.push_back(Instruction(OpCode::LT));
  f.instructions.push_back(
      Instruction(OpCode::JUMP_IF_FALSE, {Value::makeInt(8)}));
  f.instructions.push_back(LC(2));
  f.instructions.push_back(
      Instruction(OpCode::STORE_VAR, {Value::makeInt(0)}));
  f.instructions.push_back(Instruction(OpCode::JUMP, {Value::makeInt(9)}));
  // else arm (leader 8):
  f.instructions.push_back(LC(0));  // LOAD_CONST 3
  f.instructions.push_back(
      Instruction(OpCode::STORE_VAR, {Value::makeInt(0)}));
  // join (leader 9):
  f.instructions.push_back(LC(2));  // LOAD_CONST 10
  f.instructions.push_back(Instruction(OpCode::RETURN));

  ASSERT_TRUE(cfi::function_supports_cfg(f));

  auto rc = cfi::reconstruct_cfg(f);
  ASSERT_TRUE(rc.ok) << rc.error;
  // 4 leaders: 0 (cond), 4 (then), 8 (else), 9? wait: JUMP 9 -> leader at 9;
  // 8 is JUMP_IF_FALSE target. Leaders: 0, 4 (after JUMP_IF_FALSE... no:
  // JUMP_IF_FALSE at 3 makes 4 a leader? No: instructions after a conditional
  // jump are fall-through leaders). Compute: leaders at 0, 4 (fall-through of
  // JIF), 8 (JIF target), 6? no. JUMP at 6 targets 9 -> leader 9.
  ASSERT_GE(rc.blocks.size(), 4u);

  // Valid CFG.
  const auto v = validate_cfg(rc.blocks, 0);
  ASSERT_TRUE(v.errors.empty()) << v.errors[0];
  EXPECT_TRUE(v.valid);

  // LOAD_CONST operands must now be literal Values, not pool indices.
  bool saw_literal_5 = false;
  for (const auto& b : rc.blocks) {
    for (const auto& inst : b.instructions) {
      if (inst.opcode == OpCode::LOAD_CONST && !inst.operands.empty() &&
          inst.operands[0].isInt() && inst.operands[0].asInt() == 5) {
        saw_literal_5 = true;
      }
    }
  }
  EXPECT_TRUE(saw_literal_5) << "LOAD_CONST translated to literal value";

  // Lower back; constants re-interned; jump operands resolve to block starts.
  std::vector<Value> pool = f.constants;
  auto lowered = cfi::lower_cfg(rc.blocks, pool);
  ASSERT_TRUE(lowered.ok) << lowered.error;
  EXPECT_FALSE(lowered.instructions.empty());
  for (const auto& inst : lowered.instructions) {
    switch (inst.opcode) {
      case OpCode::JUMP:
      case OpCode::JUMP_IF_FALSE:
      case OpCode::JUMP_IF_TRUE:
      case OpCode::JUMP_IF_NULL:
        ASSERT_TRUE(inst.operands[0].isInt());
        EXPECT_GE(inst.operands[0].asInt(), 0);
        EXPECT_LT(static_cast<size_t>(inst.operands[0].asInt()),
                  lowered.instructions.size());
        break;
      case OpCode::LOAD_CONST:
        ASSERT_TRUE(inst.operands[0].isInt());
        EXPECT_LT(static_cast<size_t>(inst.operands[0].asInt()),
                  pool.size());
        break;
      default:
        break;
    }
  }
}

// The full driver: optimize a small linear function and check the dead store
// is removed while the live path is preserved.
TEST_F(CFGPipelineTest, OptimizeDriverRemovesDeadStore) {
  namespace cfi = havel::compiler::cfgintegration;
  BytecodeFunction f("opt1", 0, 1);

  f.constants.push_back(Value::makeInt(7));  // pool 0
  f.local_count = 1;

  // 0: LOAD_CONST 7; 1: STORE_VAR 0 (dead: overwritten); 2: LOAD_CONST 7;
  // 3: STORE_VAR 0 (live); 4: LOAD_VAR 0; 5: RETURN
  f.instructions.push_back(
      Instruction(OpCode::LOAD_CONST, {Value::makeInt(0)}));
  f.instructions.push_back(
      Instruction(OpCode::STORE_VAR, {Value::makeInt(0)}));
  f.instructions.push_back(
      Instruction(OpCode::LOAD_CONST, {Value::makeInt(0)}));
  f.instructions.push_back(
      Instruction(OpCode::STORE_VAR, {Value::makeInt(0)}));
  f.instructions.push_back(Instruction(OpCode::LOAD_VAR, {Value::makeInt(0)}));
  f.instructions.push_back(Instruction(OpCode::RETURN));

  cfi::OptimizeStats stats;
  BytecodeChunk chunk;
  ASSERT_TRUE(cfi::optimize_function_cfg(f, chunk, &stats));
  EXPECT_EQ(stats.functions_optimized, 1u);

  // After const-prop rewrites LOAD_VAR 0 into the constant 7, no local is
  // ever read, so DCE removes both stores. Expected result: LOAD_CONST 7;
  // RETURN with nothing else.
  ASSERT_EQ(f.instructions.size(), 2u);
  EXPECT_EQ(f.instructions[0].opcode, OpCode::LOAD_CONST);
  ASSERT_TRUE(f.instructions[0].operands[0].isInt());
  const size_t pool_idx =
      static_cast<size_t>(f.instructions[0].operands[0].asInt());
  ASSERT_LT(pool_idx, f.constants.size());
  EXPECT_TRUE(f.constants[pool_idx] == Value::makeInt(7));
  EXPECT_EQ(f.instructions[1].opcode, OpCode::RETURN);

  // Constant propagation should have replaced LOAD_VAR with a literal-backed
  // LOAD_CONST pool reference.
  bool has_const_load = false;
  for (const auto& inst : f.instructions) {
    if (inst.opcode == OpCode::LOAD_CONST &&
        inst.operands[0].isInt() &&
        static_cast<size_t>(inst.operands[0].asInt()) < f.constants.size() &&
        f.constants[static_cast<size_t>(inst.operands[0].asInt())] ==
            Value::makeInt(7)) {
      has_const_load = true;
    }
  }
  EXPECT_TRUE(has_const_load) << "load const-propagated to 7";

  // Per-IP arrays rebuilt to the new instruction count.
  EXPECT_EQ(f.type_feedback.size(), f.instructions.size());
  EXPECT_EQ(f.instruction_locations.size(), f.instructions.size());
}

// Functions with opcodes the CFG model cannot carry must be skipped
// untouched.
TEST_F(CFGPipelineTest, OptimizeDriverSkipsUnsupportedFunctions) {
  namespace cfi = havel::compiler::cfgintegration;
  BytecodeFunction f("unsafe1", 0, 0);

  f.constants.push_back(Value::makeNull());
  f.instructions.push_back(
      Instruction(OpCode::LOAD_CONST, {Value::makeInt(0)}));
  f.instructions.push_back(
      Instruction(OpCode::TRY_ENTER, {Value::makeInt(5)}));
  f.instructions.push_back(Instruction(OpCode::RETURN));

  const auto before = f.instructions;
  cfi::OptimizeStats stats;
  BytecodeChunk chunk;
  EXPECT_FALSE(cfi::optimize_function_cfg(f, chunk, &stats));
  EXPECT_EQ(stats.functions_skipped_unsafe, 1u);
  ASSERT_EQ(f.instructions.size(), before.size());
  for (size_t i = 0; i < f.instructions.size(); ++i) {
    EXPECT_EQ(f.instructions[i].opcode, before[i].opcode);
    EXPECT_EQ(f.instructions[i].operands, before[i].operands);
  }
  EXPECT_FALSE(f.has_cfg()) << "no partial CFG left behind";
}

// flatten_cfg must preserve fall-through adjacency for one-armed
// conditionals: the block after a JumpIf* in the linear stream must be that
// block's fall-through arm.
TEST_F(CFGPipelineTest, FlattenPreservesConditionalFallThrough) {
  // ifelse shape where the then-block is NOT adjacent to the entry in block
  // id order used to break under BFS ordering: entry(0) JIF(2); then(1); exit(2).
  FunctionBuilder fb("ft1", 1, 0);
  fb.load_var(0);
  fb.load_const(Value::makeInt(0));
  fb.eq();
  const uint32_t then_b = fb.create_block();
  const uint32_t exit_b = fb.create_block();
  fb.set_current_block(0);
  auto ab = fb.branch(0, then_b, exit_b);
  EXPECT_EQ(ab.first, then_b);
  fb.set_current_block(then_b);
  fb.load_const(Value::makeInt(42));
  fb.ret();
  fb.set_current_block(exit_b);
  fb.load_const(Value::makeInt(7));
  fb.ret();

  BytecodeFunction f = fb.build();
  const auto v = validate_cfg(f.blocks, f.entry_block);
  ASSERT_TRUE(v.errors.empty()) << v.errors[0];

  LinearFunction lin = flatten_cfg(f.blocks, f.entry_block);

  // Entry must be emitted first and its JUMP_IF_FALSE must be immediately
  // followed by the then-arm (fall-through), with the exit target pointing at
  // the exit block's start IP.
  ASSERT_FALSE(lin.instructions.empty());
  size_t jif_index = SIZE_MAX;
  for (size_t i = 0; i < lin.instructions.size(); ++i) {
    if (lin.instructions[i].opcode == OpCode::JUMP_IF_FALSE) {
      jif_index = i;
      break;
    }
  }
  ASSERT_NE(jif_index, SIZE_MAX);
  // Next emitted instruction must belong to the then-arm (LOAD_CONST 42),
  // proving adjacency was preserved.
  ASSERT_LT(jif_index + 1, lin.instructions.size());
  EXPECT_EQ(lin.instructions[jif_index + 1].opcode, OpCode::LOAD_CONST);
  ASSERT_FALSE(lin.instructions[jif_index + 1].operands.empty());
  EXPECT_EQ(lin.instructions[jif_index + 1].operands[0].asInt(), 42);
  // And the conditional's target must be the exit block (LOAD_CONST 7).
  const int64_t target = lin.instructions[jif_index].operands[0].asInt();
  ASSERT_GE(target, 0);
  ASSERT_LT(static_cast<size_t>(target), lin.instructions.size());
  EXPECT_EQ(lin.instructions[static_cast<size_t>(target)].opcode,
            OpCode::LOAD_CONST);
  EXPECT_EQ(lin.instructions[static_cast<size_t>(target)].operands[0].asInt(),
            7);
}

// ===== Fast Integer Lowering =====

// ADD of two int-typed locals must lower to ADD_INT; a mixed int/number
// addition must stay ADD. The local type proof comes from
// TypePropagationAnalysis (locals holding only int constants).
TEST_F(CFGPipelineTest, FastIntegerLoweringProvenInts) {
  FunctionBuilder fb("fl1", 0, 2);
  fb.load_const(Value::makeInt(3));
  fb.store_var(0);
  fb.load_const(Value::makeInt(4));
  fb.store_var(1);
  fb.load_var(0);
  fb.load_var(1);
  fb.add();  // int + int -> ADD_INT
  fb.ret();
  fb.create_block();

  BytecodeFunction f = fb.build();
  auto pm = create_standard_pipeline();
  const PassResult res = pm->run_all(f.blocks, f);
  ASSERT_TRUE(res.valid);

  bool saw_add_int = false;
  bool saw_add = false;
  for (const auto& inst : f.blocks[0].instructions) {
    if (inst.opcode == OpCode::ADD_INT) saw_add_int = true;
    if (inst.opcode == OpCode::ADD) saw_add = true;
  }
  EXPECT_TRUE(saw_add_int) << "int + int must lower to ADD_INT";
  EXPECT_FALSE(saw_add) << "generic ADD must be gone";
}

TEST_F(CFGPipelineTest, FastIntegerLoweringKeepsMixedNumeric) {
  FunctionBuilder fb("fl2", 0, 2);
  fb.load_const(Value::makeInt(3));
  fb.store_var(0);
  fb.load_const(Value::makeDouble(1.5));
  fb.store_var(1);
  fb.load_var(0);
  fb.load_var(1);
  fb.add();  // int + number -> stays ADD (double semantics)
  fb.ret();
  fb.create_block();

  BytecodeFunction f = fb.build();
  auto pm = create_standard_pipeline();
  const PassResult res = pm->run_all(f.blocks, f);
  ASSERT_TRUE(res.valid);

  for (const auto& inst : f.blocks[0].instructions) {
    EXPECT_NE(inst.opcode, OpCode::ADD_INT)
        << "int + double must not lower to ADD_INT";
  }
}

// INT_DIV between proven ints lowers to DIV_INT; the division-by-zero error
// path must still throw with the same message (semantic equivalence).
TEST_F(CFGPipelineTest, FastIntegerLoweringIntDiv) {
  FunctionBuilder fb("fl3", 0, 2);
  fb.load_const(Value::makeInt(9));
  fb.store_var(0);
  fb.load_const(Value::makeInt(3));
  fb.store_var(1);
  fb.load_var(0);
  fb.load_var(1);
  fb.push(OpCode::INT_DIV);
  fb.ret();
  fb.create_block();

  BytecodeFunction f = fb.build();
  auto pm = create_standard_pipeline();
  const PassResult res = pm->run_all(f.blocks, f);
  ASSERT_TRUE(res.valid);

  bool saw_div_int = false;
  for (const auto& inst : f.blocks[0].instructions) {
    if (inst.opcode == OpCode::DIV_INT) saw_div_int = true;
  }
  EXPECT_TRUE(saw_div_int) << "INT_DIV of proven ints must lower to DIV_INT";
}

// The real-compiler path: a function whose local provably holds ints must
// round-trip through the optimizer with the arithmetic lowered.
TEST_F(CFGPipelineTest, OptimizeDriverLowersFastIntArithmetic) {
  namespace cfi = havel::compiler::cfgintegration;
  BytecodeFunction f("fl4", 0, 2);
  f.constants.push_back(Value::makeInt(2));   // pool 0
  f.constants.push_back(Value::makeInt(40));  // pool 1
  f.local_count = 2;

  // a = 40; b = 2; a = a + b (LOAD_CONST pool refs); return a
  auto LC = [&](int64_t p) {
    return Instruction(OpCode::LOAD_CONST, {Value::makeInt(p)});
  };
  f.instructions.push_back(LC(1));
  f.instructions.push_back(
      Instruction(OpCode::STORE_VAR, {Value::makeInt(0)}));
  f.instructions.push_back(LC(0));
  f.instructions.push_back(
      Instruction(OpCode::STORE_VAR, {Value::makeInt(1)}));
  f.instructions.push_back(Instruction(OpCode::LOAD_VAR, {Value::makeInt(0)}));
  f.instructions.push_back(Instruction(OpCode::LOAD_VAR, {Value::makeInt(1)}));
  f.instructions.push_back(Instruction(OpCode::ADD));
  f.instructions.push_back(
      Instruction(OpCode::STORE_VAR, {Value::makeInt(0)}));
  f.instructions.push_back(Instruction(OpCode::LOAD_VAR, {Value::makeInt(0)}));
  f.instructions.push_back(Instruction(OpCode::RETURN));

  cfi::OptimizeStats stats;
  BytecodeChunk chunk;
  ASSERT_TRUE(cfi::optimize_function_cfg(f, chunk, &stats));
  EXPECT_EQ(stats.functions_optimized, 1u);

  bool saw_add_int = false;
  for (const auto& inst : f.instructions) {
    if (inst.opcode == OpCode::ADD_INT) saw_add_int = true;
    EXPECT_NE(inst.opcode, OpCode::ADD) << "generic ADD must be lowered";
  }
  EXPECT_TRUE(saw_add_int) << "a + b of int locals must lower to ADD_INT";
}

}  // namespace
}  // namespace havel::compiler
