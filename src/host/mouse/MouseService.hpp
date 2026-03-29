/*
 * MouseService.hpp - Mouse control service
 * 
 * Provides mouse control operations:
 * - Click, press, release buttons
 * - Absolute and relative movement
 * - Scrolling
 * - Position query
 * - Speed/acceleration settings
 */
#pragma once

#include <memory>
#include <string>
#include <cstdint>

namespace havel::host {

class MouseService {
public:
    // Button constants
    enum class Button {
        Left = 1,
        Right = 2,
        Middle = 3,
        Back = 4,
        Forward = 5
    };
    
    // Action constants
    enum class Action {
        Click = 0,   // Press and release
        Press = 1,   // Press and hold
        Release = 2  // Release
    };
    
    // Parse button string to enum
    static Button parseButton(const std::string& button);
    static Button parseButton(int button);
    
    // Parse action string to enum
    static Action parseAction(const std::string& action);
    static Action parseAction(int action);
    
    // Click mouse button
    static void click(Button button = Button::Left);
    static void click(const std::string& button);
    static void click(int button);
    
    // Press and hold button
    static void press(Button button);
    static void press(const std::string& button);
    static void press(int button);
    
    // Release button
    static void release(Button button);
    static void release(const std::string& button);
    static void release(int button);
    
    // Combined click with action
    static void click(Button button, Action action);
    static void click(const std::string& button, const std::string& action);
    static void click(int button, int action);
    
    // Move to absolute position
    static void move(int x, int y, int speed = 5, float accel = 1.0f);
    
    // Move relative to current position
    static void moveRel(int dx, int dy, int speed = 5, float accel = 1.0f);
    
    // Scroll wheel (positive=up, negative=down)
    static void scroll(int dy, int dx = 0);
    
    // Get current position
    static std::pair<int, int> pos();
    
    // Set mouse speed (default: 5)
    static void setSpeed(int speed);
    
    // Set acceleration (default: 1.0)
    static void setAccel(float accel);
    
    // Set DPI (if supported)
    static void setDPI(int dpi);
    
    // Get current speed
    static int getSpeed();
    
    // Get current acceleration
    static float getAccel();
    
private:
    static int currentSpeed_;
    static float currentAccel_;
};

} // namespace havel::host
