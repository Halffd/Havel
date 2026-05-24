#include "havel-lang/runtime/concurrency/Scheduler.hpp"
#include "havel-lang/runtime/concurrency/Fiber.hpp"
#include "havel-lang/compiler/core/BytecodeIR.hpp"
#include "havel-lang/compiler/vm/VM.hpp"
#include "havel-lang/runtime/HostContext.hpp"
#include <cassert>
#include <iostream>

using namespace havel::compiler;

namespace havel::test {

#define CHECK(cond, msg) do { \
    if (!(cond)) { std::cerr << "  FAIL: " << msg << "\n"; std::abort(); } \
} while(0)

#define CHECK_EQ(a, b, msg) do { \
    if ((a) != (b)) { \
        std::cerr << "  FAIL: " << msg << " (got " << a << ", expected " << b << ")\n"; \
        std::abort(); \
    } \
} while(0)

static void test_pickNext_returns_null_on_empty() {
    auto& sched = Scheduler::instance();
    sched.start();

    auto* g = sched.pickNext();
    CHECK(g == nullptr, "expected nullptr on empty scheduler");
}

static void test_spawn_creates_goroutine() {
    auto& sched = Scheduler::instance();

    uint32_t gid = sched.spawn(42, {}, 0, "worker", FiberPriority::NORMAL);
    auto* g = sched.get(gid);

    CHECK(g != nullptr, "get() returned null after spawn");
    CHECK_EQ(g->id, gid, "id mismatch");
    CHECK(g->name == "worker", "name mismatch");
    CHECK(g->state == Scheduler::GoroutineState::Created, "expected Created state");
    CHECK(g->priority == FiberPriority::NORMAL, "expected NORMAL priority");
    CHECK_EQ(g->function_id, 42u, "function_id mismatch");

    CHECK(sched.hasRunnableFibers(), "should have runnable after spawn");

    auto* picked = sched.pickNext();
    CHECK(picked != nullptr, "pickNext returned null after spawn");
    CHECK_EQ(picked->id, gid, "pickNext returned wrong goroutine");
    CHECK(picked->state == Scheduler::GoroutineState::Running, "should be Running after pick");

    picked->state = Scheduler::GoroutineState::Done;
    sched.clearCurrent();

    CHECK(!sched.hasRunnableFibers(), "queues should be empty after drain");
}

static void test_spawnHotkey() {
    auto& sched = Scheduler::instance();

    uint32_t gid = sched.spawnHotkey(99, {}, 0, "hotkey_task");
    auto* g = sched.get(gid);

    CHECK(g != nullptr, "get() returned null after spawnHotkey");
    CHECK(g->priority == FiberPriority::HOTKEY, "expected HOTKEY priority");
    CHECK(g->state == Scheduler::GoroutineState::Created, "expected Created state");

    CHECK(sched.hasRunnableFibers(), "should have runnable after hotkey spawn");

    auto* picked = sched.pickNext();
    CHECK(picked != nullptr, "pickNext returned null");
    CHECK_EQ(picked->id, gid, "wrong goroutine");
    picked->state = Scheduler::GoroutineState::Done;
    sched.clearCurrent();
}

static void test_pickNext_priority_order() {
    auto& sched = Scheduler::instance();

    uint32_t bg = sched.spawn(0, {}, 0, "bg", FiberPriority::BACKGROUND);
    uint32_t normal = sched.spawn(0, {}, 0, "mid");
    uint32_t hotkey = sched.spawn(0, {}, 0, "hotkey", FiberPriority::HOTKEY);

    auto* g = sched.pickNext();
    CHECK_EQ(g->id, hotkey, "first should be HOTKEY");
    g->state = Scheduler::GoroutineState::Done;
    sched.clearCurrent();

    g = sched.pickNext();
    CHECK_EQ(g->id, normal, "second should be NORMAL");
    g->state = Scheduler::GoroutineState::Done;
    sched.clearCurrent();

    g = sched.pickNext();
    CHECK_EQ(g->id, bg, "third should be BACKGROUND");
    g->state = Scheduler::GoroutineState::Done;
    sched.clearCurrent();

    g = sched.pickNext();
    CHECK(g == nullptr, "should be empty after draining all");
}

static void test_pickNext_skips_done() {
    auto& sched = Scheduler::instance();

    uint32_t gid = sched.spawn(0, {}, 0, "doomed");
    auto* g = sched.get(gid);
    g->state = Scheduler::GoroutineState::Done;

    auto* picked = sched.pickNext();
    CHECK(picked == nullptr, "pickNext should skip Done goroutine");
}

static void test_suspend_and_unpark() {
    auto& sched = Scheduler::instance();

    uint32_t gid = sched.spawn(0, {}, 0, "lazy");
    auto* g = sched.pickNext();
    CHECK(g != nullptr, "pickNext returned null");
    CHECK(g->state == Scheduler::GoroutineState::Running, "should be Running");

    sched.clearCurrent();

    sched.suspend(g, Scheduler::SuspensionReason::SleepWait);
    CHECK(g->state == Scheduler::GoroutineState::Suspended, "should be Suspended");
    CHECK(g->suspension_reason == Scheduler::SuspensionReason::SleepWait, "wrong reason");

    auto* empty = sched.pickNext();
    CHECK(empty == nullptr, "pickNext should return null while goroutine suspended");

    sched.unpark(g);
    CHECK(g->state == Scheduler::GoroutineState::Runnable, "should be Runnable after unpark");
    CHECK(g->suspension_reason == Scheduler::SuspensionReason::None, "reason should be None");

    auto* re_picked = sched.pickNext();
    CHECK(re_picked != nullptr, "should be able to repick after unpark");
    CHECK_EQ(re_picked->id, gid, "wrong goroutine after unpark");

    re_picked->state = Scheduler::GoroutineState::Done;
    sched.clearCurrent();
}

static void test_yield_returns_to_queue() {
    auto& sched = Scheduler::instance();

    uint32_t gid = sched.spawn(0, {}, 0, "yielder");
    auto* g = sched.pickNext();
    CHECK(g != nullptr, "pickNext returned null");
    CHECK(g->state == Scheduler::GoroutineState::Running, "should be Running after first pick");

    sched.yield(g);
    CHECK(g->state == Scheduler::GoroutineState::Runnable, "should be Runnable after yield");

    auto* g2 = sched.pickNext();
    CHECK(g2 != nullptr, "should re-pick after yield");
    CHECK_EQ(g2->id, gid, "should be same goroutine");

    g2->state = Scheduler::GoroutineState::Done;
    sched.clearCurrent();
}

static void test_spawn_background_priority() {
    auto& sched = Scheduler::instance();

    uint32_t normal = sched.spawn(0, {}, 0, "normal");
    uint32_t bg = sched.spawn(0, {}, 0, "bg", FiberPriority::BACKGROUND);

    CHECK(sched.hasRunnableFibers(), "should have fibers");

    auto* g = sched.pickNext();
    CHECK_EQ(g->id, normal, "normal should be picked before background");
    g->state = Scheduler::GoroutineState::Done;
    sched.clearCurrent();

    g = sched.pickNext();
    CHECK_EQ(g->id, bg, "background should be picked second");
    g->state = Scheduler::GoroutineState::Done;
    sched.clearCurrent();
}

static void test_goroutineCount() {
    auto& sched = Scheduler::instance();

    size_t before = sched.goroutineCount();

    uint32_t gid = sched.spawn(0, {}, 0, "countable");
    CHECK_EQ(sched.goroutineCount(), before + 1, "count should increment");

    auto* g = sched.pickNext();
    CHECK_EQ(sched.goroutineCount(), before + 1, "count unchanged when Running");

    g->state = Scheduler::GoroutineState::Done;
    sched.clearCurrent();
    CHECK_EQ(sched.goroutineCount(), before + 1, "goroutines kept in map even when Done");
}

