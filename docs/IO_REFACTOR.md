# IO System Refactor

## Overview

The IO system has been refactored to provide a unified, cross-platform input handling system with enhanced features for hotkeys, combos, and device support.

## Key Features

### 1. Universal Key Mapping System

The new `UniversalKeyMap` provides a unified key representation that works across all platforms:

- **Linux Evdev**: Direct kernel input event codes
- **Linux X11**: X11 KeySyms for compatibility
- **Windows**: Virtual Key Codes (future support)

All keys, mouse buttons, and joystick buttons are mapped through a single universal enum, making it easy to write cross-platform code.

#### Supported Input Types

- **Keyboard**: All standard keys (A-Z, 0-9, F1-F24, modifiers, special keys)
- **Mouse**: Buttons (Left, Right, Middle, XButton1, XButton2) and Wheel (Up, Down, Left, Right)
- **Joystick/Gamepad**: Buttons (A, B, X, Y, LB, RB, LT, RT, Start, Back, etc.) and D-Pad

### 2. Single Unified Evdev Listener

Instead of separate listeners for keyboard, mouse, and joystick, the refactored system uses a single unified evdev listener that:

- Handles all input devices in one event loop
- Reduces overhead and complexity
- Provides consistent event handling across device types
- Supports hot-plugging of devices

### 3. Enhanced Combo System

The combo system now supports the `&` operator for creating complex multi-input combinations:

```cpp
// Mouse button combos
io.Hotkey("@LButton & RButton", []() { /* ... */ });

// Mouse + keyboard combos
io.Hotkey("@LButton & RButton & R", []() { /* ... */ });

// Modifier + mouse combos
io.Hotkey("@^RButton", []() { /* ... */ });

// Wheel + modifier combos
io.Hotkey("@RShift & WheelDown", []() { /* ... */ });

// Joystick combos
io.Hotkey("@JoyA & JoyB", []() { /* ... */ });

// Complex multi-device combos
io.Hotkey("@CapsLock & W", []() { /* ... */ });
io.Hotkey("@JoyA & JoyY", []() { /* ... */ });
```

All inputs in a combo must be pressed simultaneously (within the combo time window, default 500ms).

### 4. Custom Repeat Intervals

Hotkeys now support custom repeat intervals using the `:ms` syntax:

```cpp
// LAlt repeats every 850ms
io.Hotkey("@LAlt:850", []() { /* ... */ });

// Ctrl+S repeats every 1 second
io.Hotkey("@^S:1000", []() { /* ... */ });

// Mouse button repeats every 500ms
io.Hotkey("@LButton:500", []() { /* ... */ });

// Wheel repeats every 200ms
io.Hotkey("@WheelUp:200", []() { /* ... */ });
```

When a key is held down, the callback will only trigger at the specified interval, allowing for controlled repeated actions.

## Hotkey Syntax Reference

### Prefixes and Modifiers

- `@` - Evdev prefix (automatically enabled globally)
- `^` - Ctrl modifier
- `+` - Shift modifier
- `!` - Alt modifier
- `#` - Meta/Win/Super modifier

### Operators

- `&` - Combo separator (all keys must be pressed together)

### Suffixes

- `:down` - Trigger on key down only
- `:up` - Trigger on key up only
- `:NNN` - Repeat interval in milliseconds (e.g., `:850`)

### Flags

- `~` - Don't grab (pass through to system)
- `|` - Don't repeat on hold
- `*` - Wildcard (ignore extra modifiers)
- `$` - Suspend with other hotkeys

## Examples

### Basic Hotkeys

```cpp
// Simple key
io.Hotkey("@W", []() { std::cout << "W pressed\n"; });

// With modifier
io.Hotkey("@^W", []() { std::cout << "Ctrl+W\n"; });

// Multiple modifiers
io.Hotkey("@^!W", []() { std::cout << "Ctrl+Alt+W\n"; });
```

### Mouse Hotkeys

```cpp
// Mouse buttons
io.Hotkey("@LButton", []() { /* Left click */ });
io.Hotkey("@RButton", []() { /* Right click */ });
io.Hotkey("@MButton", []() { /* Middle click */ });
io.Hotkey("@XButton1", []() { /* Side button 1 */ });
io.Hotkey("@XButton2", []() { /* Side button 2 */ });

// Mouse wheel
io.Hotkey("@WheelUp", []() { /* Wheel up */ });
io.Hotkey("@WheelDown", []() { /* Wheel down */ });

// With modifiers
io.Hotkey("@^LButton", []() { /* Ctrl+Left click */ });
io.Hotkey("@!WheelDown", []() { /* Alt+Wheel down */ });
```

### Combo Hotkeys

```cpp
// Two mouse buttons
io.Hotkey("@LButton & RButton", []() { /* Both buttons */ });

// Mouse + keyboard
io.Hotkey("@LButton & W", []() { /* Left click + W */ });

// Multiple keys
io.Hotkey("@W & A & S & D", []() { /* All WASD */ });

// Joystick combo
io.Hotkey("@JoyA & JoyB", []() { /* A + B buttons */ });
```

### Repeat Intervals

