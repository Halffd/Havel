/*
 * MouseService.cpp - Mouse control service implementation
 */
#include "MouseService.hpp"
#include "host/io/IOService.hpp"
#include "core/IO.hpp"

namespace havel::host {

// Static member initialization
int MouseService::currentSpeed_ = 5;
float MouseService::currentAccel_ = 1.0f;

MouseService::Button MouseService::parseButton(const std::string& button) {
    std::string lower = button;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    if (lower == "left" || lower == "l") return Button::Left;
    if (lower == "right" || lower == "r") return Button::Right;
    if (lower == "middle" || lower == "m" || lower == "center" || lower == "c") return Button::Middle;
    if (lower == "back" || lower == "b" || lower == "side1" || lower == "x1") return Button::Back;
    if (lower == "forward" || lower == "f" || lower == "side2" || lower == "x2") return Button::Forward;
    
    // Try parsing as number
    try {
        int num = std::stoi(button);
        return parseButton(num);
    } catch (...) {
        return Button::Left;  // Default to left
    }
}

MouseService::Button MouseService::parseButton(int button) {
    switch (button) {
        case 1: return Button::Left;
        case 2: return Button::Right;
        case 3: return Button::Middle;
        case 4: return Button::Back;
        case 5: return Button::Forward;
        default: return Button::Left;
    }
}

MouseService::Action MouseService::parseAction(const std::string& action) {
    std::string lower = action;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    if (lower == "click" || lower == "c" || lower == "0") return Action::Click;
    if (lower == "press" || lower == "p" || lower == "down" || lower == "1") return Action::Press;
    if (lower == "release" || lower == "r" || lower == "up" || lower == "2") return Action::Release;
    
    return Action::Click;  // Default
}

MouseService::Action MouseService::parseAction(int action) {
    switch (action) {
        case 0: return Action::Click;
        case 1: return Action::Press;
        case 2: return Action::Release;
        default: return Action::Click;
    }
}

void MouseService::click(Button button, Action action) {
    // Use IOService pattern - requires IO pointer
    // This is a placeholder - actual implementation needs IO passed in
    (void)button;
    (void)action;
}

void MouseService::click(Button button) {
    click(button, Action::Click);
}

void MouseService::click(const std::string& button) {
    click(parseButton(button), Action::Click);
}

void MouseService::click(int button) {
    click(parseButton(button), Action::Click);
}

void MouseService::press(Button button) {
    click(button, Action::Press);
}

void MouseService::press(const std::string& button) {
    click(parseButton(button), Action::Press);
}

void MouseService::press(int button) {
    click(parseButton(button), Action::Press);
}

void MouseService::release(Button button) {
    click(button, Action::Release);
}

void MouseService::release(const std::string& button) {
    click(parseButton(button), Action::Release);
}

void MouseService::release(int button) {
    click(parseButton(button), Action::Release);
}

void MouseService::click(const std::string& button, const std::string& action) {
    click(parseButton(button), parseAction(action));
}

void MouseService::click(int button, int action) {
    click(parseButton(button), parseAction(action));
}

void MouseService::move(int x, int y, int speed, float accel) {
    // Placeholder - needs IO
    (void)x; (void)y; (void)speed; (void)accel;
}

void MouseService::moveRel(int dx, int dy, int speed, float accel) {
    // Placeholder - needs IO
    (void)dx; (void)dy; (void)speed; (void)accel;
}

void MouseService::scroll(int dy, int dx) {
    // Placeholder - needs IO
    (void)dy; (void)dx;
}

std::pair<int, int> MouseService::pos() {
    // Placeholder - needs IO
    return {0, 0};
}

void MouseService::setSpeed(int speed) {
    currentSpeed_ = std::max(1, std::min(20, speed));  // Clamp 1-20
}

void MouseService::setAccel(float accel) {
    currentAccel_ = std::max(0.1f, std::min(5.0f, accel));  // Clamp 0.1-5.0
}

void MouseService::setDPI(int dpi) {
    // DPI setting is hardware-specific and may not be supported
    (void)dpi;
}

int MouseService::getSpeed() {
    return currentSpeed_;
}

float MouseService::getAccel() {
    return currentAccel_;
}

} // namespace havel::host