static void test_hotkey_prepend_not_append() {
  auto& sched = Scheduler::instance();

  uint32_t normal = sched.spawn(0, {}, 0, "normal");
  uint32_t hotkey = sched.spawn(0, {}, 0, "hotkey", FiberPriority::HOTKEY);

  auto* g = sched.pickNext();
  CHECK_EQ(g->id, hotkey, "hotkey spawned after normal should be picked FIRST");
  g->state = Scheduler::GoroutineState::Done;
  sched.clearCurrent();

  g = sched.pickNext();
  CHECK_EQ(g->id, normal, "normal should be picked after hotkey");
  g->state = Scheduler::GoroutineState::Done;
  sched.clearCurrent();
}

static void test_multiple_hotkeys_prepend_order() {
  auto& sched = Scheduler::instance();

  uint32_t normal = sched.spawn(0, {}, 0, "normal_first");
  uint32_t hk1 = sched.spawnHotkey(0, {}, 0, "hk1");
  uint32_t hk2 = sched.spawnHotkey(0, {}, 0, "hk2");

  auto* g = sched.pickNext();
  CHECK_EQ(g->id, hk2, "last spawned hotkey (latest push_front) should be picked first");
  g->state = Scheduler::GoroutineState::Done;
  sched.clearCurrent();

  g = sched.pickNext();
  CHECK_EQ(g->id, hk1, "first spawned hotkey should be picked second");
  g->state = Scheduler::GoroutineState::Done;
  sched.clearCurrent();

  g = sched.pickNext();
  CHECK_EQ(g->id, normal, "normal picked after all hotkeys");
  g->state = Scheduler::GoroutineState::Done;
  sched.clearCurrent();
}

static void test_burst_hotkeys_all_preempt_normal() {
  auto& sched = Scheduler::instance();

  uint32_t n1 = sched.spawn(0, {}, 0, "n1");
  uint32_t n2 = sched.spawn(0, {}, 0, "n2");
  uint32_t h1 = sched.spawnHotkey(0, {}, 0, "h1");
  uint32_t h2 = sched.spawnHotkey(0, {}, 0, "h2");
  uint32_t h3 = sched.spawnHotkey(0, {}, 0, "h3");

  std::vector<uint32_t> order;
  for (int i = 0; i < 5; i++) {
    auto* g = sched.pickNext();
    CHECK(g != nullptr, "should have goroutine");
    order.push_back(g->id);
    g->state = Scheduler::GoroutineState::Done;
    sched.clearCurrent();
  }

  CHECK(order[0] == h3, "last spawned hotkey picked first (LIFO)");
  CHECK(order[1] == h2, "middle hotkey picked second");
  CHECK(order[2] == h1, "first spawned hotkey picked third");
  CHECK(order[3] == n1, "fourth must be normal");
  CHECK(order[4] == n2, "fifth must be normal");
}

static void test_starvation_background_never_runs_with_normal_pending() {
  auto& sched = Scheduler::instance();

  uint32_t bg = sched.spawn(0, {}, 0, "bg", FiberPriority::BACKGROUND);
  uint32_t normal = sched.spawn(0, {}, 0, "normal1");
  CHECK(sched.hasRunnableFibers(), "should have fibers");

  auto* g1 = sched.pickNext();
  CHECK_EQ(g1->id, normal, "normal picked over background");
  sched.yield(g1);

  uint32_t normal2 = sched.spawn(0, {}, 0, "normal2");
  auto* g2 = sched.pickNext();
  CHECK_EQ(g2->id, normal, "yielded normal still picked before background");
  sched.yield(g2);

  auto* g3 = sched.pickNext();
  CHECK_EQ(g3->id, normal2, "new normal picked before background");
  g3->state = Scheduler::GoroutineState::Done;
  sched.clearCurrent();

  g1->state = Scheduler::GoroutineState::Done;
  g2->state = Scheduler::GoroutineState::Done;

  auto* g4 = sched.pickNext();
  CHECK_EQ(g4->id, bg, "background finally runs when no normal left");
  g4->state = Scheduler::GoroutineState::Done;
  sched.clearCurrent();
}

static void test_wakeSleepingGoroutines_past_time() {
  auto& sched = Scheduler::instance();

  uint32_t gid = sched.spawn(0, {}, 0, "sleeper");
  auto* g = sched.pickNext();
  CHECK(g != nullptr, "pickNext returned null");
  sched.clearCurrent();

  g->resume_at_time = std::chrono::steady_clock::now() - std::chrono::milliseconds(1);
  sched.suspend(g, Scheduler::SuspensionReason::SleepWait);
  CHECK(g->state == Scheduler::GoroutineState::Suspended, "should be Suspended");

  size_t woken = sched.wakeSleepingGoroutines();
  CHECK_EQ(woken, 1u, "should wake 1 goroutine");
  CHECK(g->state == Scheduler::GoroutineState::Runnable, "should be Runnable after wake");

  auto* re_picked = sched.pickNext();
  CHECK(re_picked != nullptr, "should be pickable after wake");
  CHECK_EQ(re_picked->id, gid, "wrong goroutine");
  re_picked->state = Scheduler::GoroutineState::Done;
  sched.clearCurrent();
}

static void test_wakeSleepingGoroutines_future_time_not_woken() {
  auto& sched = Scheduler::instance();

  uint32_t gid = sched.spawn(0, {}, 0, "deep_sleeper");
  auto* g = sched.pickNext();
  CHECK(g != nullptr, "pickNext returned null");
  sched.clearCurrent();

  g->resume_at_time = std::chrono::steady_clock::now() + std::chrono::hours(1);
  sched.suspend(g, Scheduler::SuspensionReason::SleepWait);

  size_t woken = sched.wakeSleepingGoroutines();
  CHECK_EQ(woken, 0u, "should not wake future goroutine");
  CHECK(g->state == Scheduler::GoroutineState::Suspended, "should stay Suspended");

  auto* empty = sched.pickNext();
  CHECK(empty == nullptr, "should not be pickable while suspended");

  g->resume_at_time = std::chrono::steady_clock::now() - std::chrono::milliseconds(1);
  size_t woken2 = sched.wakeSleepingGoroutines();
  CHECK_EQ(woken2, 1u, "should wake after time passes");
  CHECK(g->state == Scheduler::GoroutineState::Runnable, "should be Runnable now");

  auto* picked = sched.pickNext();
  CHECK(picked != nullptr, "should be pickable now");
  picked->state = Scheduler::GoroutineState::Done;
  sched.clearCurrent();
}

static void test_wakeSleepingGoroutines_only_sleepwait() {
  auto& sched = Scheduler::instance();

  uint32_t g1 = sched.spawn(0, {}, 0, "channel_waiter");
  uint32_t g2 = sched.spawn(0, {}, 0, "sleeper");

  auto* go1 = sched.pickNext();
  CHECK(go1 != nullptr, "pick1");
  sched.clearCurrent();
  go1->resume_at_time = std::chrono::steady_clock::now() - std::chrono::milliseconds(1);
  sched.suspend(go1, Scheduler::SuspensionReason::ChannelWait);

  auto* go2 = sched.pickNext();
  CHECK(go2 != nullptr, "pick2");
  sched.clearCurrent();
  go2->resume_at_time = std::chrono::steady_clock::now() - std::chrono::milliseconds(1);
  sched.suspend(go2, Scheduler::SuspensionReason::SleepWait);

  size_t woken = sched.wakeSleepingGoroutines();
  CHECK_EQ(woken, 1u, "only SleepWait goroutine should be woken");

  CHECK(go1->state == Scheduler::GoroutineState::Suspended, "ChannelWait should stay Suspended");
  CHECK(go2->state == Scheduler::GoroutineState::Runnable, "SleepWait should be Runnable");

  auto* picked = sched.pickNext();
  CHECK(picked != nullptr, "sleeper should be pickable");
  CHECK_EQ(picked->id, g2, "should pick the sleeper");
  picked->state = Scheduler::GoroutineState::Done;
  sched.clearCurrent();

  go1->state = Scheduler::GoroutineState::Done;
}

