---
title: "Writing Your First Hotkey Script"
description: "Step-by-step guide to creating a hotkey-driven automation script."
---

# Writing Your First Hotkey Script

## Prerequisites

- Havel built and running (`./build-debug/havel`)
- Linux with X11
- Basic familiarity with Havel syntax

---

## Step 1: Create a Script File

Create `my_hotkeys.hv`:

```hv
// my_hotkeys.hv
print("Hotkey script loaded. Press Ctrl+C to exit.")

// Simple hotkey: F1 prints message
F1 => { print("F1 pressed!") }

// Ctrl+Shift+A sends text
^+A => { send("Hello from Havel!") }
```

---

## Step 2: Run the Script

```bash
./build-debug/havel my_hotkeys.hv
```

The script registers hotkeys and waits for events. Press `F1` or `Ctrl+Shift+A` to test. Press `Ctrl+C` to exit.

---

## Step 3: Add Conditional Hotkeys

Make hotkeys context-aware:

```hv
// Only in Firefox
^!R if window.active.exe == "firefox" => { send("^F5") }

// Only in terminal
^!T if window.active.class == "Alacritty" => { send("ls\n") }

// When block: group under condition
when window.active.title.contains("GitHub") {
    ^!I => send("i")        // Open issues
    ^!P => send("p")        // Open PRs
    ^!N => send("n")        // New issue
}
```

---

## Step 4: Use Aliases and Policies

```hv
// Named hotkey for later reference
^+S => { send("^s") } alias: "save" policy: "replace"

// Find and modify at runtime
hk = Hotkey.findByAlias("save")
hk.setPolicy("queue")
hk.setInfo("Save file (queued)")
```

---

## Step 5: Persistent State in Callbacks

Hotkey goroutines are persistent — they park between triggers:

```hv
counter = 0

F2 => {
    counter += 1
    print("F2 pressed {counter} times")
}
```

Each trigger resumes the same goroutine, preserving `counter`.

---

## Step 6: Mode Integration

```hv
// Define a mode
mode.register("coding", 10, fn => { 
    window.active.class == "code" || window.active.class == "Alacritty" 
})

// Hotkeys active only in coding mode
when mode == "coding" {
    ^+F => send("^f")      // Find
    ^+P => send("^p")      // Command palette
    ^+` => send("^`")      // Terminal
}
```

---

## Complete Example: Window Manager Helpers

```hv
// winman.hv
print("Window manager hotkeys active")

// Focus movement
^+Left  => { window.active.moveRelative(-50, 0) }
^+Right => { window.active.moveRelative(50, 0) }
^+Up    => { window.active.moveRelative(0, -50) }
^+Down  => { window.active.moveRelative(0, 50) }

// Resize
^+Shift+Left  => { window.active.resizeRelative(-50, 0) }
^+Shift+Right => { window.active.resizeRelative(50, 0) }
^+Shift+Up    => { window.active.resizeRelative(0, -50) }
^+Shift+Down  => { window.active.resizeRelative(0, 50) }

// Quick actions
^+F     => { window.active.fullscreen(!window.active.fullscreen()) }
^+M     => { window.active.minimize() }
^+Q     => { window.active.close() }

// Monitor switching
#Left   => { window.active.moveToMonitor("left") }
#Right  => { window.active.moveToMonitor("right") }
```

Run: `./build-debug/havel winman.hv`

---

## Debugging Tips

```bash
# Enable hotkey debugging
./build-debug/havel -dhk my_hotkeys.hv

# List registered hotkeys in REPL
./build-debug/havel --repl
havel> .hotkeys
```

---

**Previous:** [C++ Embedding API](/compiler/cpp-api)
**Next:** [Building a Desktop Automation Tool →](/guides/desktop-automation)