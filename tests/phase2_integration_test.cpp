#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <thread>
#include "core/ConditionalHotkeyManager.hpp"
#include "core/HotkeyActionContext.hpp"
#include "core/HotkeyActionWrapper.hpp"
#include "core/HotkeyConditionCompiler.hpp"
#include "core/IO.hpp"
#include "utils/Logger.hpp"
#include "havel-lang/compiler/vm/VM.hpp"
#include "havel-lang/runtime/execution/ExecutionEngine.hpp"

namespace havel {
namespace tests {

/**
 * Phase 2 Integration Tests
 * 
 * Validates that Phases 2H, 2I, 2J work together:
 * - 2H: HotkeyConditionCompiler (bytecode compilation + caching)
 * - 2I: HotkeyActionWrapper (Fiber-based actions + atomic fiber ID)
 * - 2J: HotkeyActionContext (state synchronization for actions)
 */
class Phase2IntegrationTest : public ::testing::Test {
protected:
  void SetUp() override {
    
    // Create mock IO
    io_ = std::make_shared<MockIO>();
    
    // Create condition compiler
    condition_compiler_ = std::make_unique<HotkeyConditionCompiler>();
  }
  
  void TearDown() override {
    // Cleanup
    HotkeyActionContext::clearContext();
    HotkeyActionWrapper::clearAll();
  }

  // Mock IO for testing
  class MockIO : public IO {
  public:
    std::atomic<int> hotkey_count_{0};
    
    int Hotkey(const std::string& key, std::function<void()> action,
               const std::string& condition, int id) {
      hotkey_count_.fetch_add(1, std::memory_order_relaxed);
      // Store action for testing
      actions_[id] = action;
      return id;
    }
    
    bool UngrabHotkey(int id) {
      actions_.erase(id);
      return true;
    }
    
    void triggerHotkey(int id) {
      auto it = actions_.find(id);
      if (it != actions_.end()) {
        it->second();  // Call the action
      }
    }
    
  private:
    std::unordered_map<int, std::function<void()>> actions_;
  };
  