static void test_wakeSleepingGoroutines_multiple_sleepers() {
  auto& sched = Scheduler::instance();

  std::vector<uint32_t> gids;
  for (int i = 0; i < 3; i++) {
    gids.push_back(sched.spawn(0, {}, 0, "s" + std::to_string(i)));
  }

  for (auto gid : gids) {
    auto* g = sched.pickNext();
    CHECK(g != nullptr, "pick");
    sched.clearCurrent();
    g->resume_at_time = std::chrono::steady_clock::now() - std::chrono::milliseconds(1);
    sched.suspend(g, Scheduler::SuspensionReason::SleepWait);
  }

  size_t woken = sched.wakeSleepingGoroutines();
  CHECK_EQ(woken, 3u, "all 3 sleepers should wake");

  CHECK_EQ(sched.suspendedCount(), 0u, "no suspended left");

  for (int i = 0; i < 3; i++) {
    auto* g = sched.pickNext();
    CHECK(g != nullptr, "should pick woken goroutine");
    g->state = Scheduler::GoroutineState::Done;
    sched.clearCurrent();
  }
}

static void test_drainDeferredCallbacks_executes_all() {
  auto& sched = Scheduler::instance();

  int counter = 0;
  sched.deferToVM([&counter]() { counter += 10; });
  sched.deferToVM([&counter]() { counter += 20; });
  sched.deferToVM([&counter]() { counter += 30; });

  size_t drained = sched.drainDeferredCallbacks();
  CHECK_EQ(drained, 3u, "should drain 3 callbacks");
  CHECK_EQ(counter, 60, "all callbacks should have executed");

  size_t drained2 = sched.drainDeferredCallbacks();
  CHECK_EQ(drained2, 0u, "second drain should be empty");
}

static void test_drainDeferredCallbacks_exception_handling() {
  auto& sched = Scheduler::instance();

  int counter = 0;
  sched.deferToVM([&counter]() { counter += 1; });
  sched.deferToVM([]() { throw std::runtime_error("test error"); });
  sched.deferToVM([&counter]() { counter += 2; });

  size_t drained = sched.drainDeferredCallbacks();
  CHECK_EQ(drained, 3u, "should drain all 3 even if one throws");
  CHECK_EQ(counter, 3, "callbacks before and after exception should run");
}

static void test_drainDeferredCallbacks_spawn_integration() {
  auto& sched = Scheduler::instance();

  sched.deferToVM([&sched]() {
    sched.spawn(0, {}, 0, "deferred_spawn");
  });

  CHECK(!sched.hasRunnableFibers(), "nothing runnable before drain");

  sched.drainDeferredCallbacks();

  CHECK(sched.hasRunnableFibers(), "deferred spawn should make fibers runnable");

  auto* g = sched.pickNext();
  CHECK(g != nullptr, "should pick deferred-spawned goroutine");
  CHECK(g->name == "deferred_spawn", "wrong goroutine name");
  g->state = Scheduler::GoroutineState::Done;
  sched.clearCurrent();
}

static void test_stop_marks_all_done() {
  auto& sched = Scheduler::instance();

  uint32_t g1 = sched.spawn(0, {}, 0, "active1");
  uint32_t g2 = sched.spawn(0, {}, 0, "active2");
  uint32_t g3 = sched.spawnHotkey(0, {}, 0, "hotkey_active");

  sched.stop();

  auto* go1 = sched.get(g1);
  auto* go2 = sched.get(g2);
  auto* go3 = sched.get(g3);
  CHECK(go1->state == Scheduler::GoroutineState::Done, "g1 should be Done after stop");
  CHECK(go2->state == Scheduler::GoroutineState::Done, "g2 should be Done after stop");
  CHECK(go3->state == Scheduler::GoroutineState::Done, "g3 should be Done after stop");

  CHECK(!sched.hasRunnableFibers(), "no runnable fibers after stop");
  CHECK(!sched.isRunning(), "scheduler should not be running after stop");

  sched.start();
}

static void test_unpark_ignores_non_suspended() {
  auto& sched = Scheduler::instance();

  uint32_t gid = sched.spawn(0, {}, 0, "running_guy");
  auto* g = sched.pickNext();
  CHECK(g != nullptr, "pick");
  CHECK(g->state == Scheduler::GoroutineState::Running, "should be Running");

  sched.unpark(g);
  CHECK(g->state == Scheduler::GoroutineState::Running, "unpark should not change Running state");

  g->state = Scheduler::GoroutineState::Done;
  sched.clearCurrent();

  auto* g2 = sched.get(gid);
  sched.unpark(g2);
  CHECK(g2->state == Scheduler::GoroutineState::Done, "unpark should not change Done state");
}

static void test_yield_on_suspended_makes_runnable() {
  auto& sched = Scheduler::instance();

  uint32_t gid = sched.spawn(0, {}, 0, "suspended_yielder");
  auto* g = sched.pickNext();
  CHECK(g != nullptr, "pick");
  sched.clearCurrent();

  sched.suspend(g, Scheduler::SuspensionReason::ChannelWait);
  CHECK(g->state == Scheduler::GoroutineState::Suspended, "should be Suspended");

  sched.yield(g);
  CHECK(g->state == Scheduler::GoroutineState::Runnable, "yield on suspended goroutine makes it Runnable (VM bug if this happens)");

  auto* picked = sched.pickNext();
  CHECK(picked != nullptr, "yielded-suspended goroutine should be pickable");
  CHECK_EQ(picked->id, gid, "wrong goroutine");
  picked->state = Scheduler::GoroutineState::Done;
  sched.clearCurrent();
}

static void test_suspendedCount() {
  auto& sched = Scheduler::instance();

  CHECK_EQ(sched.suspendedCount(), 0u, "initially no suspended");

  uint32_t g1 = sched.spawn(0, {}, 0, "s1");
  uint32_t g2 = sched.spawn(0, {}, 0, "s2");

  auto* go1 = sched.pickNext();
  sched.clearCurrent();
  sched.suspend(go1, Scheduler::SuspensionReason::SleepWait);

  auto* go2 = sched.pickNext();
  sched.clearCurrent();
  sched.suspend(go2, Scheduler::SuspensionReason::ChannelWait);

  CHECK_EQ(sched.suspendedCount(), 2u, "two suspended");

  sched.unpark(go1);
  CHECK_EQ(sched.suspendedCount(), 1u, "one after unpark");

  sched.unpark(go2);
  CHECK_EQ(sched.suspendedCount(), 0u, "none after unpark all");

  go1->state = Scheduler::GoroutineState::Done;
  go2->state = Scheduler::GoroutineState::Done;
}

static void test_runnableCount_accuracy() {
  auto& sched = Scheduler::instance();

  uint32_t g1 = sched.spawn(0, {}, 0, "r1");
  uint32_t g2 = sched.spawn(0, {}, 0, "r2", FiberPriority::BACKGROUND);

  size_t count = sched.runnableCount();
  CHECK(count >= 2, "should have at least 2 runnable");

  auto* go1 = sched.pickNext();
  sched.clearCurrent();
  sched.suspend(go1, Scheduler::SuspensionReason::SleepWait);

  size_t after = sched.runnableCount();
  CHECK_EQ(after, count - 1, "runnable count should decrease by 1 after suspend");

  auto* go2 = sched.pickNext();
  CHECK(go2 != nullptr, "pick bg");
  go2->state = Scheduler::GoroutineState::Done;
  sched.clearCurrent();

  go1->state = Scheduler::GoroutineState::Done;
}

