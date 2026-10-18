# Spec: conditional-hotkeys

Scope: feature

# Spec: conditional-hotkeys

Scope: feature

# Conditional Hotkeys — Cleanup & Consolidation

## Problem

Three independent condition evaluation systems exist, two are dead code, and the one that works (`ConditionalHotkeyManager::EvaluateConditionInternal`) is a hardcoded string-matching if/else chain. The `HotkeyConditionCompiler` pretends to compile conditions to bytecode but produces useless placeholder `CompiledCondition` objects with `function_id=0/1`. The fully-implemented `ConditionEngine` is never instantiated anywhere.

Meanwhile, the Havel script → bytecode → host bridge pipeline for conditional hotkeys **already works**:
- Parser creates `ConditionalHotkey` AST nodes (`F2 if mode == "work" => { }`)
- ByteCompiler compiles condition expression to a bytecode function, passes it to `hotkey.register_conditional`
- `InputBridge::handleHotkeyRegisterConditional` wraps condition closure into `std::function<bool()>` and calls `ConditionalHotkeyManager::AddConditionalHotkey`
- `ConditionalHotkeyManager` grabs/ungrabs based on condition evaluation

The problem is: `ConditionalHotkeyManager::EvaluateConditionInternal` (the string-matching path) is redundant because the bytecode pipeline already evaluates conditions via VM closures. And the dead code systems add confusion.

## Architecture: What Exists

### 1. ConditionEngine (ConditionSystem.hpp/cpp) — DEAD CODE
- 352-line fully implemented class
- `registerProperty`, `registerIntProperty`, `registerBoolProperty`, `registerListProperty`
- `evaluateCondition` with proper operator support (==, !=, ~, matches, in, etc.)
- `parseCondition` string parser
- Dynamic variable support (`setVariable`)
- Cache with invalidation
- **ZERO external instantiation** — never `#include`d, never used
- `parseComplexCondition` declared but NOT defined (missing implementation)

### 2. HotkeyConditionCompiler (HotkeyConditionCompiler.hpp/cpp) — NO-OP
- `compileCondition` → `compilePatternCondition` → creates placeholder `CompiledCondition{function_id=0/1, instruction_ptr=0}`
- Cache works but cached values are useless (no real bytecode)
- `hasCompiledCondition` flag in `AddConditionalHotkey` is computed but never read
- Two pattern branches: `mode == X` (function_id=0) and `window.exe/title` (function_id=1)

### 3. ConditionalHotkeyManager::EvaluateConditionInternal — WORKING BUT CRUDE
- Hardcoded string `find()` chain for: `mode ==`, `mode !=`, `window.title ==`, `window.class ==`, `class ==`, `title ==`, `exe ==`
- No operator support beyond `==`/`!=`
- No AND/OR compound conditions
- Called from `EvaluateCondition` which is called from action lambdas in `AddConditionalHotkey`

### 4. HotkeyManager's conditionEvaluator lambda — SEPARATE
- `ConditionalHotkeyManager` has a `conditionEvaluator` `std::function` field
- `SetConditionEvaluator()` setter exists but is never called
- This is a fourth evaluation path that's wired but unused

### 5. HotkeyActionStateSync — DEAD CODE
- All 5 methods (`setState`, `getState`, `hasState`, `getAllState`, `clearAll`) defined
- `HotkeyActionContext.cpp:11-71` has the implementations
- Zero external callers (only `clearAll` referenced in a comment in Havel.cpp)

### 6. HotkeyActionContext::getContext/hasContext — ZERO EXTERNAL CALLERS
- Thread-local context system that `AddConditionalHotkey` writes to but nothing reads

## What Actually Works (the bytecode pipeline)

```
Havel source: F2 if mode == "work" => { print("hi") }
    ↓ Parser
ConditionalHotkey AST { condition: BinaryExpr(mode, ==, "work"), binding: HotkeyBinding(F2, block) }
    ↓ ByteCompiler (line 2454-2471)
1. Compile condition expr → "condhotkey_condition" bytecode function
2. Set when_condition_func_index_ = condFuncIndex
3. Call compileHotkeyBinding →
   - Compile action → "hotkey_action" bytecode function
   - Build wrapper with condition check (JUMP_IF_FALSE)
   - Emit: LOAD_GLOBAL "hotkey.register_conditional"
   - Call with [hotkeyStr, wrapperFunc, condFunc]
    ↓ Host Bridge (ModularHostBridges.cpp:3752-3836)
handleHotkeyRegisterConditional:
   - Register callback + condCallback with VM
   - Create conditionFn = [vm, condCallbackId]() { vm->invokeCallback(condCallbackId, {}); }
   - Call condMgr.AddConditionalHotkey(key, conditionFn, trueAction, falseAction, 0, true)
   - Track global dependencies via DependencyTracker
    ↓ ConditionalHotkeyManager
   - conditionFn (the VM closure) is called to evaluate grab/ungrab
   - Action lambda calls EvaluateCondition(string) → EvaluateConditionInternal (redundant!)
```

