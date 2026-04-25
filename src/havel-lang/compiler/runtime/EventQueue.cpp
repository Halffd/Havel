#include "EventQueue.hpp"
#include "../../../utils/Logger.hpp"
#include <algorithm>
#include <iostream>

namespace havel::compiler {

void EventQueue::push(const Event& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    events_.push(event);
}

void EventQueue::push(Callback cb) {
    if (!cb) {
        return;  // Ignore null callbacks
    }
    
    // DEPRECATED: Backward compatibility shim for legacy callback-based code
    // Store callback as a new shared_ptr on the heap
    // Will be deleted in processAll() after execution
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto cb_ptr = new Callback(std::move(cb));
    Event legacy_event(EventType::LEGACY_CALLBACK);
    legacy_event.ptr = cb_ptr;  // Store raw pointer to heap allocation
    
    events_.push(legacy_event);
}

void EventQueue::onEvent(EventType type, EventHandler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (handler) {
        handlers_[static_cast<uint8_t>(type)] = handler;
    }
}

void EventQueue::processAll() {
    // Drain queue - note we check queue.empty() in the loop
    // to avoid holding mutex during handler execution
    while (true) {
        Event event(EventType::THREAD_COMPLETE);  // Temporary default
        
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (events_.empty()) {
                break;
            }
            event = std::move(events_.front());
            events_.pop();
        }
        
        // Dispatch to handler without holding mutex
        // This allows worker threads to enqueue while we process
        try {
            if (event.type == EventType::LEGACY_CALLBACK) {
                // Special handling for legacy callbacks
                Callback* cb = static_cast<Callback*>(event.ptr);
                if (cb) {
                    (*cb)();
                    delete cb;  // Free the heap allocation
                }
            } else {
                // Normal event dispatch
                auto handler_it = handlers_.find(static_cast<uint8_t>(event.type));
                if (handler_it != handlers_.end() && handler_it->second) {
                    handler_it->second(event);
                }
                // If no handler registered, event is silently dropped
                // (enables late binding of handlers)
            }
        } catch (const std::exception& e) {
            // Log but don't crash - keep processing remaining events
            ::havel::error("[EventQueue] Handler exception: {}", e.what());
        } catch (...) {
            ::havel::error("[EventQueue] Handler unknown exception");
        }
    }
}

uint32_t EventQueue::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<uint32_t>(events_.size());
}

bool EventQueue::empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return events_.empty();
}

void EventQueue::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::queue<Event> empty;
    std::swap(events_, empty);
}

}  // namespace havel::compiler
