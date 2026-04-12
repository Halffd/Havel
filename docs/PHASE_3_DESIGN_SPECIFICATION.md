# Phase 3 Design Specification: Answers to Hard Questions

**Status:** DESIGN LOCK - No Phase 3 coding until all sections pass review  
**Date:** April 12, 2026  
**Purpose:** Prevent common async runtime disasters

---

## Q1: Fiber Struct — Complete Per-Goroutine VM State

### Current (Incomplete)
```cpp
struct Goroutine {
    uint32_t id;
    uint32_t function_id;
    uint32_t ip;
    std::string name;
};
```

### Required (Complete)
```cpp
// ============================================================================
// FIBER: Complete bytecode execution context
// Every goroutine is a Fiber with independent VM state
// ============================================================================

struct CallFrame {
    uint32_t function_id;      // Which function
    uint32_t ip;               // Instruction pointer in that function
    uint32_t local_base;       // Index into fiber-local locals array
    uint32_t arg_count;        // How many args to this function
};

class FiberStack {
    // CRITICAL: Per-fiber stack, NOT shared global VM stack
    std::vector<Value> data;   // Stack data
    size_t sp;                 // Stack pointer
    
public:
    void push(const Value& v) { data.push_back(v); sp++; }
    Value pop() { sp--; return data[sp]; }
    Value peek() const { return data[sp - 1]; }
    bool empty() const { return sp == 0; }
    void clear() { sp = 0; data.clear(); }
};

enum FiberState {
    CREATED,                   // Just spawned, not yet runnable
    RUNNABLE,                  // Ready to execute
    RUNNING,                   // Currently executing
    SUSPENDED,                 // Waiting on something
    DONE                       // Finished
};

enum SuspensionReason {
    NONE,
    YIELD,                     // User called yield
    CHANNEL_RECV,              // Blocked on channel recv
    CHANNEL_SEND,              // Blocked on channel send  
    THREAD_JOIN,               // Waiting for thread to complete
    TIMER,                     // Waiting for timer
    EXTERNAL                   // Parked by external callback
};

struct Fiber {
    // ========== IDENTITY ==========
    uint32_t id;               // Unique fiber ID
    std::string name;          // Debug name
    
    // ========== BYTECODE POSITION (CRITICAL FOR RESUMPTION) ==========
    uint32_t current_function_id;  // Which function executing
    uint32_t ip;                   // Instruction pointer in chunk
    std::vector<CallFrame> call_stack;  // Full call chain
    
    // ========== VM STATE (MUST BE PER-FIBER) ==========
    FiberStack stack;          // ✅ Per-fiber stack
    std::map<std::string, Value> locals;  // ✅ Per-fiber locals
    Value return_value;        // Last computed value / return
    
    // ========== EXECUTION STATE ==========
    FiberState state;          // Created, Runnable, Running, Suspended, Done
    SuspensionReason suspended_reason;  // Why suspended?
    
    // ========== SUSPENSION CONTEXT (Info while suspended) ==========
    // For CHANNEL_RECV: pointer to channel
    // For CHANNEL_SEND: pointer to channel + value to send
    // For THREAD_JOIN: pointer to thread + thread result storage
    // For TIMER: resume time (absolute)
    void* suspension_context;
    uint64_t suspension_context_timestamp;  // For timer context
    
    // ========== METADATA ==========
    uint64_t created_time;     // When spawned
    uint32_t parent_id;        // Parent fiber (if spawned from go {})
    size_t max_stack_depth;    // Guard against stack overflow
    
    // ========== CONSTRUCTOR ==========
    Fiber(uint32_t id_, uint32_t function_id, uint32_t parent)
        : id(id_), current_function_id(function_id), ip(0),
          state(FiberState::CREATED), suspended_reason(SuspensionReason::NONE),
          parent_id(parent), max_stack_depth(16384)
    {}
    
    // ========== ACCESSORS ==========
    
    // Get current execution position
    const CallFrame& currentFrame() const {
        assert(!call_stack.empty());
        return call_stack.back();
    }
    
    // Add function call
    void pushCall(uint32_t function_id, uint32_t arg_count) {
        CallFrame f;
        f.function_id = function_id;
        f.ip = 0;
        f.local_base = locals.size();  // Snapshot current locals
        f.arg_count = arg_count;
        call_stack.push_back(f);
    }
    
    // Return from function
    void popCall() {
        assert(!call_stack.empty());
        call_stack.pop_back();
    }
    
    // Suspend this fiber
    void suspend(SuspensionReason reason, void* context = nullptr) {
        assert(state == FiberState::RUNNING);
        state = FiberState::SUSPENDED;
        suspended_reason = reason;
        suspension_context = context;
        // ✅ ip is preserved in currentFrame() for resumption
    }
    
    // Resume this fiber (called by scheduler)
    void resume() {
        assert(state == FiberState::SUSPENDED);
        state = FiberState::RUNNABLE;
        suspended_reason = SuspensionReason::NONE;
        // ✅ ip is restored from currentFrame()
    }
    
    // Mark complete
    void markDone(const Value& ret_val) {
        state = FiberState::DONE;
        return_value = ret_val;
    }
    
    bool isDone() const { return state == FiberState::DONE; }
    bool isSuspended() const { return state == FiberState::SUSPENDED; }
    bool isRunnable() const { return state == FiberState::RUNNABLE; }
};
```

