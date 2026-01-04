#pragma once
#include <vector>
#include <chrono>
#include <string>
#include <functional>

namespace havel {

// Mouse gesture types and directions
enum class MouseGestureType {
    Direction,  // Simple directional gesture (up, down, left, right)
    Shape,      // Shape-based gesture (circle, square, triangle)
    Freeform    // Custom gesture pattern
};

enum class MouseGestureDirection {
    Up = 0,
    Down = 1,
    Left = 2,
    Right = 3,
    UpLeft = 4,
    UpRight = 5,
    DownLeft = 6,
    DownRight = 7
};

struct MouseGesture {
    MouseGestureType type = MouseGestureType::Direction;
    std::vector<MouseGestureDirection> directions;
    std::chrono::steady_clock::time_point startTime;
    std::chrono::steady_clock::time_point lastMoveTime;
    int totalDistance = 0;
    std::vector<int> xPositions;
    std::vector<int> yPositions;
    bool isActive = false;
    int minDistance = 20; // Minimum distance to count as a movement
    int maxDeviation = 30; // Maximum angle deviation in degrees
    int timeout = 1000; // Timeout in milliseconds
};

} // namespace havel