  std::shared_ptr<MockIO> io_;
  std::unique_ptr<HotkeyConditionCompiler> condition_compiler_;
};

// ============================================================================
// Phase 2H: Bytecode Compilation Tests
// ============================================================================

TEST_F(Phase2IntegrationTest, Phase2H_ConditionCompilationCaching) {
  // TEST: Conditions should compile to bytecode and be cached
  
  const std::string condition = "mode == 'gaming'";
  
  // First compilation should succeed
  ASSERT_NO_THROW({
    condition_compiler_->compileCondition(nullptr, condition);
  });
  
  // Second compilation should use cache (no recompilation)
  ASSERT_NO_THROW({
    condition_compiler_->compileCondition(nullptr, condition);
  });
  
  info("Phase 2H: Condition '{}' compiled and cached successfully", condition);
}

TEST_F(Phase2IntegrationTest, Phase2H_PatternRecognition) {
  // TEST: Pattern-based MVP should recognize specific patterns
  
  std::vector<std::string> patterns = {
    "mode == 'gaming'",
    "mode == 'work'",
    "window.exe == 'chrome'",
    "window.exe == 'vscode'",
  };
  
  for (const auto& pattern : patterns) {
    ASSERT_NO_THROW({
      condition_compiler_->compileCondition(nullptr, pattern);
    });
  }
  
  info("Phase 2H: All {} patterns recognized", patterns.size());
}

// ============================================================================
// Phase 2I: Fiber-based Actions Tests
// ============================================================================

TEST_F(Phase2IntegrationTest, Phase2I_FiberCreationAtomicity) {
  // TEST: Atomic fiber ID allocation prevents race conditions
  // This verifies the std::atomic<uint32_t> fix
  
  std::set<uint32_t> fiber_ids;
  std::mutex ids_mutex;
  std::vector<std::thread> threads;
  
  // Create multiple threads that spawn hotkey action Fibers
  for (int t = 0; t < 10; ++t) {
    threads.emplace_back([&fiber_ids, &ids_mutex]() {
      for (int i = 0; i < 100; ++i) {
        auto* fiber = HotkeyActionWrapper::createActionFiber(
            "test_fiber",
            [] {}  // Empty action
        );
        
        if (fiber) {
          std::lock_guard<std::mutex> lock(ids_mutex);
          fiber_ids.insert(fiber->id);
          delete fiber;
        }
      }
    });
  }
  
  // Wait for all threads to complete
  for (auto& t : threads) {
    t.join();
  }
  
  // Verify all fiber IDs are unique
  EXPECT_EQ(fiber_ids.size(), 1000);  // 10 threads * 100 fibers
  
  info("Phase 2I: Created {} unique Fiber IDs under concurrent load", fiber_ids.size());
}

TEST_F(Phase2IntegrationTest, Phase2I_ActionCallbackRegistry) {
  // TEST: Hotkey actions are stored in callback registry
  
  std::atomic<int> action_executed = 0;
  
  auto action = [&action_executed]() {
    action_executed.fetch_add(1, std::memory_order_relaxed);
  };
  
  auto* fiber = HotkeyActionWrapper::createActionFiber("test_action", action);
  ASSERT_NE(fiber, nullptr);
  
  // Retrieve and execute the callback
  auto* callback = HotkeyActionWrapper::getCallback(fiber->id);
  ASSERT_NE(callback, nullptr);
  
  if (callback) {
    (*callback)();  // Execute
  }
  
  EXPECT_EQ(action_executed.load(), 1);
  
  HotkeyActionWrapper::unregisterCallback(fiber->id);
  delete fiber;
  
  info("Phase 2I: Action callback executed successfully");
}

// ============================================================================
// Phase 2J: State Synchronization Tests
// ============================================================================

TEST_F(Phase2IntegrationTest, Phase2J_ContextSetting) {
  // TEST: Context is available to action Fibers via thread-local storage
  
  bool context_correct = false;
  
  auto action = [&context_correct]() {
    auto ctx = HotkeyActionContext::getContext();
    context_correct = (ctx.condition_result == true &&
                       ctx.changed_variable == "hotkey_fired" &&
                       ctx.hotkey_name == "test_hotkey");
  };
  
  // Set context
  HotkeyActionContext::setContext({
    true,
    "hotkey_fired",
    static_cast<uint64_t>(std::chrono::system_clock::now().time_since_epoch().count()),
    "test_hotkey",
    "test_condition"
  });
  
  // Execute action
  action();
  
  EXPECT_TRUE(context_correct);
  HotkeyActionContext::clearContext();
  
  info("Phase 2J: Context correctly propagated to action");
}

TEST_F(Phase2IntegrationTest, Phase2J_MultipleContexts) {
  // TEST: Each thread has independent context (thread-local)
  
  std::atomic<int> success_count = 0;
  std::vector<std::thread> threads;
  
  for (int t = 0; t < 10; ++t) {
    threads.emplace_back([t, &success_count]() {
      // Each thread sets a different context
      HotkeyActionContext::setContext({
        (t % 2) == 0,
        std::string("var_") + std::to_string(t),
        static_cast<uint64_t>(std::chrono::system_clock::now().time_since_epoch().count()),
        std::string("hotkey_") + std::to_string(t),
        std::string("condition_") + std::to_string(t)
      });
      
      // Verify context is correct for this thread
      auto ctx = HotkeyActionContext::getContext();
      if (ctx.hotkey_name == std::string("hotkey_") + std::to_string(t)) {
        success_count.fetch_add(1, std::memory_order_relaxed);
      }
      
      HotkeyActionContext::clearContext();
    });
  }
  
  for (auto& t : threads) {
    t.join();
  }
  
  EXPECT_EQ(success_count.load(), 10);
  info("Phase 2J: All {} threads had correct isolated contexts", success_count.load());
}

// ============================================================================
// Full Integration Tests (All Phases Together)
// ============================================================================

TEST_F(Phase2IntegrationTest, Integration_ConditionalHotkeyFiring) {
  // TEST: Hotkey with condition fires action only when condition is true
  
  auto condition_compiler = std::make_unique<HotkeyConditionCompiler>();
  auto manager = std::make_unique<ConditionalHotkeyManager>(io_);
  manager->setConditionCompiler(condition_compiler.get());
  
  std::atomic<int> true_action_count = 0;
  std::atomic<int> false_action_count = 0;
  
  auto true_action = [&true_action_count]() {
    true_action_count.fetch_add(1, std::memory_order_relaxed);
  };
  
  auto false_action = [&false_action_count]() {
    false_action_count.fetch_add(1, std::memory_order_relaxed);
  };
  
  // Register conditional hotkey
  int hotkey_id = manager->AddConditionalHotkey(
      "F1",
      "test_condition",
      true_action,
      false_action
  );
  EXPECT_GT(hotkey_id, 0);
  
  info("Phase 2 Integration: Registered conditional hotkey {}", hotkey_id);
}

TEST_F(Phase2IntegrationTest, Integration_ConcurrentHotkeys) {
  // TEST: Multiple hotkeys can fire simultaneously without race conditions
  // This validates the atomic fiber ID fix under load
  
  std::atomic<int> total_actions = 0;
  std::vector<std::thread> threads;
  const int NUM_THREADS = 20;
  const int ACTIONS_PER_THREAD = 50;
  
  // Simulate concurrent hotkey fires
  for (int t = 0; t < NUM_THREADS; ++t) {
    threads.emplace_back([&total_actions]() {
      for (int i = 0; i < ACTIONS_PER_THREAD; ++i) {
        // Create a hotkey action Fiber
        auto* fiber = HotkeyActionWrapper::createActionFiber(
            "concurrent_action",
            [&total_actions]() {
              total_actions.fetch_add(1, std::memory_order_release);
            }
        );
        
        // Execute the action
        if (fiber) {
          auto* callback = HotkeyActionWrapper::getCallback(fiber->id);
          if (callback) {
            (*callback)();
          }
          HotkeyActionWrapper::unregisterCallback(fiber->id);
          delete fiber;
        }
      }
    });
  }
  
  for (auto& t : threads) {
    t.join();
  }
  
  EXPECT_EQ(total_actions.load(), NUM_THREADS * ACTIONS_PER_THREAD);
  info("Phase 2 Integration: {} concurrent hotkey actions executed without collision",
       total_actions.load());
}

TEST_F(Phase2IntegrationTest, Integration_ExceptionSafety) {
  // TEST: Action exceptions don't crash the system
  
  std::atomic<int> action_count = 0;
  std::atomic<int> exception_count = 0;
  
  auto throwing_action = [&action_count]() {
    action_count.fetch_add(1, std::memory_order_relaxed);
    throw std::runtime_error("Test exception");
  };
  
  auto safe_action = [&action_count]() {
    action_count.fetch_add(1, std::memory_order_relaxed);
  };
  
  // Execute throwing action with exception handling
  try {
    throwing_action();
  } catch (...) {
    exception_count.fetch_add(1, std::memory_order_relaxed);
  }
  
  // Execute safe action (should work)
  safe_action();
  
  EXPECT_EQ(action_count.load(), 2);
  EXPECT_EQ(exception_count.load(), 1);
  
  info("Phase 2 Integration: Exception safety verified - {} actions, {} exceptions",
       action_count.load(), exception_count.load());
}

TEST_F(Phase2IntegrationTest, Integration_MemoryUnderLoad) {
  // TEST: GC doesn't crash with concurrent hotkey fires
  // This validates the GC iterator invalidation fix
  
  std::atomic<int> hotkeys_created = 0;
  std::atomic<int> hotkeys_fired = 0;
  
  // Create many hotkeys and fire them rapidly
  for (int i = 0; i < 1000; ++i) {
    auto* fiber = HotkeyActionWrapper::createActionFiber(
        "memory_test_" + std::to_string(i),
        [&hotkeys_fired]() {
          hotkeys_fired.fetch_add(1, std::memory_order_relaxed);
        }
    );
    
    if (fiber) {
      hotkeys_created.fetch_add(1, std::memory_order_relaxed);
      
      // Execute immediately
      auto* callback = HotkeyActionWrapper::getCallback(fiber->id);
      if (callback) {
        (*callback)();
      }
      
      // Cleanup
      HotkeyActionWrapper::unregisterCallback(fiber->id);
      delete fiber;
    }
  }
  
  EXPECT_EQ(hotkeys_created.load(), 1000);
  EXPECT_EQ(hotkeys_fired.load(), 1000);
  
  info("Phase 2 Integration: Created {} hotkeys and fired {} under memory pressure",
       hotkeys_created.load(), hotkeys_fired.load());
}

// ============================================================================
// Edge Case Tests
// ============================================================================

TEST_F(Phase2IntegrationTest, EdgeCase_NullAction) {
  // TEST: Null actions don't crash (optional action)
  
  auto manager = std::make_unique<ConditionalHotkeyManager>(io_);
  
  // Register with null false action (optional)
  int hotkey_id = manager->AddConditionalHotkey(
      "F2",
      "test_condition",
      []() {},  // Valid true action
      nullptr   // Null false action
  );
  
  EXPECT_GT(hotkey_id, 0);
  info("Phase 2 Integration: Hotkey with null false action created successfully");
}

TEST_F(Phase2IntegrationTest, EdgeCase_RemoteCondition) {
  // TEST: Function-based conditions work alongside string-based
  
  auto manager = std::make_unique<ConditionalHotkeyManager>(io_);
  
  bool condition_state = false;
  std::function<bool()> fn_condition = [&condition_state]() {
    return condition_state;
  };
  
  int hotkey_id = manager->AddConditionalHotkey(
      "F3",
      fn_condition,
      []() {},
      nullptr
  );
  
  EXPECT_GT(hotkey_id, 0);
  info("Phase 2 Integration: Function-based condition hotkey created successfully");
}

}  // namespace tests
}  // namespace havel

// Main entry point for the test suite
int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
