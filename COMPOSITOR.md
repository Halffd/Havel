# Havel Wayland Compositor

A fully-featured i3-like tiling window manager for Wayland, built with wlroots.

## Features

- **Tiling Window Management**: Support for horizontal, vertical, and grid tiling modes
- **Multiple Workspaces**: 10 workspaces with seamless window movement between them
- **Window Switching**: Alt+Tab style window switcher with forward/backward cycling
- **i3-like Keybindings**: Familiar Super (Windows) key-based shortcuts
- **Mouse Support**: Click-to-focus and cursor tracking
- **wlroots Compatible**: Works with wlroots 0.18+ API

## Building

```bash
# Build the compositor
./build-compositor.sh

# Run directly
./build-compositor.sh run

# Install system-wide
./build-compositor.sh install

# Show help and keybindings
./build-compositor.sh --help
```

## Keybindings

### System Control
- `Super + Escape` - Exit compositor
- `Super + Return` - Launch terminal (weston-terminal)
- `Super + d` - Launch application launcher (wofi)

### Window Management
- `Super + q` - Close focused window
- `Super + j` - Focus next window
- `Super + k` - Focus previous window

### Tiling Modes
- `Super + h` - Switch to horizontal tiling (side by side)
- `Super + v` - Switch to vertical tiling (stacked)
- `Super + g` - Switch to grid tiling (grid layout)

### Workspaces
- `Super + 1-0` - Switch to workspace 1-10
- `Super + Shift + 1-0` - Move focused window to workspace 1-10

### Window Switcher
- `Alt + Tab` - Window switcher (cycle forward)
- `Alt + Shift + Tab` - Window switcher (cycle backward)
- Release `Alt` to confirm selection

## Architecture

The compositor is built as a single unified implementation that combines:

1. **Core Window Management**: Basic window creation, focus, and positioning
2. **Workspace System**: Multiple virtual desktops with window organization
3. **Tiling Engine**: Automatic window arrangement in various layouts
4. **Input Handling**: Keyboard shortcuts and mouse interaction
5. **Output Management**: Multi-monitor support through wlroots

## Dependencies

- wlroots (>= 0.18, < 0.19 recommended for compatibility)
- wayland
- wayland-protocols
- xkbcommon
- pixman
- C++17 compatible compiler

## Technical Details

- **Language**: C++17 with C interop for wlroots
- **Build System**: Shell script with pkg-config integration
- **Window Finding**: Simple bounds-checking instead of scene API (for compatibility)
- **Rendering**: Simplified output handling focusing on window management

## Running

The compositor creates its own Wayland display server. You can run it:

1. **From TTY**: Switch to a TTY and run the compositor directly
2. **Nested**: Run within an existing Wayland/X11 session for testing
3. **Display Manager**: Install and select from your login manager

## Files

- `src/wm/compositor.cpp` - Main compositor implementation
- `build-compositor.sh` - Build script with dependency management
- `build-compositor/havel-compositor` - Built binary (after compilation)

## Future Enhancements

- Surface rendering for actual window content display
- Configuration file support
- Additional tiling modes (master/stack, spiral, etc.)
- Inter-process communication for runtime configuration
- Panel and status bar integration
- Multi-monitor workspace assignment