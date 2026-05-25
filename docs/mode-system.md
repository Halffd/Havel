# Mode System

The mode system allows Havel scripts to define application modes (e.g. "gaming", "coding", "media") that activate based on runtime conditions. Mode transitions can trigger callbacks and automatically re-evaluate conditional hotkeys.

---

## Quick Reference

### Define a Mode

```hv
mode.register("gaming", 10, fn => { window.active.exe == "steam.exe" })
```

Arguments: `name, priority, condition, enter, exit, onEnterFromMode, onEnterFrom, onExitToMode, onExitTo`

Full signature:

```hv
mode.register(
    "gaming",                      // name
    10,                            // priority (higher = evaluated first)
    fn => { window.active.exe == "steam.exe" },  // condition
    fn => { print("entered gaming") },            // onEnter
    fn => { print("left gaming") },               // onExit
    nil,                           // onEnterFromMode (unused)
    fn => { print("came from {mode.previous()}") },  // onEnterFrom
    nil,                           // onExitToMode (unused)
    fn => { print("going to {mode.current()}") }     // onExitTo
)
```

### Query Current Mode

```hv
mode.current()     // "gaming"
mode.previous()    // "default"
mode               // shorthand for mode.current()
```

### Set Mode Manually

```hv
mode.set("gaming")  // returns true on success
```

---

## How Modes Work

1. **Registration**: `mode.register()` creates a `ModeDefinition` with a name, priority, condition callback, and optional enter/exit callbacks.

2. **Evaluation**: When `ModeManager::update()` is called, all mode conditions are evaluated in priority order (highest first). The first mode whose condition returns true becomes the active mode.

3. **Transitions**: When the active mode changes, the old mode's `onExit` and `onExitTo` callbacks fire, then the new mode's `onEnter` and `onEnterFrom` callbacks fire.

4. **Reactive Updates**: The mode manager registers a handler for `VAR_CHANGED` events on the EventQueue. When a watched variable changes, all mode conditions are re-evaluated automatically.

5. **Default Mode**: If no mode condition is true, the system falls back to `"default"`.

---

## Mode Priority

Modes are evaluated in descending priority order. Higher priority modes win when multiple conditions are true simultaneously.

```hv
mode.register("media", 5, fn => { window.active.exe == "mpv" })
mode.register("gaming", 10, fn => { window.active.exe == "steam.exe" })
```

If both conditions are somehow true at the same time, `"gaming"` (priority 10) wins over `"media"` (priority 5).

---

## Mode and Conditional Hotkeys

All mode transitions — whether triggered by `mode.set()`, automatic condition evaluation via `ModeManager::update()`, or VAR_CHANGED events — now automatically propagate to the conditional hotkey system via the `ModeManager::onModeChange` callback, which is wired to `ConditionalHotkeyManager::SetMode()` during HotkeyManager initialization.

This means:

```hv
// Define a mode
mode.register("gaming", 10, fn => { window.active.exe == "steam.exe" })

// Conditional hotkey auto-grabs/ungrabs based on mode
^!A if mode == "gaming" => { click() }
```

When the active window changes to `steam.exe`:
1. `VAR_CHANGED` event fires
2. ModeManager re-evaluates conditions
3. `"gaming"` mode activates
4. `onModeChange` callback fires → `ConditionalHotkeyManager::SetMode("gaming")`
5. Conditional hotkey re-evaluation triggers
6. `^!A` is grabbed because `mode == "gaming"` is now true

When the active window changes away from `steam.exe`:
1. Same flow in reverse
2. `"gaming"` mode deactivates
3. `^!A` is ungrabbed

---

## When Blocks with Modes

The `when` block is syntactic sugar for conditional hotkey registration:

```hv
when mode == "gaming" {
    ^!A => click()
    ^!B => click("right")
    F1 => send("e")
}
```

Compiles to:

```hv
hotkey.register_conditional("^!A", fn => { click() }, fn => { mode == "gaming" })
hotkey.register_conditional("^!B", fn => { click("right") }, fn => { mode == "gaming" })
hotkey.register_conditional("F1", fn => { send("e") }, fn => { mode == "gaming" })
```

---

## Signals

Signals are named boolean values that can be used in mode conditions:

```hv
mode.register("gaming", 10, fn => { signal("game_running") && signal("fullscreen") })
```

Signals are defined via `ModeManager::defineSignal()` from C++ and updated via the event system.

---

## Transition Callbacks

| Callback | Signature | When Called |
|----------|-----------|------------|
| `onEnter` | `fn => { ... }` | When mode becomes active |
| `onExit` | `fn => { ... }` | When mode deactivates |
| `onEnterFrom` | `fn (fromMode) => { ... }` | When entering from a specific mode |
| `onExitTo` | `fn (toMode) => { ... }` | When exiting to a specific mode |

---

## Host Functions

| Function | Args | Returns | Description |
|----------|------|---------|-------------|
| `mode()` | - | string | Current mode name (shorthand) |
| `mode.current()` | - | string | Current mode name |
| `mode.previous()` | - | string | Previous mode name |
| `mode.set(name)` | string | bool | Set mode manually, triggers hotkey re-evaluation |
| `mode.register(name, priority, condition, enter, exit, ...)` | 9 args | bool | Define a new mode |

---

## Architecture

```
Script: mode.register("gaming", 10, fn => { ... })
         |
         v
    ModeBridge::handleRegister()
         |
         v
    ModeManager::defineMode()
         |
         v
    [VAR_CHANGED event]
         |
         v
    ModeManager::update()
         |
         v
    Evaluate conditions by priority
         |
         v
triggerEnter() / triggerExit()
|
v
onModeChange callback (fires for ALL transitions)
|
v
ConditionalHotkeyManager::SetMode()
|
v
Re-evaluate all conditional hotkeys
```

### Key Files

| File | Role |
|------|------|
| `src/core/mode/ModeManager.hpp` | ModeDefinition, Signal, ModeGroup structs |
| `src/core/mode/ModeManager.cpp` | Mode evaluation, transitions, VAR_CHANGED handler |
| `src/host/module/ModularHostBridges.cpp` | ModeBridge (install, handleRegister, handleGetCurrent, handleSet, handleGetPrevious) |
| `src/core/hotkey/HotkeyManager.cpp` | setMode/getMode forwarding to ConditionalHotkeyManager |
| `src/core/condition/ConditionalHotkeyManager.hpp` | SetMode(), condition evaluation |
| `src/core/condition/ConditionalHotkeyManager.cpp` | SetMode implementation, batch reevaluation |
