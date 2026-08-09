---
title: "Hotkeys"
description: "Hotkey registration, modifiers, policies, conditional hotkeys, when blocks, and Hotkey object API."
---

# Hotkeys

The hotkey system registers global X11 key combinations, receives events when they fire, and executes script callbacks. Hotkeys are **persistent**: after a callback runs, the goroutine parks and waits for the next trigger.

---

## Basic Registration

```hv
F1 => { print("F1 pressed") }
^+A => send("hello")        // Ctrl+Shift+A
!C => map("caps", "esc")    // Alt+C
#F4 => { window.active.close() }  // Super+F4
```

### Modifier Prefixes

| Symbol | Modifier |
|--------|----------|
| `^` | Ctrl |
| `+` | Shift |
| `!` | Alt |
| `#` | Super (Win/Command) |

Combine by prepending: `^+F1` = Ctrl+Shift+F1.

**NOT allowed**: `hotkey "Ctrl+Shift+F1" { }` or `hotkey.register()` — use `=>` arrow form only.

---

## Hotkey Policies

When a hotkey fires while its callback is already running:

| Policy | Value | Behavior |
|--------|-------|----------|
| `drop` | 0 | Discard new trigger (default) |
| `replace` | 1 | Kill running callback, restart fresh |
| `queue` | 2 | Queue trigger, run after current finishes |
| `coalesce` | 3 | Merge with current trigger, update args |

### Setting Policy

```hv
// At registration
^A => { sleep(1000); send("a") } policy: "replace"

// At runtime
hk = Hotkey.findByAlias("mykey")
hk.setPolicy("queue")
```

---

## Conditional Hotkeys

Conditional hotkeys are grabbed/released based on a runtime condition. The condition is re-evaluated when relevant variables change (event-driven, no polling).

### Syntax

```hv
// Postfix conditional
^!S if window.active.exe == "chrome" => send("^F5")

// Prefix conditional
^V when mode == "gaming" => { click() }
```

### When Blocks

Group multiple hotkeys under a shared condition:

```hv
when mode == "gaming" {
    ^!A => click()
    ^!B => click("right")
    F1 if health < 50 => send("e")
}
```

### Host Functions

| Function | Args | Returns | Description |
|----------|------|---------|-------------|
| `hotkey.register_conditional(key, action, condition)` | str, fn, fn | bool | Register with condition callback |
| `hotkey.remove_conditional(id)` | int | bool | Remove by ID |
| `hotkey.enable_conditional(id)` | int | bool | Enable monitoring |
| `hotkey.disable_conditional(id)` | int | bool | Disable monitoring |
| `hotkey.set_condition(id, expr)` | int, str | bool | Update condition expression |
| `hotkey.evaluate_condition(id)` | int | bool | Evaluate condition now |
| `hotkey.conditional_list()` | — | array | List all conditional hotkeys |

### How It Works

1. `register_conditional()` creates `ConditionalHotkey` with condition function
2. Condition evaluated immediately to decide whether to grab key
3. On `VAR_CHANGED` event, `ConditionalHotkeyManager::ScheduleReevaluation()` called
4. All conditionals re-evaluated in batch
5. False→true: `GrabHotkey()`, true→false: `UngrabHotkey()`

---

## Integration with Mode System

```hv
mode.register("gaming", 10, fn => { window.active.exe == "steam.exe" })

// Auto-grabs when mode becomes "gaming"
^!A if mode == "gaming" => { click() }

// When blocks auto-sync with mode
when mode == "gaming" {
    ^!A => click()
    ^!B => click("right")
}
```

When `mode.set(name)` is called, it triggers re-evaluation of all conditional hotkeys referencing the mode.

---

## Hotkey Object API

Every registered hotkey is a `Hotkey` object. Access via `Hotkey.findByAlias()` or `Hotkey.findByKey()`.

### Read-Only Properties

| Method | Returns | Description |
|--------|---------|-------------|
| `.id()` | string | Unique identifier |
| `.alias()` | string | Assigned alias |
| `.key()` | string | Key combo (e.g., `"^+A"`) |
| `.condition()` | string/nil | Condition expression |
| `.info()` | string | Description |
| `.callback()` | fn/nil | Callback function |
| `.state()` | string | `"active"`, `"suspended"`, etc. |
| `.modifiers()` | string | Modifier portion |
| `.combo()` | string | Full combo string |
| `.addedAt()` | int | Unix ms when registered |
| `.count()` | int | Times triggered |
| `.lastTriggeredAt()` | int | Unix ms of last trigger |
| `.isActive()` | bool | Runnable |
| `.isEnabled()` | bool | Not disabled |
| `.isSuspended()` | bool | Parked waiting |
| `.goroutineId()` | int/nil | Backing goroutine ID |
| `.age()` | num | Seconds since registration |
| `.elapsed()` | num | Seconds since last trigger |
| `.getPolicy()` | string | Current policy name |
| `.toString()` | string | Human-readable summary |

### Mutation Methods

| Method | Args | Returns | Description |
|--------|------|---------|-------------|
| `.enable()` | — | self | Re-enable disabled hotkey |
| `.disable()` | — | self | Disable (ungrab from OS) |
| `.toggle()` | — | self | Toggle enabled state |
| `.remove()` | — | nil | Remove permanently |
| `.setPolicy(name)` | string | self | Change policy |
| `.setAlias(name)` | string | self | Change alias |
| `.setEnabled(bool)` | bool | self | Set enabled state |
| `.setKey(combo)` | string | self | Change key combo |
| `.setInfo(text)` | string | self | Change description |
| `.resetCount()` | — | self | Reset trigger count |
| `.removeAll()` | — | int | Remove all hotkeys |
| `.clearAll()` | — | int | Alias for removeAll |

### Comparison

| Method | Args | Returns | Description |
|--------|------|---------|-------------|
| `.equals(other)` | Hotkey | bool | Same hotkey ID |

---

## Static Methods

Called on `Hotkey` type (not instance):

| Method | Args | Returns | Description |
|--------|------|---------|-------------|
| `Hotkey.count()` | — | int | Total registered |
| `Hotkey.findByAlias(name)` | string | Hotkey/nil | Find by alias |
| `Hotkey.findByKey(combo)` | string | Hotkey/nil | Find by key combo |
| `Hotkey.all()` | — | array | All hotkey objects |
| `Hotkey.activeCount()` | — | int | Active (runnable) count |
| `Hotkey.suspendedCount()` | — | int | Suspended count |
| `Hotkey.policies()` | — | array | Available policy names |
| `Hotkey.aliases()` | — | array | All registered aliases |

---

## On/Off Key Events

```hv
on keydown keylist { body }
on keyup keylist { body }
off keydown keylist
off keyup keylist
```

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

---

**Previous:** [Modules](/language/modules)
**Next:** [DSL & Input Commands →](/language/dsl)