// ============================================================
// Fiber state save/load round-trip tests
// ============================================================

static void test_fiber_callframe_saves_chunk_ptr() {
  BytecodeChunk chunk;
  chunk.addFunction(BytecodeFunction("first", 0, 2));
  chunk.addFunction(BytecodeFunction("second", 1, 3));

  Fiber fib(1, 1, 0, "test-fiber");
  fib.pushCall(1, 0, &chunk);

  assert(!fib.call_stack.empty());
  assert(fib.call_stack.back().chunk_ptr == &chunk);
  assert(fib.call_stack.back().function_id == 1);
}

static void test_fiber_callframe_default_chunk_ptr_null() {
  Fiber fib(1, 0);
  assert(!fib.call_stack.empty());
  assert(fib.call_stack.back().chunk_ptr == nullptr);
  assert(fib.call_stack.back().function_id == 0);
}

static void test_fiber_popCall_restores_chunk_ptr() {
  BytecodeChunk chunk;
  chunk.addFunction(BytecodeFunction("main_fn", 0, 2));
  chunk.addFunction(BytecodeFunction("callee", 0, 3));

  Fiber fib(1, 0, 0, "test-fiber");
  fib.pushCall(0, 0, &chunk);
  fib.call_stack.back().ip = 5;

  fib.pushCall(1, 1, &chunk);
  assert(fib.call_stack.back().chunk_ptr == &chunk);
  assert(fib.call_stack.back().function_id == 1);

  fib.popCall();
  assert(fib.call_stack.back().chunk_ptr == &chunk);
  assert(fib.call_stack.back().function_id == 0);
  assert(fib.current_chunk_ptr == &chunk);
}

static void test_fiber_multiple_chunks_different_pointers() {
  BytecodeChunk chunk_a;
  chunk_a.addFunction(BytecodeFunction("fn_a", 0, 1));

  BytecodeChunk chunk_b;
  chunk_b.addFunction(BytecodeFunction("fn_b", 0, 1));

  Fiber fib(1, 0, 0, "multi-chunk");

  fib.pushCall(0, 0, &chunk_a);
  fib.call_stack.back().ip = 3;

  fib.pushCall(0, 0, &chunk_b);
  assert(fib.call_stack.back().chunk_ptr == &chunk_b);

  fib.popCall();
  assert(fib.call_stack.back().chunk_ptr == &chunk_a);
}

static void test_chunk_getFunctionIndex_valid() {
  BytecodeChunk chunk;
  chunk.addFunction(BytecodeFunction("alpha", 0, 1));
  chunk.addFunction(BytecodeFunction("beta", 0, 2));
  chunk.addFunction(BytecodeFunction("gamma", 1, 3));

  const auto *f0 = chunk.getFunction(0);
  const auto *f1 = chunk.getFunction(1);
  const auto *f2 = chunk.getFunction(2);

  assert(chunk.getFunctionIndex(f0) == 0);
  assert(chunk.getFunctionIndex(f1) == 1);
  assert(chunk.getFunctionIndex(f2) == 2);
}

static void test_chunk_getFunctionIndex_nullptr() {
  BytecodeChunk chunk;
  chunk.addFunction(BytecodeFunction("only", 0, 1));
  assert(chunk.getFunctionIndex(nullptr) == UINT32_MAX);
}

static void test_chunk_getFunctionIndex_out_of_range() {
  BytecodeChunk chunk;
  chunk.addFunction(BytecodeFunction("only", 0, 1));
  uint8_t raw_memory[sizeof(BytecodeFunction)];
  const auto *bogus = reinterpret_cast<const BytecodeFunction *>(raw_memory);
  assert(chunk.getFunctionIndex(bogus) == UINT32_MAX);
}

static void test_saveFiberState_preserves_function_id_and_chunk() {
  BytecodeChunk chunk;
  chunk.addFunction(BytecodeFunction("first", 0, 4));
  chunk.addFunction(BytecodeFunction("second", 1, 4));
  chunk.addFunction(BytecodeFunction("third", 2, 4));

  HostContext ctx;
  VM vm(ctx);

  vm.frame_arena_.resize(2);
  vm.frame_count_ = 1;
  auto &vm_frame = vm.frame_arena_[0];
  vm_frame.function = chunk.getFunction(2);
  vm_frame.chunk = &chunk;
  vm_frame.ip = 7;
  vm_frame.locals_base = 0;
  vm_frame.closure_id = 0;
  vm_frame.stack_depth = 3;
  vm_frame.owns_globals = true;
  vm.current_chunk = &chunk;

  Fiber fib(10, 2, 0, "save-test");
  vm.saveFiberState(&fib);

  assert(!fib.call_stack.empty());
  const auto &saved = fib.call_stack.back();
  assert(saved.function_id == 2);
  assert(saved.chunk_ptr == &chunk);
  assert(saved.ip == 7);
  assert(saved.stack_depth == 3);
  assert(saved.owns_globals == true);
}

static void test_saveFiberState_zero_function_when_no_chunk() {
  HostContext ctx;
  VM vm(ctx);

  vm.frame_arena_.resize(1);
  vm.frame_count_ = 1;
  auto &vm_frame = vm.frame_arena_[0];
  vm_frame.function = nullptr;
  vm_frame.chunk = nullptr;
  vm_frame.ip = 0;

  Fiber fib(11, 0, 0, "no-chunk-test");
  vm.saveFiberState(&fib);

  assert(!fib.call_stack.empty());
  assert(fib.call_stack.back().function_id == 0);
  assert(fib.call_stack.back().chunk_ptr == nullptr);
}

static void test_loadFiberState_resolves_function_from_chunk_ptr() {
  BytecodeChunk chunk;
  chunk.addFunction(BytecodeFunction("first", 0, 2));
  chunk.addFunction(BytecodeFunction("second", 1, 4));
  chunk.addFunction(BytecodeFunction("third", 2, 4));

  HostContext ctx;
  VM vm(ctx);

  Fiber fib(20, 2, 0, "load-test");
  fib.call_stack.clear();
  CallFrame cf;
  cf.function_id = 2;
  cf.chunk_ptr = &chunk;
  cf.ip = 9;
  cf.locals_base = 0;
  cf.closure_id = 0;
  cf.stack_depth = 5;
  cf.owns_globals = true;
  fib.call_stack.push_back(cf);

  vm.loadFiberState(&fib);

  assert(vm.frame_count_ == 1);
  const auto &vm_frame = vm.frame_arena_[0];
  assert(vm_frame.function == chunk.getFunction(2));
  assert(vm_frame.chunk == &chunk);
  assert(vm_frame.stack_depth == 5);
  assert(vm_frame.owns_globals == true);
}

static void test_loadFiberState_fallback_to_current_chunk() {
  BytecodeChunk chunk;
  chunk.addFunction(BytecodeFunction("fallback_fn", 0, 2));
  chunk.addFunction(BytecodeFunction("other", 0, 2));

  HostContext ctx;
  VM vm(ctx);
  vm.current_chunk = &chunk;

  Fiber fib(21, 0, 0, "fallback-test");
  fib.call_stack.clear();
  CallFrame cf;
  cf.function_id = 1;
  cf.chunk_ptr = nullptr;
  cf.ip = 0;
  cf.locals_base = 0;
  cf.closure_id = 0;
  cf.stack_depth = 0;
  cf.owns_globals = false;
  fib.call_stack.push_back(cf);

  vm.loadFiberState(&fib);

  assert(vm.frame_count_ == 1);
  assert(vm.frame_arena_[0].function == chunk.getFunction(1));
  assert(vm.frame_arena_[0].chunk == &chunk);
}

