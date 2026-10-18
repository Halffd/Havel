# Spec: mode-system

Scope: feature

# Spec: mode-system

Scope: feature

# Mode System — Consolidation & Dead Code Cleanup

## Core Insight

There are ~3 overlapping mode/condition subsystems when only 1 is needed. The VM already has:
- Expression parsing (the Havel language itself)
- Bytecode compilation (ByteCompiler)
- Condition evaluation (`vm->evaluateConditionBytecode()`)
- Reactive watchers (`WatcherRegistry` — edge-triggered, dependency-aware)
- Event-driven reactivity (`VAR_CHANGED` → `onVariableChanged` → re-evaluate affected watchers)
- Fiber scheduling (Scheduler + Goroutines)
- Dynamic variables with change events (`VM::setGlobal()` fires `VAR_CHANGED`)

This means every custom condition mini-language is redundant. The VM IS the condition system.

## Architecture: What Exists vs What Should Exist

### The parallel system that should not exist

| What was built | What already exists | Lines |
|---|---|---|
| `EvaluateConditionInternal()` string parser | VM expression evaluator | ~110 |
| `ConditionEngine` (dead) | `WatcherRegistry` + `evaluateConditionBytecode()` | 352 |
| `HotkeyConditionCompiler` (placeholder) | ByteCompiler (compiles real closures) | 110 |
| `CompiledCondition{fn=0, ptr=0}` | `BytecodeFunction` + `ClosureId` | — |
| `HotkeyActionStateSync` (dead) | VM globals + Fiber state | 30 |
| `HotkeyActionContext` (no readers) | VM closure captures | 40 |
| `"mode gaming"` string parsing | `mode.current() == "gaming"` Havel expression | — |
| `"title Firefox"` string parsing | `window.title().contains("Firefox")` Havel expression | — |
| `"A && B"` string splitting | Havel `&&` operator | — |
| `ConditionalHotkeyManager::currentMode` (static) | `ModeManager::currentMode` (instance) | — |
| Periodic condition polling | Event-driven `VAR_CHANGED` + `WatcherRegistry` | — |
| ~1200 lines total | 0 lines needed (reuse VM) | ~1200 |

### What should exist

```cpp
// ConditionalHotkey simplified:
struct ConditionalHotkey {
    std::string key;
    CallbackId conditionCallback;  // VM closure that returns bool
    CallbackId actionCallback;     // VM closure for the hotkey action
    bool currentlyGrabbed = false;
};

// Evaluation: just call the VM
bool shouldGrab(ConditionalHotkey& h) {
    return vm->invokeCallback(h.conditionCallback).asBool();
}
```

In Havel:
```havel
// Clean: when block already does this
when window.title.contains("Firefox") {
    F1 => { print("Firefox F1") }
    F2 => { print("Firefox F2") }
}

// Or explicit:
hotkey.register("F1", fn() { ... }, fn() => mode.current() == "gaming")
```

The `when` block already compiles to:
1. `conditionFn` = bytecode function for the condition
2. `bodyFn` = bytecode function for non-hotkey statements
3. Hotkeys inside → `hotkey.register_conditional(key, wrapper, conditionFn)`
4. `when.register(conditionFn, bodyFn)` for the reactive watcher

**The when block IS the conditional hotkey system.** Everything else is a duplicate.

## Problem 1: Duplicated Mode State

Two independent `currentMode` variables:

| Location | Type | Initialized | Updated by |
|---|---|---|---|
| `ModeManager::currentMode` | instance `std::string` | `""` (empty) | `ModeManager::setMode()`, `ModeManager::update()` |
| `ConditionalHotkeyManager::currentMode` | **static** `std::string` | `"default"` | `ConditionalHotkeyManager::SetMode()` |

### Desync paths

These code paths update `ConditionalHotkeyManager::currentMode` but **NOT** `ModeManager::currentMode`:

| File:Line | Code | What it skips |
|---|---|---|
| `HotkeyManager.cpp:462` | `conditionalManager.SetMode(mode)` | ModeManager::setMode() |
| `HostAPI.cpp:174` | `hotkeyManager->setMode(mode)` | ModeManager::setMode() |
| `IO.cpp:1382` | `hotkeyManager->setMode("default")` | ModeManager::setMode() |
| `IO.cpp:1430` | `hotkeyManager->setMode("suspend")` | ModeManager::setMode() |