```cpp
// Slow repeat (850ms)
io.Hotkey("@LAlt:850", []() { /* Every 850ms */ });

// Fast repeat (200ms)
io.Hotkey("@Space:200", []() { /* Every 200ms */ });

// No repeat
io.Hotkey("@|W", []() { /* Only on first press */ });
```

### Event Types

```cpp
// Down only
io.Hotkey("@W:down", []() { /* On press */ });

// Up only
io.Hotkey("@W:up", []() { /* On release */ });

// Both (default)
io.Hotkey("@W", []() { /* On press and release */ });
```

### Joystick/Gamepad

```cpp
// Face buttons
io.Hotkey("@JoyA", []() { /* A button */ });
io.Hotkey("@JoyB", []() { /* B button */ });
io.Hotkey("@JoyX", []() { /* X button */ });
io.Hotkey("@JoyY", []() { /* Y button */ });

// Shoulder buttons
io.Hotkey("@JoyLB", []() { /* Left bumper */ });
io.Hotkey("@JoyRB", []() { /* Right bumper */ });
io.Hotkey("@JoyLT", []() { /* Left trigger */ });
io.Hotkey("@JoyRT", []() { /* Right trigger */ });

// D-Pad
io.Hotkey("@JoyDPadUp", []() { /* D-Pad up */ });
io.Hotkey("@JoyDPadDown", []() { /* D-Pad down */ });
io.Hotkey("@JoyDPadLeft", []() { /* D-Pad left */ });
io.Hotkey("@JoyDPadRight", []() { /* D-Pad right */ });

// System buttons
io.Hotkey("@JoyStart", []() { /* Start */ });
io.Hotkey("@JoyBack", []() { /* Back/Select */ });
io.Hotkey("@JoyGuide", []() { /* Guide/Home */ });
```

## Architecture

### UniversalKeyMap

The `UniversalKeyMap` class provides bidirectional mapping between:
- String names (e.g., "LButton", "W", "JoyA")
- Universal key enum values
- Platform-specific codes (Evdev, X11, Windows)

This allows the same hotkey code to work across all platforms without modification.

### UnifiedEvdevListener (Planned)

The unified listener will:
1. Open multiple input devices (keyboard, mouse, joystick)
2. Use `select()` or `epoll()` to monitor all devices efficiently
3. Process events from all devices in a single thread
4. Maintain unified state for all inputs
5. Evaluate hotkeys and combos across all input types

### HotKey Structure

Enhanced with:
- `repeatInterval`: Custom repeat timing in milliseconds
- `lastTriggerTime`: Timestamp of last trigger for interval checking
- Support for combo sequences with `&` operator

## Migration Guide

### Old Syntax → New Syntax

```cpp
// Old: Separate mouse and keyboard hotkeys
io.AddMouseHotkey("LButton", callback);
io.AddHotkey("W", callback);

// New: Unified syntax
io.Hotkey("@LButton", callback);
io.Hotkey("@W", callback);

// Old: No combo support
// (Required manual state tracking)

// New: Built-in combo support
io.Hotkey("@LButton & RButton", callback);

// Old: No repeat interval control
// (Used system key repeat)

// New: Custom repeat intervals
io.Hotkey("@LAlt:850", callback);
```

### Removed Functions

The following functions have been removed or consolidated:
- `AddMouseHotkey()` - Use `Hotkey()` instead
- `StartEvdevMouseListener()` - Now part of unified listener
- `StartEvdevGamepadListener()` - Now part of unified listener
- Separate mouse/keyboard state tracking - Now unified

## Performance Improvements

1. **Single Event Loop**: One thread handles all input devices instead of multiple threads
2. **Efficient Polling**: Uses `select()` or `epoll()` for minimal CPU usage
3. **Reduced Lock Contention**: Unified state reduces mutex overhead
4. **Batch Processing**: Events can be processed in batches for better throughput

## Future Enhancements

- [ ] Gesture recognition (swipes, circles, etc.)
- [ ] Analog input support (joystick axes as triggers)
- [ ] Macro recording and playback
- [ ] Profile system for different applications
- [ ] GUI for hotkey configuration
- [ ] Network input forwarding

## Troubleshooting

### Hotkeys Not Working

1. Check device permissions: User must be in `input` group
2. Verify device paths in config
3. Enable debug logging: `Configs::Get().Set("Debug.VerboseKeyLogging", true)`
4. Check for conflicting hotkeys

### Combo Not Triggering

1. Verify all keys are pressed within combo time window (default 500ms)
2. Check modifier state - modifiers must match exactly
3. Ensure grab is enabled for combo keys

### Repeat Interval Not Working

1. Verify syntax: `@Key:NNN` where NNN is milliseconds
2. Check that repeat is not disabled with `|` flag
3. Ensure key is held down long enough

## See Also

- [examples/io_refactor_examples.cpp](../examples/io_refactor_examples.cpp) - Complete examples
- [src/core/io/UniversalKeyMap.hpp](../src/core/io/UniversalKeyMap.hpp) - Key mapping system
- [src/core/IO.hpp](../src/core/IO.hpp) - Main IO interface
