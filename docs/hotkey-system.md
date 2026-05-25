# Hotkey System

The hotkey system is Havel's core feature — it registers global key combinations with the OS, receives events when they fire, and executes script callbacks. Hotkeys are persistent by default: after a callback runs, the goroutine parks itself and waits for the next trigger.

---

## Quick Reference

### Register a Hotkey

```hv
F1 => { print("F1 pressed") }
^+A => send("hello")       // Ctrl+Shift+A
!C => map("caps", "esc")   // Alt+C
```

### Conditional Hotkey

```hv
^!S if window.active.exe == "steam.exe" => send("^s")
^V when mode == "gaming" => { click() }
```

### When Blocks

```hv
when mode == "gaming" {
    ^!A => click()
    ^!B => click("right")
    F1 if health < 50 => send("e")
}
```

---

## Modifier Key Syntax

| Symbol | Modifier |
|--------|----------|
| `^` | Ctrl |
| `+` | Shift |
| `!` | Alt |
| `#` | Super (Windows key) |

Combine modifiers by prepending them: `^+F1` = Ctrl+Shift+F1.

---

## Hotkey Policies

When a hotkey fires while its callback is already running, the policy determines what happens.

| Policy | Value | Behavior |
|--------|-------|----------|
| `drop` | 0 | Discard the new trigger (default) |
| `replace` | 1 | Kill the running callback, restart it |
| `queue` | 2 | Queue the trigger, run after current finishes |
| `coalesce` | 3 | Merge with the current trigger, update args |

Set policy at registration:

```hv
^A => { sleep(1000); send("a") } policy: "replace"
```

Or change at runtime:

```hv
hk = Hotkey.findByAlias("mykey")
hk.setPolicy("queue")
```

---

## Hotkey Object API

Every registered hotkey is a `Hotkey` object with prototype methods. Access via `Hotkey.findByAlias()` or `Hotkey.findByKey()`.

### Read-Only Properties

| Method | Returns | Description |
|--------|---------|-------------|
| `.id()` | string | Unique identifier |
| `.alias()` | string | Assigned alias |
| `.key()` | string | Key combo string (e.g. `"^+A"`) |
| `.condition()` | string or nil | Condition expression |
| `.info()` | string | Description |
| `.callback()` | fn or nil | The callback function |
| `.state()` | string | Current state (`"active"`, `"suspended"`, etc.) |
| `.modifiers()` | string | Modifier portion of combo |
| `.combo()` | string | Full combo string |
| `.addedAt()` | int | Unix timestamp (ms) when registered |
| `.count()` | int | Number of times triggered |
| `.lastTriggeredAt()` | int | Unix timestamp (ms) of last trigger |
| `.isActive()` | bool | True if runnable |
| `.isEnabled()` | bool | True if not disabled |
| `.isSuspended()` | bool | True if parked (waiting for trigger) |
| `.goroutineId()` | int or nil | ID of the backing goroutine |
| `.age()` | num | Seconds since registration |
| `.elapsed()` | num | Seconds since last trigger |
| `.getPolicy()` | string | Current policy name |
| `.toString()` | string | Human-readable summary |

### Mutation Methods

| Method | Args | Returns | Description |
|--------|------|---------|-------------|
| `.enable()` | - | self | Re-enable a disabled hotkey |
| `.disable()` | - | self | Disable hotkey (ungrab from OS) |
| `.toggle()` | - | self | Toggle enabled state |
| `.remove()` | - | nil | Remove hotkey permanently |
| `.setPolicy(name)` | string | self | Change policy (`"drop"`, `"replace"`, `"queue"`, `"coalesce"`) |
| `.setAlias(name)` | string | self | Change alias |
| `.setEnabled(bool)` | bool | self | Set enabled state |
| `.setKey(combo)` | string | self | Change key combo |
| `.setInfo(text)` | string | self | Change description |
| `.resetCount()` | - | self | Reset trigger count to 0 |
| `.removeAll()` | - | int | Remove all hotkeys, returns count |
| `.clearAll()` | - | int | Alias for removeAll |

### Comparison

| Method | Args | Returns | Description |
|--------|------|---------|-------------|
| `.equals(other)` | Hotkey | bool | True if same hotkey ID |

---

## Hotkey Static Methods

These are called on the `Hotkey` type itself (not on an instance).

