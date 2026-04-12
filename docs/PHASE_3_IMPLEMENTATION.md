# Phase 3 Implementation - Main Loop# Phase 3 Implementation - Main Loop



































































































































































**Gate Requirement:** All three components (main loop, executeOneStep, tests) must be complete before Phase 4 (channels, time.sleep, thread integration).---**Estimated Time:** 6-9 hours total5. **Write tests** - Validate concurrency4. **Implement executeOneStep()** - One instruction execution3. **Implement executeFrame()** - Main loop logic2. **Define VMExecutionResult** in VM.hpp1. **Identify EventListener** - Find or create execution context## Next Steps---- [ ] Gate requirements met- [ ] All tests pass- [ ] Build passes### Part D: Cleanup & Validation (1 hour)- [ ] No segfaults, clean shutdown- [ ] Verify all edge cases- [ ] Implement 8 test cases### Part C: Comprehensive Tests (2-3 hours)- [ ] Test basic execution (single instruction per call)- [ ] Handle all four result types- [ ] Implement `VM::executeOneStep(Fiber*)`- [ ] Create `VMExecutionResult` struct### Part B: Create executeOneStep (2-3 hours)- [ ] No compilation errors- [ ] Wire up EventQueue- [ ] Integrate with Scheduler- [ ] Create `EventListener::executeFrame()`### Part A: Create Main Loop (1-2 hours)## Work Breakdown- **How:** VM instruction loop has early exit after first instruction- **Why:** Prevents starvation, enables fair scheduling, simplifies cancellation- **What:** `executeOneStep()` returns after one bytecode instruction### Decision 4: Exactly One Instruction- **How:** VM only calls methods on Fibers passed to it- **Why:** Consistent lifetime management, GC safety- **What:** Scheduler owns all Fibers, VM never creates/destroys them### Decision 3: Fiber Ownership- **How:** `eventQueue_->processAll()` drains entire queue each frame- **Why:** Ensures callbacks (timer completions, channel sends, thread wakeups) execute promptly- **What:** Process all pending callbacks before picking next fiber### Decision 2: EventQueue First- **How:** VM executes one instruction, yields back to scheduler- **Why:** Eliminates race conditions, simplifies state management- **What:** Only one fiber runs at a time### Decision 1: Single-Threaded Main Loop## Design Decisions (LOCKED)8. **ErrorHandling** - Exception in suspended fiber doesn't crash VM7. **GCSafety** - Suspended fibers visible to GC (roots collected)6. **YieldSemantics** - Value passing and state preservation through yield5. **ThreadAwait** - fiber.spawn() + await integration4. **EventAndSuspend** - Channel message arrives after send, unpark works3. **DeepStack** - Nested function calls then yield at bottom2. **TwoFibersYield** - Two concurrent fibers yield back-and-forth1. **SimpleYield** - Single fiber yields value, recoversTests validate correctness of the main loop and scheduler interaction:### Component 3: Test Cases (8 minimum)**Key Constraint:** Must execute **exactly one instruction**, then return. This prevents starvation and enables fiber interleaving.```};  VMExecutionResult executeOneStep(Fiber* current_fiber);  // Returns immediately after one instruction (no blocking)  // Execute exactly one bytecode instruction from current fiberpublic:class VM {```cpp**Method Signature:**```};  std::string error_message;    // For ERROR  Value result_value;           // For YIELD or RETURNED    } type;    ERROR         // Exception (message in error_message)    RETURNED,     // function returned (value in result_value)    SUSPENDED,    // recv/sleep/await blocked (fiber already marked suspended)    YIELD,        // yield X (pushed to stack, fiber back to runnable)  enum Type {struct VMExecutionResult {```cpp**Result Type:****File:** `src/havel-lang/runtime/VM.hpp`### Component 2: executeOneStep Signature**Key Insight:** This is a **single-instruction-per-call** loop. The main program calls `executeFrame()` repeatedly (via timer, event loop, etc).```}  }      break;      // Handle exception - mark DONE with error    case ERROR:      break;      // fiber finished executing - marked DONE    case RETURNED:      break;      // EventQueue will unpark when condition met      // fiber blocked on channel/sleep/thread - already in suspended state    case SUSPENDED:      break;      scheduler_->unpark(fiber);  // Back to runnable      // fiber yielded X - returned to runnable queue    case YIELD:  switch (result.type) {  // HANDLE: Process execution result    VMExecutionResult result = vm_->executeOneStep(fiber);  // STEP: Execute exactly one bytecode instruction    }    return;    // All suspended or done - go idle or shutdown  if (!fiber) {  auto fiber = scheduler_->pickNext();  // PICK: Get next runnable fiber    eventQueue_->processAll();  // DRAIN: Process all pending callbacks (channel sends, timer completions, thread wakeups)void EventListener::executeFrame() {```cpp**Pseudocode:****File:** `src/havel-lang/runtime/execution/EventListener.cpp`### Component 1: Main Loop (EventListener::executeFrame)## Required Components- **Test Suite** - 8+ test cases verifying concurrency semantics- **Step Execution** - `VM::executeOneStep(Fiber*)` - Execute exactly one bytecode instruction- **Main Loop** - `EventListener::executeFrame()` - Governs per-cycle executionPhase 3 implements the core execution loop that drives concurrency in Havel:## Overview**Gate Status:** CLEARED (consolidation complete)**Started:** April 12, 2026  **Status:** ACTIVE  
**Status:** ACTIVE  
**Started:** April 12, 2026  
**Gate Status:** CLEARED (consolidation complete)

