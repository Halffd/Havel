# Phase 3B Implementation Checklist

## Step 0: Prerequisites (Already Done ✅)

- [x] Fiber class fully defined (Fiber.hpp)
- [x] Scheduler infrastructure (Scheduler.hpp) 
- [x] VMExecutionResult types (VM.hpp)
- [x] YIELD opcode (0x47 in BytecodeIR.hpp)
- [x] Test cases written (test_phase3_main_loop.hv)

## Step 1: Fiber ↔ VM State Synchronization

### 1.1 Load Fiber State into VM
**File:** `src/havel-lang/compiler/vm/VM.hpp`

Add method:
```cpp
class VM {
public:
  // Load a fiber's state into VM's global state
  void loadFiberState(Fiber* fiber) {
    // Copy fiber->stack into local VM state
    // Copy fiber->locals into locals_ map
    // Set frame_arena_ from fiber->call_stack
    // Set current IP from top CallFrame
  }
};
```

### 1.2 Save VM State back to Fiber
**File:** `src/havel-lang/compiler/vm/VM.hpp`

Add method:
```cpp
class VM {
public:
  // Save VM's global state back to fiber
  void saveFiberState(Fiber* fiber) {
    // Copy local stack into fiber->stack
    // Copy locals_ into fiber->locals
    // Update fiber->call_stack from frame_arena_
    // Update current CallFrame's IP
  }
};
```

### 1.3 Call in ExecutionEngine
**File:** `src/havel-lang/runtime/execution/ExecutionEngine.cpp`

Update executeFrame():
```cpp
void ExecutionEngine::executeFrame() {
  Scheduler::Goroutine* g = scheduler_->pickNext();
  
  // TODO: Convert Goroutine to Fiber if needed
  Fiber* fiber = ...; // Get from goroutine
  
  // LOAD fiber state into VM
  vm_->loadFiberState(fiber);
  
  // Execute one instruction
  VMExecutionResult result = vm_->executeOneStep(fiber);
  
  // SAVE VM state back to fiber
  vm_->saveFiberState(fiber);
  
  // Handle result...
}
```

## Step 2: Yield Operation Implementation

### 2.1 Handle YIELD opcode
**File:** `src/havel-lang/compiler/vm/VM.cpp`

Update `executeInstruction()` case OpCode::YIELD:
```cpp
case OpCode::YIELD: {
  Value yield_value = popStack();
  
  // DON'T CONTINUE - suspend and return to caller
  // Current fiber continues to next iteration
  // This fiber is marked SUSPENDED with reason=YIELD
  
  // Suspend will happen in ExecutionEngine::handleYield()
  // Just return the yield result
  return VMExecutionResult::Yield(yield_value);
}
```

### 2.2 Implement ExecutionEngine::handleYield()
**File:** `src/havel-lang/runtime/execution/ExecutionEngine.hpp`

```cpp
void ExecutionEngine::handleYield(Scheduler::Goroutine* g) {
  if (debug_mode_) {
    std::cout << "[ExecutionEngine] Goroutine yielded" << std::endl;
  }
  // Mark fiber suspended with reason=YIELD
  // g->state = Goroutine::SUSPENDED
  // g->suspension_reason = SuspensionReason::YIELD
  // Scheduler will pick next goroutine on next iteration
}
```

### 2.3 Return Control in executeFrame()
**File:** `src/havel-lang/runtime/execution/ExecutionEngine.cpp`

```cpp
void ExecutionEngine::executeFrame() {
  // ... load/execute/save ...
  
  switch (result.type) {
    case VMExecutionResult::YIELD:
      handleYield(g);
      break;
    case VMExecutionResult::SUSPENDED:
      handleSuspended(g);
      break;
    case VMExecutionResult::RETURNED:
      handleReturned(g);
      break;
    case VMExecutionResult::ERROR:
      handleError(g, result.error_message);
      break;
  }
}
```

## Step 3: Generator Function Detection

### 3.1 Detect yield in semantic analysis
**File:** `src/havel-lang/semantic/SemanticAnalyzer.cpp`

In `visitFunctionDef()` or after AST walk:
```cpp
void SemanticAnalyzer::analyzeFunctionDefinition(FunctionDef* fn) {
  // Walk the function body
  // If any YieldExpression found -> mark function as generator
  
  // Store in symbol table:
  // function_symbol->is_generator = true;
}
```

### 3.2 Check for yield in expressions
**File:** `src/havel-lang/ast/AST.hpp`

Add method to FunctionDef:
```cpp
class FunctionDef : public Expr {
  bool hasYield() const {
    // Walk body and return true if contains yield
    // Or store during semantic analysis
  }
};
```

## Step 4: Generator Function Calling

### 4.1 Spawn goroutine on generator call
**File:** `src/havel-lang/compiler/vm/VM.cpp`

