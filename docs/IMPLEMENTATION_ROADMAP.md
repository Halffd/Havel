# Implementation Roadmap - Concurrency Architecture

## Current Critical Blocker

**File:** `src/havel-lang/compiler/runtime/ConcurrencyBridge.cpp`  
**Line:** 140 in `threadJoin()`  
**Problem:**
```cpp
it->second.join();  // ❌ BLOCKS VM THREAD - DEADLOCK RISK
```

**Impact:** Any code calling `thread.join()` will freeze the entire application.

```havel
let t = go expensive_task()
t.join()  // ❌ This line blocks main thread, makes system unresponsive
```

---

## Implementation Phases

### Phase 0: Architecture Lock (DONE)
✅ Execution model documented  
✅ All interactions validated  
✅ Critical gap identified (thread.join blocking)

### Phase 1: Create Event Queue (REQUIRED)
**Files to create/modify:**
- `src/havel-lang/compiler/runtime/EventQueue.hpp` (NEW)
- `src/havel-lang/compiler/runtime/EventQueue.cpp` (NEW)

**Purpose:** Bridge between OS threads, timers, and VM execution

**API:**
```cpp
class EventQueue {
public:
    using Callback = std::function<void()>;
    
    void push(Callback cb);       // Enqueue callback (thread-safe)
    void processAll();            // Drain queue in main thread
    size_t size() const;          // For testing
    
private:
    std::queue<Callback> callbacks_;
    mutable std::mutex mutex_;
};
```

**Design Invariant:** 
- Only main thread calls `processAll()`
- Any thread can call `push()`
- Callbacks never call VM directly

**Test:** Create unit tests verifying thread-safety and ordering

---

### Phase 2: Refactor Scheduler (REQUIRED)
**Files to modify:**
- `src/havel-lang/runtime/concurrency/Scheduler.hpp`
- `src/havel-lang/runtime/concurrency/Scheduler.cpp`

**Current Issues:**
1. Maintains OS threads per goroutine (wrong model)
2. Runs `task()` functions directly (not bytecode)
3. Not integrated with VM.executeStep()
4. Has duplicate Goroutine definitions (also in VM.hpp)

**Changes:**
```cpp
// OLD
struct Goroutine {
    std::function<Value()> task;  // ❌ Arbitrary function
};

// NEW
struct Goroutine {
    uint32_t function_id;         // ✓ Bytecode address
    uint32_t ip;                  // ✓ Instruction pointer
    std::vector<Value> stack;     // ✓ VM stack
    std::vector<Value> locals;    // ✓ VM locals
    
    // Suspension state
    enum State {
        Runnable,      // Ready to execute next instruction
        Suspended,     // Waiting on channel/timer/thread
        Done           // Execution finished
    } state;
    
    // Suspension reasons
    uint32_t waiting_for_channel = 0;   // Which channel ID (if suspended on channel)
    uint32_t waiting_for_thread = 0;    // Which thread ID (if waiting for join)
    std::chrono::steady_clock::time_point resume_at_time;  // For sleep()
};
```

**API to expose:**
```cpp
class Scheduler {
public:
    // Goroutine management
    uint32_t spawn(uint32_t function_id, const std::vector<Value> &args, const std::string &name = "");
    std::shared_ptr<Goroutine> current();
    void suspend(Goroutine* g, uint32_t suspension_reason);
    void unpark(Goroutine* g);
    
    // Scheduler state
    bool hasRunnable();
    Goroutine* pickNext();
    
    // Lifecycle
    void start();
    void stop();
    void waitAll();
};
```

**Important:** Remove ALL OS thread spawning from Scheduler itself.

---

### Phase 3: VM Integration (REQUIRED)
**Files to modify:**
- `src/havel-lang/compiler/vm/VM.hpp`
- `src/havel-lang/compiler/vm/VM.cpp`

**Current Issues:**
1. `execute()` runs full function (not stepping)
2. No integration with Scheduler
3. Multiple Coroutine definitions

**Changes:**

1. **Split execute() into executeStep():**
```cpp
// OLD
Value execute(const FunctionDef& func) {
    // Executes entire function at once
}

// NEW
void executeStep() {
    // Execute ONE instruction
    // Then yield if needed
}
```

2. **Integrate Scheduler:**
```cpp
class VM {
private:
    Scheduler* scheduler_;
    
public:
    void executeStep() {
        if (!current_goroutine_) {
            current_goroutine_ = scheduler_->pickNext();
            if (!current_goroutine_) return;  // No runnable
        }
        
        // Fetch current instruction
        const BytecodeChunk* chunk = chunks_[current_goroutine_->function_id];
        uint8_t op = chunk->bytecode[current_goroutine_->ip];
        
        // Execute ONE instruction
        executeInstruction(op);
        current_goroutine_->ip++;
        
        // If yielded or suspended, next executeStep() picks new goroutine
    }
    
    void yield() {
        // Park current goroutine, VM will pick next on next step
        scheduler_->suspend(current_goroutine_, YIELD);
        current_goroutine_ = nullptr;
    }
};
```