## Overview

Phase 3 implements the core execution loop that drives concurrency in Havel:

- **Main Loop** - `EventListener::executeFrame()` - Governs per-cycle execution
- **Step Execution** - `VM::executeOneStep(Fiber*)` - Execute exactly one bytecode instruction
- **Test Suite** - 8+ test cases verifying concurrency semantics

## Required Components

### Component 1: Main Loop (EventListener::executeFrame)

**File:** `src/havel-lang/runtime/execution/EventListener.cpp`

**Pseudocode:**
```cpp
void EventListener::executeFrame() {
  // DRAIN: Process all pending callbacks (channel sends, timer completions, thread wakeups)
  eventQueue_->processAll();
  
  // PICK: Get next runnable fiber
  auto fiber = scheduler_->pickNext();
  if (!fiber) {
    // All suspended or done - go idle or shutdown
    return;
  }
  
  // STEP: Execute exactly one bytecode instruction
  VMExecutionResult result = vm_->executeOneStep(fiber);
  
  // HANDLE: Process execution result
  switch (result.type) {
    case YIELD:
      // fiber yielded X - returned to runnable queue
      scheduler_->unpark(fiber);  // Back to runnable
      break;
    case SUSPENDED:
      // fiber blocked on channel/sleep/thread - already in suspended state
      // EventQueue will unpark when condition met
      break;
    case RETURNED:
      // fiber finished executing - marked DONE
      break;
    case ERROR:
      // Handle exception - mark DONE with error
      break;
  }
}
```

**Key Insight:** This is a **single-instruction-per-call** loop. The main program calls `executeFrame()` repeatedly (via timer, event loop, etc).

### Component 2: executeOneStep Signature

**File:** `src/havel-lang/runtime/VM.hpp`

**Result Type:**
```cpp
struct VMExecutionResult {
  enum Type {
    YIELD,        // yield X (pushed to stack, fiber back to runnable)
    SUSPENDED,    // recv/sleep/await blocked (fiber already marked suspended)
    RETURNED,     // function returned (value in result_value)
    ERROR         // Exception (message in error_message)
  } type;
  
  Value result_value;           // For YIELD or RETURNED
  std::string error_message;    // For ERROR
};
```

**Method Signature:**
```cpp
class VM {
public:
  // Execute exactly one bytecode instruction from current fiber
  // Returns immediately after one instruction (no blocking)
  VMExecutionResult executeOneStep(Fiber* current_fiber);
};
```

**Key Constraint:** Must execute **exactly one instruction**, then return. This prevents starvation and enables fiber interleaving.

### Component 3: Test Cases (8 minimum)

Tests validate correctness of the main loop and scheduler interaction:

1. **SimpleYield** - Single fiber yields value, recovers
2. **TwoFibersYield** - Two concurrent fibers yield back-and-forth
3. **DeepStack** - Nested function calls then yield at bottom
4. **EventAndSuspend** - Channel message arrives after send, unpark works
5. **ThreadAwait** - fiber.spawn() + await integration
6. **YieldSemantics** - Value passing and state preservation through yield
7. **GCSafety** - Suspended fibers visible to GC (roots collected)
8. **ErrorHandling** - Exception in suspended fiber doesn't crash VM

## Design Decisions (LOCKED)

### Decision 1: Single-Threaded Main Loop
- **What:** Only one fiber runs at a time
- **Why:** Eliminates race conditions, simplifies state management
- **How:** VM executes one instruction, yields back to scheduler

### Decision 2: EventQueue First
- **What:** Process all pending callbacks before picking next fiber
- **Why:** Ensures callbacks (timer completions, channel sends, thread wakeups) execute promptly
- **How:** `eventQueue_->processAll()` drains entire queue each frame

### Decision 3: Fiber Ownership
- **What:** Scheduler owns all Fibers, VM never creates/destroys them
- **Why:** Consistent lifetime management, GC safety
- **How:** VM only calls methods on Fibers passed to it

### Decision 4: Exactly One Instruction
- **What:** `executeOneStep()` returns after one bytecode instruction
- **Why:** Prevents starvation, enables fair scheduling, simplifies cancellation
- **How:** VM instruction loop has early exit after first instruction

## Work Breakdown

### Part A: Create Main Loop (1-2 hours)
- [ ] Create `EventListener::executeFrame()`
- [ ] Integrate with Scheduler
- [ ] Wire up EventQueue
- [ ] No compilation errors

### Part B: Create executeOneStep (2-3 hours)
- [ ] Create `VMExecutionResult` struct
- [ ] Implement `VM::executeOneStep(Fiber*)`
- [ ] Handle all four result types
- [ ] Test basic execution (single instruction per call)

### Part C: Comprehensive Tests (2-3 hours)
- [ ] Implement 8 test cases
- [ ] Verify all edge cases
- [ ] No segfaults, clean shutdown

### Part D: Cleanup & Validation (1 hour)
- [ ] Build passes
- [ ] All tests pass
- [ ] Gate requirements met

---

## Next Steps

1. **Identify EventListener** - Find or create execution context
2. **Define VMExecutionResult** in VM.hpp
3. **Implement executeFrame()** - Main loop logic
4. **Implement executeOneStep()** - One instruction execution
5. **Write tests** - Validate concurrency

**Estimated Time:** 6-9 hours total

---

**Gate Requirement:** All three components (main loop, executeOneStep, tests) must be complete before Phase 4 (channels, time.sleep, thread integration).
