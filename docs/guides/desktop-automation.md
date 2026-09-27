---
title: "Building a Desktop Automation Tool"
description: "Create a complete desktop automation tool with window management, input injection, and conditional logic."
---

# Building a Desktop Automation Tool

## Project Structure

```
automation/
  main.hv           # Entry point
  config.hv         # Configuration
  window.hv         # Window management
  input.hv          # Input automation
  modes.hv          # Mode system
  utils.hv          # Helpers
```

---

## Configuration (config.hv)

```hv
// config.hv
config {
    // Hotkey step sizes
    move_step = 50
    resize_step = 50
    
    // Mode priorities
    modes = {
        gaming: 100
        coding: 50
        default: 0
    }
    
    // Application-specific settings
    apps = {
        browser: { exe: "firefox", class: "Firefox" }
        editor: { exe: "code", class: "Code" }
        terminal: { exe: "alacritty", class: "Alacritty" }
    }
}

// Monitor configs
monitor "HDMI-0" { primary: true }
monitor "DP-1" { primary: false }
```

---

## Window Management (window.hv)

```hv
// window.hv
use config

fn get_active() => window.active()

fn move(dx, dy) {
    win = get_active()
    p = win.pos()
    win.move(p.x + dx, p.y + dy)
}

fn resize(dw, dh) {
    win = get_active()
    s = win.size()
    win.resize(s.w + dw, s.h + dh)
}

fn move_to_monitor(dir) {
    win = get_active()
    monitors = window.getMonitors()
    current = win.getCurrentMonitor()
    
    target = if dir == "left" {
        array.find(array.reversed(monitors), m => m.x < current.x)
    } else if dir == "right" {
        array.find(monitors, m => m.x > current.x)
    } else { null }
    
    if target { win.moveMonitor(target.name) }
}

fn snap(direction) {
    win = get_active()
    mon = win.getCurrentMonitor()
    
    match direction {
        "left" => win.moveResize(mon.x, mon.y, mon.w / 2, mon.h)
        "right" => win.moveResize(mon.x + mon.w / 2, mon.y, mon.w / 2, mon.h)
        "top" => win.moveResize(mon.x, mon.y, mon.w, mon.h / 2)
        "bottom" => win.moveResize(mon.x, mon.y + mon.h / 2, mon.w, mon.h / 2)
        "max" => win.max()
    }
}
```

---

## Input Automation (input.hv)

```hv
// input.hv
// dsl { } is a statement form, not a module — no `use dsl`.

fn type_text(text, delay = 50) {
    dsl {
        "${text}"
        : delay
    }
}

fn send_keys(keys) {
    for k in keys { io.sendKey(k) }
}

fn click_at(x, y, button = "left") {
    dsl {
        m(x, y)
        : 100
        click(button)
    }
}

fn drag(from_x, from_y, to_x, to_y) {
    dsl {
        m(from_x, from_y)
        io.mouseDown(1)
        : 100
        m(to_x, to_y)
        io.mouseUp(1)
    }
}
```

Inside `dsl` blocks: bare `"${text}"` sends text, `: ms` sleeps,
`m(x, y)` moves the mouse, `click("left"|"right"|"middle")` clicks,
`{key}` sends a key. `lmb_down`/`lmb_up` are not implemented — use
`io.mouseDown(1)` / `io.mouseUp(1)` (1 = left button).

---

## Modes (modes.hv)

```hv
// modes.hv
use config

// Gaming mode
mode.register("gaming", config.modes.gaming, fn => {
    win = window.active()
    win.exe == "steam" || win.exe == "lutris" || string.match(win.title, ".*Game.*")
})

// Coding mode
mode.register("coding", config.modes.coding, fn => {
    win = window.active()
    win.class == "Code" || win.class == "Alacritty" || win.exe == "vim"
})

// Browser mode
mode.register("browser", 30, fn => {
    win = window.active()
    win.exe == "firefox" || win.exe == "chrome" || win.class == "Firefox"
})

// Mode transition handlers — the mode module exposes `set`,
// `register`, `current`, `previous`, `transitions`, `list`, `time`.
// React to switches from hotkeys instead:
^+F9 => {
    mode.set("gaming")
    brightness.set(0.8)
    print("🎮 Gaming mode")
}
```

---

## Main Entry Point (main.hv)

```hv
// main.hv
use config
use window
use input
use modes

print("Desktop automation loaded")
print("Modes: " + array.join(object.keys(config.modes), ", "))

// Global hotkeys
^+Escape => { print("Exiting..."); exit(0) }

// Mode-aware window management
when mode == "gaming" {
    // Gaming: WASD for movement
    ^+W => window.move(0, -config.move_step)
    ^+S => window.move(0, config.move_step)
    ^+A => window.move(-config.move_step, 0)
    ^+D => window.move(config.move_step, 0)
}

when mode == "coding" {
    // Coding: window snap
    ^+H => window.snap("left")
    ^+L => window.snap("right")
    ^+K => window.snap("top")
    ^+J => window.snap("bottom")
    ^+M => window.snap("max")
    
    // Terminal toggle
    ^+` => { 
        hits = window.find({ class: "Alacritty" })
        if len(hits) > 0 { hits[0].focus() } else { process.spawn("alacritty") }
    }
}

when mode == "browser" {
    // Browser: tab management
    ^+T => input.send_keys(["Ctrl", "t"])
    ^+W => input.send_keys(["Ctrl", "w"])
    ^+Shift+T => input.send_keys(["Ctrl", "Shift", "t"])
    ^+Tab => input.send_keys(["Ctrl", "Tab"])
    ^+Shift+Tab => input.send_keys(["Ctrl", "Shift", "Tab"])
}

// Monitor hotkeys (always active)
#Left  => window.move_to_monitor("left")
#Right => window.move_to_monitor("right")

// Brightness
^+F1 => brightness.decrease(0.1)
^+F2 => brightness.increase(0.1)

// Volume (via shell)
^+F3 => shell.run("pactl set-sink-volume @DEFAULT_SINK@ -5%")
^+F4 => shell.run("pactl set-sink-volume @DEFAULT_SINK@ +5%")

// Screenshot
Print => {
    screenshot.capture()
    print("Screenshot saved")
}
```

---

## Running the Tool

```bash
./build-debug/havel automation/main.hv
```

---

## Extending: Add Application-Specific Profiles

```hv
// profiles/discord.hv
when window.active.exe == "discord" {
    ^+M => input.send_keys(["Ctrl", "Shift", "m"])  // Mute
    ^+D => input.send_keys(["Ctrl", "Shift", "d"])  // Deafen
    ^+Shift+S => input.send_keys(["Ctrl", "Shift", "s"])  // Stream
}
```

```hv
// profiles/browser.hv
when string.match(window.active.exe, "firefox|chrome") {
    ^+L => input.send_keys(["Ctrl", "l"])      // Address bar
    ^+R => input.send_keys(["Ctrl", "r"])      // Reload
    ^+Shift+R => input.send_keys(["Ctrl", "Shift", "r"])  // Hard reload
}
```

Load dynamically:
```hv
// Load dynamically with `process.run("havel profiles/discord.hv")` or
// gate usage on the current app inside your `when` blocks.```

---

## Debugging

```bash
# Debug mode
./build-debug/havel -d -dhk automation/main.hv

# Test specific functions in REPL
./build-debug/havel --repl
havel> use window
havel> window.active()
```

---

**Previous:** [First Hotkey Script](/guides/first-hotkey)
**Next:** [Creating a Web Server →](/guides/web-server)