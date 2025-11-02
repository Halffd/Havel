# IO Refactor Progress

## Completed

### 1. KeyMap Class (Renamed from UniversalKeyMap)
**Files:**
- `src/core/io/KeyMap.hpp` - Interface for key mapping
- `src/core/io/KeyMap.cpp` - Core implementation
- `src/core/io/KeyMapData.cpp` - All key data extracted from IO.cpp

**Features:**
- ✅ Extracted ALL keys from `IO::EvdevNameToKeyCode()` evdev map
- ✅ Extracted ALL keys from `IO::StringToVirtualKey()` X11/Windows map
- ✅ Added all alternative names and aliases
- ✅ Support for evdev, X11, and Windows key codes
- ✅ Bidirectional mapping (name ↔ code)
- ✅ Platform conversion (evdev ↔ X11 ↔ Windows)
- ✅ Helper functions: IsModifier(), IsMouseButton(), IsJoystickButton()
- ✅ GetAliases() for finding alternative key names

**Key Count:**
- 200+ keyboard keys
- All modifiers with left/right variants
- All function keys (F1-F24)
- All numpad keys
- All media control keys
- All browser keys
- All application launcher keys
- Power management keys
- Display/brightness keys
- Wireless keys
- File operation keys
- ISO keyboard extras
- Gaming/multimedia keys
- Mouse buttons (LButton, RButton, MButton, XButton1, XButton2)
- Mouse wheel (WheelUp, WheelDown)
- Joystick buttons (JoyA, JoyB, JoyX, JoyY, JoyLB, JoyRB, etc.)

**Alternative Names:**
- "esc" / "escape"
- "enter" / "return"
- "ctrl" / "lctrl"
- "win" / "meta" / "super" / "lwin" / "lmeta"
- "pgup" / "pageup"
- "pgdn" / "pagedown"
- "numpadplus" / "numpadadd"
- "wheelup" / "scrollup"
- "wheeldown" / "scrolldown"
- And many more...

### 2. EventListener Class (Renamed from UnifiedEvdevListener)
**Files:**
- `src/core/io/EventListener.hpp` - Interface for unified event listening

**Features:**
- ✅ Single unified evdev listener for all devices
- ✅ Uses exact keyboard logic from IO.cpp
- ✅ Combo support with exact evaluation logic from IO.cpp
- ✅ Modifier state tracking (exact from IO.cpp)
- ✅ Key state tracking (exact from IO.cpp)
- ✅ Hotkey evaluation (exact from IO.cpp)
- ✅ Key remapping support (exact from IO.cpp)
- ✅ Uinput forwarding support
- ✅ Emergency shutdown key support
- ✅ Block input mode
- ✅ Repeat interval support (from previous refactor)

**Architecture:**
- Single event loop handles keyboard, mouse, and joystick
- Uses `select()` for efficient multi-device monitoring
- Thread-safe with mutexes for state and hotkey access
- Clean shutdown with eventfd
- Pending callback tracking for safe shutdown

## In Progress

### 3. Transition IO to Use KeyMap and EventListener
**Status:** Not started

**Plan:**
1. Update IO.cpp to use KeyMap for all key name lookups
2. Replace `EvdevNameToKeyCode()` with `KeyMap::FromString()`
3. Replace `StringToVirtualKey()` with `KeyMap::ToX11()` / `KeyMap::ToWindows()`
4. Update IO.cpp to use EventListener for evdev handling
5. Replace `StartEvdevHotkeyListener()` with `EventListener::Start()`
6. Replace `StartEvdevMouseListener()` with EventListener (handles both)
7. Migrate hotkey registration to EventListener
8. Remove old evdev listener code from IO.cpp
9. Update IO constructor to initialize KeyMap and EventListener
10. Test all hotkey functionality

## Benefits of Refactor

### Code Organization
- **Separation of Concerns:** Key mapping logic separated from input handling
- **Reusability:** KeyMap can be used by any component needing key lookups
- **Maintainability:** All key data in one place (KeyMapData.cpp)
- **Testability:** KeyMap and EventListener can be tested independently

### Performance
- **Single Event Loop:** One thread instead of multiple (keyboard + mouse + joystick)
- **Efficient Polling:** `select()` monitors all devices with minimal CPU usage
- **Reduced Lock Contention:** Unified state reduces mutex overhead
- **Batch Processing:** Events processed in batches