3. **Fix duplicate Coroutine definition:**
- Scheduler.hpp Goroutine = source of truth
- Remove from VM.hpp
- Update VM to use Scheduler's Goroutine

---

### Phase 4: Fix thread.join() Blocking (CRITICAL)
**File:**  
- `src/havel-lang/compiler/runtime/ConcurrencyBridge.cpp` (threadJoin method)

**Current (WRONG):**
```cpp
Value ConcurrencyBridge::threadJoin(const std::vector<Value> &args) {
    uint32_t thread_id = args[0].asThreadId();
    std::lock_guard<std::mutex> lock(threads_mutex_);
    
    auto it = active_threads_.find(thread_id);
    if (it != active_threads_.end() && it->second.joinable()) {
        it->second.join();  // ❌ BLOCKS VM
        active_threads_.erase(it);
    }
    return Value::makeNull();
}
```

**New (CORRECT):**
```cpp
Value ConcurrencyBridge::threadJoin(const std::vector<Value> &args) {
    uint32_t thread_id = args[0].asThreadId();
    
    Goroutine* g = scheduler_->current();
    if (!g) return Value::makeNull();
    
    // Mark goroutine as suspended
    g->waiting_for_thread = thread_id;
    scheduler_->suspend(g, WAITING_FOR_THREAD);
    
    // Enqueue callback for when thread completes
    eventQueue_->push([this, thread_id, g]() {
        // This runs in the main event loop after thread finishes
        std::lock_guard<std::mutex> lock(threads_mutex_);
        auto it = active_threads_.find(thread_id);
        if (it != active_threads_.end() && it->second.joinable()) {
            it->second.join();  // ✓ In event loop, not blocking VM step
            active_threads_.erase(it);
        }
        
        // Unpark the waiting goroutine
        scheduler_->unpark(g);
    });
    
    // Return immediately (goroutine will resume when callback runs)
    return Value::makeNull();
}
```

**Key Difference:**
- OLD: Blocks VM thread, waits for OS thread
- NEW: Parks goroutine, enqueues a callback, VM continues
- When callback runs (in event loop), thread is definitely done (no race)

---

### Phase 5: Channel recv/send (REQUIRED)
**Files to modify:**
- `src/havel-lang/compiler/runtime/ConcurrencyBridge.cpp` (channelReceive/channelSend)

**Current Issues:**
1. recv() on empty channel: probably returns null (should suspend)
2. send() might block (should return immediately)

**Changes:**

```cpp
// Receive - SUSPEND if empty
Value ConcurrencyBridge::channelReceive(const std::vector<Value> &args) {
    if (args.empty() || !args[0].isChannelId()) {
        return Value::makeNull();
    }
    
    uint32_t channel_id = args[0].asChannelId();
    
    std::lock_guard<std::mutex> lock(channels_mutex_);
    auto& channel = channels_[channel_id];
    
    // If data available, return immediately
    if (!channel->queue.empty()) {
        Value msg = channel->queue.front();
        channel->queue.pop();
        return msg;
    }
    
    // If empty, suspend current goroutine
    Goroutine* g = scheduler_->current();
    if (!g) return Value::makeNull();
    
    g->waiting_for_channel = channel_id;
    scheduler_->suspend(g, WAITING_FOR_CHANNEL);
    
    // When data arrives, sender will unpark us
    // This is a yield point - control returns to event loop
    return Value::makeNull();  // Will be resumed with actual data
}

// Send - ENQUEUE and WAKE receiver
Value ConcurrencyBridge::channelSend(const std::vector<Value> &args) {
    if (args.size() < 2 || !args[0].isChannelId()) {
        return Value::makeNull();
    }
    
    uint32_t channel_id = args[0].asChannelId();
    Value message = args[1];
    
    {
        std::lock_guard<std::mutex> lock(channels_mutex_);
        auto& channel = channels_[channel_id];
        channel->queue.push(message);
    }
    
    // Find goroutine waiting on this channel
    Goroutine* waiting = scheduler_->findSuspended(WAITING_FOR_CHANNEL, channel_id);
    if (waiting) {
        scheduler_->unpark(waiting);
    }
    
    return Value::makeTrue();  // Send succeeded
}
```

---

### Phase 6: Timer Reimplementation (REQUIRED)
**Files to modify:**
- `src/havel-lang/compiler/runtime/ConcurrencyBridge.cpp` (intervalStart, timeoutStart)

**Current Issues:**
1. Timers might spawn OS threads (Interval class does)
2. Callbacks executed directly (should be enqueued)

**Changes:**