The only sync is a one-way callback: `modeManager->setOnModeChange()` → `conditionalManager.SetMode()`. This keeps `ConditionalHotkeyManager` in sync when `ModeManager` changes, but NOT the reverse.

### Fix

Delete `ConditionalHotkeyManager::currentMode` (static member) and its `modeMutex`. Replace `GetMode()`/`SetMode()` with delegation to `ModeManager`:

```cpp
std::string ConditionalHotkeyManager::GetMode() const {
    if (auto mgr = modeManager.lock()) {
        return mgr->getCurrentMode();
    }
    return "default";
}
// SetMode() → deleted, callers go through ModeManager::setMode() instead
```

Then fix the desync paths to call `ModeManager::setMode()` instead of `conditionalManager.SetMode()`.

## Problem 2: Parallel Condition Systems

Three condition evaluators exist when zero are needed (VM does it):

### A. `ConditionalHotkeyManager::EvaluateConditionInternal()` — DELETE
- Hardcoded `std::string::find()` chain for mode/title/class/exe
- No operator support beyond `==`/`!=`
- No AND/OR compound conditions
- Special-cases `"gaming"` string (lines 746-749)
- Called from `EvaluateCondition()` → called from action lambdas in `AddConditionalHotkey()`

### B. `HotkeyManager` lambda evaluator — DELETE
- `HotkeyManager.cpp:84-153`: Separate string parser for `mode X`, `title X`, `class X`, `process X`, `A && B`
- Wired via `conditionalManager.SetConditionEvaluator(evalCondition)`
- Different syntax from `EvaluateConditionInternal()` (`mode gaming` vs `mode == 'gaming'`)
- Never actually called by `ConditionalHotkeyManager` (the `conditionEvaluator` field is set but the code paths that use it don't exist in practice — `EvaluateConditionInternal` is called directly)

### C. `ConditionEngine` — ALREADY DELETED (was 352 lines, zero callers)

### Fix

All three replaced by: `vm->invokeCallback(conditionClosureId).asBool()`

The `when` block + `hotkey.register_conditional` pipeline already does this correctly at the bytecode level. The C++ string condition paths are only used by C++ API callers, and all C++ callers have VM access.

**Migration**: String-condition `AddConditionalHotkey(key, string_condition, ...)` overload → deprecated. All callers should use the `std::function<bool()>` overload with a VM closure.

## Problem 3: Dead Code

### Already deleted
- `ConditionSystem.hpp/cpp` (ConditionEngine — 352 lines, never instantiated)

### To delete
- `HotkeyConditionCompiler.hpp/cpp` — `compileCondition()` produces `CompiledCondition{function_id=0/1, ip=0}` — placeholder values never consumed. The `hasCompiledCondition` flag is computed but never read. Cache works but cached values are useless.
- `HotkeyActionStateSync` class (5 methods, 0 external callers) — in `HotkeyActionContext.hpp/cpp`
- `HotkeyActionContext::setContext/getContext/hasContext/clearContext` — written by `ConditionalHotkeyManager` but never read by anything. The `hotkey_context()` builtin does not exist.
- `ConditionalHotkeyManager::conditionEvaluator` field + `SetConditionEvaluator()` — set by HotkeyManager but never actually used in evaluation paths
- `ConditionalHotkeyManager::conditionCache` + `conditionCacheMutex` + `CachedCondition` + `CACHE_DURATION_MS` — caching for string conditions that should not exist
- `ConditionalHotkeyManager::currentMode` static + `modeMutex` static — replaced by `ModeManager` delegation

### To remove from ConditionalHotkeyManager action lambdas
The `AddConditionalHotkey` string overload (lines 93-175) wraps the action in:
```cpp
auto action = [this, condition, trueAction, falseAction, async]() {
    if (EvaluateCondition(condition)) {  // ← REDUNDANT GUARD
        trueAction();
    }
};
```
This re-evaluates the condition when the hotkey fires. But the hotkey is only grabbed when the condition is true (grabbed/ungrabbed by the manager). Remove the guard — just call `trueAction()`.

Same for the function-condition overload (lines 213-296): action lambda re-evaluates `condition()` before calling `trueAction()`.

## Problem 4: ByteCompiler ModesBlock Stubs

### Enter/exit closures are placeholder integers

`ByteCompiler.cpp:2050-2063`:
```cpp
if (modeDef.enterBlock) {
    emit(OpCode::LOAD_CONST, addConstant(Value::makeInt(0)));  // ← STUB
}
if (modeDef.exitBlock) {
    emit(OpCode::LOAD_CONST, addConstant(Value::makeInt(0)));  // ← STUB
}
```

Should compile `enterBlock`/`exitBlock` to real bytecode functions and pass `Value::makeFunctionObjId(funcIndex)` as the closure, like `compileHotkeyBinding` does for action functions.

### preventRetrigger field doesn't exist

`ByteCompiler.cpp:2079`:
```cpp
// Load preventRetrigger flag (commented out - field doesn't exist)
```

Emitting `Value::makeBool(false)` as placeholder. Either add the field to `ModeDefinition` AST or remove the comment and the dead arg position.

### Fix

Compile enter/exit blocks as nested BytecodeFunctions, same pattern as hotkey action compilation:

```cpp
if (modeDef.enterBlock) {
    BytecodeFunction enterFn("mode_enter");
    enterFunction(std::move(enterFn));
    compileStatement(*modeDef.enterBlock);
    emit(OpCode::LOAD_CONST, addConstant(Value::makeNull()));
    emit(OpCode::RETURN);
    leaveFunction();
    uint32_t enterFuncIndex = compiled_functions.size() - 1;
    emit(OpCode::LOAD_CONST, addConstant(Value::makeFunctionObjId(enterFuncIndex)));
} else {
    emit(OpCode::LOAD_CONST, addConstant(Value::makeNull()));
}
```

## Problem 5: Signal/Group AST Nodes Never Reach Runtime

| Feature | Lexer | Parser | AST | ByteCompiler | Host Bridge | ModeManager |
|---|---|---|---|---|---|---|
| `signal name = expr` | ✅ | ✅ `parseSignalDefinition()` | ✅ `SignalDefinition` | ❌ no case | ❌ no `mode.defineSignal` | ✅ `defineSignal()` exists |
| `group name { modes: [...] }` | ✅ | ✅ `parseGroupDefinition()` | ✅ `GroupDefinition` | ❌ no case | ❌ no `mode.defineGroup` | ✅ `defineGroup()` exists |
| `on mode <name> { ... }` | ✅ | ✅ `parseOnModeStatementBody()` | ✅ | ❌ no case | ❌ | — |
| Window hooks (onClose, onMinimize, etc.) | ✅ | ✅ AST fields exist | ✅ | ❌ not emitted | ❌ | ✅ callbacks exist |

ModeManager has `defineSignal()` and `defineGroup()` implementations, but nobody calls them. The pipeline stops at the AST.

### Decision

Two options:

**Option A: Complete the pipeline** — Add ByteCompiler cases for `SignalDefinition`, `GroupDefinition`, `OnModeStatement`. Add `mode.defineSignal`, `mode.defineGroup` host functions. Wire everything.

**Option B: Remove the parser support** — Delete `parseSignalDefinition()`, `parseGroupDefinition()`, `parseOnModeStatementBody()`. Remove AST nodes. ModeManager's `defineSignal()`/`defineGroup()` become dead code too. Signals and groups can be expressed in Havel:

```havel
// Instead of: signal steam_running = window.any(exe == "steam.exe")
// Just use:    val steam_running = fn() => window.exe.contains("steam")

// Instead of: group gaming { modes: ["fps", "rts"] }
// Just use:   val gaming_modes = {"fps": true, "rts": true}
```

**Recommendation: Option B** — Signals and groups are Havel-level abstractions, not C++ runtime features. A signal is just a function that returns bool. A group is just a set/map. Don't build special C++ support for what the scripting language already expresses naturally. Remove the parser support and the dead `ModeManager` methods.

If signals/groups are later needed as named observable entities, they should be Havel modules (`modules/app/signals.hv`), not C++ runtime features.

## Problem 6: onEnterFrom/onExitTo Discard Parameters

`ModularHostBridges.cpp:6371-6390`:
```cpp
mode.onEnterFrom = [vm, onEnterFromId](const std::string &fromMode) {
    (void)fromMode;  // ← discarded!
    vm->invokeCallback(onEnterFromId);  // callback never gets fromMode
};
```

The `ModeDefinition` callbacks `onEnterFrom(fromMode)` and `onExitTo(toMode)` take a string parameter, but the bridge discards it when invoking the VM callback.

### Fix

Pass the mode name as an argument to the callback:
```cpp
mode.onEnterFrom = [vm, onEnterFromId](const std::string &fromMode) {
    auto* vm_ = static_cast<VM*>(vm);
    uint32_t strId = vm_->addStringConstant(fromMode);
    vm_->invokeCallback(onEnterFromId, {Value::makeStringValId(strId)});
};
```

## Problem 7: ModeService Created With Null VM

`HostModules.cpp:95`:
```cpp
ModeService(nullptr, hostAPI->GetModeManager())
```

The `nullptr` is the VM pointer. `ModeService::defineMode()` with callback IDs requires VM to invoke them. The guard `if (!m_vm || !m_manager) return;` silently fails.

### Fix

Pass the VM pointer from HostAPI or HostContext.

## Problem 8: HostAPI Mode Methods Bypass ModeManager

`HostAPI.cpp:165-176`:
```cpp
std::string HostAPI::GetCurrentMode() const {
    return hotkeyManager->getMode();  // → ConditionalHotkeyManager::GetMode()
}
void HostAPI::SetCurrentMode(const std::string &mode) {
    hotkeyManager->setMode(mode);  // → ConditionalHotkeyManager::SetMode()
}
```

These read/write `ConditionalHotkeyManager::currentMode`, NOT `ModeManager::currentMode`. After fixing Problem 1 (deleting the duplicate state), these should delegate to `modeManager->getCurrentMode()` and `modeManager->setMode()`.

## Problem 9: ModeManager::update() Called Without Evaluator

`ModeManager.cpp:301` (VAR_CHANGED handler):
```cpp
update();  // no evaluator passed
```

Without an evaluator, AST-based conditions (`conditionExpr`) are skipped — only callback-based conditions (`conditionCallback`) are evaluated. Since the bytecode pipeline uses callbacks (not raw AST), this works for Havel-defined modes. But the API is misleading.

### Fix

Either remove the `ExprEvaluator` parameter entirely (since only callbacks are used in practice) or wire it properly. Recommendation: remove `conditionExpr` from `ModeDefinition` since the VM closure path is the only one that works.

## Problem 10: ConfigProcessor::processModesBlock() Is A Stub

`ConfigProcessor.cpp:30-32`:
```cpp
void processModesBlock(...) {
    // Modes block processing (placeholder)
}
```

### Fix

Wire to `ModeManager::defineMode()` or delete if config-driven mode definition is not needed.

## Deletion Summary

### Delete entirely
| File/Class | Lines | Reason |
|---|---|---|
| `ConditionSystem.hpp/cpp` | 352+86 | Already deleted. Never instantiated. |
| `HotkeyConditionCompiler.hpp/cpp` | 96+110 | Placeholder output, never consumed |
| `HotkeyActionStateSync` (class in HotkeyActionContext) | ~55 | 5 methods, 0 callers |
| `HotkeyActionContext::setContext/getContext/hasContext/clearContext` | ~35 | Written but never read |
| `ConditionalHotkeyManager::currentMode` static + `modeMutex` static | — | Duplicated state |
| `ConditionalHotkeyManager::conditionEvaluator` + `SetConditionEvaluator()` | — | Set but unused in practice |
| `ConditionalHotkeyManager::conditionCache` + mutex + `CachedCondition` + `CACHE_DURATION_MS` | ~20 | Caching for string conditions being removed |
| `ConditionalHotkeyManager::EvaluateCondition()` + `EvaluateConditionInternal()` | ~110 | Replaced by VM closures |
| `ConditionalHotkeyManager::SetMode()` | — | Replaced by `ModeManager::setMode()` |
| `HotkeyManager` lambda evaluator (lines 84-153) | ~70 | Duplicate of EvaluateConditionInternal |
| `ModeManager::defineSignal()` + `signalList` + `Signal` struct + `isSignalActive()` | ~30 | Never called from runtime |
| `ModeManager::defineGroup()` + `groups` + `ModeGroup` struct + `triggerTransition()` group logic | ~40 | Never called from runtime |
| Parser: `parseSignalDefinition()` + `parseGroupDefinition()` + `parseOnModeStatementBody()` | ~150 | AST never reaches ByteCompiler |
| AST: `SignalDefinition` + `GroupDefinition` nodes | ~50 | Dead AST nodes |
| Lexer: `TokenType::Signal` + `TokenType::Group` | — | Dead tokens |
| `ConfigProcessor::processModesBlock()` | ~3 | Stub |
| `ModeService` null-VM constructor path | — | Silently fails |

### Modify
| File | Change |
|---|---|
| `ConditionalHotkeyManager.hpp/cpp` | Remove string-condition API, all dead fields, delegate GetMode to ModeManager |
| `HotkeyActionContext.hpp/cpp` | Remove StateSync class, remove Context set/get/clear |
| `HotkeyManager.cpp` | Remove lambda evaluator, fix setMode() to go through ModeManager |
| `HostAPI.cpp` | Fix GetCurrentMode/SetCurrentMode to use ModeManager |
| `IO.cpp` | Fix setMode("default"/"suspend") to go through ModeManager |
| `ByteCompiler.cpp` | Fix ModesBlock enter/exit stubs (placeholder 0 → real closures) |
| `ModularHostBridges.cpp` | Fix onEnterFrom/onExitTo parameter passing, fix ModeBridge::handleSet |
| `HostModules.cpp` | Fix ModeService null VM |
| `Parser.cpp` | Remove `parseSignalDefinition()`, `parseGroupDefinition()`, `parseOnModeStatementBody()` |
| `AST.h` | Remove `SignalDefinition`, `GroupDefinition` nodes, remove `Signal`/`Group` token types |
| `SemanticAnalyzer.cpp` | Fix `knownModules_["mode"]` (currently `{"get", "set", "previous", "is"}` — wrong names) |

## Implementation Order

1. **Delete dead code** — ConditionSystem (done), HotkeyConditionCompiler, HotkeyActionStateSync, HotkeyActionContext methods, condition cache
2. **Consolidate mode state** — Delete `ConditionalHotkeyManager::currentMode` static, delegate to `ModeManager`. Fix all call sites (HotkeyManager, HostAPI, IO)
3. **Remove string condition path** — Delete `EvaluateConditionInternal`, `EvaluateCondition`, `SetConditionEvaluator`, HotkeyManager lambda evaluator. Remove string overload of `AddConditionalHotkey`. Remove redundant action-guard evaluation.
4. **Fix ByteCompiler stubs** — ModesBlock enter/exit closures (placeholder 0 → real bytecode functions)
5. **Fix bridge bugs** — onEnterFrom/onExitTo param passing, ModeService null VM, HostAPI mode methods
6. **Remove dead parser/AST** — Signal/Group/OnMode support from lexer, parser, AST, ByteCompiler cases
7. **Remove dead ModeManager methods** — defineSignal, defineGroup, signalList, groups, triggerTransition group logic, isSignalActive
8. **Build and test**

## Net Effect

- ~1200 lines deleted (dead parallel condition system)
- ~250 lines deleted (dead signal/group/on-mode parser support)
- 1 mode state (ModeManager), not 2
- 1 condition evaluator (VM closures), not 3
- 1 reactive system (WatcherRegistry + VAR_CHANGED), not a polling loop
- Enter/exit blocks actually work (real closures instead of `Value::makeInt(0)`)
- onEnterFrom/onExitTo actually receive their mode name parameter