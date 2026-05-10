#include "EventQueue.hpp"
#include "../../../utils/Logger.hpp"
#include <algorithm>
#include <iostream>
#include <cstring>

namespace havel::compiler {

EventQueue::EventQueue() {
    wakeupFd_ = eventfd(0, EFD_NONBLOCK);
    if (wakeupFd_ < 0) {
        ::havel::error("[EventQueue] Failed to create wakeup eventfd: {}", strerror(errno));
    }
}

EventQueue::~EventQueue() {
    if (wakeupFd_ >= 0) {
        close(wakeupFd_);
        wakeupFd_ = -1;
    }
}

void EventQueue::push(const Event& event) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        events_.push(event);
    }
    signalWakeup();
}

void EventQueue::push(Callback cb) {
    if (!cb) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
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
    std::lock_guard<std::mutex> lock(mutex_);
    if (handler) {
        handlers_[static_cast<uint8_t>(type)] = handler;
    }
}

void EventQueue::processAll() {
    drainWakeup();

    while (true) {
        Event event(EventType::THREAD_COMPLETE);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (events_.empty()) {
                break;
            }
            event = std::move(events_.front());
            events_.pop();
        }

        try {
            if (event.type == EventType::LEGACY_CALLBACK) {
                Callback* cb = static_cast<Callback*>(event.ptr);
                if (cb) {
                    (*cb)();
                    delete cb;
                }
            } else {
                auto handler_it = handlers_.find(static_cast<uint8_t>(event.type));
                if (handler_it != handlers_.end() && handler_it->second) {
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

} // namespace havel::compiler
