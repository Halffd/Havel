#pragma once

#include "IO.hpp"
#include <atomic>
#include <thread>
#include <functional>
#include <chrono>

namespace havel {

class AutoClicker {
public:
    AutoClicker(std::shared_ptr<IO> io);
    ~AutoClicker();

    // Start auto-clicking with the specified interval in milliseconds
    void Start(int intervalMs);
    
    // Stop auto-clicking
    void Stop();
    
    // Toggle auto-clicking state
    void Toggle(int intervalMs);
    
    // Check if auto-clicker is running
    bool IsRunning() const;
    
    // Set the click type (left, right, middle)
    enum class ClickType { Left, Right, Middle };
    void SetClickType(ClickType type);
    
    // Set a custom click function
    void SetClickFunction(std::function<void()> clickFunc);

private:
    std::shared_ptr<IO> io;
    std::atomic<bool> running{false};
    std::thread clickerThread;
    std::chrono::milliseconds interval{100};
    ClickType clickType{ClickType::Left};
    std::function<void()> customClickFunc;
    
    void ClickerLoop();
    void PerformClick();
};

} // namespace havel
