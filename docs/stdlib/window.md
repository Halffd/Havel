---
title: "Window Module"
description: "Query and control X11 windows: list, find, focus, geometry, state, monitors, and desktops."
---

# Window Module

```hv
use window
```

Accessed via the `window` global. Function names below were verified
against the live module registry.

There are two surfaces:

- **Module functions**: `window.list()`, `window.active()`, ... — module
  level, some return window objects.
- **Window object methods**: `w.title()`, `w.move(x, y)`, ... — called on
  window objects returned by module functions.

## Querying

```hv
w = window.active()        // active window object
print(w.title())           // title of active window
id = window.activeId()     // active window id
wins = window.list()       // all windows
found = window.find({ exe: "firefox" })  // returns array
one = found[0] if len(found) else null
```

Verified module-level query functions: `active`, `activeId`, `list`,
`all`, `count`, `any`, `exists`, `find`, `findAllBySpec`, `findByPid`,
`findByClass`, `findByTitle`, `pidWindow`, `filter`, `map`, `each`,
`sort`, `cmd`.

`window.findOne` does not exist — use `window.find(...)` and take index 0.

## Window object methods

Verified on objects returned by `window.active()` / `window.list()` /
`window.find(...)`:

### Identity & info

| Method | Description |
|--------|-------------|
| `w.title()` | Window title |
| `w.class()` | WM_CLASS |
| `w.exe()` | Executable name |
| `w.cmdline()` | Command line |
| `w.pid()` | Process ID |
| `w.id()` | Window ID |
| `w.type()` | Window type |
| `w.geometry()` | `{x, y, width, height, border, depth}` |
| `w.pos()` | `{x, y}` position |
| `w.size()` | `{width, height}` |
| `w.frameExtents()` | Frame extents |
| `w.states()` | Window state atoms |
| `w.isMinimized()` | Minimized state |
| `w.isMaximized()` | Maximized state |
| `w.isShaded()` | Shaded state |
| `w.isFullscreen()` | Fullscreen state |
| `w.isBorderless()` | Borderless state |
| `w.isSticky()` | Sticky state |
| `w.isSkipPager()` | Skip-pager state |
| `w.isSkipTaskbar()` | Skip-taskbar state |
| `w.isAlwaysOnTopState()` | Always-on-top state |
| `w.exists()` | Window still valid |
| `w.getOpacity()` | Current opacity |
| `w.getDesktop()` | Current desktop |
| `w.getCurrentMonitor()` | Monitor of this window |
| `w.getMonitors()` | Monitor list |

### Manipulation

| Method | Description |
|--------|-------------|
| `w.move(x, y)` | Move window |
| `w.cmd(cmd)` | Send raw command |
| `w.resize(w, h)` | Resize window |
| `w.setPos(x, y)` | Set position |
| `w.setSize(w, h)` | Set size |
| `w.moveResize(x, y, w, h)` | Move and resize |
| `w.center()` | Center window |
| `w.snap(dir)` | Snap window |
| `w.shade(v)` | Shade |
| `w.sticky(v)` | Set sticky |
| `w.stickyToDesktop()` | Stick to desktop |
| `w.borderless(v)` | Set borderless |
| `w.toggleBorderless()` | Toggle borderless |
| `w.fullscreen(v)` | Set fullscreen |
| `w.toggleFullscreen()` | Toggle fullscreen |
| `w.min()` | Minimize |
| `w.max()` | Maximize |
| `w.unmin()` | Unminimize |
| `w.unmax()` | Unmaximize |
| `w.toggleMax()` | Toggle maximize |
| `w.raise()` | Raise window |
| `w.lower()` | Lower window |
| `w.focus()` | Focus window |
| `w.show()` | Show window |
| `w.hide()` | Hide window |
| `w.restore()` | Restore window |
| `w.close()` | Send close request |
| `w.terminate()` | Force kill |
| `w.moveMonitor()` | Move to monitor |
| `w.moveMonitorNext()` | Move to next monitor |
| `w.moveMonitorPrev()` | Move to prev monitor |
| `w.skipPager(v)` | Set skip-pager |
| `w.skipTaskbar(v)` | Set skip-taskbar |
| `w.setAlwaysOnTop(v)` | Set always-on-top |
| `w.setOpacity(x)` | Set opacity |

`w.minimize()` / `w.maximize()` / `w.kill()` / `w.info()` / `w.role()` /
`w.transient()` / `w.desktop()` / `w.screenPos()` / `w.isHidden()` /
`w.isAbove()` / `w.isBelow()` / `w.decorated()` / `w.focusable()` /
`w.className()` were previously documented here but do not exist.

## Desktops & monitors

Module functions verified:

| Function | Description |
|----------|-------------|
| `window.getMonitors()` | All monitors |
| `window.getCurrentMonitor()` | Current monitor |
| `window.currentDesktop()` | Current desktop |
| `window.desktopCount()` | Number of desktops |
| `window.desktopName(i)` | Desktop name |
| `window.switchDesktop(i)` | Switch desktop |
| `window.moveToDesktop(w, i)` | Move window to desktop |
| `window.moveToMonitor(w, m)` | Move window to monitor |
| `window.moveMonitorNext(w)` | Move to next monitor |
| `window.moveMonitorPrev(w)` | Move to prev monitor |

`window.monitors()` and `window.monitorOf(w)` do not exist.

Example:

```hv
use window

mons = window.getMonitors()
for m in mons {
    print("${m.name}: ${m.width}x${m.height}")
}
```

(Window objects are object-typed; call methods with parens, e.g.
`w.title()`.)

---

**Previous:** [Clipboard Module](/stdlib/clipboard)
**Next:** [Brightness Module →](/stdlib/brightness)
