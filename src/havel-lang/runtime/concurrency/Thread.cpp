#include "Thread.hpp"
#include <chrono>

namespace havel {

// ============================================================================
// Thread Implementation
// ============================================================================

Thread::Thread() = default;

Thread::~Thread() {
    stop();
    if (thread.joinable()) {
        thread.join();
    }
}

void Thread::start(MessageHandler handler) {
    if (running.load()) {
        return;  // Already running
    }
    
    running.store(true);
    stopped.store(false);
    thread = std::thread(&Thread::messageLoop, this, handler);
}

void Thread::send(const Message& msg) {
    if (!running.load() || stopped.load()) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(queueMutex);
    messageQueue.push(msg);
}

std::optional<Thread::Message> Thread::receive() {
    std::lock_guard<std::mutex> lock(queueMutex);
    if (messageQueue.empty()) {
        return std::nullopt;
    }
    
    Message msg = messageQueue.front();
    messageQueue.pop();
    return msg;
}

void Thread::pause() {
    paused.store(true);
}

void Thread::resume() {
    paused.store(false);
}

void Thread::stop() {
    stopped.store(true);
    running.store(false);
    paused.store(false);
}

void Thread::messageLoop(MessageHandler handler) {
    while (running.load() && !stopped.load()) {
        // Check if paused
        while (paused.load() && running.load() && !stopped.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        if (!running.load() || stopped.load()) {
            break;
        }
        
        // Check for messages
        auto msg = receive();
        if (msg.has_value()) {
            handler(msg.value());
        } else {
            // No messages, sleep briefly
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

// ============================================================================
// Interval Implementation
// ============================================================================

Interval::Interval(int intervalMs, std::function<void()> callback)
    : intervalMs(intervalMs), callback(callback) {
    thread = std::thread(&Interval::timerLoop, this);
}

Interval::~Interval() {
    stop();
    if (thread.joinable()) {
        thread.join();
    }
}

void Interval::pause() {
    paused.store(true);
}

void Interval::resume() {
    paused.store(false);
}

void Interval::stop() {
    stopped.store(true);
    running.store(false);
}

void Interval::timerLoop() {
    while (running.load() && !stopped.load()) {
        // Check if paused
        while (paused.load() && running.load() && !stopped.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        if (!running.load() || stopped.load()) {
            break;
        }
        
        callback();
        
        // Sleep for interval
        std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
    }
}

// ============================================================================
// Timeout Implementation
// ============================================================================

Timeout::Timeout(int timeoutMs, std::function<void()> callback)
    : timeoutMs(timeoutMs), callback(callback) {
    thread = std::thread(&Timeout::timerLoop, this);
}

Timeout::~Timeout() {
    cancel();
    if (thread.joinable()) {
        thread.join();
    }
}

void Timeout::cancel() {
    cancelled.store(true);
}

void Timeout::timerLoop() {
    // Sleep in small increments to allow cancellation
    int elapsed = 0;
    const int stepMs = 10;
    
    while (elapsed < timeoutMs && !cancelled.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(stepMs));
        elapsed += stepMs;
    }
    
    if (!cancelled.load()) {
        callback();
    }
}

// ============================================================================
// Range Implementation
// ============================================================================

Range::Range(int start, int end) : start(start), end(end) {}

bool Range::contains(int value) const {
    return value >= start && value <= end;
}

} // namespace havel
