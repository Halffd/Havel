# Timer Callback Invocation Analysis: Pure Mode Execution

## Executive Summary

**CRITICAL ISSUE FOUND:** Timer callbacks (`checkTimers()`) are **NOT** invoked during pure mode script execution (`--run` flag). The execution path uses a tight dispatch loop with no timer check integration.

---

## 1. Where `runBytecodePipeline()` is Defined

### Primary Definitions:
- **File:** `src/havel-lang/compiler/core/Pipeline.cpp`
- **Entry Point (with options):** Lines 516-645 (main implementation)
- **Entry Point (simple overload):** Lines 657-659

### Key Code:
```cpp
// src/havel-lang/compiler/core/Pipeline.cpp, line 516+
BytecodeSmokeResult runBytecodePipeline(
    const std::string &source, 
    const std::string &entry_function,
    const PipelineOptions &options) {
  // ... compilation stages (lexer, parser, semantic, bytecode) ...
  
  // LINE 639: This is where execution happens in pure mode
  result.return_value = vm->execute(*chunk, entry_function);
}
```

### Related Headers:
- **File:** `src/havel-lang/compiler/core/Pipeline.hpp` (lines 1-100)
  - Defines `PipelineOptions` struct
  - Declares `runBytecodePipeline()` overloads

---

## 2. VM Execution Loop Architecture

### High-Level Flow (Pure Mode):

```
runBytecodePipeline() [Pipeline.cpp:639]
    ↓
vm->execute() [VM.cpp:2314-2370]
    ↓
runDispatchLoop(0) [VM.cpp:2906-2995] ← TIGHT LOOP - NO TIMERS
    ↓
    Loop: executeInstruction() → processPendingCalls() → increment IP
    ↓
Return result to Pipeline
```

### VM::execute() Implementation:
**File:** `src/havel-lang/compiler/vm/VM.cpp`, lines 2314-2370

```cpp
Value VM::execute(const BytecodeChunk &chunk,
                  const std::string &function_name,
                  const std::vector<Value> &args) {
  
  current_chunk = &chunk;
  const auto *entry = chunk.getFunction(function_name);
  
  // Initialize VM state...
  frame_count_ = 0;
  registerDefaultHostGlobals();
  
  // Set up entry frame...
  frame_arena_.push_back(CallFrame{entry, 0, 0, 0});
  frame_count_++;
  locals.resize(entry->local_count);
  
  // LINE 2361: ENTER THE DISPATCH LOOP
  runDispatchLoop(0);  // ← NO TIMER CHECKS HAPPEN HERE!
  
  // Return stack top as result
  return stack.top();
}
```

### VM::runDispatchLoop() Implementation:
**File:** `src/havel-lang/compiler/vm/VM.cpp`, lines 2906-2995

```cpp
void VM::runDispatchLoop(size_t stop_frame_depth) {
  while (frame_count_ > stop_frame_depth) {
    
    // Get current frame/instruction
    const auto *function = frame_arena_[active_frame_idx].function;
    const auto &instruction = function->instructions[ip];
    
    // CRITICAL: NO TIMER CHECK HERE!
    // Simply execute instruction after instruction:
    try {
      executeInstruction(instruction);
    } catch (const ScriptThrow &thrown) {
      // Exception handling
    } catch (const std::runtime_error &e) {
      // Error handling
    }
    
    // Call pending callbacks (but not timer callbacks)
    processPendingCalls();
    
    // Increment IP and loop
    frame_arena_[active_frame_idx].ip++;
  }
}
```

---

## 3. Timer Check Architecture (How It SHOULD Work)

### Correct Implementation in Event Loop:
**File:** `src/core/io/EventListener.cpp`, lines 429-451

```cpp
void EventListener::EventLoop() {
  while (running.load() && !shutdown.load()) {
    
    // LINE 445: Check for expired timers
    if (hostBridge) {
      hostBridge->checkTimers();  // ← CALLED HERE IN NORMAL MODE
    }
    
    // LINE 450: Execute one bytecode instruction
    if (executionEngine) {
      executionEngine->executeFrame();
    }
    
    // Poll for input
    // ...
  }
}
```

### What Happens During Timer Check:
**File:** `src/havel-lang/compiler/runtime/HostBridge.cpp`, lines 2589-2598