**Key insight**: The condition evaluation happens in TWO places:
1. **Grab/ungrab decision**: Uses `conditionFn` (VM closure) — works correctly
2. **Action execution guard**: The action lambda in `AddConditionalHotkey` also calls `EvaluateCondition(condition_string)` before running `trueAction` — this is redundant because the hotkey is only grabbed when the condition is true, and ungrabbed when false

## Plan

### Phase 1: Delete dead code
1. Delete `ConditionSystem.hpp` and `ConditionSystem.cpp` (ConditionEngine — never used)
2. Delete `HotkeyActionStateSync` class from `HotkeyActionContext.hpp/cpp` (5 methods, zero callers)
3. Remove `HotkeyActionContext::setContext`/`clearContext` calls from `ConditionalHotkeyManager.cpp` (writes context that nobody reads)
4. Remove `hasCompiledCondition` flag computation in `AddConditionalHotkey` (computed but never read)

### Phase 2: Fix HotkeyConditionCompiler — make it real or delete it
Two options:

**Option A: Delete it** — Since the bytecode pipeline already evaluates conditions via VM closures, `HotkeyConditionCompiler` is redundant. The condition string is already compiled to a bytecode function by ByteCompiler. Having a second compilation path adds confusion.

**Option B: Wire it** — Make `compilePatternCondition` emit real bytecode to a synthetic BytecodeFunction, store the function_id, and have `EvaluateConditionInternal` call `vm->callFunction(compiled.function_id)` instead of string matching. This would make string-condition hotkeys (from C++ API) faster.

**Recommendation: Option A** — The C++ API path (`AddConditionalHotkey` with string condition) should be migrated to use `std::function<bool()>` conditions backed by VM closures, just like the bytecode pipeline does. The string-matching path exists only for C++ callers who don't have a VM, but all C++ callers DO have VM access.

### Phase 3: Remove redundant action-guard evaluation
In `AddConditionalHotkey` (string overload, line 93-175), the action lambda does:
```cpp
auto action = [this, condition, trueAction, falseAction, async]() {
    if (EvaluateCondition(condition)) {  // ← REDUNDANT
        trueAction();
    }
};
```

This re-evaluates the condition when the hotkey fires. But the hotkey is only grabbed when the condition is true, so the condition will always be true when the action runs. Remove the guard — just call `trueAction()` directly.

For the function-condition overload (line 203-318), same issue: the action lambda re-evaluates `condition()` before calling `trueAction`. Remove the guard.

### Phase 4: Delete string-condition path from ConditionalHotkeyManager
After Phase 2-3:
- `EvaluateCondition()` / `EvaluateConditionInternal()` become unused
- `conditionEvaluator` field and `SetConditionEvaluator()` become unused
- `conditionCache` / `conditionCacheMutex` / `CACHE_DURATION_MS` become unused
- `CachedCondition` struct becomes unused
- `currentMode` static and `GetMode()`/`SetMode()` — check if other callers exist

Delete all of the above. Keep only the `std::function<bool()>` variant of `AddConditionalHotkey`.

### Phase 5: Consolidate remaining condition evaluation
After cleanup, the only condition evaluation path is:
- `ConditionalHotkeyManager` stores `std::function<bool()>` conditions
- `BatchUpdateConditionalHotkeys` / `ReevaluateConditionalHotkeys` call the function
- The function is a VM closure (from bytecode pipeline) or a C++ lambda (from C++ API)

This is the single source of truth. No consolidation needed — the dead alternatives are gone.

## Files Changed

### Deleted
- `src/core/condition/ConditionSystem.hpp` (ConditionEngine — dead)
- `src/core/condition/ConditionSystem.cpp` (ConditionEngine — dead)

### Modified
- `src/core/hotkey/HotkeyActionContext.hpp` — Remove `HotkeyActionStateSync` class
- `src/core/hotkey/HotkeyActionContext.cpp` — Remove `HotkeyActionStateSync` implementations
- `src/core/condition/ConditionalHotkeyManager.hpp` — Remove string-condition API, `EvaluateCondition`, `EvaluateConditionInternal`, `conditionEvaluator`, condition cache, mode statics
- `src/core/condition/ConditionalHotkeyManager.cpp` — Remove action-guard evaluation, string condition overload, dead methods
- `src/core/condition/HotkeyConditionCompiler.hpp` — Delete or gut (depending on Phase 2 decision)
- `src/core/condition/HotkeyConditionCompiler.cpp` — Delete or gut
- `src/core/init/Havel.cpp` — Remove `HotkeyActionStateSync::clearAll()` reference (if any real calls exist)

### CMakeLists.txt
- Remove `ConditionSystem.cpp` from sources

## Risks
- **String-condition C++ API callers**: If any external code calls `AddConditionalHotkey(key, string_condition, ...)`, it will break. Must verify no such callers exist (or provide migration path).
- **Mode management**: `SetMode`/`GetMode` on `ConditionalHotkeyManager` are used for string condition evaluation. If other code calls `SetMode()`, need to redirect to ModeManager directly.