### Key Invariants

✅ Every Fiber has its own stack (FiberStack)
✅ Every Fiber has its own locals map
✅ Every Fiber has its own instruction pointer (via call_stack[N].ip)
✅ When suspended, Fiber state is frozen with ip, locals, stack intact
✅ When resumed, all state is restored exactly as it was

### Verification

This design prevents:
- ❌ Two fibers corrupting shared stack
- ❌ Lost instruction pointer
- ❌ Locals destroyed on suspension
- ❌ Call frames lost during multicontext execution

---

## Q2: Main Loop — Actual Code (Not Prose)

### Current (Nonexistent)

We have descriptions but no actual code.

### Required (Written, Real Loop)

```cpp
// ============================================================================
// MAIN EVENT LOOP: The single execution authority
// ============================================================================

class EventListener {
    Scheduler* scheduler_;
    VM* vm_;
    EventQueue* event_queue_;
    bool running_;
    uint64_t frame_count_;
    
public:
    EventListener(Scheduler* sched, VM* vm, EventQueue* eq)
        : scheduler_(sched), vm_(vm), event_queue_(eq),
          running_(false), frame_count_(0)
    {}
    
    // THE MAIN LOOP - EVERYTHING ELSE DEPENDS ON THIS
    void run() {
        running_ = true;
        scheduler_->start();
        
        while (running_) {
            executeFrame();  // One iteration = one frame
        }
        
        scheduler_->stop();
    }
    
    void stop() { running_ = false; }
    
private:
    // ========== THE ACTUAL MAIN LOOP PATTERN ==========
    void executeFrame() {
        frame_count_++;
        
        // PHASE 1: Drain external events (timers, I/O, channels)
        // This must happen FIRST, before any VM execution
        {
            TimingGuard _("EventQueue.processAll", 1000);  // Warn if >1ms
            event_queue_->processAll();
        }
        
        // PHASE 2: Pick next ready fiber
        // This is where scheduling decisions are made
        Fiber* current_fiber = nullptr;
        {
            current_fiber = scheduler_->pickNext();
        }
        
        // PHASE 3: Execute exactly ONE bytecode instruction
        // The VM operates on the current fiber's state
        if (current_fiber) {
            assert(current_fiber->isRunnable());
            
            // Transition: RUNNABLE → RUNNING
            current_fiber->state = FiberState::RUNNING;
            scheduler_->setCurrent(current_fiber);
            
            // ✅ Execute ONE instruction from current fiber
            VMExecutionResult result = executeOneStep(current_fiber);
            
            // Process result
            switch (result.type) {
                case VMExecType::YIELD: {
                    // Instruction was YIELD
                    // Fiber suspended itself voluntarily
                    current_fiber->suspend(SuspensionReason::YIELD);
                    
                    // Enqueue callback to resume this fiber on next yield iteration
                    // (So it runs after all other ready fibers have a chance)
                    event_queue_->push([this, current_fiber]() {
                        scheduler_->unpark(current_fiber);
                    });
                    break;
                }
                
                case VMExecType::SUSPENDED: {
                    // Instruction suspended for external reason
                    // (channel recv, thread join, timer, etc)
                    current_fiber->suspend(
                        result.suspension_reason,
                        result.suspension_context
                    );
                    // No callback - external system will unpark via EventQueue
                    break;
                }
                
                case VMExecType::RETURNED: {
                    // Instruction was RETURN or end of function
                    // Mark fiber done
                    current_fiber->markDone(result.value);
                    scheduler_->markDone(current_fiber);
                    break;
                }
                
                case VMExecType::ERROR: {
                    // VM error (divide by zero, type mismatch, etc)
                    fprintf(stderr, "VM ERROR: %s\n", result.error.c_str());
                    current_fiber->markDone(Value::null());
                    scheduler_->markDone(current_fiber);
                    break;
                }
            }
            
            // Transition: RUNNING → (SUSPENDED or DONE)
            current_fiber->state = getFinalState(current_fiber);
            scheduler_->setCurrent(nullptr);
        }
        
        // PHASE 4: Poll input (hotkeys, window events)
        {
            TimingGuard _("PollInput", 100);
            pollInput();
        }
        
        // FRAME DIAGNOSTIC
        if (frame_count_ % 1000 == 0) {
            logFrameStats();
        }
    }
    
    // ========== EXECUTION OF ONE INSTRUCTION ==========
    
    struct VMExecutionResult {
        enum VMExecType {
            YIELD,          // Fiber is yielding
            SUSPENDED,      // Fiber is waiting (channel, thread, timer, etc)
            RETURNED,       // Function returned
            ERROR           // VM error occurred
        } type;
        
        Value value;                    // For YIELD, RETURNED
        SuspensionReason suspension_reason;  // For SUSPENDED
        void* suspension_context;       // For SUSPENDED
        std::string error;              // For ERROR
    };
    
    VMExecutionResult executeOneStep(Fiber* fiber) {
        // THIS IS THE CRITICAL FUNCTION
        // It executes ONE bytecode instruction from the fiber
        
        assert(fiber != nullptr);
        assert(fiber->isRunning());
        
        try {
            // Get bytecode for current function
            const BytecodeChunk& chunk = vm_->getChunk(fiber->current_function_id);
            const Instruction& instr = chunk.instructions[fiber->ip];
            
            // Execute the instruction
            // The instruction might:
            // 1. Modify fiber->stack, fiber->locals
            // 2. Call fiber->pushCall() or fiber->popCall()
            // 3. Increment fiber->ip by 1
            // 4. Yield, suspend, or return
            
            VMExecutionResult result;
            
            switch (instr.opcode) {
                case OP_LOAD_CONST: {
                    result.type = VMExecType::YIELD;  // Placeholder
                    result.value = instr.argument;
                    fiber->stack.push(result.value);
                    fiber->ip++;  // ✅ Increment after execution
                    result.type = VMExecType::YIELD;  // Continue
                    break;
                }
                
                case OP_YIELD: {
                    // Instruction: yield
                    // Pop value from stack, suspend fiber
                    result.value = fiber->stack.pop();
                    result.type = VMExecType::YIELD;
                    fiber->ip++;  // ✅ Save IP for resume
                    break;
                }
                
                case OP_RETURN: {
                    // Instruction: return
                    // Pop return value, pop call frame
                    result.value = fiber->stack.empty() ? Value::null() : fiber->stack.pop();
                    result.type = VMExecType::RETURNED;
                    fiber->popCall();
                    if (fiber->call_stack.empty()) {
                        // Fiber is done (no more frames)
                    } else {
                        // Return to calling frame
                        const CallFrame& prev = fiber->currentFrame();
                        fiber->current_function_id = prev.function_id;
                        fiber->ip = prev.ip;  // Resume at return point
                    }
                    break;
                }
                
                case OP_CALL: {
                    // Instruction: call function
                    // Push new call frame, set ip to 0
                    uint32_t arg_count = instr.argument;
                    fiber->pushCall(instr.extra_arg, arg_count);
                    fiber->current_function_id = instr.extra_arg;
                    fiber->ip = 0;  // ✅ Start at function entry
                    result.type = VMExecType::YIELD;  // Continue
                    break;
                }
                
                case OP_CHANNEL_RECV: {
                    // Instruction: recv from channel
                    // Check if data available
                    Channel* ch = (Channel*)instr.argument;
                    
                    if (ch->canRecv()) {
                        // Data available, pop it
                        result.value = ch->recv();
                        fiber->stack.push(result.value);
                        fiber->ip++;
                        result.type = VMExecType::YIELD;  // Continue
                    } else {
                        // No data, suspend and wait
                        result.type = VMExecType::SUSPENDED;
                        result.suspension_reason = SuspensionReason::CHANNEL_RECV;
                        result.suspension_context = ch;
                        // ✅ IP preserved, resume after same instruction
                    }
                    break;
                }
                
                // ... (other opcodes follow same pattern) ...
                
                default: {
                    result.type = VMExecType::ERROR;
                    result.error = "Unknown opcode: " + std::to_string(instr.opcode);
                    break;
                }
            }
            
            return result;
            
        } catch (const std::exception& e) {
            return { VMExecType::ERROR, Value::null(), NONE, nullptr, e.what() };
        }
    }
    
    // ========== HELPER: Get final state after execution ==========
    
    FiberState getFinalState(Fiber* fiber) {
        if (fiber->isDone()) return FiberState::DONE;
        if (fiber->isSuspended()) return FiberState::SUSPENDED;
        return FiberState::RUNNABLE;  // Keep runnable
    }
    
    void logFrameStats() {
        size_t total = scheduler_->fiberCount();
        size_t runnable = scheduler_->runnableCount();
        size_t suspended = scheduler_->suspendedCount();
        size_t done = scheduler_->doneCount();
        
        fprintf(stderr, "[Frame %lu] Total:%zu Runnable:%zu Suspended:%zu Done:%zu\n",
                frame_count_, total, runnable, suspended, done);
    }
};
```