| Method | Args | Returns | Description |
|--------|------|---------|-------------|
| `Hotkey.count()` | - | int | Total registered hotkeys |
| `Hotkey.findByAlias(name)` | string | Hotkey or nil | Find by alias |
| `Hotkey.findByKey(combo)` | string | Hotkey or nil | Find by key combo |
| `Hotkey.all()` | - | array | All registered hotkey objects |
| `Hotkey.activeCount()` | - | int | Count of active (runnable) hotkeys |
| `Hotkey.suspendedCount()` | - | int | Count of suspended hotkeys |
| `Hotkey.policies()` | - | array | Available policy names |
| `Hotkey.aliases()` | - | array | All registered aliases |

---

## Conditional Hotkey API

Conditional hotkeys are grabbed/released based on a runtime condition. The condition is re-evaluated when relevant variables change (event-driven, no polling).

### Host Functions

| Function | Args | Returns | Description |
|----------|------|---------|-------------|
| `hotkey.register_conditional(key, action, condition)` | string, fn, fn | bool | Register with a condition callback |
| `hotkey.remove_conditional(id)` | int | bool | Remove by ID |
| `hotkey.enable_conditional(id)` | int | bool | Enable monitoring |
| `hotkey.disable_conditional(id)` | int | bool | Disable monitoring |
| `hotkey.set_condition(id, expr)` | int, string | bool | Update condition expression |
| `hotkey.evaluate_condition(id)` | int | bool or nil | Evaluate condition now |
| `hotkey.conditional_list()` | - | array | List all conditional hotkeys |

### Conditional List Entry

Each entry in `hotkey.conditional_list()` is an object:

```hv
{
    id: 1,
    key: "^!A",
    condition: "window.active.exe == 'steam.exe'",
    enabled: true,
    active: true,
    lastResult: true
}
```

### How Conditional Evaluation Works

1. `hotkey.register_conditional()` creates a `ConditionalHotkey` with a condition function
2. The condition is evaluated immediately to decide whether to grab the key
3. When a watched variable changes (VAR_CHANGED event), `ConditionalHotkeyManager::ScheduleReevaluation()` is called
4. All conditional hotkeys are re-evaluated in batch
5. Hotkeys whose condition changed from false to true are grabbed; true to false are ungrabbed

---

## Integration with Mode System

When `mode.set(name)` is called, it also calls `HotkeyManager::setMode(name)`, which propagates to `ConditionalHotkeyManager::SetMode()`. This triggers re-evaluation of all conditional hotkeys whose conditions reference the current mode.

```hv
mode.register("gaming", 10, fn => { window.active.exe == "steam.exe" })

// This conditional hotkey auto-grabs when mode becomes "gaming"
^!A if mode == "gaming" => { click() }

// Or using when blocks
when mode == "gaming" {
    ^!A => click()
    ^!B => click("right")
}
```

---

## Persistent Goroutines

Each hotkey registration spawns a persistent goroutine that parks itself after each callback execution. This means:

- The callback goroutine is reused across triggers (no spawn cost per trigger)
- The goroutine's stack and locals are preserved between triggers
- The goroutine appears as `Suspended(HotkeyWait)` when idle
- `HotkeyPolicy::Replace` kills the current goroutine and re-queues it fresh
- `HotkeyPolicy::Queue` wakes the goroutine again after the current callback finishes

---

## Architecture

```
Script: F1 => { ... }
         |
         v
    HotkeyModule (stdlib)
         |
         v
    HostBridge::handleHotkeyRegister()
         |
         v
    HotkeyManager::AddHotkey() + GrabHotkey()
         |
         v
    Scheduler::spawn() -> persistent goroutine
         |
         v
    Goroutine parks: Suspended(HotkeyWait)
         |
         v
    [OS key event] -> EventQueue -> wakeHotkey()
         |
         v
    Scheduler::wakeGoroutine() -> goroutine resumes
         |
         v
    Callback executes, goroutine re-parks
```

### Key Files

| File | Role |
|------|------|
| `src/havel-lang/stdlib/HotkeyModule.cpp` | Prototype methods, static utilities |
| `src/havel-lang/stdlib/HotkeyModule.hpp` | HotkeyModule static interface |
| `src/host/module/ModularHostBridges.cpp` | Host function registration (InputBridge) |
| `src/core/hotkey/HotkeyManager.hpp` | OS-level hotkey grab/ungrab |
| `src/core/hotkey/HotkeyManager.cpp` | HotkeyManager implementation |
| `src/core/condition/ConditionalHotkeyManager.hpp` | Conditional hotkey data model |
| `src/core/condition/ConditionalHotkeyManager.cpp` | Event-driven reevaluation |
| `src/havel-lang/runtime/concurrency/Scheduler.hpp` | HotkeyPolicy enum, goroutine management |
| `src/havel-lang/runtime/concurrency/Scheduler.cpp` | Hotkey scheduling implementation |
