// IO Refactor Examples
// Demonstrates the new unified hotkey system with universal key mapping,
// enhanced combos, and repeat intervals

#include "core/IO.hpp"
#include <iostream>

using namespace havel;

void ExampleBasicHotkeys(IO& io) {
    std::cout << "=== Basic Hotkey Examples ===" << std::endl;
    
    // Simple keyboard hotkey
    io.Hotkey("@W", []() {
        std::cout << "W key pressed!" << std::endl;
    });
    
    // Keyboard hotkey with modifiers
    io.Hotkey("@^W", []() {
        std::cout << "Ctrl+W pressed!" << std::endl;
    });
    
    // Multiple modifiers
    io.Hotkey("@^!W", []() {
        std::cout << "Ctrl+Alt+W pressed!" << std::endl;
    });
}

void ExampleMouseHotkeys(IO& io) {
    std::cout << "=== Mouse Hotkey Examples ===" << std::endl;
    
    // Simple mouse button
    io.Hotkey("@LButton", []() {
        std::cout << "Left mouse button clicked!" << std::endl;
    });
    
    // Mouse button with modifier
    io.Hotkey("@^LButton", []() {
        std::cout << "Ctrl+Left mouse button!" << std::endl;
    });
    
    // Mouse wheel
    io.Hotkey("@WheelUp", []() {
        std::cout << "Mouse wheel up!" << std::endl;
    });
    
    io.Hotkey("@!WheelDown", []() {
        std::cout << "Alt+Mouse wheel down!" << std::endl;
    });
    
    // Side buttons
    io.Hotkey("@XButton1", []() {
        std::cout << "Side button 1 pressed!" << std::endl;
    });
}

void ExampleComboHotkeys(IO& io) {
    std::cout << "=== Combo Hotkey Examples ===" << std::endl;
    
    // Mouse button combo
    io.Hotkey("@LButton & RButton", []() {
        std::cout << "Left + Right mouse buttons pressed together!" << std::endl;
    });
    
    // Mouse button combo with keyboard
    io.Hotkey("@LButton & RButton & R", []() {
        std::cout << "Left + Right mouse + R key combo!" << std::endl;
    });
    
    // Modifier + mouse button combo
    io.Hotkey("@^RButton", []() {
        std::cout << "Ctrl + Right mouse button!" << std::endl;
    });
    
    // Modifier + wheel combo
    io.Hotkey("@RShift & WheelDown", []() {
        std::cout << "Right Shift + Wheel Down!" << std::endl;
    });
    
    // Joystick button combo
    io.Hotkey("@JoyA & JoyB", []() {
        std::cout << "Joystick A + B buttons!" << std::endl;
    });
    
    // Complex combo with multiple inputs
    io.Hotkey("@CapsLock & W", []() {
        std::cout << "CapsLock + W pressed!" << std::endl;
    });
    
    // Joystick combo
    io.Hotkey("@JoyA & JoyY", []() {
        std::cout << "Joystick A + Y combo!" << std::endl;
    });
}

void ExampleRepeatIntervals(IO& io) {
    std::cout << "=== Repeat Interval Examples ===" << std::endl;
    
    // LAlt with 850ms repeat interval
    io.Hotkey("@LAlt:850", []() {
        std::cout << "LAlt pressed (repeats every 850ms)" << std::endl;
    });
    
    // Ctrl+S with 1000ms repeat interval
    io.Hotkey("@^S:1000", []() {
        std::cout << "Ctrl+S (repeats every 1 second)" << std::endl;
    });
    
    // Mouse button with repeat interval
    io.Hotkey("@LButton:500", []() {
        std::cout << "Left button (repeats every 500ms)" << std::endl;
    });
    
    // Wheel with custom interval
    io.Hotkey("@WheelUp:200", []() {
        std::cout << "Wheel up (repeats every 200ms)" << std::endl;
    });
}

void ExampleJoystickHotkeys(IO& io) {
    std::cout << "=== Joystick/Gamepad Examples ===" << std::endl;
    
    // Basic joystick buttons
    io.Hotkey("@JoyA", []() {
        std::cout << "Joystick A button!" << std::endl;
    });
    
    io.Hotkey("@JoyB", []() {
        std::cout << "Joystick B button!" << std::endl;
    });
    
    io.Hotkey("@JoyX", []() {
        std::cout << "Joystick X button!" << std::endl;
    });
    
    io.Hotkey("@JoyY", []() {
        std::cout << "Joystick Y button!" << std::endl;
    });
    
    // Shoulder buttons
    io.Hotkey("@JoyLB", []() {
        std::cout << "Left bumper!" << std::endl;
    });
    
    io.Hotkey("@JoyRB", []() {
        std::cout << "Right bumper!" << std::endl;
    });
    
    // Triggers
    io.Hotkey("@JoyLT", []() {
        std::cout << "Left trigger!" << std::endl;
    });
    
    io.Hotkey("@JoyRT", []() {
        std::cout << "Right trigger!" << std::endl;
    });
    
    // D-Pad
    io.Hotkey("@JoyDPadUp", []() {
        std::cout << "D-Pad Up!" << std::endl;
    });
    
    io.Hotkey("@JoyDPadDown", []() {
        std::cout << "D-Pad Down!" << std::endl;
    });
    
    // System buttons
    io.Hotkey("@JoyStart", []() {
        std::cout << "Start button!" << std::endl;
    });
    
    io.Hotkey("@JoyBack", []() {
        std::cout << "Back/Select button!" << std::endl;
    });
}