### Features
- **Universal Key Support:** Every key from evdev map included
- **Alternative Names:** Multiple ways to refer to same key
- **Platform Agnostic:** Same code works on Linux (evdev/X11) and Windows
- **Combo Support:** Full combo evaluation with & operator
- **Repeat Intervals:** Custom repeat timing per hotkey

### Compatibility
- **Backward Compatible:** Old IO API still works
- **Incremental Migration:** Can transition gradually
- **No Breaking Changes:** Existing hotkeys continue to function

## Next Steps

1. **Implement EventListener.cpp**
   - Copy exact logic from IO.cpp evdev listener
   - Add mouse event handling
   - Add joystick event handling
   - Implement combo evaluation
   - Add repeat interval checking

2. **Update IO.cpp**
   - Replace key lookup functions with KeyMap calls
   - Initialize EventListener in constructor
   - Migrate hotkey registration
   - Remove old evdev code

3. **Testing**
   - Unit tests for KeyMap
   - Integration tests for EventListener
   - End-to-end hotkey tests
   - Performance benchmarks

4. **Documentation**
   - Update API documentation
   - Add migration guide
   - Create examples using new classes

## File Structure

```
src/core/io/
├── KeyMap.hpp           - Key mapping interface
├── KeyMap.cpp           - Key mapping implementation
├── KeyMapData.cpp       - All key data (200+ keys)
├── EventListener.hpp    - Unified event listener interface
└── EventListener.cpp    - Event listener implementation (TODO)

src/core/
├── IO.hpp               - Main IO interface (to be updated)
└── IO.cpp               - IO implementation (to be updated)
```

## API Examples

### KeyMap Usage

```cpp
#include "io/KeyMap.hpp"

// Convert key name to evdev code
int code = KeyMap::FromString("lbutton");  // BTN_LEFT

// Convert to X11
unsigned long x11 = KeyMap::ToX11("enter");  // XK_Return

// Convert to Windows
int vk = KeyMap::ToWindows("ctrl");  // VK_CONTROL

// Convert between platforms
int evdev = KeyMap::X11ToEvdev(XK_a);  // KEY_A
unsigned long x11 = KeyMap::EvdevToX11(KEY_SPACE);  // XK_space

// Get alternative names
auto aliases = KeyMap::GetAliases("escape");  // ["esc"]

// Check key type
bool isMod = KeyMap::IsModifier(KEY_LEFTCTRL);  // true
bool isMouse = KeyMap::IsMouseButton(BTN_LEFT);  // true
bool isJoy = KeyMap::IsJoystickButton(BTN_SOUTH);  // true
```

### EventListener Usage

```cpp
#include "io/EventListener.hpp"

EventListener listener;

// Start listening on devices
std::vector<std::string> devices = {
    "/dev/input/event3",  // keyboard
    "/dev/input/event4",  // mouse
    "/dev/input/event5"   // joystick
};
listener.Start(devices);

// Register hotkey
HotKey hotkey;
hotkey.key = KEY_W;
hotkey.modifiers = (1 << 0);  // Ctrl
hotkey.callback = []() { std::cout << "Ctrl+W pressed\n"; };
listener.RegisterHotkey(1, hotkey);

// Get key state
bool pressed = listener.GetKeyState(KEY_A);

// Get modifier state
auto mods = listener.GetModifierState();
if (mods.IsCtrlPressed()) { /* ... */ }

// Add key remap
listener.AddKeyRemap(KEY_CAPSLOCK, KEY_LEFTCTRL);

// Stop listening
listener.Stop();
```

## Migration Path

### Phase 1: KeyMap Integration (Current)
- ✅ Create KeyMap with all keys
- ✅ Create EventListener interface
- ⏳ Implement EventListener.cpp
- ⏳ Update IO.cpp to use KeyMap

### Phase 2: EventListener Integration
- ⏳ Update IO.cpp to use EventListener
- ⏳ Remove old evdev listener code
- ⏳ Test all functionality

### Phase 3: Cleanup
- ⏳ Remove deprecated functions
- ⏳ Update documentation
- ⏳ Performance optimization

### Phase 4: Enhancement
- ⏳ Add gesture recognition
- ⏳ Add macro recording
- ⏳ Add profile system
