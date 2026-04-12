# Concurrency Consolidation - TWO SCHEDULERS PROBLEM

**Date:** April 12, 2026  
**Status:** CRITICAL DECISION NEEDED  
**Problem:** We have two incompatible schedulers + a conflicting AsyncService

---

## The Problem

### Two Competing Scheduler Implementations

**Scheduler.hpp/cpp (OLD - Go-style M:N)**
```cpp
// WRONG FOR OUR MODEL
spawn(std::function<Value()> task)  // Lambdas, not bytecode
start(num_processors)               // Spawns OS threads
M:N model with condition_variable
// This is what BREAKS Phase 3
```

**Scheduler_v2.hpp/cpp (NEW - Single-threaded bytecode)**
```cpp
// CORRECT FOR OUR MODEL
spawn(uint32_t function_id, args)   // Bytecode-aware
No OS threads
FIFO cooperative scheduling
// This is what PHASE 3 NEEDS
```

### AsyncService.hpp (HOST - ALSO CONFLICTS)
```cpp
spawn(std::function<void()>)        // Lambdas
await(taskId)                       // BLOCKS (kills our model)
Spawns OS threads for tasks
// This ALSO conflicts with unified concurrency
```

### AsyncModule.cpp
```cpp
// STUBBED - interpreter removed
// Not used, can be deleted
```

---

## The Decision

**Consolidate to a single, unified concurrency model:**

| Component | Current | Action | Reason |
|-----------|---------|--------|--------|
| **Scheduler.hpp/cpp** | ❌ OLD M:N model | **DELETE** | Spawns OS threads, breaks our model |
| **Scheduler_v2.hpp/cpp** | ✅ Bytecode-aware | **RENAME → Scheduler** | This is the right design |
| **AsyncService.hpp/cpp** | ❌ Spawns threads, blocks | **DISABLE** | Conflicts with unified model |
| **AsyncModule.cpp** | Stubbed | **DELETE** | Not used, already removed |

---

## Detailed Plan

### STEP 1: Delete Old Scheduler (Broken Model)
```bash
rm src/havel-lang/runtime/concurrency/Scheduler.hpp
rm src/havel-lang/runtime/concurrency/Scheduler.cpp
```

**Files deleted:** 2  
**Lines deleted:** ~500  
**Impact:** HIGH on codebase but LOW on Phase 3 (uses v2 anyway)

### STEP 2: Rename Scheduler_v2 → Scheduler (New Single Truth)

Rename files:
```bash
mv src/havel-lang/runtime/concurrency/Scheduler_v2.hpp \
   src/havel-lang/runtime/concurrency/Scheduler.hpp

mv src/havel-lang/runtime/concurrency/Scheduler_v2.cpp \
   src/havel-lang/runtime/concurrency/Scheduler.cpp
```

Update includes:
```cpp
// OLD:
#include "Scheduler_v2.hpp"

// NEW:
#include "Scheduler.hpp"
```

**Files affected:** 
- Scheduler_v2.hpp (rename, update namespace declarations)
- Scheduler_v2.cpp (rename, update include)
- Any files that include Scheduler_v2.hpp

### STEP 3: Disable AsyncService (Conflicts with Unified Model)

Create stub:
```cpp
// src/host/async/AsyncService.hpp

namespace havel::host {

class AsyncService {
public:
    // DEPRECATED - Use Havel's unified concurrency model instead
    // This service spawned OS threads and blocked
    // For Phase 3+, all concurrency goes through:
    // - Scheduler (single-threaded, bytecode-aware)
    // - Fiber (per-goroutine state)
    // - EventQueue (callback bridge)
    
    // Methods stubbed to prevent compilation errors if referenced:
    std::string spawn(std::function<void()> fn) { return ""; }
    bool await(const std::string&) { return false; }
    bool isRunning(const std::string&) const { return false; }
    // ... (other methods stubbed)
};

}  // namespace havel::host
```

**Files affected:**
- host/async/AsyncService.hpp
- host/async/AsyncService.cpp

