#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>

namespace havel {

template<typename T>
class SafeQueue {
public:
    SafeQueue() = default;
    ~SafeQueue() = default;

    // Disable copying
    SafeQueue(const SafeQueue&) = delete;
    SafeQueue& operator=(const SafeQueue&) = delete;

    // Allow moving
    SafeQueue(SafeQueue&&) = default;
    SafeQueue& operator=(SafeQueue&&) = default;

    // Add an item to the queue
    void enqueue(T item) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(std::move(item));
        cond_.notify_one();
    }

    // Try to dequeue an item (returns immediately if queue is empty)
    std::optional<T> try_dequeue() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return std::nullopt;
        }
        T item = std::move(queue_.front());
        queue_.pop();
        return item;
    }

    // Dequeue an item (blocks until an item is available)
    T dequeue() {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.wait(lock, [this] { return !queue_.empty(); });
        T item = std::move(queue_.front());
        queue_.pop();
        return item;
    }

    // Get the current size of the queue
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    // Check if the queue is empty
    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    // Clear all items from the queue
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!queue_.empty()) {
            queue_.pop();
        }
    }

private:
    mutable std::mutex mutex_;
    std::queue<T> queue_;
    std::condition_variable cond_;
};

} // namespace havel