void ExampleAdvancedCombos(IO& io) {
    std::cout << "=== Advanced Combo Examples ===" << std::endl;
    
    // Keyboard + Mouse combo
    io.Hotkey("@W & LButton", []() {
        std::cout << "W key + Left mouse button!" << std::endl;
    });
    
    // Multiple keyboard keys
    io.Hotkey("@W & A & S & D", []() {
        std::cout << "WASD all pressed together!" << std::endl;
    });
    
    // Modifier + multiple buttons
    io.Hotkey("@^LButton & MButton", []() {
        std::cout << "Ctrl + Left + Middle mouse!" << std::endl;
    });
    
    // Joystick + keyboard combo
    io.Hotkey("@JoyA & Space", []() {
        std::cout << "Joystick A + Spacebar!" << std::endl;
    });
    
    // Complex multi-device combo
    io.Hotkey("@LShift & W & LButton", []() {
        std::cout << "Shift + W + Left mouse button!" << std::endl;
    });
}

void ExampleEventTypes(IO& io) {
    std::cout << "=== Event Type Examples ===" << std::endl;
    
    // Key down event only
    io.Hotkey("@W:down", []() {
        std::cout << "W key pressed down!" << std::endl;
    });
    
    // Key up event only
    io.Hotkey("@W:up", []() {
        std::cout << "W key released!" << std::endl;
    });
    
    // Both events (default)
    io.Hotkey("@W", []() {
        std::cout << "W key event (both down and up)!" << std::endl;
    });
    
    // Mouse button down
    io.Hotkey("@LButton:down", []() {
        std::cout << "Left button pressed down!" << std::endl;
    });
    
    // Mouse button up
    io.Hotkey("@LButton:up", []() {
        std::cout << "Left button released!" << std::endl;
    });
}

void ExampleUniversalKeyMapping() {
    std::cout << "=== Universal Key Mapping Examples ===" << std::endl;
    
    // The new system automatically maps keys across platforms:
    // - Evdev codes (Linux)
    // - X11 KeySyms (Linux X11)
    // - Windows Virtual Key Codes (Windows)
    
    // All of these work the same way:
    std::cout << "Keyboard keys: A-Z, 0-9, F1-F12" << std::endl;
    std::cout << "Modifiers: LCtrl, RCtrl, LShift, RShift, LAlt, RAlt, LMeta, RMeta" << std::endl;
    std::cout << "Special: Escape, Enter, Space, Tab, Backspace, Delete, etc." << std::endl;
    std::cout << "Mouse: LButton, RButton, MButton, XButton1, XButton2" << std::endl;
    std::cout << "Wheel: WheelUp, WheelDown, WheelLeft, WheelRight" << std::endl;
    std::cout << "Joystick: JoyA, JoyB, JoyX, JoyY, JoyLB, JoyRB, etc." << std::endl;
}

int main() {
    IO io;
    
    std::cout << "=== IO Refactor Examples ===" << std::endl;
    std::cout << "This demonstrates the new unified hotkey system" << std::endl;
    std::cout << std::endl;
    
    // Run all examples
    ExampleBasicHotkeys(io);
    ExampleMouseHotkeys(io);
    ExampleComboHotkeys(io);
    ExampleRepeatIntervals(io);
    ExampleJoystickHotkeys(io);
    ExampleAdvancedCombos(io);
    ExampleEventTypes(io);
    ExampleUniversalKeyMapping();
    
    std::cout << std::endl;
    std::cout << "=== Hotkey Syntax Summary ===" << std::endl;
    std::cout << "@ - Evdev prefix (auto-enabled globally)" << std::endl;
    std::cout << "^ - Ctrl modifier" << std::endl;
    std::cout << "+ - Shift modifier" << std::endl;
    std::cout << "! - Alt modifier" << std::endl;
    std::cout << "# - Meta/Win modifier" << std::endl;
    std::cout << "& - Combo separator (all keys must be pressed)" << std::endl;
    std::cout << ":down - Trigger on key down only" << std::endl;
    std::cout << ":up - Trigger on key up only" << std::endl;
    std::cout << ":NNN - Repeat interval in milliseconds" << std::endl;
    std::cout << "~ - Don't grab (pass through to system)" << std::endl;
    std::cout << "| - Don't repeat on hold" << std::endl;
    std::cout << "* - Wildcard (ignore extra modifiers)" << std::endl;
    std::cout << "$ - Suspend with other hotkeys" << std::endl;
    
    return 0;
}
