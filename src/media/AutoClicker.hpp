#pragma once
#include <atomic>
#include <thread>

namespace H {

class AutoClicker {
public:
    AutoClicker();
    ~AutoClicker();
    
    void Start(int intervalMs);
    void Stop();
    bool IsRunning() const;
    
private:
    void ClickerThread();
    
    std::thread clickerThread;
    std::atomic<bool> running{false};
    int interval = 1000;
};

} // namespace H 