```cpp
void HostBridge::checkTimers() {
  if (concurrencyBridge_) {
    concurrencyBridge_->checkTimers();  // Delegates to ConcurrencyBridge
  }
}
```

**File:** `src/havel-lang/compiler/runtime/ConcurrencyBridge.cpp`, lines 377-400

```cpp
void ConcurrencyBridge::checkTimers() {
  std::lock_guard<std::mutex> lock(timers_mutex_);
  auto now = std::chrono::steady_clock::now();
  
  for (auto &timer : timers_) {
    if (timer.active && timer.next_run <= now) {
      
      // INVOKE CALLBACK VIA VM
      if (vm_) {
        try {
          CallbackId cbId = vm_->registerCallback(timer.callback);
          vm_->invokeCallback(cbId, {});  // ← TIMER CALLBACK INVOKED
          vm_->releaseCallback(cbId);
        } catch (const std::exception &e) {
          std::cerr << "Error executing timer callback: " << e.what() << std::endl;
        }
      }
      
      // Reschedule if interval timer
      if (timer.interval_ms > 0) {
        timer.next_run = now + std::chrono::milliseconds(timer.interval_ms);
      } else {
        timer.active = false;  // One-shot timer - deactivate
      }
    }
  }
}
```

---

## 4. Why Timer Checks Are MISSING in Pure Mode

### The Root Cause:

| Context | Timer Checks | Why |
|---------|-------------|-----|
| **Normal Event Loop** (EventListener.cpp) | ✅ **YES** | Explicitly called at line 445 before ExecutionEngine |
| **Pure Mode** (runBytecodePipeline) | ❌ **NO** | Calls `vm->execute()` which uses `runDispatchLoop()` - a tight loop with no integration points |
| **Phase 3 Execution** (ExecutionEngine.cpp) | ✅ **YES** | Main loop calls `vm_->executeOneStep()` (one instruction per call); timer checks happen in EventListener.cpp |

### The Tight Loop Problem:

`runDispatchLoop()` is designed for **synchronous, uninterruptible execution** of a complete function. It has no:
- Timer check integration
- Event queue drain points
- Callback invocation opportunities
- Yield points between instructions

---

## 5. Specific Locations Where Timer Checks SHOULD Happen But Don't

### PROBLEM #1: No Timer Checks in runDispatchLoop()

| File | Lines | Issue |
|------|-------|-------|
| `src/havel-lang/compiler/vm/VM.cpp` | 2906-2995 | **Complete absence of timer check logic** |

**Missing Code:**
```cpp
// SHOULD occur inside runDispatchLoop() loop:
if (should_check_timers_at_this_interval) {
  // Call ConcurrencyBridge::checkTimers() or similar
  checkExpiredTimers();
}
```

### PROBLEM #2: No Bridge Between runBytecodePipeline and HostBridge

| File | Lines | Issue |
|------|-------|-------|
| `src/havel-lang/compiler/core/Pipeline.cpp` | 639 | Calls `vm->execute()` directly without timer context |

**What Happens Now:**
```cpp
result.return_value = vm->execute(*chunk, entry_function);
// vm->execute calls runDispatchLoop(), which has NO timer checks
```

**What Should Happen:**
```cpp
// Before or after execute, timers need to be checked
// OR: vm->execute() needs to accept a timer check callback
result.return_value = vm->execute(*chunk, entry_function);
// checkTimers() call missing here
```

### PROBLEM #3: VM::executeOneStep() Exists But NOT Used During Pure Mode

| File | Lines | Purpose |
|------|-------|---------|
| `src/havel-lang/compiler/vm/VM.cpp` | 2512-2644 | Executes ONE instruction; designed for Phase 3 main loop |

**Current Usage:**
- ✅ Used: `ExecutionEngine::executeFrame()` (interrupts to allow timer checks)
- ❌ NOT Used: `runBytecodePipeline()` pure mode (uses `runDispatchLoop()` instead)

---

## 6. Execution Path Comparison

### Path A: Normal Event Loop (CORRECT - Timers Work)
```
EventListener::EventLoop() [EventListener.cpp:429]
  ↓
hostBridge->checkTimers() [EventListener.cpp:445] ✅ TIMERS CHECKED
  ↓
executionEngine->executeFrame() [EventListener.cpp:450]
  ↓
vm_->executeOneStep(fiber) [ExecutionEngine.cpp:115]
  ↓
Execute ONE instruction, return result
  ↓
Loop continues - timer check happens AGAIN before next instruction
```

