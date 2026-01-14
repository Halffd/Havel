# Fix: Remapped Key Hotkeys Not Working

## Problem

When remapping a key to a modifier (e.g., `io.Map("CapsLock", "LAlt")`), hotkeys registered for the original key with modifiers were not being detected:

```cpp
io.Map("CapsLock", "LAlt");
io.Hotkey("@+CapsLock", [this]() { io.Send("{CapsLock}"); });  // NOT WORKING
io.Hotkey("@^CapsLock", [this]() { io.Send("{CapsLock}"); });  // NOT WORKING
```

## Root Cause

When a key is remapped to a modifier key:

1. **Physical event**: User presses CapsLock (physical key that generates KEY_CAPSLOCK events)
2. **Remapping**: EventListener remaps KEY_CAPSLOCK → KEY_LEFTALT  
3. **Modifier state update**: UpdateModifierState() sees the target key is a modifier and updates `modifierState.leftAlt = true`
4. **Hotkey evaluation**: EvaluateHotkeys() checks if the hotkey matches
   - Hotkey is registered for KEY_CAPSLOCK with Shift modifier
   - evdevCode is KEY_CAPSLOCK (correct!)
   - Key matches hotkey.key (correct!)
   - **BUG**: CheckModifierMatch() is called, expecting ONLY Shift modifier to be pressed
   - **But** the modifier state now has Alt + Shift (because CapsLock was remapped to LAlt)
   - Exact modifier match fails! The hotkey is never triggered.

## Solution

Added special handling for keys that are remapped to modifiers in the hotkey evaluation logic:

### Changes Made

#### 1. New function: `CheckModifierMatchExcludingModifier()` 
In [EventListener.cpp](src/core/io/EventListener.cpp):
- Similar to `CheckModifierMatch()` but allows excluding a specific modifier
- Used when a key is remapped to a modifier to ignore that modifier's contribution to the state
- This effectively "removes" the remapped modifier from consideration, making the check match the original intent

#### 2. Enhanced hotkey evaluation logic
In `EvaluateHotkeys()` method:
- **Detect remapped keys**: Check if `evdevCode` is in `keyRemaps` and maps to a modifier
- **Apply correct matching**:
  - If the key itself has no modifiers required and is remapped to a modifier: allow it (bare modifier key case)
  - If modifiers are required: use `CheckModifierMatchExcludingModifier()` to ignore the remapped-to modifier
- **Example**: When CapsLock is remapped to LAlt and you have hotkey `@+CapsLock` (Shift+CapsLock):
  - Modifier state will be: Shift + Alt
  - Hotkey requires: Shift
  - Excluding Alt from the check: Result is Shift only → **MATCH!**

### Example Behavior After Fix

```cpp
io.Map("CapsLock", "LAlt");

// These now work correctly:
io.Hotkey("@+CapsLock", [this]() { 
  // Triggered when pressing: Shift + CapsLock (which acts as Shift + LAlt)
  io.Send("{CapsLock}"); 
});

io.Hotkey("@^CapsLock", [this]() { 
  // Triggered when pressing: Ctrl + CapsLock (which acts as Ctrl + LAlt)
  io.Send("{CapsLock}"); 
});

// This also works (bare modifier):
io.Hotkey("@CapsLock", [this]() { 
  // Triggered when pressing: just CapsLock (which acts as LAlt)
  io.Send("{CapsLock}"); 
});
```

## Implementation Details

### Key Remaps Storage
- Maintained in `EventListener::keyRemaps` map (evdevCode → mapped evdevCode)
- Set via `eventListener->AddKeyRemap()` called from `IO::Map()`

### Modifier State Handling
- When a remapped key is pressed, `UpdateModifierState()` updates based on the target key
- If target is a modifier, modifier state is updated accordingly
- New function accounts for this and "removes" the remapped modifier from matching logic

### Backward Compatibility
- Non-remapped keys: No change in behavior
- Keys remapped to non-modifiers: No change in behavior  
- Keys remapped to modifiers: Fixed to work as intended
- The fix only activates when `keyRemappedToModifier` is true

## Files Modified

1. **src/core/io/EventListener.cpp**
   - Added `CheckModifierMatchExcludingModifier()` implementation
   - Enhanced `EvaluateHotkeys()` to detect and handle remapped-to-modifier keys

2. **src/core/io/EventListener.hpp**
   - Added `CheckModifierMatchExcludingModifier()` declaration

3. **src/gui/HavelApp.hpp** (unrelated build fixes)
   - Fixed include path for Interpreter.hpp
   - Added conditional compilation guards for ENABLE_HAVEL_LANG

## Testing

To verify the fix works:

```cpp
// In your hotkey registration code:
io.Map("CapsLock", "LAlt");
io.Hotkey("@+CapsLock", []() { 
    info("Shift+CapsLock detected!"); 
});

// Now press Shift and then CapsLock - it should trigger
```

Monitor the debug output:
```
🔑 Key PRESS: original=58 mapped=56 | Modifiers: Shift+
[keyRemappedToModifier check] true
[CheckModifierMatchExcludingModifier] Matching Shift only (excluding Alt)
✅ Hotkey '@+CapsLock' triggered on key press
```

## Notes

- The fix assumes only the remapped key contributes to that modifier
- If multiple keys are remapped to the same modifier and pressed together, behavior may be unexpected (edge case)
- The solution is general and works for any key remapped to any modifier (Ctrl, Shift, Alt, Meta)
