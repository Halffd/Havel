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

void run_scheduler_tests() {
    std::cout << "=== Scheduler Tests ===\n\n";

    test_pickNext_returns_null_on_empty();
    std::cout << "  PASS  pickNext returns null on empty\n";

    test_spawn_creates_goroutine();
    std::cout << "  PASS  spawn creates goroutine\n";

    test_spawnHotkey();
    std::cout << "  PASS  spawnHotkey creates HOTKEY goroutine\n";

    test_pickNext_priority_order();
    std::cout << "  PASS  pickNext respects priority (hotkey > normal > background)\n";

    test_pickNext_skips_done();
    std::cout << "  PASS  pickNext skips Done goroutines\n";

    test_suspend_and_unpark();
    std::cout << "  PASS  suspend and unpark\n";

    test_yield_returns_to_queue();
    std::cout << "  PASS  yield returns goroutine to queue\n";

    test_spawn_background_priority();
    std::cout << "  PASS  background priority is last\n";

    test_goroutineCount();
    std::cout << "  PASS  goroutineCount tracking\n";

    test_hotkey_prepend_not_append();
    std::cout << "  PASS  hotkey prepends (not appends) to queue\n";

    std::cout << "\n=== All " << 10 << " tests passed! ===\n";
}

} // namespace havel::test