static void test_save_load_roundtrip_preserves_identity() {
  BytecodeChunk chunk;
  chunk.addFunction(BytecodeFunction("alpha", 0, 2));
  chunk.addFunction(BytecodeFunction("beta", 1, 3));
  chunk.addFunction(BytecodeFunction("gamma", 2, 4));

  HostContext ctx;
  VM vm(ctx);

  vm.frame_arena_.resize(2);
  vm.frame_count_ = 1;
  auto &vm_frame = vm.frame_arena_[0];
  vm_frame.function = chunk.getFunction(2);
  vm_frame.chunk = &chunk;
  vm_frame.ip = 42;
  vm_frame.locals_base = 10;
  vm_frame.closure_id = 7;
  vm_frame.stack_depth = 8;
  vm_frame.owns_globals = true;
  vm.current_chunk = &chunk;

  Fiber fib(30, 2, 0, "roundtrip");
  vm.saveFiberState(&fib);

  assert(fib.call_stack.back().function_id == 2);
  assert(fib.call_stack.back().chunk_ptr == &chunk);
  assert(fib.call_stack.back().ip == 42);
  assert(fib.call_stack.back().stack_depth == 8);
  assert(fib.call_stack.back().owns_globals == true);

  vm.frame_count_ = 0;
  vm.frame_arena_[0].function = nullptr;
  vm.frame_arena_[0].chunk = nullptr;
  vm.frame_arena_[0].stack_depth = 0;
  vm.frame_arena_[0].owns_globals = false;
  vm.current_chunk = nullptr;

  vm.loadFiberState(&fib);

  assert(vm.frame_count_ == 1);
  assert(vm.frame_arena_[0].function == chunk.getFunction(2));
  assert(vm.frame_arena_[0].chunk == &chunk);
  assert(vm.frame_arena_[0].stack_depth == 8);
  assert(vm.frame_arena_[0].owns_globals == true);
}

static void test_save_load_roundtrip_nonzero_function_not_zero() {
  BytecodeChunk chunk;
  chunk.addFunction(BytecodeFunction("zeroth", 0, 1));
  chunk.addFunction(BytecodeFunction("first", 0, 1));

  HostContext ctx;
  VM vm(ctx);

  vm.frame_arena_.resize(2);
  vm.frame_count_ = 1;
  vm.frame_arena_[0].function = chunk.getFunction(1);
  vm.frame_arena_[0].chunk = &chunk;
  vm.frame_arena_[0].ip = 0;
  vm.frame_arena_[0].locals_base = 0;
  vm.frame_arena_[0].closure_id = 0;
  vm.frame_arena_[0].stack_depth = 0;
  vm.frame_arena_[0].owns_globals = false;
  vm.current_chunk = &chunk;

  Fiber fib(31, 1, 0, "nonzero-test");
  vm.saveFiberState(&fib);

  assert(fib.call_stack.back().function_id == 1);
  assert(fib.call_stack.back().function_id != 0);

  vm.frame_count_ = 0;
  vm.current_chunk = nullptr;
  vm.frame_arena_[0].function = nullptr;
  vm.frame_arena_[0].chunk = nullptr;
  vm.frame_arena_[0].stack_depth = 0;
  vm.frame_arena_[0].owns_globals = false;
  vm.frame_arena_[0].ip = 0;

  vm.loadFiberState(&fib);

  assert(vm.frame_arena_[0].function == chunk.getFunction(1));
  assert(vm.frame_arena_[0].function != chunk.getFunction(0));
}

static void test_loadFiberState_restores_current_chunk_from_top_frame() {
  BytecodeChunk chunk_a;
  chunk_a.addFunction(BytecodeFunction("main_fn", 0, 2));

  BytecodeChunk chunk_b;
  chunk_b.addFunction(BytecodeFunction("helper", 0, 2));

  HostContext ctx;
  VM vm(ctx);

  Fiber fib(40, 0, 0, "chunk-restore");
  fib.call_stack.clear();

  CallFrame bottom;
  bottom.function_id = 0;
  bottom.chunk_ptr = &chunk_a;
  bottom.ip = 5;
  bottom.locals_base = 0;
  fib.call_stack.push_back(bottom);

  CallFrame top;
  top.function_id = 0;
  top.chunk_ptr = &chunk_b;
  top.ip = 3;
  top.locals_base = 2;
  fib.call_stack.push_back(top);

  vm.loadFiberState(&fib);

  assert(vm.frame_count_ == 2);
  assert(vm.current_chunk == &chunk_b);
  assert(vm.frame_arena_[0].chunk == &chunk_a);
  assert(vm.frame_arena_[1].chunk == &chunk_b);
}

// ============================================================
// HotkeyPolicy + wakeHotkey + requeueFront + wakeHotkeyByAlias
// ============================================================

static void test_requeueFront_resets_state_and_requeues() {
    auto& sched = Scheduler::instance();

    uint32_t gid = sched.spawnHotkey(7, {Value::makeInt(42)}, 5, "hk_requeue");
    auto* g = sched.get(gid);
    CHECK(g != nullptr, "get() returned null");
    g->persistent = true;
    g->hotkey_function_id = 7;
    g->hotkey_closure_id = 5;
    g->hotkey_args = {Value::makeInt(42)};

    auto* picked = sched.pickNext();
    CHECK(picked != nullptr, "pickNext returned null");
    CHECK_EQ(picked->id, gid, "wrong goroutine picked");
    sched.clearCurrent();

    sched.suspend(g, Scheduler::SuspensionReason::HotkeyWait);
    CHECK(g->state == Scheduler::GoroutineState::Suspended, "should be Suspended");

    sched.requeueFront(g);
    CHECK(g->state == Scheduler::GoroutineState::Created, "requeueFront should set Created");
    CHECK(g->suspension_reason == Scheduler::SuspensionReason::None, "reason should be None");
    CHECK_EQ(g->function_id, 7u, "function_id should be restored from hotkey_args");
    CHECK_EQ(g->closure_id, 5u, "closure_id should be restored from hotkey_args");
    CHECK_EQ(g->locals.size(), 1u, "locals should be repopulated from hotkey_args");
    CHECK_EQ(g->locals[0].asInt(), 42, "locals[0] should be hotkey_args[0]");
    CHECK_EQ(g->stack.size(), 1u, "stack should be repopulated from hotkey_args for persistent goroutine");
    CHECK_EQ(g->stack[0].asInt(), 42, "stack[0] should be hotkey_args[0]");

    auto* re_picked = sched.pickNext();
    CHECK(re_picked != nullptr, "should be pickable after requeueFront");
    CHECK_EQ(re_picked->id, gid, "wrong goroutine after requeue");

    g->state = Scheduler::GoroutineState::Done;
    sched.clearCurrent();
}

static void test_requeueFront_non_persistent_clears_args() {
    auto& sched = Scheduler::instance();

    uint32_t gid = sched.spawn(3, {Value::makeInt(99)}, 0, "nonpersistent");
    auto* g = sched.get(gid);

    auto* picked = sched.pickNext();
    CHECK(picked != nullptr, "pick");
    sched.clearCurrent();

    sched.requeueFront(g);
    CHECK_EQ(g->function_id, 3u, "function_id preserved for non-persistent");
    CHECK(g->locals.empty(), "non-persistent requeueFront should not populate locals from hotkey_args");

    auto* re_picked = sched.pickNext();
    CHECK(re_picked != nullptr, "should be pickable");
    re_picked->state = Scheduler::GoroutineState::Done;
    sched.clearCurrent();
}