### STEP 4: Delete AsyncModule (Already Stubbed)

```bash
# Check if any code depends on it:
grep -r "AsyncModule" src/ docs/

# If only stubs/examples, delete:
rm src/modules/async/AsyncModule.cpp
rm src/modules/async/AsyncModule.hpp  # If it exists
```

### STEP 5: Update References (Find & Replace)

Search for any references:
```bash
grep -r "Scheduler_v2" src/ include/

grep -r "asyncService\|AsyncService" src/ tests/

grep -r "AsyncModule" src/ include/
```

Fix includes:
```cpp
// OLD:
#include "concurrency/Scheduler_v2.hpp"

// NEW:
#include "concurrency/Scheduler.hpp"
```

---

## Impact Analysis

### Breaking Changes (Intentional)

❌ **Code using old Scheduler:**
- ConcurrencyBridge might reference old API
- Tests might use old scheduler
- **Fix:** Use Scheduler_v2 API instead (or renamed Scheduler)

❌ **Code using AsyncService:**
- ModularHostBridges.cpp references asyncService->spawn(), await(), etc
- **Fix:** Use new unified concurrency model (must wait for Phase 3 + Phase 4+)

### Non-Breaking

✅ **Fiber.hpp** - No changes needed (per-goroutine state)
✅ **EventQueue.hpp** - No changes needed (callback bridge)
✅ **Bytecode compiler** - No changes needed

---

## Consolidation Checklist

Before Phase 3 code, execute:

- [ ] Delete Scheduler.hpp (old)
- [ ] Delete Scheduler.cpp (old)
- [ ] Rename Scheduler_v2.hpp → Scheduler.hpp
- [ ] Rename Scheduler_v2.cpp → Scheduler.cpp
- [ ] Update all includes (Scheduler_v2 → Scheduler)
- [ ] Stub out AsyncService methods (mark deprecated)
- [ ] Delete AsyncModule if unused
- [ ] Search for dangling references
- [ ] Verify compilation

---

## Why This Matters for Phase 3

**Phase 3 will need:**
```cpp
// NEW unified model (single truth):
Scheduler* sched = &Scheduler::instance();

Fiber* current = sched->pickNext();        // Not pickNext(processor)
vm.executeStep(current);                   // Not vm.execute()
sched->suspend(current, reason);           // Not scheduleGoroutine()
sched->unpark(current);                    // Not resumeG()
```

**If we keep both:**
- VM might accidentally call old Scheduler
- Tests might use old API
- Confusion about which is "real"
- Duplicate code paths to maintain

**After consolidation:**
- Single, clear Scheduler API
- No ambiguity
- Phase 3 implementation is straightforward

---

## Risk Assessment

| Risk | Severity | Mitigation |
|------|----------|-----------|
| Old code depends on Scheduler | MEDIUM | Search codebase, fix all references |
| Old code depends on AsyncService | MEDIUM | Most are in host/ modules, can be updated |
| Build breaks from missing includes | MEDIUM | Compiler catches these immediately |
| Version mismatch with git history | LOW | We're replacing old broken design |

---

## Recommendation

**Execute consolidation in this order:**

1. **Delete old Scheduler** (1 min)
2. **Rename Scheduler_v2 → Scheduler** (2 min)
3. **Update includes** (5 min)
4. **Verify compilation** (5 min)
5. **Stub AsyncService** (2 min)
6. **Delete AsyncModule** (1 min)
7. **Final verification** (5 min)

**Total time:** ~20 minutes  
**Benefit:** No duplicate scheduler conflict in Phase 3  
**Cost:** If something depends on old scheduler, we'll know immediately from compiler

---

## Sign-Off

This consolidation is:
- ✅ Architecturally necessary (unified model requires single scheduler)
- ✅ Low risk (compiler catches all breakage)
- ✅ Required before Phase 3 (can't have two schedulers)
- ✅ Clean (removes ~500 lines of broken code)

**Proceed with consolidation?** (Recommended: YES)
