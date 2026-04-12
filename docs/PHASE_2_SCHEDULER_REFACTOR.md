# Phase 2: Scheduler Refactor - Design & Migration Guide

## Overview

The current Scheduler implements a Go-style M:N model with OS threads (Machines). This doesn't work for our architecture because:
1. Machines (OS threads) spawn arbitrary `task()` functions
2. No bytecode awareness - can't execute one instruction at a time
3. Processors managing worker threads - not needed for single VM thread
4. Complex synchronization with condition_variables

**New Design:**
- Single-threaded (VM thread drives execution)
- Bytecode-aware (goroutines have function_id + ip for resumption)
- Cooperative scheduling (suspend/unpark model)
- Simple (minimal synchronization needed)

## File Status

**Old:** `/src/havel-lang/runtime/concurrency/Scheduler.hpp` (current)
**New:** `/src/havel-lang/runtime/concurrency/Scheduler_v2.hpp` (refactored design spec)

## Architecture Comparison

### OLD Model (M:N with OS threads)
```
                   Scheduler singleton
                         │
         ┌───────────────┼───────────────┐
         │               │               │
      Machine       Machine          Machine
      (OS thread)   (OS thread)      (OS thread)
         │               │               │
         ├─ Processor    ├─ Processor   ├─ Processor
         │  (P1)         │  (P2)        │  (P3)
         │  │           │  │           │  │
         │  ├─ G1       │  ├─ G4       │  ├─ G7
         │  ├─ G2       │  └─ G5       │  └─ G8
         │  └─ G3       │              │
         │              │              │
         └──────────────┴──────────────┘
                        │
                        ↓
                   Execute task()
                (any function, not bytecode)
```

### NEW Model (Single-threaded, cooperative)
```
          VM Thread (main execution)
                    │
          ┌─────────┴─────────┐
          │                   │
     executeStep()      Scheduler.pickNext()
          │                   │
          ├─ Current G ←──────┘
          │  (running)
          ├─ Execute ONE instr
          ├─ Yield
          │
          └─ Runnable queue
             │
             ├─ G2 (ready)
             ├─ G5 (ready)
             └─ G9 (ready)
             
    Suspended:
    ├─ G1 (waiting on channel)
    ├─ G3 (waiting on sleep)
    └─ G7 (waiting on thread.join)
```

## Key API Changes

### spawn() - NOW takes bytecode function_id

**OLD:**
```cpp
uint32_t spawn(std::function<Value()> task, const std::string& name = "");

// Usage:
scheduler->spawn([]() { 
    print("hello");
    return nullptr;
});
```

**NEW:**
```cpp
uint32_t spawn(uint32_t function_id, const std::vector<Value>& args,
               const std::string& name = "");

// Usage:
scheduler->spawn(my_havel_function_index, {arg1, arg2});
```

Why: Allows resumption at arbitrary bytecode instruction after yield.

### current() - NEW API

**NEW:**
```cpp
Goroutine* current();  // Get currently executing goroutine
```

Used by: VM.executeStep() to know which goroutine to run.

### suspend() - NEW API

**NEW:**
```cpp
void suspend(Goroutine* g, SuspensionReason reason);
```

Reasons:
- `SuspensionReason::ChannelWait` - recv() on empty
- `SuspensionReason::ThreadWait` - thread.join()
- `SuspensionReason::SleepWait` - sleep(ms)
- `SuspensionReason::TimerWait` - timeout/interval wait

### unpark() - NEW API

**NEW:**
```cpp
void unpark(Goroutine* g);  // Wake suspended goroutine
```

Called from event queue callbacks to resume sleeping goroutines.

### pickNext() - REPLACES complex processor logic

**NEW:**
```cpp
Goroutine* pickNext();  // Get next runnable goroutine (or nullptr)
```

Simple: Pops from runnable queue, returns it. Done.

## State Transitions

### OLD
```
Created → Runnable → Running → {Waiting, Done}
```