static void test_wakeHotkey_drop_while_pending() {
    auto& sched = Scheduler::instance();

    uint32_t gid = sched.spawnHotkey(1, {Value::makeInt(10)}, 0, "drop_hk");
    auto* g = sched.get(gid);
    g->persistent = true;
    g->hotkey_policy = HotkeyPolicy::Drop;
    g->hotkey_function_id = 1;
    g->hotkey_args = {Value::makeInt(10)};

    auto* picked = sched.pickNext();
    CHECK(picked != nullptr, "pick");
    CHECK(picked->state == Scheduler::GoroutineState::Running, "should be Running");
    sched.clearCurrent();
    sched.yield(g);
    CHECK(g->state == Scheduler::GoroutineState::Runnable, "should be Runnable after yield");

    bool result = sched.wakeHotkey(g);
    CHECK(!result, "Drop policy should return false when goroutine is pending (Runnable)");

    g->state = Scheduler::GoroutineState::Done;
    sched.clearCurrent();
}

static void test_wakeHotkey_drop_while_suspended() {
    auto& sched = Scheduler::instance();

    uint32_t gid = sched.spawnHotkey(1, {Value::makeInt(10)}, 0, "drop_hk_susp");
    auto* g = sched.get(gid);
    g->persistent = true;
    g->hotkey_policy = HotkeyPolicy::Drop;
    g->hotkey_function_id = 1;
    g->hotkey_args = {Value::makeInt(10)};

    auto* picked = sched.pickNext();
    sched.clearCurrent();
    sched.suspend(g, Scheduler::SuspensionReason::HotkeyWait);

    bool result = sched.wakeHotkey(g);
    CHECK(result, "Drop policy should return true when goroutine is Suspended");
    CHECK(g->state == Scheduler::GoroutineState::Created, "should be Created after requeue");

    g->state = Scheduler::GoroutineState::Done;
    sched.clearCurrent();
}

static void test_wakeHotkey_replace_while_pending() {
    auto& sched = Scheduler::instance();

    uint32_t gid = sched.spawnHotkey(1, {Value::makeInt(10)}, 0, "replace_hk");
    auto* g = sched.get(gid);
    g->persistent = true;
    g->hotkey_policy = HotkeyPolicy::Replace;
    g->hotkey_function_id = 1;
    g->hotkey_args = {Value::makeInt(10)};

    auto* picked = sched.pickNext();
    sched.clearCurrent();
    sched.yield(g);

    bool result = sched.wakeHotkey(g, {Value::makeInt(20)});
    CHECK(result, "Replace policy should return true even when pending");
    CHECK_EQ(g->hotkey_args[0].asInt(), 20, "hotkey_args should be updated with new args");
    CHECK(g->state == Scheduler::GoroutineState::Created, "should be requeued (Created)");

    g->state = Scheduler::GoroutineState::Done;
    sched.clearCurrent();
}

static void test_wakeHotkey_replace_while_suspended() {
    auto& sched = Scheduler::instance();

    uint32_t gid = sched.spawnHotkey(1, {Value::makeInt(5)}, 0, "replace_susp");
    auto* g = sched.get(gid);
    g->persistent = true;
    g->hotkey_policy = HotkeyPolicy::Replace;
    g->hotkey_function_id = 1;
    g->hotkey_args = {Value::makeInt(5)};

    auto* picked = sched.pickNext();
    sched.clearCurrent();
    sched.suspend(g, Scheduler::SuspensionReason::HotkeyWait);

    bool result = sched.wakeHotkey(g, {Value::makeInt(99)});
    CHECK(result, "Replace policy should return true when Suspended");
    CHECK_EQ(g->hotkey_args[0].asInt(), 99, "args should be updated");

    g->state = Scheduler::GoroutineState::Done;
    sched.clearCurrent();
}

static void test_wakeHotkey_queue_always_wakes() {
    auto& sched = Scheduler::instance();

    uint32_t gid = sched.spawnHotkey(1, {Value::makeInt(1)}, 0, "queue_hk");
    auto* g = sched.get(gid);
    g->persistent = true;
    g->hotkey_policy = HotkeyPolicy::Queue;
    g->hotkey_function_id = 1;
    g->hotkey_args = {Value::makeInt(1)};

    auto* picked = sched.pickNext();
    sched.clearCurrent();
    sched.yield(g);

    bool result = sched.wakeHotkey(g, {Value::makeInt(2)});
    CHECK(result, "Queue policy should always return true");
    CHECK_EQ(g->hotkey_args[0].asInt(), 2, "args updated for Queue");
    CHECK(g->state == Scheduler::GoroutineState::Created, "requeued");

    g->state = Scheduler::GoroutineState::Done;
    sched.clearCurrent();
}

static void test_wakeHotkey_coalesce_while_pending() {
    auto& sched = Scheduler::instance();

    uint32_t gid = sched.spawnHotkey(1, {Value::makeInt(100)}, 0, "coalesce_hk");
    auto* g = sched.get(gid);
    g->persistent = true;
    g->hotkey_policy = HotkeyPolicy::Coalesce;
    g->hotkey_function_id = 1;
    g->hotkey_args = {Value::makeInt(100)};

    auto* picked = sched.pickNext();
    sched.clearCurrent();
    sched.yield(g);

    bool result = sched.wakeHotkey(g, {Value::makeInt(200)});
    CHECK(result, "Coalesce policy should return true when pending and has newArgs");
    CHECK_EQ(g->hotkey_args[0].asInt(), 200, "args should be updated in-place");
    CHECK_EQ(g->locals[0].asInt(), 200, "locals should also be updated");

    g->state = Scheduler::GoroutineState::Done;
    sched.clearCurrent();
}

static void test_wakeHotkey_coalesce_pending_no_args() {
    auto& sched = Scheduler::instance();

    uint32_t gid = sched.spawnHotkey(1, {Value::makeInt(50)}, 0, "coalesce_noargs");
    auto* g = sched.get(gid);
    g->persistent = true;
    g->hotkey_policy = HotkeyPolicy::Coalesce;
    g->hotkey_function_id = 1;
    g->hotkey_args = {Value::makeInt(50)};

    auto* picked = sched.pickNext();
    sched.clearCurrent();
    sched.yield(g);

    bool result = sched.wakeHotkey(g, {});
    CHECK(result, "Coalesce pending with no newArgs should return true (no-op wake)");
    CHECK_EQ(g->hotkey_args[0].asInt(), 50, "args should stay unchanged when no newArgs");

    g->state = Scheduler::GoroutineState::Done;
    sched.clearCurrent();
}

static void test_wakeHotkey_coalesce_while_suspended() {
    auto& sched = Scheduler::instance();

    uint32_t gid = sched.spawnHotkey(1, {Value::makeInt(1)}, 0, "coalesce_susp");
    auto* g = sched.get(gid);
    g->persistent = true;
    g->hotkey_policy = HotkeyPolicy::Coalesce;
    g->hotkey_function_id = 1;
    g->hotkey_args = {Value::makeInt(1)};

    auto* picked = sched.pickNext();
    sched.clearCurrent();
    sched.suspend(g, Scheduler::SuspensionReason::HotkeyWait);

    bool result = sched.wakeHotkey(g, {Value::makeInt(2)});
    CHECK(result, "Coalesce should return true when Suspended (requeues)");
    CHECK_EQ(g->hotkey_args[0].asInt(), 2, "args updated before requeue");

    g->state = Scheduler::GoroutineState::Done;
    sched.clearCurrent();
}