### Path B: Pure Mode (BROKEN - No Timer Checks)
```
runBytecodePipeline() [Pipeline.cpp:639]
  ↓
vm->execute() [VM.cpp:2314]
  ↓
runDispatchLoop(0) [VM.cpp:2361]
  ↓
TIGHT LOOP: Execute ALL instructions without ANY timer checks
  ↓
Return result
  ↓
No more opportunities for timer callbacks
```

---

## 7. Concrete Impact

### Scenario: Timer Configured During Pure Mode Script

**Script:**
```havel
fn main() {
  timer.setTimeout(1000, fn() {
    print("This callback never runs!")
  })
  
  sleep(2000)  // Wait for timer
}

__main__ = main
```

**What Happens:**
1. ✅ Timer is registered in ConcurrencyBridge
2. ✅ Script enters VM via `runBytecodePipeline()`
3. ❌ `runDispatchLoop()` executes function instructions
4. ❌ `sleep()` blocks (or yields) but timer check NEVER happens
5. ❌ Timer fires in background thread, but callback is never invoked
6. ❌ Script completes and returns

**Why:**
- `runDispatchLoop()` has no timer check integration
- Callbacks would be invoked via `vm->invokeCallback()` but there's no mechanism to call it from within `runDispatchLoop()`

---

## 8. Files Requiring Analysis/Modification

| File | Lines | Required Change |
|------|-------|-----------------|
| `src/havel-lang/compiler/vm/VM.cpp` | 2906-2995 | Add periodic timer checks inside loop OR |
| `src/havel-lang/compiler/vm/VM.cpp` | 2906-2995 | Split into smaller steps that allow external checks |
| `src/havel-lang/compiler/core/Pipeline.cpp` | 639 | Call `hostBridge->checkTimers()` after execute |
| `src/havel-lang/compiler/runtime/ConcurrencyBridge.hpp` | 1-50 | Verify `checkTimers()` interface |
| `src/core/init/HavelLauncher.cpp` | 594 | Check if launcher's bytecode execution path has timers |

---

## 9. Summary Table

| Component | Location | Timer Check? | Issue |
|-----------|----------|-------------|-------|
| **Event Loop** | `src/core/io/EventListener.cpp:445` | ✅ YES | Working correctly |
| **EventListener event loop** | `src/core/io/EventListener.cpp:429-475` | ✅ YES | Working correctly |
| **Pure Mode Entry** | `src/havel-lang/compiler/core/Pipeline.cpp:639` | ❌ NO | Missing integration |
| **VM::execute()** | `src/havel-lang/compiler/vm/VM.cpp:2314-2370` | ❌ NO | Calls runDispatchLoop directly |
| **VM::runDispatchLoop()** | `src/havel-lang/compiler/vm/VM.cpp:2906-2995` | ❌ NO | **Critical Gap** - tight loop |
| **VM::executeOneStep()** | `src/havel-lang/compiler/vm/VM.cpp:2512-2644` | N/A | Designed for interruptible execution |
| **ExecutionEngine::executeFrame()** | `src/havel-lang/runtime/execution/ExecutionEngine.cpp:100-140` | ✅ YES | Works with timer checks in EventLoop |

---

## Recommendation

**To enable timer callbacks in pure mode, consider:**

1. **Option A (Recommended):** Modify `runBytecodePipeline()` to manually call `checkTimers()` at appropriate intervals
   - Requires: Adding a hostBridge parameter or retrieving it from context
   - Impact: Low - isolated to Pipeline.cpp

2. **Option B:** Replace `runDispatchLoop()` with loop using `executeOneStep()` in pure mode
   - Requires: Significant refactoring of VM execution model
   - Impact: High - affects initialization, fiber management
   - Benefit: Unifies execution paths

3. **Option C:** Add callback/hook mechanism to `runDispatchLoop()`
   - Requires: Timer check callback parameter to `execute()`
   - Impact: Medium - modifies VM execute method signature
   - Benefit: Maintains current tight loop while allowing integration

