#include "EventQueue.hpp"
#include "../../../utils/Logger.hpp"
#include "havel-lang/core/Value.hpp"
#include <algorithm>
#include <iostream>
#include <cstring>
#include <string>
#include <utility>

namespace havel::compiler {

EventQueue::EventQueue() {
    wakeupFd_ = eventfd(0, EFD_NONBLOCK);
    if (wakeupFd_ < 0) {
        ::havel::error("[EventQueue] Failed to create wakeup eventfd: {}", strerror(errno));
    }
    initCallbackWorkers(2); // Start 2 worker threads for callbacks
}

EventQueue::~EventQueue() {
    if (wakeupFd_ >= 0) {
        close(wakeupFd_);
        wakeupFd_ = -1;
    }
    shutdownWorkers();
}

void EventQueue::push(const Event& event) {
    events_.push(event);
    signalWakeup();
}

void EventQueue::push(Callback cb) {
    if (!cb) {
        return;
    }

    {
        auto cb_ptr = new Callback(std::move(cb));
        Event legacy_event(EventType::LEGACY_CALLBACK);
        legacy_event.ptr = cb_ptr;
        events_.push(legacy_event);
    }
    signalWakeup();
}

void EventQueue::signalWakeup() {
    if (wakeupFd_ >= 0) {
        uint64_t val = 1;
        ssize_t ret = write(wakeupFd_, &val, sizeof(val));
        if (ret < 0 && errno != EAGAIN) {
            ::havel::error("[EventQueue] wakeup write failed: {}", strerror(errno));
        }
    }
}

void EventQueue::drainWakeup() {
    if (wakeupFd_ >= 0) {
        uint64_t val;
        while (read(wakeupFd_, &val, sizeof(val)) == sizeof(val)) {
            // drain all pending signals
        }
    }
}

void EventQueue::onEvent(EventType type, EventHandler handler) {
    std::lock_guard<std::mutex> lock(handlers_mutex_);
    if (handler) {
        handlers_[static_cast<uint8_t>(type)] = handler;
    }
}

void EventQueue::processAll() {
    drainWakeup();

    std::unordered_map<uint8_t, EventHandler> local_handlers;
    {
        std::lock_guard<std::mutex> lock(handlers_mutex_);
        local_handlers = handlers_;
    }

    Event event;
    while (events_.pop(event)) {
        try {
            if (event.type == EventType::LEGACY_CALLBACK) {
                Callback* cb = static_cast<Callback*>(event.ptr);
                if (cb) {
                    {
                        std::lock_guard<std::mutex> cb_lock(callback_mutex_);
                        callback_queue_.push(*cb);
                    }
                    callback_cv_.notify_one();
                    delete cb;
                }
            } else {
                auto handler_it = local_handlers.find(static_cast<uint8_t>(event.type));
                if (handler_it != local_handlers.end() && handler_it->second) {
                    handler_it->second(event);
                }
            }
        } catch (const std::exception& e) {
            ::havel::error("[EventQueue] Handler exception: {}", e.what());
        } catch (...) {
            ::havel::error("[EventQueue] Handler unknown exception");
        }
    }
}

uint32_t EventQueue::size() const {
    return static_cast<uint32_t>(events_.unsafe_size());
}

bool EventQueue::empty() const {
    return events_.empty();
}

void EventQueue::clear() {
  Event ev;
  while (events_.pop(ev)) {
    if (ev.type == EventType::LEGACY_CALLBACK && ev.ptr) {
      delete static_cast<Callback*>(ev.ptr);
    } else if (ev.type == EventType::TIMER_FIRE && ev.ptr) {
      delete static_cast<std::pair<havel::core::Value, uint32_t>*>(ev.ptr);
    } else if (ev.type == EventType::VAR_CHANGED && ev.ptr) {
      delete static_cast<std::string*>(ev.ptr);
    }
  }
}

void EventQueue::initCallbackWorkers(size_t pool_size) {
    for (size_t i = 0; i < pool_size; ++i) {
        callback_workers_.emplace_back([this]() { callbackWorkerLoop(); });
    }
}

void EventQueue::callbackWorkerLoop() {
    while (true) {
        Callback cb;
        
        {
            std::unique_lock<std::mutex> lock(callback_mutex_);
            callback_cv_.wait(lock, [this]() {
                return shutdown_workers_ || !callback_queue_.empty();
            });
            
            if (shutdown_workers_ && callback_queue_.empty()) {
                return;
            }
            
            if (!callback_queue_.empty()) {
                cb = std::move(callback_queue_.front());
                callback_queue_.pop();
            } else {
                continue;
            }
        }
        
        // Execute callback outside the lock
        try {
            cb();
        } catch (const std::exception& e) {
            ::havel::error("[EventQueue] Callback execution exception: {}", e.what());
        } catch (...) {
            ::havel::error("[EventQueue] Callback execution unknown exception");
        }
    }
}

void EventQueue::shutdownWorkers() {
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        shutdown_workers_ = true;
    }
    callback_cv_.notify_all();
    
    for (auto& worker : callback_workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

} // namespace havel::compiler