static void test_wakeHotkey_null_goroutine() {
    auto& sched = Scheduler::instance();
    bool result = sched.wakeHotkey(nullptr);
    CHECK(!result, "wakeHotkey(nullptr) should return false");
}

static void test_wakeHotkeyByAlias_finds_matching() {
    auto& sched = Scheduler::instance();

    uint32_t gid = sched.spawnHotkey(1, {}, 0, "alias_hk");
    auto* g = sched.get(gid);
    g->persistent = true;
    g->hotkey_alias = "my_key";
    g->hotkey_function_id = 1;
    g->hotkey_args = {};
    g->hotkey_policy = HotkeyPolicy::Drop;

    auto* picked = sched.pickNext();
    sched.clearCurrent();
    sched.suspend(g, Scheduler::SuspensionReason::HotkeyWait);

    bool result = sched.wakeHotkeyByAlias("my_key");
    CHECK(result, "wakeHotkeyByAlias should find matching persistent goroutine");

    bool no_result = sched.wakeHotkeyByAlias("nonexistent_key");
    CHECK(!no_result, "wakeHotkeyByAlias should return false for unknown alias");

    g->state = Scheduler::GoroutineState::Done;
    sched.clearCurrent();
}

static void test_wakeHotkeyByAlias_skips_non_persistent() {
    auto& sched = Scheduler::instance();

    uint32_t gid = sched.spawnHotkey(1, {}, 0, "non_persistent_alias");
    auto* g = sched.get(gid);
    g->persistent = false;
    g->hotkey_alias = "ignored_key";

    auto* picked = sched.pickNext();
    sched.clearCurrent();
    sched.suspend(g, Scheduler::SuspensionReason::HotkeyWait);

    bool result = sched.wakeHotkeyByAlias("ignored_key");
    CHECK(!result, "wakeHotkeyByAlias should skip non-persistent goroutines");

    g->state = Scheduler::GoroutineState::Done;
    sched.clearCurrent();
}

static void test_wakeHotkeyByAlias_multiple_same_alias() {
    auto& sched = Scheduler::instance();

    uint32_t g1 = sched.spawnHotkey(1, {}, 0, "alias_dup1");
    uint32_t g2 = sched.spawnHotkey(1, {}, 0, "alias_dup2");

    auto* go1 = sched.get(g1);
    auto* go2 = sched.get(g2);

    go1->persistent = true;
    go1->hotkey_alias = "shared_key";
    go1->hotkey_function_id = 1;
    go1->hotkey_policy = HotkeyPolicy::Drop;

    go2->persistent = true;
    go2->hotkey_alias = "shared_key";
    go2->hotkey_function_id = 1;
    go2->hotkey_policy = HotkeyPolicy::Drop;

    auto* p1 = sched.pickNext();
    sched.clearCurrent();
    sched.suspend(go2, Scheduler::SuspensionReason::HotkeyWait);

    auto* p2 = sched.pickNext();
    sched.clearCurrent();
    sched.suspend(go1, Scheduler::SuspensionReason::HotkeyWait);

    bool result = sched.wakeHotkeyByAlias("shared_key");
    CHECK(result, "should wake at least one goroutine with matching alias");

    go1->state = Scheduler::GoroutineState::Done;
    go2->state = Scheduler::GoroutineState::Done;
    sched.clearCurrent();
}

static void test_wakeHotkeyByAlias_with_drop_policy_only_wakes_suspended() {
    auto& sched = Scheduler::instance();

    uint32_t g1 = sched.spawnHotkey(1, {}, 0, "alias_drop_run");
    uint32_t g2 = sched.spawnHotkey(1, {}, 0, "alias_drop_susp");

    auto* go1 = sched.get(g1);
    auto* go2 = sched.get(g2);

    go1->persistent = true;
    go1->hotkey_alias = "drop_key";
    go1->hotkey_function_id = 1;
    go1->hotkey_policy = HotkeyPolicy::Drop;

    go2->persistent = true;
    go2->hotkey_alias = "drop_key";
    go2->hotkey_function_id = 1;
    go2->hotkey_policy = HotkeyPolicy::Drop;

    auto* p1 = sched.pickNext();
    sched.clearCurrent();
    sched.yield(go2);

    auto* p2 = sched.pickNext();
    sched.clearCurrent();
    sched.suspend(go1, Scheduler::SuspensionReason::HotkeyWait);

    bool result = sched.wakeHotkeyByAlias("drop_key");
    CHECK(result, "should wake the Suspended one (go1)");

    go1->state = Scheduler::GoroutineState::Done;
    go2->state = Scheduler::GoroutineState::Done;
    sched.clearCurrent();
}

static void test_persistent_goroutine_fields() {
    auto& sched = Scheduler::instance();

    uint32_t gid = sched.spawnHotkey(5, {Value::makeInt(77)}, 3, "persistent_check");
    auto* g = sched.get(gid);

    g->persistent = true;
    g->hotkey_policy = HotkeyPolicy::Replace;
    g->hotkey_alias = "test_alias";
    g->hotkey_function_id = 5;
    g->hotkey_closure_id = 3;
    g->hotkey_args = {Value::makeInt(77)};

    CHECK(g->persistent == true, "persistent should be true");
    CHECK(g->hotkey_policy == HotkeyPolicy::Replace, "policy should be Replace");
    CHECK(g->hotkey_alias == "test_alias", "alias mismatch");
    CHECK_EQ(g->hotkey_function_id, 5u, "hotkey_function_id mismatch");
    CHECK_EQ(g->hotkey_closure_id, 3u, "hotkey_closure_id mismatch");
    CHECK_EQ(g->hotkey_args.size(), 1u, "hotkey_args should have 1 element");
    CHECK_EQ(g->hotkey_args[0].asInt(), 77, "hotkey_args[0] value mismatch");

    g->state = Scheduler::GoroutineState::Done;
    sched.clearCurrent();
}

static void test_hotkey_policy_default_is_drop() {
    auto& sched = Scheduler::instance();

    uint32_t gid = sched.spawnHotkey(0, {}, 0, "default_policy");
    auto* g = sched.get(gid);

    CHECK(g->hotkey_policy == HotkeyPolicy::Drop, "default hotkey_policy should be Drop");

    g->state = Scheduler::GoroutineState::Done;
    sched.clearCurrent();
}

static void test_requeueFront_hotkey_goes_to_front_of_hotkey_queue() {
    auto& sched = Scheduler::instance();

    uint32_t hk1 = sched.spawnHotkey(1, {}, 0, "hk1_front");
    uint32_t normal = sched.spawn(1, {}, 0, "normal_front");
    auto* g1 = sched.get(hk1);
    auto* gn = sched.get(normal);

    g1->persistent = true;
    g1->hotkey_function_id = 1;
    g1->hotkey_args = {};

    auto* p = sched.pickNext();
    CHECK_EQ(p->id, hk1, "hotkey should be picked first");
    sched.clearCurrent();
    sched.suspend(g1, Scheduler::SuspensionReason::HotkeyWait);

    sched.requeueFront(g1);

    auto* first = sched.pickNext();
    CHECK(first != nullptr, "should have runnable");
    CHECK_EQ(first->id, hk1, "requeued hk1 should be at front (before normal)");

    first->state = Scheduler::GoroutineState::Done;
    sched.clearCurrent();

    auto* second = sched.pickNext();
    CHECK(second != nullptr, "should have normal");
    CHECK_EQ(second->id, normal, "normal should be after requeued hotkey");

    second->state = Scheduler::GoroutineState::Done;
    sched.clearCurrent();
}

