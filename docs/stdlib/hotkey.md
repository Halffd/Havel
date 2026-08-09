---
title: "Hotkey Module"
description: "Hotkey registration, conditional hotkeys, and Hotkey object API with full prototype methods."
---

# Hotkey Module

```hv
use hotkey
```

**Source**: `src/havel-lang/stdlib/HotkeyModule.cpp` (C++ prototype methods on Hotkey objects + global static methods)

---

## Hotkey Registration

### Basic Registration (Arrow Syntax)

```hv
F1 => { print("F1 pressed") }
^+A => { send("hello") }
```

The `=>` arrow syntax registers a persistent hotkey. The callback runs in a dedicated goroutine that parks between triggers.

### Conditional Hotkeys

```hv
# Postfix conditional
^!S if window.active.exe == "chrome" => { send("^F5") }

# When block (grouped condition)
when mode == "gaming" {
    ^!A => { click() }
    ^!B => { click("right") }
}
```

### Host Functions (for dynamic registration)

```hv
hotkey.register(key, action, policy?, alias?)    // Register hotkey
hotkey.register_conditional(key, action, condition, alias?)  // Conditional
```

---

## Hotkey Object (Prototype Methods)

Every registered hotkey is a `Hotkey` object. Access via `Hotkey.findByAlias()` or `Hotkey.findByKey()`.

### Properties (Read-Only)

| Method | Returns | Description |
|--------|---------|-------------|
| `hk.id()` | `str` | Unique identifier |
| `hk.alias()` | `str` | Assigned alias |
| `hk.key()` | `str` | Key combo (e.g., `"^+A"`) |
| `hk.condition()` | `str?` | Condition expression |
| `hk.info()` | `str` | Description |
| `hk.callback()` | `fn?` | Callback function |
| `hk.status()` | `str` | Live: "registered", "running", "suspended", "disabled", "stopped" |
| `hk.modifiers()` | `str` | Modifier portion |
| `hk.combo()` | `str` | Full combo string |
| `hk.addedAt()` | `int` | Unix ms when registered |
| `hk.count()` | `int` | Times triggered |
| `hk.lastTriggeredAt()` | `int` | Unix ms of last trigger (-1 if never) |
| `hk.isActive()` | `bool` | Running/runnable/created |
| `hk.isEnabled()` | `bool` | Not disabled |
| `hk.isSuspended()` | `bool` | Parked waiting for trigger |
| `hk.goroutineId()` | `int` | Scheduler goroutine ID |
| `hk.age()` | `int` | Milliseconds since registration |
| `hk.elapsed()` | `int` | Milliseconds since last trigger (-1 if never) |
| `hk.getPolicy()` | `str` | Current policy name |
| `hk.policy` | `str` | Live policy (property) |
| `hk.toString()` | `str` | Human-readable summary |

### Mutation Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `hk.enable()` | `bool` | Re-enable disabled hotkey |
| `hk.disable()` | `bool` | Disable (ungrab from OS) |
| `hk.toggle()` | `bool` | Toggle enabled state |
| `hk.remove()` | `bool` | Remove permanently |
| `hk.setPolicy(name)` | `bool` | Change policy: "drop", "replace", "queue", "coalesce" |
| `hk.setAlias(name)` | `bool` | Change alias |
| `hk.setEnabled(bool)` | `bool` | Set enabled state |
| `hk.setKey(combo)` | `bool` | Change key combo |
| `hk.setInfo(text)` | `bool` | Change description |
| `hk.resetCount()` | `bool` | Reset trigger counter |
| `hk.stop()` | `bool` | Stop goroutine, disable OS hotkey |
| `hk.resume()` | `bool` | Unpark suspended goroutine |
| `hk.wait(timeoutMs?)` | `bool` | Block until done/suspended |
| `hk.trigger()` | `bool` | Programmatically trigger |
| `hk.edit(props)` | `bool` | Update multiple properties |

### Comparison

| Method | Returns | Description |
|--------|---------|-------------|
| `hk.equals(other)` | `bool` | Same hotkey ID |

---

## Static Methods (on `Hotkey` Global)

| Method | Returns | Description |
|--------|---------|-------------|
| `Hotkey.count()` | `int` | Total registered contexts |
| `Hotkey.findByAlias(name)` | `Hotkey?` | Find by alias |
| `Hotkey.findByKey(combo)` | `array` | Find all matching key |
| `Hotkey.all()` | `array` | All hotkey objects |
| `Hotkey.activeCount()` | `int` | Running/runnable count |
| `Hotkey.suspendedCount()` | `int` | Suspended count |
| `Hotkey.policies()` | `array` | ["drop", "replace", "queue", "coalesce"] |
| `Hotkey.aliases()` | `array` | All registered aliases |

---

## Policies

When a hotkey fires while its callback is already running:

| Policy | Value | Behavior |
|--------|-------|----------|
| `drop` | 0 | Discard new trigger (default) |
| `replace` | 1 | Kill running, restart fresh |
| `queue` | 2 | Queue trigger, run after current |
| `coalesce` | 3 | Merge with current, update args |

```hv
# At registration
^+A => { sleep(1000) } policy: "replace"

# At runtime
hk = Hotkey.findByAlias("mykey")
hk.setPolicy("queue")
```

---

## Conditional Hotkeys (Host Functions)

```hv
hotkey.register_conditional("^!S", fn { send("^s") }, fn => window.active.exe == "chrome")
```

### Conditional Management

| Function | Signature | Description |
|----------|-----------|-------------|
| `hotkey.remove_conditional(id)` | `(int) -> bool` | Remove by ID |
| `hotkey.enable_conditional(id)` | `(int) -> bool` | Enable monitoring |
| `hotkey.disable_conditional(id)` | `(int) -> bool` | Disable monitoring |
| `hotkey.set_condition(id, expr)` | `(int, str) -> bool` | Update condition |
| `hotkey.evaluate_condition(id)` | `(int) -> bool` | Evaluate now |
| `hotkey.conditional_list()` | `() -> array` | List all conditionals |

---

## Example Usage

```hv
use hotkey

# Register via arrow syntax
^+A => { send("hello") } alias: "greeting" policy: "replace"

# Access hotkey object
hk = Hotkey.findByAlias("greeting")
print(hk.key())           // "^+A"
print(hk.getPolicy())     // "replace"
print(hk.count())         // 0
print(hk.toString())      // "Hotkey<greeting key=^+A policy=replace enabled=true count=0>"

# Modify at runtime
hk.setPolicy("queue")
hk.setInfo("Send greeting")

# Static methods
print("Total: " + Hotkey.count())
print("Active: " + Hotkey.activeCount())
print("Aliases: " + Hotkey.aliases().join(", "))
```

---

## Conditional Hotkey Example

```hv
# Only active in Chrome
id = hotkey.register_conditional(
    "^!R",
    fn { send("^F5") },
    fn => window.active.exe == "chrome"
)

# Disable/enable
hotkey.disable_conditional(id)
hotkey.enable_conditional(id)

# Update condition
hotkey.set_condition(id, "mode == 'gaming' && health < 50")

# List all
for c in hotkey.conditional_list() {
    print(c.key + " -> " + c.condition + " (" + (c.enabled ? "on" : "off") + ")")
}
```

---

## When Blocks

```hv
when mode == "gaming" {
    ^!A => { click() }
    ^!B => { click("right") }
    F1 if health < 50 => { send("e") }
}
```

---

**Previous:** [Brightness Module](/stdlib/brightness)
**Next:** [API Reference →](/reference/host-functions)