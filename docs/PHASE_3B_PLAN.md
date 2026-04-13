# Phase 3B: Integration and Fiber-Aware Execution

## Goal
Make the coroutine tests pass by implementing:
1. Fiber-aware instruction execution
2. Fiber state synchronization with VM
3. Yield operation handling
4. Fiber resumption from suspension points
5. Scheduler integration

## Status: Integrating existing Fiber & ExecutionEngine infrastructure

## Current Codebase State

**What Already Exists:**
- ✅ `Fiber` class (Fiber.hpp) - Complete per-fiber state (stack, locals, call frames)
- ✅ `Scheduler` - Manages multiple goroutines in runnable/suspended states
- ✅ `ExecutionEngine` - Skeleton, needs implementation
- ✅ `VMExecutionResult` enum - YIELD, SUSPENDED, RETURNED, ERROR
- ✅ `SuspensionReason` enum - YIELD, CHANNEL_RECV, TIMER, SLEEP, etc.
- ✅ YIELD bytecode opcode (0x47) - Defined but incomplete impl
- ⚠️ `VM::executeInstruction()` has partial YIELD handling - doesn't properly suspend
- ❌ Generator spawning on function call - NOT IMPLEMENTED
- ❌ Coroutine object/callable - MISSING

**Critical Gaps:**
1. VM uses global `frame_arena_`, `stack`, `locals` - Fiber-aware loading not done
2. Calling generator() tries to execute normally, not spawn goroutine
3. YIELD implementation returns to next opcode, not to caller
4. No load/save fiber state around executeOneStep()

## Outstanding Tasks

### Task 1: Fiber ↔ VM State Synchronization

**Current State:** VM.cpp has frame_arena_, stack, locals as global VM state  
**Problem:** Each Fiber needs its own isolated state  
**Solution:** Load/save fiber state into VM state at execution boundaries

```cpp
// In VM::executeOneStep(Fiber* fiber)
void ExecutionEngine::executeFrame() {
    Scheduler::Goroutine* g = scheduler_->pickNext();
    
    // TODO: Load goroutine state into VM
    // vm_->loadGoroutineState(g);
    
    VMExecutionResult result = vm_->executeOneStep(g);
    
    // TODO: Save VM state back to goroutine
    // vm_->saveGoroutineState(g);
}
```

**Implementation:**
- Add `VM::loadGoroutineState(Goroutine*)` - Copy goroutine stack/locals/frames into VM
- Add `VM::saveGoroutineState(Goroutine*)` - Copy VM stack/locals/frames into goroutine
- Call before/after every executeOneStep()

### Task 2: Yield Operation Implementation

**Current:** `yield` bytecode instruction exists but does nothing special  
**Problem:** Yield must suspend fiber and return value to caller  
**Solution:** Handle YIELD opcode specially in executeOneStep()

```cpp
// In VM::executeInstruction()
case OpCode::YIELD: {
    Value yield_value = popStack();
    // TODO: Suspend current fiber
    // TODO: Return YIELD result with value
    // This makes executeOneStep() return immediately
    throw YieldException(yield_value);
}
```

**Implementation:**
- Create `YieldException` carrying the yield value
- Catch it in executeOneStep() and convert to `VMExecutionResult::YIELD`
- Scheduler unparks fiber to runnable state
- Next call to generator resumes from after yield

### Task 3: Generator Function Support

**Current:** Functions compile to bytecode, can't be called multiple times with suspension  
**Problem:** Generator functions need to maintain execution position (IP)  
**Solution:** Store bytecode function entry, use Goroutine to track IP

```cpp
// In compiler/semantic analysis
// Detect functions containing yield:
fn generator() {
    yield 1
    yield 2
}

// Compile as:
// 1. Function object with bytecode
// 2. Calling it creates a Goroutine (not a regular function call)
// 3. Each call to generator() resumes the Goroutine
```

**Implementation:**
- Mark functions containing `yield` as generators
- Calling generator returns a Goroutine (coroutine object)
- Calling coroutine object invokes scheduler to run it once

### Task 4: Coroutine Object Creation

**Current:** Function calls always complete (no suspension)  
**Problem:** Generators need to return coroutine objects, not values  
**Solution:** Detect generator functions, wrap calls specially

