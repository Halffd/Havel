#pragma once

#include <csignal>
#include <thread>
#include <atomic>
#include <string>
#include <functional>

namespace havel::util {

class SignalWatcher {
private:
    std::atomic<bool> shouldExit{false};
    std::thread watcherThread;
    std::function<void()> cleanupCallback;
    
    static void logSignal(int sig);
    
public:
    SignalWatcher() = default;
    ~SignalWatcher();
    
    // Prevent copying
    SignalWatcher(const SignalWatcher&) = delete;
    SignalWatcher& operator=(const SignalWatcher&) = delete;
    
    // Move operations
    SignalWatcher(SignalWatcher&&) = delete;
    SignalWatcher& operator=(SignalWatcher&&) = delete;
    
    void start();
    void stop();
    
    void setCleanupCallback(std::function<void()> callback) {
        cleanupCallback = std::move(callback);
    }
    
    bool shouldExitNow() const { 
        return shouldExit.load(std::memory_order_relaxed);
    }
};

// Block signals in the calling thread
void blockAllSignals();

// Block specific signals in the calling thread
void blockSignals(const std::initializer_list<int>& signalsToBlock);

} // namespace Havel::Util