```cpp
// Instead of spawning thread, use timer queue + event loop
void ConcurrencyBridge::checkTimers(std::chrono::steady_clock::time_point now) {
    std::lock_guard<std::mutex> lock(timers_mutex_);
    
    for (auto& timer : timers_) {
        if (!timer.active) continue;
        
        if (now >= timer.next_run) {
            // Enqueue callback instead of calling directly
            eventQueue_->push([this, callback = timer.callback]() {
                // Execute in event loop context
                vm_->callFunction(callback);
            });
            
            timer.next_run = now + std::chrono::milliseconds(timer.interval_ms);
            
            if (timer.interval_ms == 0) {
                // One-shot timeout
                timer.active = false;
            }
        }
    }
}

Value ConcurrencyBridge::intervalStart(const std::vector<Value> &args) {
    if (args.size() < 2) return Value::makeNull();
    
    int64_t interval_ms = args[0].asInt();
    Value callback = args[1];
    
    std::lock_guard<std::mutex> lock(timers_mutex_);
    uint32_t timer_id = next_timer_id_++;
    
    Timer timer;
    timer.id = timer_id;
    timer.next_run = std::chrono::steady_clock::now() + std::chrono::milliseconds(interval_ms);
    timer.interval_ms = interval_ms;  // Repeating
    timer.callback = callback;
    timer.active = true;
    
    timers_.push_back(timer);
    return Value::makeTimerId(timer_id);
}

Value ConcurrencyBridge::timeoutStart(const std::vector<Value> &args) {
    if (args.size() < 2) return Value::makeNull();
    
    int64_t timeout_ms = args[0].asInt();
    Value callback = args[1];
    
    std::lock_guard<std::mutex> lock(timers_mutex_);
    uint32_t timer_id = next_timer_id_++;
    
    Timer timer;
    timer.id = timer_id;
    timer.next_run = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    timer.interval_ms = 0;       // One-shot
    timer.callback = callback;
    timer.active = true;
    
    timers_.push_back(timer);
    return Value::makeTimerId(timer_id);
}
```

---

### Phase 7: Main Event Loop Integration (REQUIRED)
**Files to modify:**
- `src/core/io/EventListener.cpp` (EventLoop method)

**Current Code:**
```cpp
void EventListener::EventLoop() {
    while (running.load() && !shutdown.load()) {
        // Check for expired timers
        if (hostBridge) {
            hostBridge->checkTimers();
        }
        
        // Poll input
        // ...
    }
}
```

**Changes:**
```cpp
void EventListener::EventLoop() {
    while (running.load() && !shutdown.load()) {
        // Level 0: Execute ONE VM bytecode instruction
        vm_->executeStep();
        
        // Level 1: Check scheduler for ready goroutines
        // (Already integrated into VM.executeStep(), but explicit check)
        
        // Level 2: Process queued callbacks from timers, threads, channels
        eventQueue_->processAll();
        
        // Level 3: Check and fire expired timers
        // (Safe because timers just enqueue, don't execute)
        concurrencyBridge_->checkTimers(now);
        
        // Level 4: Poll input events
        // ... existing input handling ...
    }
}
```

**Order is critical:**
1. VM step executes one instruction
2. Callbacks process (wake goroutines from previous timers)
3. Timers checked (new callbacks enqueued)
4. Input polled

---

### Phase 8: Test Suite (VALIDATION)
**Create:**
- `tests/concurrency/test_eventqueue.cpp` - Event queue thread-safety
- `tests/concurrency/test_scheduler_goroutines.cpp` - Goroutine parking/unparking
- `scripts/test_go_simple.hv` - Basic go expression
- `scripts/test_channel_simple.hv` - Channel send/recv
- `scripts/test_thread_join_safe.hv` - Verify thread.join() doesn't block
- `scripts/test_sleep_nonblock.hv` - Verify sleep() doesn't block VM
- `scripts/test_concurrent_timers.hv` - Multiple timers fire correctly

**Critical test:**
```havel
// Should NOT freeze the application
let t = go { sleep(1000) }
print("Still responsive")  // Should print immediately
t.join()                   // Should unblock when sleep done
print("Done")
```

---

## Implementation Order

1. ✅ Phase 0: Architecture locked (DONE)
2. → **Phase 1: EventQueue** (NEXT - simplest dependency)
3. → Phase 2: Scheduler refactor
4. → Phase 3: VM integration
5. → Phase 4: Fix thread.join()
6. → Phase 5: Channel recv/send
7. → Phase 6: Timers
8. → Phase 7: Main loop
9. → Phase 8: Tests

Each phase should be compilable and testable separately.

---

## Success Criteria

✅ All phases complete when:
1. No blocking calls in VM thread
2. test_threads_timers.hv runs without freezing
3. All concurrency tests pass
4. No deadlocks detected under stress testing
5. Performance acceptable (one step ≈ 1 microsecond)

---

## Risk Mitigation

| Risk | Mitigation |
|------|-----------|
| VM state corruption | Scheduler/VM in same thread |
| Deadlock on channel | Always suspend, never block |
| Timer precision | Document ±20ms precision limit |
| Memory cleanup | Track goroutines, clean on done |
| Shutdown race | Graceful shutdown in EventLoop |