### NEW (More detailed)
```
Created → Runnable → Running 
   ↓
Created: Just created, waiting for scheduler to make runnable
Runnable: In queue, ready for next VM.executeStep()
Running: Currently executing in VM
Suspended: Parked (waiting on channel/thread/sleep)
Done: Execution finished
   ↑
   └─ unpark() moves from Suspended → Runnable → (VM picks up on next step)
```

## Implementation Steps

### Step 1: Add Scheduler_v2 header (DONE)
- New header with refactored API
- Design document this file

### Step 2: Implement Scheduler_v2.cpp
- [ ] Constructor/destructor
- [ ] spawn() - create new Goroutine with bytecode span
- [ ] current() - return currently executing goroutine
- [ ] pickNext() - return next from runnable queue
- [ ] suspend() - move to suspended map
- [ ] unpark() - move from suspended to runnable

### Step 3: Update VM to use Scheduler_v2
- [ ] Change VM.executeStep() to call scheduler->pickNext()
- [ ] Track current goroutine
- [ ] Call scheduler->suspend() when hitting blocking ops

### Step 4: Keep OLD Scheduler for compatibility
- Old scheduler remains untouched
- Gradually migrate code that uses it
- Eventually deprecate once all uses migrated

### Step 5: Delete OLD Scheduler
- Only after all code migrated
- Clean up M:N related code

## Migration Path

**Phase 1 (Now):** EventQueue (foundation) ✅
**Phase 2 (This):** Add Scheduler_v2 alongside old scheduler
**Phase 3:** VM integration uses new Scheduler_v2
**Phase 4:** thread.join() leverage new API
**Phase 5-7:** Other features use event queue + new scheduler
**Phase 8:** Tests validate new model
**Future:** Remove old scheduler when fully migrated

## Code That Won't Change

These remain the same in new scheduler:

1. **Goroutine struct basics**
   - id, name, state
   - ip, stack, locals (execution state) - ALREADY THERE!

2. **Core concept**
   - Goroutines yield/resume
   - Multiple goroutines in progress
   - Scheduling to fair execution

## Code That WILL Change

1. **No Processor struct** - Removed (was for OS thread management)
2. **No task() function** - Replaced with function_id + bytecode span
3. **No Machines/OS threads in scheduler** - Users provide context
4. **No complex P/M interaction** - Single runnable queue
5. **API simplified** - spawn, current, suspend, unpark, pickNext

## Test Strategy

**Unit tests for new scheduler:**
1. spawn() creates runnable goroutine
2. pickNext() returns in FIFO order
3. suspend() moves to suspended
4. unpark() moves back to runnable
5. Multiple goroutines fair scheduling
6. Complex suspensions/wakeups

**Integration tests:**
1. VM.executeStep() interacts with scheduler correctly
2. Channels wake goroutines via unpark()
3. Timers wake goroutines via unpark()
4. thread.join() parks/unparks correctly

## Risk Mitigation

**Risk:** Bugs in migration
**Mitigation:** Keep old scheduler, test new scheduler side-by-side

**Risk:** Different semantics than Go scheduler
**Mitigation:** Document as "cooperative scheduler", not "M:N scheduler"

**Risk:** Performance regression
**Mitigation:** Profile both, optimize if needed

## Victory Conditions for Phase 2

✅ Scheduler_v2.hpp designed  
✅ Scheduler_v2.cpp implemented  
✅ Unit tests pass (spawn, suspend, unpark, pickNext)  
✅ No OS threads spawned by scheduler  
✅ Bytecode-aware (function_id + ip preserved)  
✅ Single-threaded model ready for VM integration  

## What Comes After Phase 2

With Scheduler_v2 complete:
- Phase 3: VM.executeStep() integration
- Phase 4: Fix thread.join() (now simple with new API)
- Phase 5: Channel suspend/resume
- Phase 6: Timers enqueue via EventQueue
- Phase 7: Main loop integration
- Phase 8: Tests & validation
