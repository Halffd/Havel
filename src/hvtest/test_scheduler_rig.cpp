#include "havel-lang/runtime/concurrency/Scheduler.hpp"
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

  constexpr int total = 25;
  std::cout << "\n=== All " << total << " tests passed! ===\n";
}

} // namespace havel::test