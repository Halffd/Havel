# Spec: wayland-input-protocols

Scope: repo

# Wayland Input Simulation — Protocol Implementation Spec

## Overview
Replace uinput-only Wayland input simulation with native Wayland protocol support, keeping uinput as fallback. This covers keyboard sending, mouse sending, and clipboard read/write via Wayland protocols.

## Protocols to Add

### 1. `virtual-keyboard-unstable-v1` (zwp_virtual_keyboard_v1)
- **Source**: `/usr/share/wayland-protocols/unstable/virtual-keyboard/virtual-keyboard-unstable-v1.xml` (system wayland-protocols) OR `protocols/wlr/virtual-keyboard-unstable-v1.xml` (in-tree from wlroots)
- **Purpose**: Create virtual keyboard device, send key/modifier events
- **Flow**: bind `zwp_virtual_keyboard_manager_v1` global → create virtual keyboard for seat → set keymap (memfd with XKB keymap) → send `key(time, key, state)` and `modifiers(depressed, latched, locked, group)`
- **Compositor support**: Sway, Hyprland, KWin, Mutter 49.2+, COSMIC, Labwc, Wayfire, niri, river, Weston, Mir

### 2. `wlr-virtual-pointer-unstable-v1` (zwlr_virtual_pointer_v1)
- **Source**: `protocols/wlr/virtual-pointer-unstable-v1.xml` (in-tree, from wlroots)
- **Purpose**: Create virtual pointer device, send mouse motion/button/axis events
- **Flow**: bind `zwlr_virtual_pointer_manager_v1` global → create virtual pointer → send `motion()`, `motion_absolute()`, `button()`, `axis()`, `frame()`
- **Compositor support**: Sway, Hyprland, KWin, COSMIC, Labwc, Wayfire, niri, river, GameScope, Mir
- **NOT supported**: Mutter/GNOME

### 3. `ext-data-control-v1` (ext_data_control_manager_v1)
- **Source**: `/usr/share/wayland-protocols/staging/ext-data-control/ext-data-control-v1.xml` (system wayland-protocols)
- **Purpose**: Cross-compositor clipboard read/write (replaces shelling out to wl-copy/wl-paste)
- **Flow**: bind `ext_data_control_manager_v1` global → create data control device for seat → `set_selection()` / `set_primary_selection()` for writing; receive `selection()` / `primary_selection()` events for reading; offer `offer()` MIME types; receive `data()` for reading content
- **Compositor support**: KWin, Mutter 49.2+, Sway, Hyprland, COSMIC, Labwc, Wayfire, niri, Mir, Weston
- **Fallback**: `zwr_data_control_manager_v1` (wlr version) for older compositors

### 4. `wlr-foreign-toplevel-unstable-v1` (zwlr_foreign_toplevel_v1)
- **Source**: `protocols/wlr/wlr-foreign-toplevel-unstable-v1.xml` (in-tree, from wlroots)
- **Purpose**: Enumerate toplevel windows, get titles/app-ids, receive active/fullscreen state changes — replaces JSON parsing of hyprctl/swaymsg output for window listing
- **Flow**: bind `zwlr_foreign_toplevel_manager_v1` → receive `toplevel()` events for each window → get `title()`, `app_id()`, `output_enter()`, `output_leave()`, `state()` (maximized/fullscreen/activated), `done()`, `closed()` events → `activate()`, `close()`, `set_rectangle()`, `set_fullscreen()`, `unset_fullscreen()` requests
- **Compositor support**: Sway, Hyprland, KWin, COSMIC, Labwc, Wayfire, niri, river, Mir

## Protocol XML Sources
- In-tree: `protocols/wlr/` directory (already exists for gamma-control)
- System: `/usr/share/wayland-protocols/` for virtual-keyboard and ext-data-control

## Architecture

### WaylandProtocolClient (new class)
Central Wayland display connection manager for input protocols. Single `wl_display` connection shared across all protocol backends. Handles:
- `wl_display_connect` / `wl_registry` setup
- Global binding detection (which protocols the compositor supports)
- Roundtrip synchronization
- Compositor capability flags

### VirtualKeyboard (new class, replaces uinput for keyboard on supported compositors)
- Uses `zwp_virtual_keyboard_v1`
- Loads XKB keymap from compositor or compiles default (via xkbcommon)
- Sends key/modifier events
- Falls back to UinputDevice if protocol not available

### VirtualPointer (new class, replaces uinput for mouse on supported compositors)
- Uses `zwlr_virtual_pointer_v1`
- Sends motion/button/axis events with frame grouping
- Supports both relative and absolute positioning
- Falls back to UinputDevice if protocol not available

### WaylandClipboardBackend (new class, implements IClipboardBackend)
- Uses `ext_data_control_v1` (preferred) or `zwr_data_control_v1` (fallback)
- Replaces `popen("wl-paste")` / `popen("wl-copy")` subprocess calls
- Direct Wayland protocol for zero-latency clipboard access

### ForeignToplevel (new class, supplements WaylandBackend window listing)
- Uses `zwlr_foreign_toplevel_v1`
- Provides real-time window list with titles, app-ids, states
- Replaces JSON string parsing of hyprctl/swaymsg for window enumeration
- Still needs compositor IPC (hyprctl/swaymsg) for window manipulation that foreign-toplevel doesn't support (move, resize)

## Build System Changes
Add to CMakeLists.txt:
1. Copy `virtual-keyboard-unstable-v1.xml`, `virtual-pointer-unstable-v1.xml`, `foreign-toplevel-unstable-v1.xml` to `protocols/wlr/`
2. Reference `ext-data-control-v1.xml` from system wayland-protocols
3. Add `wayland-scanner` custom commands for each new protocol
4. Add generated sources to `wayland-protos` static library
5. Add `xkbcommon` as dependency (for keymap compilation in VirtualKeyboard)
6. Link `wayland-protos` to main binary (already done)

## Fallback Chain
For each capability, the system tries protocol first, then uinput:
- **Keyboard**: `zwp_virtual_keyboard_v1` → uinput
- **Mouse**: `zwlr_virtual_pointer_v1` → uinput (also: Mutter RemoteDesktop DBus for GNOME, future)
- **Clipboard**: `ext_data_control_v1` → `zwr_data_control_v1` → `wl-copy`/`wl-paste` subprocess
- **Window list**: `zwlr_foreign_toplevel_v1` → hyprctl/swaymsg JSON parsing → wmctrl