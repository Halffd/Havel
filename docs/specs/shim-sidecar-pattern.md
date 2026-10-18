# Spec: shim-sidecar-pattern

Scope: feature

# Spec: C++ Shim + Havel Sidecar Migration Pattern

## Goal
Establish a reusable pattern for converting C++ host modules into thin C++ shims + pure Havel sidecars, moving business logic from C++ to Havel while keeping OS/VM intrinsics in C++.

## Architecture

```
Before:                          After:
┌──────────────────┐            ┌──────────────┐  ┌──────────────────┐
│ C++ Host Module  │            │ C++ Shim      │  │ Havel Sidecar    │
│ - OS bridge      │            │ - mod._* host │  │ - mod.func()     │
│ - VM intrinsics  │  ──────►  │   functions   │  │   wraps mod._*   │
│ - Business logic │            │ - Sidecar     │  │ - Business logic │
│ - Data objects   │            │   loader      │  │ - Data classes   │
│ - Proto methods  │            │ - mergeExports│  │ - Public API     │
└──────────────────┘            └──────────────┘  └──────────────────┘
```

## Pattern Rules

### 1. C++ Shim: Only OS Bridges and VM Intrinsics
- Rename all remaining C++ functions to `mod._funcName` (underscore prefix)
- `_` prefix = "raw, no defaults, no validation, no business logic"
- Shim loads sidecar via `vm.loadModule("mod/mod")`
- Shim calls `mergeExports()` to copy non-underscore exports into namespace object and globals

### 2. Havel Sidecar: Business Logic + Public API
- Located at `modules/app/mod.hv` (or `modules/std/mod.hv` for stdlib)
- Wraps `mod._*` with defaults, validation, error messages, mode strings
- Exposes clean public API (no underscore prefix)
- Can define classes/structs for data that was C++ prototype objects
- `mergeExports` copies these into the `mod` namespace object automatically

### 3. mergeExports Pattern
```cpp
// In C++ shim, after vm.loadModule("mod/mod"):
vm.mergeExports(nsObj, "mod");  // copies non-_ fields from loaded module to nsObj + globals
```
- Iterates loaded module's exports
- Skips keys starting with `_`
- Calls `api.setField(nsObj, key, value)` and `api.setGlobal(key, value)`
- This means sidecar's `fn foo()` becomes both `mod.foo` and global `foo`

### 4. When NOT to Use This Pattern
- Module is pure OS bridge with zero business logic (WindowMonitorModule → keep C++, but could add sidecar for OOP wrapping)
- Module has no Havel-accessible path for its core operations (ObjectModule → all 16 functions need VM heap intrinsics)
- Module is too small to justify a sidecar (ArrayModule → 2 duplicate functions, sidecar plumbing would be more code)

### 5. Class Migration Pattern
When a C++ module creates prototype objects with many methods (like HotkeyModule's `Hotkey` object):
```havel
// modules/app/hotkey.hv
class Hotkey {
    id
    alias
    key
    fn new(id, alias, key) {
        @id = id
        @alias = alias
        @key = key
    }
    fn enable() { hotkey._enable(@id) }
    fn disable() { hotkey._disable(@id) }
    fn toggle() { hotkey._toggle(@id) }
    fn isActive() { hotkey._isActive(@id) }
    fn count() { hotkey._count(@id) }
    fn elapsed() { hotkey._elapsed(@id) }
    // ... business logic methods ...
}
```
- Move data (id, alias, key, enabled, etc.) from C++ struct to Havel class fields
- Move operations from C++ prototype methods to Havel class methods
- C++ shim only provides `hotkey._enable(id)`, `hotkey._disable(id)`, etc. (raw ID-based operations)

### 6. Module Loading Paths
VM search paths for `vm.loadModule()`:
1. `modules/lang/` — self-hosted compiler
2. `modules/std/` — standard library sidecars
3. `modules/app/` — application sidecars
4. `modules/` (root) — fallback

### 7. Smoke Test Compatibility
- `io._*` functions must return safe default values when IO is null (test env)
- Sidecar functions should handle null/failure from `mod._*` gracefully
- Use `hvtest --smoke` to verify — it creates VM without IO/HotkeyManager