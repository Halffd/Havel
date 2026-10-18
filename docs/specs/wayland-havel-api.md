# Spec: wayland-havel-api

Scope: feature

# Wayland Native Input — Havel Script API Feature Spec

## Module: `wayland`

Havel scripts access Wayland input simulation through a `wayland` module object with sub-namespaces.

```havel
use wayland
```

### `wayland.keyboard`

| Method | Signature | Description |
|--------|-----------|-------------|
| `press` | `wayland.keyboard.press(key: str)` | Press a key by name (e.g. "ctrl", "a", "F1") |
| `release` | `wayland.keyboard.release(key: str)` | Release a key by name |
| `tap` | `wayland.keyboard.tap(key: str)` | Press and immediately release a key |
| `type` | `wayland.keyboard.type(text: str)` | Type a string of text (unicode-aware) |
| `modifiers` | `wayland.keyboard.modifiers({ctrl: bool, alt: bool, shift: bool, super: bool})` | Set modifier state directly |

Key names follow XKB keysym names: `"a"`, `"Return"`, `"space"`, `"ctrl"`, `"alt"`, `"shift"`, `"super"`, `"F1"`-`"F20"`, `"Escape"`, `"Tab"`, `"BackSpace"`, `"Up"`, `"Down"`, `"Left"`, `"Right"`, etc.

Modifier shorthand: `"ctrl"` = `"Control_L"`, `"alt"` = `"Alt_L"`, `"shift"` = `"Shift_L"`, `"super"` = `"Super_L"`.

### `wayland.mouse`

| Method | Signature | Description |
|--------|-----------|-------------|
| `move` | `wayland.mouse.move(dx: int, dy: int)` | Move pointer by relative offset |
| `moveTo` | `wayland.mouse.moveTo(x: int, y: int)` | Move pointer to absolute coordinates |
| `click` | `wayland.mouse.click(button: str)` | Click a mouse button (`"left"`, `"middle"`, `"right"`, `"side"`, `"extra"`) |
| `press` | `wayland.mouse.press(button: str)` | Press and hold a mouse button |
| `release` | `wayland.mouse.release(button: str)` | Release a held mouse button |
| `scroll` | `wayland.mouse.scroll(dy: int, dx: int)` | Scroll by discrete steps (positive = down/right) |
| `scrollSmooth` | `wayland.mouse.scrollSmooth(dy: num, dx: num)` | Smooth scroll by arbitrary amounts |

### `wayland.clipboard`

| Method | Signature | Description |
|--------|-----------|-------------|
| `get` | `wayland.clipboard.get() -> str` | Read current clipboard content |
| `set` | `wayland.clipboard.set(text: str)` | Write to clipboard |
| `getPrimary` | `wayland.clipboard.getPrimary() -> str` | Read primary selection (middle-click) |
| `setPrimary` | `wayland.clipboard.setPrimary(text: str)` | Write to primary selection |
| `clear` | `wayland.clipboard.clear()` | Clear clipboard |
| `hasText` | `wayland.clipboard.hasText() -> bool` | Check if clipboard has text |

### `wayland.windows`

| Method | Signature | Description |
|--------|-----------|-------------|
| `list` | `wayland.windows.list() -> array` | List all toplevel windows as array of objects |
| `active` | `wayland.windows.active() -> object` | Get currently active window |
| `find` | `wayland.windows.find(query: str) -> object` | Find window by title or app-id substring |

Each window object has fields:
- `title: str` — window title
- `appId: str` — application identifier (class/app-id)
- `fullscreen: bool`
- `maximized: bool`
- `active: bool`

### `wayland.info`

| Method | Signature | Description |
|--------|-----------|-------------|
| `compositor` | `wayland.info.compositor() -> str` | Compositor name (e.g. "Hyprland", "Sway", "KWin", "Mutter") |
| `protocols` | `wayland.info.protocols() -> array` | List available Wayland protocols |
| `hasProtocol` | `wayland.info.hasProtocol(name: str) -> bool` | Check if a specific protocol is available |

## Fallback Behavior

All methods automatically fall back from Wayland protocol → uinput when the compositor doesn't support a protocol. The user never needs to choose. `wayland.info.protocols()` can be used for debugging.

```havel
// Example: type text and click
use wayland
wayland.keyboard.type("hello world")
wayland.mouse.moveTo(500, 300)
wayland.mouse.click("left")

// Example: clipboard round-trip
wayland.clipboard.set("copied text")
wayland.keyboard.tap("ctrl+v")

// Example: find and check window
windows = wayland.windows.list()
active = wayland.windows.active()
print(active.title)
```

## C++ Host Module Registration

New file: `src/havel-lang/stdlib/WaylandModule.cpp`

Register as `wayland` global object with nested objects for each sub-namespace. Uses the existing `api.registerFunction()` / `api.makeObject()` / `api.setField()` / `api.setGlobal()` pattern.

The module depends on `WaylandProtocolClient` (from the repo spec) to determine which protocols are available and to get the shared `wl_display` connection.