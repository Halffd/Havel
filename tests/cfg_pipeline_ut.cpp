// CFG/IR unit tests for the BytecodeIR stabilization (TODO.md Phase 1).
//
// Exercises the FunctionBuilder, CFGValidationResult/validate_cfg, and
// LinearFunction/flatten_cfg against concrete control-flow shapes (if/else,
// loops, switch) plus the typed-local/parameter metadata round-trip.
//
// One test = one concern; every test has assertions; no dependency on external
// files or scripts.

#include "BytecodeIR.hpp"

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

}  // namespace
}  // namespace havel::compiler