```cpp
// Generator detection:
if (function_has_yield) {
    // Calling fn() spawns goroutine and returns coroutine_id
    uint32_t g_id = scheduler.spawn(function_id, args);
    return Value::CoroutineId(g_id);
}

// Calling coroutine:
if (value.isCoroutineId()) {
    uint32_t g_id = value.asCoroutineId();
    Goroutine* g = scheduler.get(g_id);
    result = vm.executeOneStep(g);
    // Return yield value if YIELD result
    // Return final value on completion
    // Return null if dead
}
```

**Implementation:**
- Detect `yield` in semantic analysis phase
- Mark function as generator
- In VM, check if calling generator - spawn goroutine instead
- Return coroutine object (encapsulates goroutine ID)
- Calling coroutine object invokes scheduler

### Task 5: Scheduler Integration in VM

**Current:** ExecutionEngine calls scheduler to pick goroutine  
**Problem:** VM doesn't know about scheduler, isolated execution  
**Solution:** Pass Goroutine or goroutine context to VM methods

```cpp
// Option A: Pass Goroutine pointer
VMExecutionResult VM::executeOneStep(Scheduler::Goroutine* g) {
    loadGoroutineState(g);
    // Execute one instruction
    saveGoroutineState(g);
}

// Option B: Use thread-local scheduler
Scheduler& sched = Scheduler::instance();
Goroutine* current = sched.current();
```

**Implementation:**
- Option A: Modify VM::executeOneStep to take Goroutine*
- OR Option B: Use singleton scheduler, thread_local current goroutine
- Load/save state around instruction execution

### Task 6: Fiber Lifecycle Management

**Current:** Goroutine has state enum  
**Problem:** Need to detect when goroutine is "dead" and return null  
**Solution:** Implement proper state machine

```cpp
enum GoroutineState {
    Created,      // Just spawned
    Runnable,     // Ready
    Running,      // Currently executing
    Suspended,    // Waiting on event
    Done          // Finished
};

// When calling coroutine:
if (goroutine->state == GoroutineState::Done) {
    return null;  // Dead fiber returns null
}
```

**Implementation:**
- Mark goroutine DONE when function returns (no more frames)
- Return null when calling dead coroutine
- All tests should validate this

## Implementation Order

1. **Phase 3B-1:** Fiber ↔ VM state sync (loadGoroutineState/saveGoroutineState)
2. **Phase 3B-2:** Yield exception and result handling
3. **Phase 3B-3:** Generator detection in semantic analysis
4. **Phase 3B-4:** Goroutine spawning on generator calls
5. **Phase 3B-5:** Coroutine object implementation
6. **Phase 3B-6:** Test execution and validation

## Critical Implementation Details

### State Synchronization Pattern
```cpp
void VM::loadGoroutineState(Goroutine* g) {
    frame_arena_ = g->call_stack;      // TODO: mapping issues
    stack = g->stack;
    locals = g->locals;
    frame_count_ = g->call_stack.size();
    // Set current_chunk from goroutine's chunk_index
}

void VM::saveGoroutineState(Goroutine* g) {
    // TODO: Save frame_arena_ -> goroutine's call_stack
    g->stack = stack;
    g->locals = locals;
}
```

### IP Preservation
- Every yield must preserve exact IP in current frame
- On resume, executeOneStep() starts from that IP
- IP++ only happens if instruction didn't restore it

### Call Stack
- Goroutine has call_stack vector
- VM has frame_arena_ vector
- Must map between them properly

## Testing Strategy

1. Run test_phase3_main_loop.hv with debug output
2. Verify Test 1 (simple yield/resume)
3. Verify Test 2 (state preservation)
4. Verify remaining tests in order
5. All 8 tests should pass

## Estimated Effort

- State sync: 2 hours
- Yield handling: 1 hour  
- Generator detection: 2 hours
- Coroutine objects: 2 hours
- Integration: 1 hour
- Testing/debugging: 2 hours

**Total Phase 3B: 10 hours**

## Success Criteria

✅ Test 1: Yield and resume
✅ Test 2: State preservation
✅ Test 3: Yield in loop
✅ Test 4: Nested yields
✅ Test 5: Concurrent generators (scheduler)
✅ Test 6: Fiber death detection
✅ Test 7: Exception handling
✅ Test 8: Deep stack preservation

All tests passing with correct output = Phase 3B complete
