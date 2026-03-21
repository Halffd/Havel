# CallbackId Architecture - Complete Flow

## The Problem We Solved

**BEFORE (WRONG):** ModeManager held VM closures directly
```cpp
struct ModeDefinition {
    std::function<void()> onEnter;  // Holds closure ❌
};
// Problems: GC hazards, circular dependencies, unclear lifetime
```

**AFTER (CORRECT):** VM owns closures, systems use CallbackId
```cpp
// VM owns ALL closures
CallbackId id = vm.registerCallback(closure);  // VM pins closure

// Systems store only IDs
struct ModeBinding {
    std::optional<CallbackId> enter_id;  // Opaque handle ✅
};
```

## Complete Callback Flow

### 1. Registration (HostBridge::handleModeDefine)

```cpp
// HostBridge.cpp
CallbackId HostBridgeRegistry::handleModeDefine(...) {
    // 1. Get closure from script, register with VM
    CallbackId enterId = registerCallback(args[1]);  // VM pins closure
    
    // 2. Store CallbackId in HostBridge (not in ModeManager!)
    mode_bindings_[mode_name].enter_id = enterId;
    
    // 3. Create thin lambda wrapper for ModeManager
    mode.onEnter = [weak_self, enterId]() {
        self->invokeCallback(enterId);  // ← Calls VM through HostBridge
    };
    
    // 4. Give lambda to ModeManager (std::function holds LAMBDA, not closure)
    deps_.mode_manager->defineMode(std::move(mode));
}
```

### 2. Storage

```
┌─────────────────────────────────────────────────────────────┐
│  VM                                                          │
│  externalRoots[enterId] → Closure (the actual function)     │
└─────────────────────────────────────────────────────────────┘
         ↑
         │ CallbackId (uint32_t)
         │
┌─────────────────────────────────────────────────────────────┐
│  HostBridge                                                  │
│  mode_bindings_[mode].enter_id = enterId                    │
└─────────────────────────────────────────────────────────────┘
         ↑
         │ Lambda (captures enterId)
         │
┌─────────────────────────────────────────────────────────────┐
│  ModeManager (core/)                                         │
│  ModeDefinition.onEnter = [enterId]() { invoke(enterId); }  │
│  (std::function holds LAMBDA, not closure!)                 │
└─────────────────────────────────────────────────────────────┘
```

### 3. Invocation

```cpp
// ModeManager triggers callback
mode.onEnter();  // Calls lambda
    ↓
// Lambda invokes through HostBridge
invokeCallback(enterId);
    ↓
// HostBridge invokes through VM
vm_.invokeCallback(enterId);
    ↓
// VM retrieves closure from externalRoots and executes
closure = externalRoots[enterId];
call(closure);
```

### 4. Cleanup

```cpp
// HostBridge::clear()
for (auto& [mode, binding] : mode_bindings_) {
    if (binding.enter_id) {
        releaseCallback(binding.enter_id);  // VM unpins, GC can collect
    }
}
```

## Key Insights

### ✅ std::function in ModeManager is OK

ModeManager still has `std::function<void()> onEnter`, but it holds a **lambda**, not the closure:

```cpp
// What ModeManager stores:
std::function<void()> onEnter = [weak_self, enterId]() {
    self->invokeCallback(enterId);  // Just invokes by ID
};

// What it does NOT store:
std::function<void()> onEnter = [closure]() {  // ❌ This would be wrong
    call(closure);
};
```

The lambda is tiny (just captures enterId and weak_self). The VM owns the actual closure.

### ✅ CallbackId is Opaque

Services and ModeManager don't know what CallbackId means. They just store and pass it around. Only VM and HostBridge know how to use it.

### ✅ One GC Owner

VM pins ALL closures via `pinExternalRoot()`. When `releaseCallback()` is called, VM unpins and GC can collect.

## Service Layer Status

Services like `ModeService`, `HotkeyService`, `IOService` **don't need CallbackId** because:

1. **They don't store callbacks** - They do pure operations (getVolume, setBrightness, etc.)
2. **Callbacks are handled by HostBridge** - HostBridge registers with VM, stores CallbackId
3. **Services are VM-agnostic** - They don't know about closures, GC, or VM internals

### Example: HotkeyService

```cpp
// HotkeyService.hpp - No CallbackId needed!
class HotkeyService {
public:
    bool registerHotkey(const std::string& key,
                        std::function<void()> callback,  // ← Called by hotkey system
                        int id);
};

// HostBridge.cpp - HostBridge handles CallbackId
CallbackId callbackId = registerCallback(args[1]);  // Get ID from VM

hotkeyService->registerHotkey(key, 
    [weak_self, callbackId]() {  // ← Lambda wraps invokeCallback
        self->invokeCallback(callbackId);
    },
    callbackId);
```

HotkeyService just calls the callback it's given. It doesn't know the callback is a lambda that invokes through VM.

## Summary

| Component | Uses CallbackId? | Why/Why Not |
|-----------|-----------------|-------------|
| **VM** | ✅ Yes | Owns closures, provides register/invoke/release |
| **HostBridge** | ✅ Yes | Stores CallbackId, creates lambda wrappers |
| **ModeManager** | ❌ No (uses std::function) | Holds lambdas, not closures |
| **Services** | ❌ No | Pure operations, no callbacks |
| **Modules** | ❌ No | Thin bindings, delegate to services |

## The Architecture is Correct ✅

The CallbackId pattern is fully implemented:
- VM owns all closures
- HostBridge stores CallbackId
- ModeManager holds lambda wrappers (not closures)
- Services are VM-agnostic
- One GC owner (VM)
- Clear lifetime (releaseCallback frees)

This is exactly how Lua, game engines, and Unreal/Unity do scripting integration.