In `executeInstruction()` for CALL opcode:
```cpp
case OpCode::CALL: {
  uint32_t func_id = instruction.arg1;
  
  // Check if this function is a generator
  if (chunks_[func_id].is_generator) {
    // Spawn new goroutine instead of regular call
    Fiber* fiber = new Fiber(next_fiber_id++, func_id);
    uint32_t fiber_id = scheduler_->spawn(fiber);
    
    // Push coroutine object (wraps fiber_id)
    Value coro = Value::makeCoroutineId(fiber_id);
    pushStack(coro);
  } else {
    // Regular function call
    // ... existing code ...
  }
  break;
}
```

### 4.2 Mark chunk as generator
**File:** `src/havel-lang/compiler/core/BytecodeChunk.hpp`

Add to BytecodeChunk:
```cpp
struct BytecodeChunk {
  bool is_generator = false;  // Set during compilation if contains yield
};
```

## Step 5: Coroutine Object Execution

### 5.1 Calling coroutine objects
**File:** `src/havel-lang/compiler/vm/VM.cpp`

Handle CALL on coroutine value:
```cpp
case OpCode::CALL: {
  Value func = stack.top();  // Might be coroutine_id
  
  if (func.isCoroutineId()) {
    uint32_t fiber_id = func.asCoroutineId();
    Fiber* fiber = scheduler_->getFiber(fiber_id);
    
    if (fiber->isDone()) {
      // Dead fiber returns null
      pushStack(Value::makeNull());
    } else if (fiber->state == FiberState::CREATED) {
      // First call - mark RUNNABLE
      fiber->state = FiberState::RUNNABLE;
      scheduler_->makeRunnable(fiber);
      // TODO: Yield control to scheduler for next iteration
      return VMExecutionResult::Yield(Value::makeNull());
    } else {
      // Already running/suspended - error
      COMPILER_THROW("Cannot call coroutine, not in proper state");
    }
    break;
  }
  
  // ... regular function call ...
}
```

### 5.2 Value types for coroutines
**File:** `src/havel-lang/runtime/core/Value.hpp`

Check if coroutine ID support already exists:
```cpp
class Value {
public:
  static Value makeCoroutineId(uint32_t id);
  bool isCoroutineId() const;
  uint32_t asCoroutineId() const;
};
```

## Step 6: Fiber Lifecycle Management

### 6.1 Fiber done detection
**File:** `src/havel-lang/runtime/concurrency/Fiber.hpp`

Ensure methods exist:
```cpp
class Fiber {
public:
  bool isDone() const { return state == FiberState::DONE; }
  bool isRunning() const { return state == FiberState::RUNNING; }
  bool isRunnable() const { return state == FiberState::RUNNABLE; }
  bool isSuspended() const { return state == FiberState::SUSPENDED; }
};
```

### 6.2 Mark fiber done on completion
**File:** `src/havel-lang/runtime/execution/ExecutionEngine.cpp`

In `handleReturned()`:
```cpp
void ExecutionEngine::handleReturned(Scheduler::Goroutine* g) {
  // Check if this was last frame
  if (g->call_stack.empty()) {
    // Fiber execution complete
    g->state = Goroutine::DONE;
    // Result value saved in g->return_value or somewhere
  } else {
    // Continue in caller
    // Scheduler will pick it up next iteration
  }
}
```

## Implementation Order (Recommended)

1. **Phase 3B-1:** Load/save fiber state (Step 1) - 2 hours
2. **Phase 3B-2:** YIELD handling and suspension (Step 2) - 1.5 hours
3. **Phase 3B-3:** Generator detection (Step 3) - 1.5 hours
4. **Phase 3B-4:** Spawning generators on call (Step 4) - 2 hours
5. **Phase 3B-5:** Coroutine object handling (Step 5) - 2 hours
6. **Phase 3B-6:** Fiber lifecycle (Step 6) - 1 hour
7. **Phase 3B-7:** Testing and debug (All steps) - 3 hours

**Total: ~13 hours**

## Testing Progression

After each step:
- Test 1: Simple yield (after Step 2)
- Test 2: State preservation (after Step 1+2)
- Test 3: Yield in loop (after Step 1-4)
- Test 4: Nested yields (after all steps)
- Tests 5-8: Additional validation

## Critical Implementation Notes

### State Synchronization
- Must be bidirectional (load before, save after)
- Must preserve exact IP for resume
- Stack order matters (LIFO)

### IP Preservation
- YIELD increments IP before suspension
- RETURN handles frame cleanup first
- Resume continues at IP+1

### Generator vs Regular Functions
- Detection happens during compilation
- Calling generator ≠ calling regular function
- Return type differs (coroutine object vs value)

### Scheduler Integration
- ExecutionEngine drives scheduler
- Scheduler picks next runnable fiber
- Each frame executes ONE instruction
- Yield/suspension handled in result processing

## Success Criteria

✅ Test 1 passes: yield and resume values
✅ Test 2 passes: state preserved across yields
✅ Test 3 passes: loop counter survives suspension
✅ Test 4 passes: nested generator yields
✅ Test 5 passes: scheduler interleaves generators
✅ Test 6 passes: dead fiber returns null
✅ Test 7 passes: exceptions caught in generators
✅ Test 8 passes: deep stack preserved

All 8 tests pass with expected output = Phase 3B complete
