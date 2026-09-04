#pragma once

#include <mutex>
#include <deque>
#include <optional>
#include <string>
#include <condition_variable>
#include <atomic>

namespace havel::repl {

class ReplInputQueue {
public:
    ReplInputQueue() = default;
    ~ReplInputQueue() = default;
    ReplInputQueue(const ReplInputQueue&) = delete;
    ReplInputQueue& operator=(const ReplInputQueue&) = delete;
    ReplInputQueue(ReplInputQueue&&) = delete;
    ReplInputQueue& operator=(ReplInputQueue&&) = delete;

    // Called by REPL thread (producer) — never blocks except on mutex
    void push(std::string line) {
        std::lock_guard<std::mutex> lock(mtx_);
        queue_.push_back(std::move(line));
        cv_.notify_one();
    }

    // Called by VM thread (consumer) in bc.tick() — non-blocking
    std::optional<std::string> try_pop() {
        std::lock_guard<std::mutex> lock(mtx_);
        if (queue_.empty()) return std::nullopt;
        std::string line = std::move(queue_.front());
        queue_.pop_front();
        return line;
    }

    // Called on shutdown to wake REPL thread from blocking read
    void wake() {
        std::lock_guard<std::mutex> lock(mtx_);
        shutdown_ = true;
        cv_.notify_all();
    }

    bool is_shutdown() const {
        return shutdown_.load(std::memory_order_acquire);
    }

    // Wait for input with timeout (for REPL thread)
    // Returns true if data available or shutdown, false on timeout
    bool wait_for_input(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mtx_);
        return cv_.wait_for(lock, timeout, [this] {
            return !queue_.empty() || shutdown_.load(std::memory_order_acquire);
        });
    }

private:
    mutable std::mutex mtx_;
    std::deque<std::string> queue_;
    std::condition_variable cv_;
    std::atomic<bool> shutdown_{false};
};

} // namespace havel::repl