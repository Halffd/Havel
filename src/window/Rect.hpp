#pragma once

namespace havel {

// Simple rectangle structure for window positions
class Rect {
public:
    int x{0}, y{0}, width{0}, height{0};

    Rect() = default;
    
    Rect(int x, int y, int w, int h) : x(x), y(y), width(w), height(h) {}

    bool operator==(const Rect& other) const {
        return x == other.x && y == other.y && 
               width == other.width && height == other.height;
    }

    bool operator!=(const Rect& other) const {
        return !(*this == other);
    }
};

// Display server enum for Linux
enum class DisplayServer {
    X11,
    Wayland,
    Unknown
};

} // namespace havel 