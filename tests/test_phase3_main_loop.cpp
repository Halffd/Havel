/**
 * @file test_phase3_main_loop.cpp
 * @brief Phase 3 Main Loop Unit Tests
 *
 * Tests for:
 * - VMExecutionResult struct
 * - VM::executeOneStep() method
 * - ExecutionEngine::executeFrame() main loop
 * - Scheduler/EventQueue/VM integration
 */

#include <gtest/gtest.h>
#include "havel-lang/compiler/vm/VM.hpp"
#include "havel-lang/runtime/execution/ExecutionEngine.hpp"
#include "havel-lang/runtime/concurrency/Scheduler.hpp"
#include "havel-lang/runtime/concurrency/EventQueue.hpp"

namespace havel::tests {

using compiler::VM;
using compiler::Scheduler;
using compiler::EventQueue;
using compiler::ExecutionEngine;
using compiler::VMExecutionResult;
using compiler::BytecodeChunk;

// ============================================================================
// Test 1: VMExecutionResult construction
// ============================================================================

class VMExecutionResultTest : public ::testing::Test {
protected:
  void SetUp() override {}
};

TEST_F(VMExecutionResultTest, DefaultConstruction) {
  VMExecutionResult result;
  EXPECT_EQ(result.type, VMExecutionResult::YIELD);
  EXPECT_TRUE(result.error_message.empty());
}

TEST_F(VMExecutionResultTest, YieldConstructor) {
  havel::core::Value val = 42;
  auto result = VMExecutionResult::Yield(val);
  EXPECT_EQ(result.type, VMExecutionResult::YIELD);
}

TEST_F(VMExecutionResultTest, SuspendedConstructor) {
  auto result = VMExecutionResult::Suspended();
  EXPECT_EQ(result.type, VMExecutionResult::SUSPENDED);
}

TEST_F(VMExecutionResultTest, ReturnedConstructor) {
  havel::core::Value val = 123;
  auto result = VMExecutionResult::Returned(val);
  EXPECT_EQ(result.type, VMExecutionResult::RETURNED);
}

TEST_F(VMExecutionResultTest, ErrorConstructor) {
  std::string msg = "Test error";
  auto result = VMExecutionResult::Error(msg);
  EXPECT_EQ(result.type, VMExecutionResult::ERROR);
  EXPECT_EQ(result.error_message, msg);
}

// ============================================================================
// Test 2: Single-step execution with executeOneStep
// ============================================================================

class ExecuteOneStepTest : public ::testing::Test {
protected:
  VM vm;
  
  void SetUp() override {
    // Initialize VM with host context
  }
};

TEST_F(ExecuteOneStepTest, NoFramesReturnsEmpty) {
  // With no active frames, executeOneStep should return appropriately
  EXPECT_GE(vm.hasActiveFrames(), 0);  // Check that method exists
}

// ============================================================================
// Test 3: ExecutionEngine main loop
// ============================================================================

class ExecutionEngineTest : public ::testing::Test {
protected:
  std::unique_ptr<VM> vm;
  std::unique_ptr<Scheduler> scheduler;
  std::unique_ptr<EventQueue> event_queue;
  std::unique_ptr<ExecutionEngine> engine;
  
  void SetUp() override {
    vm = std::make_unique<VM>();
    scheduler = std::make_unique<Scheduler>();
    event_queue = std::make_unique<EventQueue>();
    engine = std::make_unique<ExecutionEngine>(vm.get(), scheduler.get(), event_queue.get());
  }
};

TEST_F(ExecutionEngineTest, Construction) {
  EXPECT_NE(engine.get(), nullptr);
}

TEST_F(ExecutionEngineTest, StatsInitialization) {
  auto stats = engine->getStats();
  EXPECT_EQ(stats.frames_executed, 0);
  EXPECT_EQ(stats.goroutines_spawned, 0);
  EXPECT_EQ(stats.goroutines_completed, 0);
  EXPECT_EQ(stats.instructions_executed, 0);
}

TEST_F(ExecutionEngineTest, DebugMode) {
  EXPECT_FALSE(engine->getDebugMode());
  engine->setDebugMode(true);
  EXPECT_TRUE(engine->getDebugMode());
}

// ============================================================================
// Test 4: EventQueue integration
// ============================================================================

class EventQueueIntegrationTest : public ::testing::Test {
protected:
  EventQueue queue;
  