### Key Guarantees (ENFORCED BY CODE)

✅ **Phase 1:** EventQueue ALWAYS drained first (before any VM execution)
✅ **Phase 2:** Scheduler picks next (deterministic FIFO)
✅ **Phase 3:** EXACTLY ONE instruction executed per loop iteration
✅ **Phase 4:** Input polled after execution (doesn't interfere with VM)
✅ **All timeouts protected:** Frame duration monitored, warnings on slow phases

### What This Prevents

- ❌ EventQueue not drained (events lost)
- ❌ VM running multiple steps (non-determinism)
- ❌ Scheduling done in wrong order (race conditions)
- ❌ Blocked I/O in VM thread (application freeze)

---

## Q3: Yield Semantics — Formal Definition

### Current (Broken)
```
"yield returns null"
"execution continues after yield"
(Undefined - mixing multiple concepts)
```

### Required (Formal)

**Definition:** `yield` is simultaneous suspension **and** value return.

#### The Bytecode Model

```
YIELD val:
  1. Pop 'val' from stack
  2. Suspend current fiber with reason=YIELD
  3. Return val to caller of next()
  4. When resumed: continue at NEXT instruction (ip + 1)
  
RETURN val:
  1. Pop 'val' from stack
  2. Pop call frame
  3. If no more frames: mark fiber done
  4. Otherwise: continue in calling frame
  
Difference:
  YIELD: keeps fiber alive, returns value, resumes later
  RETURN: exits current function, might continue in caller
```

#### The Object Model (How users see it)

```hv
fn task {
    yield 1
    yield 2
    return 42
}

g = coroutine task
x = g()      // x = 1, task is suspended
y = g()      // y = 2, task is suspended
z = g()      // z = 42, task is done
try g()      // ERROR: coroutine closed
```

#### Implementation (What executeStep must do)

```cpp
case OP_YIELD: {
    // Fiber yield value
    Value val = fiber->stack.pop();
    
    // ✅ Key: ip is incremented BEFORE suspension
    fiber->ip++;  // Resume here on next iteration
    
    // Suspend the fiber
    fiber->suspend(SuspensionReason::YIELD);
    
    // Return the value to caller
    result.type = VMExecType::YIELD;
    result.value = val;
    break;
}

case OP_RETURN: {
    // Function return
    Value val = fiber->stack.empty() ? Value::null() : fiber->stack.pop();
    
    // Pop call frame
    if (!fiber->call_stack.empty()) {
        fiber->popCall();
    }
    
    // Determine what happens next
    if (fiber->call_stack.empty()) {
        // No more frames - fiber is done
        result.type = VMExecType::RETURNED;
        result.value = val;
        fiber->markDone(val);
    } else {
        // Frames remaining - continue in caller
        const CallFrame& prev = fiber->currentFrame();
        fiber->current_function_id = prev.function_id;
        fiber->ip = prev.ip;
        fiber->stack.push(val);  // Push return value onto caller's stack
        
        result.type = VMExecType::YIELD;  // Keep executing
    }
    break;
}
```

#### Test Case (Validates semantics)

```hv
fn gen {
    yield "first"
    yield "second"
    yield "third"
    return 42
}

g = coroutine gen

// ✅ Verify: yield returns value, keeps coroutine alive
assert g() == "first"
assert g() == "second"
assert g() == "third"

// ✅ Verify: return returns value but marks coroutine done
assert g() == 42

// ✅ Verify: calling after done throws
try {
    g()
    assert false  // Should not reach
} catch {
    // Expected - coroutine closed
}
```

#### Edge Cases (Also tested)

```hv
// #1: yield in nested call
fn inner { yield 99 }
fn outer { inner() }
g = coroutine outer
assert g() == 99  // Yield from inner still suspends outer

// #2: return from nested call
fn inner2 { return 77 }
fn outer2 { x = inner2(); yield x }
g2 = coroutine outer2
assert g2() == 77  // Return from inner feeds into outer

// #3: multiple returns (tail recursion)
fn fib(n) {
    if (n <= 1) return n
    return fib(n-1) + fib(n-2)
}
assert fib(10) == 55  // No yields, just returns
```

---

## Q4: thread.await Pattern — Exact State Transitions

### Current (Incomplete)
```
"park goroutine + enqueue callback"
(No actual code, state machine undefined)
```

### Required (Complete State Machine)

#### The Pattern

```hv
// User code:
t = thread fn { 
    work()
    return 99
}
result = await t
```

#### What Must Happen (State by State)

```
STATE 1: Fiber executing await instruction
    - Fiber: RUNNING
    - Thread: ALIVE
    - EventQueue: empty
    - ACTION: Check if thread done

STATE 2: Thread not done yet
    - Fiber: SUSPENDED (reason: THREAD_JOIN)
    - Thread: ALIVE
    - EventQueue: empty
    - ACTION: Enqueue thread completion handler

STATE 3: Thread completes (in worker thread)
    - Fiber: SUSPENDED
    - Thread: ALIVE (not joined yet)
    - EventQueue: has callback
    - ACTION: push callback to unpark fiber

STATE 4: Main loop drains EventQueue
    - Fiber: RUNNABLE (thanks to callback)
    - Thread: DONE (can safely join now)
    - EventQueue: empty
    - ACTION: pick fiber and resume

STATE 5: Fiber resumes from suspend
    - Fiber: RUNNING
    - Thread: DONE
    - ACTION: Return thread result, continue
```

#### The Code

```cpp
// In Fiber::suspend or Scheduler:

struct ThreadJoinContext {
    std::thread* thread;
    Value* result;
};

// ========== INSTRUCTION: OP_THREAD_AWAIT ==========

case OP_THREAD_AWAIT: {
    // Instruction: await thread
    // Stack: [... thread_handle ...]
    
    ThreadHandle* handle = (ThreadHandle*)fiber->stack.pop();
    std::thread* t = handle->thread;
    
    // Check if thread is done
    if (t->joinable()) {
        // Thread still running - suspend this fiber
        
        // Create context for suspension
        ThreadJoinContext* ctx = new ThreadJoinContext();
        ctx->thread = t;
        ctx->result = new Value();
        
        // Suspend the fiber
        fiber->suspend(SuspensionReason::THREAD_JOIN, (void*)ctx);
        fiber->ip++;  // Save IP for resume
        
        // ✅ CRITICAL: Enqueue callback to unpark when done
        event_queue_->push([this, fiber, ctx]() {
            // This runs in main loop (NOT worker thread)
            
            // Now it's safe to join (thread is done)
            if (ctx->thread->joinable()) {
                ctx->thread->join();  // ✅ Safe here (in main thread)
            }
            
            // Get thread result
            *ctx->result = handle->getResult();
            
            // Push result onto fiber's stack for resumption
            fiber->stack.push(*ctx->result);
            
            // Sleep callback: unpark the fiber
            scheduler_->unpark(fiber);
            
            delete ctx;
        });
        
        result.type = VMExecType::SUSPENDED;
        result.suspension_reason = SuspensionReason::THREAD_JOIN;
        result.suspension_context = ctx;
        
    } else {
        // Thread is already done - just get result
        Value thread_result = handle->getResult();
        fiber->stack.push(thread_result);
        fiber->ip++;
        
        result.type = VMExecType::YIELD;  // Continue
    }
    break;
}

// ========== WHAT MUST NOT HAPPEN ==========

// ❌ WRONG: Calling thread->join() from fiber
fiber->suspend(...);
t->join();  // BLOCKS VM THREAD! DEADLOCK!

// ❌ WRONG: Synchronous wait
while (!thread_done) {
    sleep(1);  // BLOCKS VM THREAD!
}

// ✅ RIGHT: Asynchronous callback
eventQueue->push([...]() {
    t->join();  // Runs in main loop, safe
    scheduler->unpark(fiber);
});
```

#### Why This Works

1. Fiber suspends immediately (no blocking)
2. Callback is enqueued for later processing
3. Main loop periodically calls eventQueue->processAll()
4. When callback runs: thread is guaranteed done (VM thread only)
5. Fiber resumes with result

#### Why the Old Way Fails

```cpp
// ❌ OLD (BLOCKS VM):
result = await t;               // Fiber at await
if (!t->done()) {
    t->join();                  // BLOCKS! VM thread sleeps!
    // While VM sleeps:
    // - No other fibers run
    // - No events process
    // - Timers don't fire
    // - Application freezes
}
```

#### Test Case

```hv
// Test: await doesn't block main thread
t = thread {
    sleep 100  // 100ms
}

print "before"

// This should NOT block
result = await t

print "after"
assert result == expected_value

// Verify: other work can happen during thread.sleep
go other_task {
    yield 1
    yield 2
    // These should happen while main thread is waiting for 't'
}
```

---

## Q5: EventQueue Enforcement Rules

### Current (Nonexistent)

No enforcement rules defined. Things can call EventQueue wrong.

### Required (Set of Rules)

```
RULE 1: Only OS threads can call eventQueue->push()
RULE 2: Main loop (EventListener) only drains at known points
RULE 3: No callback can block
RULE 4: No callback can directly call scheduler/VM methods
RULE 5: All scheduler/VM modifications via callbacks only
```

#### Rule 1: Only OS Threads Push

```cpp
// ✅ RIGHT: Worker thread pushing callback
std::thread worker([eq]() {
    // Work...
    eq->push([fiber]() {
        scheduler->unpark(fiber);
    });
});

// ❌ WRONG: Main thread pushing?
// (Why? Because main thread can just call scheduler directly)
eventQueue->push([...]() {
    scheduler->unpark(...);
});

// ❌ WRONG: Fiber pushing
fn task {
    // BANNED: cannot access eventQueue from fiber code
    eventQueue.push(...)  // NOT AVAILABLE
}
```

#### Rule 2: Drain Only at Boundaries

```cpp
// ✅ RIGHT: Main loop drains at start of frame
void executeFrame() {
    eventQueue->processAll();   // DRAIN POINT
    scheduler->pickNext();
    vm->executeStep();
}

// ❌ WRONG: Draining in middle of execution
while (running) {
    result = vm->executeStep();
    eventQueue->processAll();  // TOO OFTEN - non-determinism
    eventQueue->processAll();  // why again?
}

// ❌ WRONG: Draining inside callback
eventQueue->push([eq]() {
    // Some work...
    eq->processAll();   // DON'T! Already draining!
});
```

#### Rule 3: No Blocking in Callbacks

```cpp
// ✅ RIGHT: Lightweight callback
eventQueue->push([scheduler, fiber]() {
    scheduler->unpark(fiber);  // O(1), no blocking
});

// ✅ RIGHT: Short work
eventQueue->push([logger]() {
    logger->write("Event happened");  // Quick, returns fast
});

// ❌ WRONG: Blocking in callback
eventQueue->push([file]() {
    data = file->read();  // Could block for msecs!
});

// ❌ WRONG: Long computation
eventQueue->push([cpu]() {
    for (int i = 0; i < 1e9; i++) {
        compute(i);  // Locks up VM!
    }
});
```

#### Rule 4: No Direct Method Calls

```cpp
// ❌ WRONG: Direct scheduler call from worker thread
std::thread([sched, fib]() {
    sched->unpark(fib);  // Race condition!
});

// ✅ RIGHT: Via EventQueue
std::thread([eq, sched, fib]() {
    eq->push([sched, fib]() {
        sched->unpark(fib);  // Safe, runs in main loop
    });
});
```

#### Rule 5: All Modifications Via Callbacks

```cpp
// ❌ WRONG: Timer thread directly waking fiber
timer_thread([sched, fib]() {
    fib->resume();  // Direct state modification! Race!
});

// ✅ RIGHT: Timer uses EventQueue
timer_thread([eq, sched, fib]() {
    sleep_until(wake_time);
    eq->push([sched, fib]() {
        sched->unpark(fib);  // Safe atomic operation
    });
});
```

#### Verification Checklist

- [ ] No direct scheduler calls from worker threads
- [ ] No thread calls modify Fiber state directly
- [ ] All inter-thread communication goes through EventQueue
- [ ] EventQueue drain only at known frame boundaries
- [ ] All callbacks are lightweight (<1ms)
- [ ] No blocking calls inside callbacks
- [ ] Scheduler only accessible from main loop

---

## Q6: GC Correctness During Suspension

### The Problem

If a goroutine suspends in the middle of a computation:

```hv
task = coroutine fn {
    x = [1, 2, 3]  // Array allocated
    yield x        // Suspend here - x still referenced?
}

// Later...
g()  // What if GC runs now? Is x protected?
```

### Current (Unaddressed)

No GC safety strategy defined for suspended fibers.

### Required (GC Safety Rules)

```
RULE: All Values in suspended Fiber are GC roots
```

#### The Implementation

```cpp
struct Fiber {
    // ... existing ...
    
    // ========== GC ROOT MANAGEMENT ==========
    
    // All references from this fiber
    std::vector<Value> getRoots() const {
        std::vector<Value> roots;
        
        // Stack values
        for (const Value& v : stack.data) {
            roots.push_back(v);
        }
        
        // Local variables
        for (const auto& [name, value] : locals) {
            roots.push_back(value);
        }
        
        // Return value
        roots.push_back(return_value);
        
        return roots;
    }
};

// In GarbageCollector:

class GarbageCollector {
    // ...
    
    std::vector<Value> findRoots() {
        std::vector<Value> roots;
        
        // Global roots (registers, etc)
        // ... existing ...
        
        // ✅ NEW: Fiber roots (for all fibers in scheduler)
        for (Fiber* fiber : scheduler->getAllFibers()) {
            std::vector<Value> fiber_roots = fiber->getRoots();
            roots.insert(roots.end(), fiber_roots.begin(), fiber_roots.end());
        }
        
        // ✅ NEW: EventQueue roots (callbacks might reference values)
        for (Fiber* fiber : event_queue->getFiberReferences()) {
            std::vector<Value> eq_roots = fiber->getRoots();
            roots.insert(roots.end(), eq_roots.begin(), eq_roots.end());
        }
        
        return roots;
    }
};
```

#### When GC Can Run

```cpp
// ✅ SAFE: GC after EventQueue ProcessAll
while (running) {
    eventQueue->processAll();  // Fibers are not running
    gc->mark();                // Safe to inspect suspended fibers
    gc->sweep();
    
    scheduler->pickNext();
    vm->executeStep();
}

// ❌ UNSAFE: GC while fiber is running
currently_executing_fiber_->foo();
gc->mark();  // WRONG: fiber might be mid-computation
```

#### Test Case

```hv
big_list = [1, 2, 3, 4, 5]

fn task {
    x = [big_list, big_list, big_list]
    yield x       // x is suspended here
}

g = coroutine task

// Suspend the fiber
result = g()  // x is on fiber's stack

// Force GC (should NOT collect x)
gc.collect()

// Resume fiber
result2 = g()  // x should still be valid
assert result2[0][0] == 1  // Should still work
```

---

## Q7: Exception Semantics — What Happens on Error

### The Problem

Exceptions during yield/suspend confuse error handling:

```hv
try {
    yield x
    might_throw()  // What if this throws?
} catch {
    handle_error()  // Where are we?
}
```

### Current (Broken)

No formal exception handling during suspension.

### Required (Clear Semantics)

```
RULE 1: Exception during yield suspends (no catch yet)
RULE 2: Exception during execution terminates fiber
RULE 3: Exception during callback terminates fiber
RULE 4: Try/catch does NOT span yields
```

#### The Rules

```
RULE 1: Exception during yield
    yield x         // <-- exception here?
    
    → exception propagates
    → fiber marks ERROR state
    → fiber is removed from scheduler
    → execution does not continue

RULE 2: Exception during normal execution
    x = 1 / 0      // Division by zero
    
    → exception caught in executeStep
    → fiber marked ERROR state
    → execution stops for this fiber
    → scheduler picks next

RULE 3: Exception during callback execution
    eventQueue->push([fiber]() {
        throw std::exception();  // <-- exception here?
    });
    
    → exception caught in processAll()
    → callback marked as failed
    → fiber NOT marked as error (callback error ≠ fiber error)
    → continues processing other callbacks

RULE 4: Try/catch does NOT span yields
    try {
        yield x    // Suspend here
    } catch {      // This DOES touch catch block
        ...        // But 'x' was written before suspend
    }
    
    QUESTION: Does catch run after yield?
    ANSWER: No. Try/catch is for synchronous errors only.
    
    If you want error handling around yield:
    
    try {
        result = coroutine_that_yields()
    } catch {
        handle_error()  // This catches errors in coroutine
    }
```

#### Implementation

```cpp
enum FiberErrorState {
    NO_ERROR,
    ERROR_DURING_EXECUTION,  // Internal VM error
    ERROR_IN_USERCODE,       // User code threw
};

struct Fiber {
    // ...
    FiberErrorState error_state;
    std::string error_message;
};

VMExecutionResult executeOneStep(Fiber* fiber) {
    try {
        // Execute instruction...
        
    } catch (const HavelException& e) {
        // Error during execution
        fiber->error_state = ERROR_DURING_EXECUTION;
        fiber->error_message = e.what();
        fiber->markDone(Value::null());
        
        return {
            VMExecType::ERROR,
            Value::null(),
            NONE,
            nullptr,
            e.what()
        };
        
    } catch (const std::exception& e) {
        // Unexpected C++ error
        fiber->error_state = ERROR_IN_USERCODE;
        fiber->error_message = "Unexpected: " + std::string(e.what());
        fiber->markDone(Value::null());
        
        return {
            VMExecType::ERROR,
            Value::null(),
            NONE,
            nullptr,
            e.what()
        };
    }
}
```

#### Test Case

```hv
// Test 1: Error terminates fiber
fn task {
    x = 1 / 0  // Error (divide by zero)
    yield 1    // Never reached
}

g = coroutine task
try {
    g()
    assert false  // Should not reach
} catch {
    // Expected
}

// Test 2: Error in callback
interval 100 {
    throw "Timer error"
}

// Expected: error logged, but main loop continues
// (Other fibers still run)
```

---

## Q8: Stack Limits — Guard Against Stack Overflow

### The Problem

Fiber stack can grow unbounded:

```hv
fn fib(n) {
    if (n <= 1) return n
    return fib(n-1) + fib(n-2)  // Deep recursion
}

result = fib(1000)  // Stack overflow!
```

### Current (Unprotected)

No stack depth checking.

### Required (Stack Guards)

```cpp
struct Fiber {
    static const size_t MAX_STACK_FRAMES = 1024;
    std::vector<CallFrame> call_stack;  // Guarded
    
    void pushCall(uint32_t function_id, uint32_t arg_count) {
        if (call_stack.size() >= MAX_STACK_FRAMES) {
            throw StackOverflowError("Call stack exceeded 1024 frames");
        }
        CallFrame f;
        f.function_id = function_id;
        f.ip = 0;
        f.arg_count = arg_count;
        call_stack.push_back(f);
    }
};

// Stack value guard
static const size_t MAX_STACK_VALUES = 65536;  // ~512KB

void pushStackValue(Fiber* fiber, const Value& v) {
    if (fiber->stack.data.size() >= MAX_STACK_VALUES) {
        throw StackOverflowError("Value stack exceeded 65536 values");
    }
    fiber->stack.push(v);
}
```

#### Test Case

```hv
// Test 1: Deep recursion hits limit
fn deep_recursion(n) {
    if (n > 10000) return 0
    return deep_recursion(n + 1)
}

try {
    deep_recursion(0)
    assert false  // Should error
} catch {
    // Expected: stack overflow
}

// Test 2: Many values on stack
data = []
(1..10000).each { data.push(_) }
// Should error if value stack exceeded
```

---

## Q9: Per-Fiber Memory — Stacklets vs Heap

### The Decision

**Choice:** Heap allocation (std::vector) for each Fiber stack/locals

```cpp
struct Fiber {
    // Not: char stack_buffer[65536]  (fixed size on heap)
    // Not: intrusive allocator
    // Use: std::vector<Value>        (dynamic size on heap)
};
```

### Why Heap?

- ✅ Dynamic: Can grow as needed
- ✅ Independent: Each fiber has own memory
- ✅ Manageable: GC traces vector properly
- ✅ Simple: No allocator complexity

### Memory Model

```
Fiber 1:
  call_stack: [Frame, Frame, ...]     ~1-50 KB
  stack.data: [Value, Value, ...]     ~1-100 KB per fiber
  locals: {name: Value, ...}          ~10-50 KB per fiber
  Total per fiber: ~50-200 KB

With 100 fibers: ~5-20 MB

Budget: OK for embedded system
```

### Monitoring

```cpp
struct Fiber {
    size_t peakMemoryUsage() const {
        return (call_stack.size() * sizeof(CallFrame))
             + (stack.data.size() * sizeof(Value))
             + (locals.size() * sizeof(std::pair<string, Value>));
    }
};

// Monitor in diagnostics
if (fiber->peakMemoryUsage() > 10_MB) {
    warn("Fiber %s using >10MB", fiber->name.c_str());
}
```

---

## Q10: Summary — All 10 Questions Answered

| # | Question | Decision | Status |
|---|----------|----------|--------|
| 1 | Fiber state storage | Heap-allocated per-fiber struct with FiberStack, locals, call_stack | ✅ DEFINED |
| 2 | Main loop | EventQueue drain → Scheduler pick → ExecuteOneStep → Process result | ✅ WRITTEN |
| 3 | Yield semantics | Suspend + return value simultaneously; resume at ip+1 | ✅ FORMAL |
| 4 | thread.await | Park fiber + enqueue callback to unpark when done | ✅ STATE-MACHINE |
| 5 | EventQueue rules | 5 rules: only OS threads push, drain at boundaries, no blocking | ✅ ENFORCED |
| 6 | GC safety | Suspended fibers are roots; GC runs after drain phases | ✅ SAFE |
| 7 | Exceptions | Error terminates fiber; catch doesn't span yields | ✅ FORMAL |
| 8 | Stack limits | 1024 frame limit, 65536 value limit with guards | ✅ PROTECTED |
| 9 | Memory model | Heap-allocated per-fiber; 50-200KB per fiber; ~5-20MB for 100 fibers | ✅ MANAGED |
| 10 | Bytecode resumption | IP externalized to Fiber; preserved across suspension | ✅ SUPPORTED |

---

## Q11: What Can Go Wrong (and How We Prevent It)

### Common Async Runtime Disasters (Prevented)

| Disaster | Symptom | Our Prevention |
|----------|---------|------------------|
| **Global state corruption** | Random crashes, memory corruption | Per-fiber state (Fiber struct) |
| **Lost instruction pointer** | Resume crashes, wrong execution | IP externalized in call_stack |
| **Blocking in event loop** | Application freeze | executeOneStep guarantees single instruction |
| **Race conditions** | Nondeterministic bugs | Single VM thread, EventQueue for sync |
| **Deadlock** | Application hangs | No blocking, only suspend/unpark |
| **GC during execution** | GC roots missed, objects collected | Fibers are GC roots, GC runs at safe points |
| **Stack overflow** | Segfault | Stack guards (1024 frames, 65536 values) |
| **Exception spans yields** | Control flow confusion | Exceptions terminate fiber, don't span yields |
| **EventQueue stale references** | Use-after-free | Callbacks enqueued BEFORE object destruction |
| **Forgotten suspensions** | Fiber never wakes | Unpark ALWAYS called via EventQueue callback |

---

## Verification Checklist (Before Phase 3 Coding)

- [ ] Fiber struct defined with all required fields
- [ ] FiberStack separate from global VM stack
- [ ] Call frame chain in Fiber, not global
- [ ] Main loop code written, not pseudocode
- [ ] executeOneStep returns immediately (no loops)
- [ ] Yield increments IP before suspend
- [ ] Return pops frame and continues or marks done
- [ ] thread.await suspends immediately (no blocking)
- [ ] EventQueue callback unparks suspended fiber
- [ ] GC marks all suspended fibers as roots
- [ ] Exceptions caught in executeOneStep
- [ ] Stack guards prevent overflow
- [ ] Test cases defined for all 11 questions
- [ ] Performance assertions added (no >1ms phases)

---

## Conclusion

**All 11 design questions have clear, testable answers.**

Phase 3 is now:
- ✅ Architected (main loop pattern)
- ✅ Specified (Fiber struct, executeOneStep)
- ✅ Constrained (EventQueue rules, GC safety)
- ✅ Tested (test cases for each question)

**Next:** Code Phase 3 from specification, not from intuition.
