---
title: "Window Module"
description: "Window management: active window, list, focus, move, resize, geometry, and state."
---

# Window Module

```hv
use window
```

**Source**: `modules/app/window.hv` (Havel class-based wrapper around X11/FFI)

---

## Window Class

```hv
w = Window(windowId)
```

### Properties (Getters)

| Method | Returns | Description |
|--------|---------|-------------|
| `w.id` | `int` | Window X11 ID |
| `w.title()` | `str` | Window title (_NET_WM_NAME) |
| `w.className()` | `str` | Window class (WM_CLASS) |
| `w.class()` | `str` | Alias for className |
| `w.pid()` | `int` | Process ID |
| `w.exe()` | `str` | Executable path |
| `w.cmdline()` | `str` | Command line |
| `w.type()` | `str` | Window type (normal, dialog, dock, etc.) |
| `w.role()` | `str` | WM_WINDOW_ROLE |
| `w.transient()` | `int` | Transient for window ID |
| `w.desktop()` | `int` | Desktop number |
| `w.geometry()` | `object` | `{x, y, width, height, border, depth}` |
| `w.pos()` | `object` | `{x, y}` screen position |
| `w.size()` | `object` | `{width, height}` |
| `w.screenPos()` | `object` | `{x, y}` absolute screen position |
| `w.frameExtents()` | `object` | `{left, right, top, bottom}` frame borders |
| `w.state()` | `array` | Array of _NET_WM_STATE atoms |
| `w.states()` | `array` | Human-readable state names |
| `w.isMinimized()` | `bool` | Iconified/hidden |
| `w.isMaximized()` | `bool` | Vertically + horizontally maximized |
| `w.isFullscreen()` | `bool` | Fullscreen state |
| `w.isHidden()` | `bool` | Unmapped |
| `w.isAbove()` | `bool` | Above state |
| `w.isBelow()` | `bool` | Below state |
| `w.decorated()` | `bool` | Has frame decorations |
| `w.focusable()` | `bool` | Always true |
| `w.exists()` | `bool` | Window still valid |

```hv
w = Window(123456)
print(w.title())        // "Firefox"
print(w.pid())          // 12345
print(w.geometry())     // {x: 100, y: 100, width: 800, height: 600}
print(w.states())       // ["maximized_vert", "maximized_horz"]
print(w.info())         // Full info dump
```

### Focus & Activation

| Method | Returns | Description |
|--------|---------|-------------|
| `w.activate()` | `bool` | Send _NET_ACTIVE_WINDOW message |
| `w.raise()` | `bool` | Raise above siblings |
| `w.lower()` | `bool` | Lower below siblings |

```hv
w.activate()
w.raise()
```

### Visibility & State

| Method | Returns | Description |
|--------|---------|-------------|
| `w.close()` | `bool` | Send WM_DELETE_WINDOW |
| `w.kill()` | `bool` | Force kill (XKillClient) |
| `w.minimize()` | `bool` | Iconify window |
| `w.maximize()` | `bool` | Maximize (vert + horz) |
| `w.restore()` | `bool` | Remove maximized states |
| `w.hide()` | `bool` | Unmap window |
| `w.show()` | `bool` | Map window |

```hv
w.minimize()
w.maximize()
w.close()
```

### Movement & Resizing

| Method | Returns | Description |
|--------|---------|-------------|
| `w.move(x, y)` | `self` | Move window |
| `w.resize(w, h)` | `self` | Resize window |
| `w.moveResize(x, y, w, h)` | `self` | Move and resize |
| `w.center()` | `self` | Center on primary monitor |

```hv
w.move(100, 100)
w.resize(800, 600)
w.moveResize(100, 100, 800, 600)
w.center()
```

---

## Module Functions

### Active Window

```hv
window.active()  // -> Window object
```

```hv
w = window.active()
print(w.title())
```

### List Windows

```hv
window.list()  // -> array of Window objects
```

```hv
wins = window.list()
for w in wins {
    print(w.title() + " (" + w.exe() + ")")
}
```

### Find Windows

```hv
window.find(criteria)  // -> array of Window objects
window.findOne(criteria)  // -> Window object or null
```

Criteria object can contain:
- `exe`: executable name
- `class`: window class
- `title`: title substring match

```hv
# Find all Firefox windows
wins = window.find({ exe: "firefox" })

# Find by class
wins = window.find({ class: "Code" })

# Combined
win = window.findOne({ exe: "discord", title: "General" })
```

---

## Monitor Info

```hv
window.monitors()  // -> array of monitor objects
window.monitorOf(windowId)  // -> monitor object for window
```

Monitor object:
```hv
{
    name: "HDMI-0",
    x: 0, y: 0,
    width: 1920, height: 1080,
    primary: true
}
```

```hv
mons = window.monitors()
for m in mons {
    print(m.name + ": " + m.width + "x" + m.height)
}
```

---

## Example Usage

```hv
use window

# Active window
w = window.active()
print("Active: " + w.title())

# List all
for w in window.list() {
    print(w.title() + " [" + w.exe() + "]")
}

# Find specific
firefox = window.findOne({ exe: "firefox" })
if firefox {
    firefox.activate()
    firefox.moveResize(0, 0, 1920, 1080)
}

# Snap to left half
w = window.active()
w.moveResize(0, 0, 960, 1080)
```

---

**Previous:** [Clipboard Module](/stdlib/clipboard)
**Next:** [Brightness Module →](/stdlib/brightness)