void run_scheduler_tests() {
  std::cout << "=== Scheduler Tests ===\n\n";

  test_pickNext_returns_null_on_empty();
  std::cout << " PASS pickNext returns null on empty\n";

  test_spawn_creates_goroutine();
  std::cout << " PASS spawn creates goroutine\n";

  test_spawnHotkey();
  std::cout << " PASS spawnHotkey creates HOTKEY goroutine\n";

  test_pickNext_priority_order();
  std::cout << " PASS pickNext respects priority (hotkey > normal > background)\n";

  test_pickNext_skips_done();
  std::cout << " PASS pickNext skips Done goroutines\n";

  test_suspend_and_unpark();
  std::cout << " PASS suspend and unpark\n";

  test_yield_returns_to_queue();
  std::cout << " PASS yield returns goroutine to queue\n";

  test_spawn_background_priority();
  std::cout << " PASS background priority is last\n";

  test_goroutineCount();
  std::cout << " PASS goroutineCount tracking\n";

  test_hotkey_prepend_not_append();
  std::cout << " PASS hotkey prepends (not appends) to queue\n";

  test_multiple_hotkeys_prepend_order();
  std::cout << " PASS multiple hotkeys prepend in LIFO order\n";

  test_burst_hotkeys_all_preempt_normal();
  std::cout << " PASS burst hotkeys all preempt normal tasks\n";

  test_starvation_background_never_runs_with_normal_pending();
  std::cout << " PASS background starvation: never runs while normal pending\n";

  test_wakeSleepingGoroutines_past_time();
  std::cout << " PASS wakeSleepingGoroutines wakes past-time sleepers\n";

  test_wakeSleepingGoroutines_future_time_not_woken();
  std::cout << " PASS wakeSleepingGoroutines skips future-time sleepers\n";

  test_wakeSleepingGoroutines_only_sleepwait();
  std::cout << " PASS wakeSleepingGoroutines only wakes SleepWait, not ChannelWait\n";

  test_wakeSleepingGoroutines_multiple_sleepers();
  std::cout << " PASS wakeSleepingGoroutines wakes all due sleepers\n";

  test_drainDeferredCallbacks_executes_all();
  std::cout << " PASS drainDeferredCallbacks executes all actions\n";

  test_drainDeferredCallbacks_exception_handling();
  std::cout << " PASS drainDeferredCallbacks continues after exception\n";

  test_drainDeferredCallbacks_spawn_integration();
  std::cout << " PASS drainDeferredCallbacks spawn integration\n";

  test_stop_marks_all_done();
  std::cout << " PASS stop marks all goroutines Done\n";

  test_unpark_ignores_non_suspended();
  std::cout << " PASS unpark ignores non-suspended goroutines\n";

  test_yield_on_suspended_makes_runnable();
  std::cout << " PASS yield on suspended goroutine makes it runnable\n";

  test_suspendedCount();
  std::cout << " PASS suspendedCount tracking\n";

  test_runnableCount_accuracy();
  std::cout << " PASS runnableCount accuracy\n";

  test_fiber_callframe_saves_chunk_ptr();
  std::cout << " PASS fiber CallFrame saves chunk_ptr\n";

  test_fiber_callframe_default_chunk_ptr_null();
  std::cout << " PASS fiber CallFrame default chunk_ptr is null\n";

  test_fiber_popCall_restores_chunk_ptr();
  std::cout << " PASS fiber popCall restores chunk_ptr\n";

  test_fiber_multiple_chunks_different_pointers();
  std::cout << " PASS fiber multiple chunks preserve different pointers\n";

  test_chunk_getFunctionIndex_valid();
  std::cout << " PASS chunk getFunctionIndex returns correct indices\n";

  test_chunk_getFunctionIndex_nullptr();
  std::cout << " PASS chunk getFunctionIndex returns UINT32_MAX for nullptr\n";

  test_chunk_getFunctionIndex_out_of_range();
  std::cout << " PASS chunk getFunctionIndex returns UINT32_MAX for out-of-range\n";

  test_saveFiberState_preserves_function_id_and_chunk();
  std::cout << " PASS saveFiberState preserves function_id and chunk\n";

  test_saveFiberState_zero_function_when_no_chunk();
  std::cout << " PASS saveFiberState yields function_id=0 when no chunk\n";

  test_loadFiberState_resolves_function_from_chunk_ptr();
  std::cout << " PASS loadFiberState resolves function from chunk_ptr\n";

  test_loadFiberState_fallback_to_current_chunk();
  std::cout << " PASS loadFiberState falls back to current_chunk\n";

  test_save_load_roundtrip_preserves_identity();
  std::cout << " PASS save/load roundtrip preserves identity\n";

  test_save_load_roundtrip_nonzero_function_not_zero();
  std::cout << " PASS save/load roundtrip nonzero function stays nonzero\n";

    test_loadFiberState_restores_current_chunk_from_top_frame();
    std::cout << " PASS loadFiberState restores current_chunk from top frame\n";

    // Hotkey policy + wakeHotkey + requeueFront + wakeHotkeyByAlias
    test_requeueFront_resets_state_and_requeues();
    std::cout << " PASS requeueFront resets state and requeues persistent goroutine\n";

    test_requeueFront_non_persistent_clears_args();
    std::cout << " PASS requeueFront non-persistent does not populate hotkey_args\n";

    test_wakeHotkey_drop_while_pending();
    std::cout << " PASS wakeHotkey Drop policy: returns false while pending\n";

    test_wakeHotkey_drop_while_suspended();
    std::cout << " PASS wakeHotkey Drop policy: wakes when suspended\n";

    test_wakeHotkey_replace_while_pending();
    std::cout << " PASS wakeHotkey Replace policy: resets with new args while pending\n";

    test_wakeHotkey_replace_while_suspended();
    std::cout << " PASS wakeHotkey Replace policy: wakes when suspended\n";

    test_wakeHotkey_queue_always_wakes();
    std::cout << " PASS wakeHotkey Queue policy: always wakes\n";

    test_wakeHotkey_coalesce_while_pending();
    std::cout << " PASS wakeHotkey Coalesce policy: updates args in-place while pending\n";

    test_wakeHotkey_coalesce_pending_no_args();
    std::cout << " PASS wakeHotkey Coalesce policy: returns true with no newArgs\n";

    test_wakeHotkey_coalesce_while_suspended();
    std::cout << " PASS wakeHotkey Coalesce policy: wakes when suspended\n";

    test_wakeHotkey_null_goroutine();
    std::cout << " PASS wakeHotkey returns false for nullptr\n";

    test_wakeHotkeyByAlias_finds_matching();
    std::cout << " PASS wakeHotkeyByAlias finds matching persistent goroutine\n";

    test_wakeHotkeyByAlias_skips_non_persistent();
    std::cout << " PASS wakeHotkeyByAlias skips non-persistent goroutines\n";

    test_wakeHotkeyByAlias_multiple_same_alias();
    std::cout << " PASS wakeHotkeyByAlias wakes multiple goroutines with same alias\n";

    test_wakeHotkeyByAlias_with_drop_policy_only_wakes_suspended();
    std::cout << " PASS wakeHotkeyByAlias with Drop policy only wakes suspended\n";

    test_persistent_goroutine_fields();
    std::cout << " PASS persistent goroutine fields (policy, alias, fn, closure, args)\n";

    test_hotkey_policy_default_is_drop();
    std::cout << " PASS hotkey_policy default is Drop\n";

    test_requeueFront_hotkey_goes_to_front_of_hotkey_queue();
    std::cout << " PASS requeueFront puts hotkey at front of hotkey queue\n";

    constexpr int total = 25 + 14 + 18;
    std::cout << "\n=== All " << total << " tests passed! ===\n";
}

} // namespace havel::test