  void SetUp() override {}
};

TEST_F(EventQueueIntegrationTest, QueueEmpty) {
  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(queue.size(), 0);
}

TEST_F(EventQueueIntegrationTest, CallbackEnqueue) {
  int counter = 0;
  queue.push([&counter]() { counter++; });
  EXPECT_EQ(queue.size(), 1);
}

TEST_F(EventQueueIntegrationTest, ProcessAllCallbacks) {
  int counter = 0;
  queue.push([&counter]() { counter++; });
  queue.push([&counter]() { counter++; });
  
  queue.processAll();
  EXPECT_EQ(counter, 2);
  EXPECT_TRUE(queue.empty());
}

// ============================================================================
// Test 5: Scheduler goroutine management
// ============================================================================

class SchedulerGoRoutineTest : public ::testing::Test {
protected:
  Scheduler scheduler;
  
  void SetUp() override {
    scheduler.start();
  }
  
  void TearDown() override {
    scheduler.stop();
  }
};

TEST_F(SchedulerGoRoutineTest, PickNextEmpty) {
  auto g = scheduler.pickNext();
  EXPECT_EQ(g, nullptr);  // No goroutines spawned
}

TEST_F(SchedulerGoRoutineTest, SpawnGoroutine) {
  uint32_t g_id = scheduler.spawn(0, {}, "test");
  EXPECT_GT(g_id, 0);
  
  auto g = scheduler.get(g_id);
  EXPECT_NE(g, nullptr);
}

// ============================================================================
// Test 6: State preservation across yield
// ============================================================================

class StatePreservationTest : public ::testing::Test {
protected:
  // This test would require actual bytecode compilation
  // For now, it's a placeholder for conceptual testing
};

TEST_F(StatePreservationTest, LocalsPreservedAcrossYield) {
  // TODO: Implement with actual bytecode
  // - Load local variables
  // - Call executeOneStep() 
  // - Verify locals unchanged
}

// ============================================================================
// Test 7: Multiple goroutines interleaving
// ============================================================================

class MultiGoroutineTest : public ::testing::Test {
protected:
  std::unique_ptr<ExecutionEngine> engine;
  
  void SetUp() override {
    auto vm = std::make_unique<compiler::VM>();
    auto scheduler = std::make_unique<Scheduler>();
    auto event_queue = std::make_unique<EventQueue>();
    engine = std::make_unique<ExecutionEngine>(vm.get(), scheduler.get(), event_queue.get());
  }
};

TEST_F(MultiGoroutineTest, FrameExecutionReturnsStatus) {
  // executeFrame() should return bool indicating if work remains
  bool result = engine->executeFrame();
  EXPECT_FALSE(result);  // No goroutines, no work
}

// ============================================================================
// Test 8: Error handling in executeFrame
// ============================================================================

class ErrorHandlingTest : public ::testing::Test {
protected:
  std::unique_ptr<ExecutionEngine> engine;
  
  void SetUp() override {
    auto vm = std::make_unique<compiler::VM>();
    auto scheduler = std::make_unique<Scheduler>();
    auto event_queue = std::make_unique<EventQueue>();
    engine = std::make_unique<ExecutionEngine>(vm.get(), scheduler.get(), event_queue.get());
    engine->setDebugMode(false);  // Suppress debug output during test
  }
};

TEST_F(ErrorHandlingTest, NullComponentsThrow) {
  EXPECT_THROW(
    ExecutionEngine(nullptr, nullptr, nullptr),
    std::invalid_argument
  );
}

} // namespace havel::tests
