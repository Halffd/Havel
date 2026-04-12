#include "EventQueue.hpp"
#include <algorithm>

namespace havel::compiler {

void EventQueue::push(Callback cb) {
    if (!cb) {
        return;  // Ignore null callbacks
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    callbacks_.push(std::move(cb));
}

void EventQueue::processAll() {
    // Drain queue - note we check queue.empty() in the loop
    // to avoid holding mutex during callback execution
    while (true) {
        Callback cb;
        
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (callbacks_.empty()) {
                break;
            }
            cb = std::move(callbacks_.front());
            callbacks_.pop();
        }
        
        // Execute callback without holding mutex
        // This allows worker threads to enqueue while we process
        try {
            cb();
        } catch (const std::exception& e) {
            // Log but don't crash - keep processing remaining callbacks
            // In a real system, would use spdlog here
            // error("EventQueue callback exception: {}", e.what());
        } catch (...) {
            // Unknown exception - log and continue
            // error("EventQueue callback unknown exception");
        }
    }
}

uint32_t EventQueue::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<uint32_t>(callbacks_.size());
}

bool EventQueue::empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return callbacks_.empty();
}

void EventQueue::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::queue<Callback> empty;
    std::swap(callbacks_, empty);  // Clear without destroying callbacks
}

}  // namespace havel::compiler
