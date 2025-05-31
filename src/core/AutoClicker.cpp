#include "AutoClicker.hpp"
#include "../utils/Logger.hpp"

namespace havel {

AutoClicker::AutoClicker(std::shared_ptr<IO> io) : io(io) {
    // Initialize with default values
}

AutoClicker::~AutoClicker() {
    // Make sure to stop the clicker thread when the object is destroyed
    Stop();
}

void AutoClicker::Start(int intervalMs) {
    // Don't start if already running
    if (running.load()) {
        return;
    }
    
    // Set the interval
    interval = std::chrono::milliseconds(intervalMs);
    
    // Set the running flag
    running.store(true);
    
    // Start the clicker thread
    clickerThread = std::thread(&AutoClicker::ClickerLoop, this);
    
    Logger::getInstance().info("AutoClicker started with interval " + std::to_string(intervalMs) + " ms");
}

void AutoClicker::Stop() {
    // Set the running flag to false
    running.store(false);
    
    // Join the thread if it's joinable
    if (clickerThread.joinable()) {
        clickerThread.join();
    }
    
    Logger::getInstance().info("AutoClicker stopped");
}

void AutoClicker::Toggle(int intervalMs) {
    if (running.load()) {
        Stop();
    } else {
        Start(intervalMs);
    }
}

bool AutoClicker::IsRunning() const {
    return running.load();
}

void AutoClicker::SetClickType(ClickType type) {
    clickType = type;
}

void AutoClicker::SetClickFunction(std::function<void()> clickFunc) {
    customClickFunc = clickFunc;
}

void AutoClicker::ClickerLoop() {
    while (running.load()) {
        // Perform the click
        PerformClick();
        
        // Sleep for the specified interval
        std::this_thread::sleep_for(interval);
    }
}

void AutoClicker::PerformClick() {
    // If a custom click function is set, use it
    if (customClickFunc) {
        customClickFunc();
        return;
    }
    
    // Otherwise, use the default click based on the click type
    switch (clickType) {
        case ClickType::Left:
            io->Click(MouseButton::Left, MouseAction::Click);
            break;
        case ClickType::Right:
            io->Click(MouseButton::Right, MouseAction::Click);
            break;
        case ClickType::Middle:
            io->Click(MouseButton::Middle, MouseAction::Click);
            break;
    }
}

} // namespace havel
