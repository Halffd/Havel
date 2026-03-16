#pragma once
#include <memory>
#include <functional>
#include <queue>
#include <mutex>
#include <atomic>
#include <thread>
#include <variant>
#include <optional>

namespace havel {

struct HavelValue;  // Forward declaration

/**
 * Thread - Lightweight actor-based concurrency
 * 
 * Usage:
 *   let worker = thread {
 *     on message(msg) {
 *       print(msg)
 *     }
 *   }
 *   worker.send("hello")
 */
class Thread {
public:
    using Message = std::variant<std::string, int, double, std::shared_ptr<HavelValue>>;
    using MessageHandler = std::function<void(const Message&)>;
    
    Thread();
    ~Thread();
    
    // Start the thread with a handler
    void start(MessageHandler handler);
    
    // Send a message to the thread
    void send(const Message& msg);
    
    // Receive a message (blocking)
    std::optional<Message> receive();
    
    // Control methods
    void pause();
    void resume();
    void stop();
    
    // Status
    bool isRunning() const { return running.load(); }
    bool isPaused() const { return paused.load(); }
    
private:
    void messageLoop(MessageHandler handler);
    
    std::thread thread;
    std::queue<Message> messageQueue;
    std::mutex queueMutex;
    std::atomic<bool> running{false};
    std::atomic<bool> paused{false};
    std::atomic<bool> stopped{false};
};

/**
 * Interval - Repeating timer with control
 * 
 * Usage:
 *   let timer = interval 1000 {
 *     print("tick")
 *   }
 *   timer.pause()
 *   timer.resume()
 *   timer.stop()
 */
class Interval {
public:
    Interval(int intervalMs, std::function<void()> callback);
    ~Interval();
    
    void pause();
    void resume();
    void stop();
    
    bool isRunning() const { return running.load(); }
    
private:
    void timerLoop();
    
    std::thread thread;
    int intervalMs;
    std::function<void()> callback;
    std::atomic<bool> running{true};
    std::atomic<bool> paused{false};
    std::atomic<bool> stopped{false};
};

/**
 * Timeout - One-shot delayed execution
 * 
 * Usage:
 *   timeout 5000 {
 *     print("5 seconds later")
 *   }
 */
class Timeout {
public:
    Timeout(int timeoutMs, std::function<void()> callback);
    ~Timeout();
    
    void cancel();
    
private:
    void timerLoop();
    
    std::thread thread;
    int timeoutMs;
    std::function<void()> callback;
    std::atomic<bool> cancelled{false};
};

/**
 * Range - First-class range type
 * 
 * Usage:
 *   if time.hour in (8..18) { ... }
 *   schedule (8..18) { ... }
 */
class Range {
public:
    Range(int start, int end);
    
    bool contains(int value) const;
    int getStart() const { return start; }
    int getEnd() const { return end; }
    
private:
    int start;
    int end;
};

} // namespace havel
