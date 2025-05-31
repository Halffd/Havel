#pragma once
#include <string>

namespace havel {

struct GUIEvent {
    enum class Type {
        // Mouse events
        MOUSE_MOVE,
        MOUSE_CLICK,
        MOUSE_DRAG,
        MOUSE_WHEEL,
        MOUSE_ENTER,
        MOUSE_LEAVE,
        
        // Keyboard events
        KEY_PRESS,
        KEY_RELEASE,
        KEY_REPEAT,
        
        // Window events
        WINDOW_MOVE,
        WINDOW_RESIZE,
        WINDOW_FOCUS,
        WINDOW_BLUR,
        WINDOW_MINIMIZE,
        WINDOW_MAXIMIZE,
        WINDOW_RESTORE,
        
        // Theme events
        THEME_CHANGE,
        THEME_RELOAD,
        
        // System events
        DISPLAY_CHANGE,
        DPI_CHANGE,
        QUIT
    };
    
    Type type;
    int x{0}, y{0};
    int width{0}, height{0};
    unsigned int button{0};
    unsigned int keycode{0};
    unsigned int modifiers{0};
    std::string data;
};

// Common key codes
constexpr unsigned int KEY_RETURN = 0xFF0D;
constexpr unsigned int KEY_ESCAPE = 0xFF1B;
constexpr unsigned int KEY_TAB = 0xFF09;
constexpr unsigned int KEY_SPACE = 0x0020;

} // namespace havel 