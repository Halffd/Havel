#include "EventQueue.hpp"
#include "../../../utils/Logger.hpp"
#include "havel-lang/core/Value.hpp"
#include <algorithm>
#include <iostream>
#include <cstring>
#include <cstdlib>
#include <string>
#include <utility>
#include "havel_platform.h"
#if HAVEL_PLATFORM_WINDOWS
#include "havel-lang/compiler/platform/platform_win.hpp"
#else
#include <cerrno>
#include <sys/eventfd.h>
#include <unistd.h>
#endif

namespace havel::compiler {

EventQueue::EventQueue() {
#if HAVEL_PLATFORM_WINDOWS
    // Windows has no eventfd and, more to the point, no file descriptor to
    // put in a select()/poll() set. An auto-reset event carries the same
    // "poke the loop, it is only ever a hint" semantics: SetEvent releases
    // one waiter and the handle clears itself, so a burst of pushes
    // collapses into one wakeup exactly as the eventfd counter draining
    // does. Manual-reset would need an explicit ResetEvent in drainWakeup()
    // and would lose signals if the loop was not waiting.
    HANDLE ev = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!ev) {
        ::havel::error("[EventQueue] Failed to create wakeup event: {}", ::GetLastError());
    } else {
        wakeupEvent_ = ev;
    }
    // wakeupFd_ stays -1: see wakeupHandle() in the header.
#else
    wakeupFd_ = eventfd(0, EFD_NONBLOCK);
    if (wakeupFd_ < 0) {
        ::havel::error("[EventQueue] Failed to create wakeup eventfd: {}", strerror(errno));
    }
#endif
    // Worker count override for scripts with many concurrent blocked
    // host calls. Default 4 covers realistic goroutine IO overlap; X11
    // serializes server-side anyway.
    size_t workers = 4;
    if (const char *env = std::getenv("HAVEL_ASYNC_WORKERS")) {
        int n = std::atoi(env);
        if (n > 0 && n <= 64) workers = static_cast<size_t>(n);
    }
    initCallbackWorkers(workers);
}

void EventQueue::postToWorker(Callback cb) {
    if (!cb || shutdown_workers_.load(std::memory_order_acquire)) return;
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        callback_queue_.push(std::move(cb));
    }
    callback_cv_.notify_one();
}

EventQueue::~EventQueue() {
    shutdown_workers_.store(true, std::memory_order_release);
    clear();
#if HAVEL_PLATFORM_WINDOWS
    if (wakeupEvent_) {
        ::CloseHandle(static_cast<HANDLE>(wakeupEvent_));
        wakeupEvent_ = nullptr;
    }
#else
    if (wakeupFd_ >= 0) {
        close(wakeupFd_);
        wakeupFd_ = -1;
    }
#endif
    shutdownWorkers();
}

void EventQueue::push(const Event& event) {
    if (shutdown_workers_.load(std::memory_order_acquire)) {
        // Teardown: drop the event, but free owned payloads - raw new'd
        // pointers would otherwise leak on the drop path (TIMER_FIRE was
        // leaking here before ASYNC_HOST_COMPLETE made the case explicit).
        if (event.ptr) {
            switch (event.type) {
            case EventType::LEGACY_CALLBACK:
                delete static_cast<Callback*>(event.ptr); break;
            case EventType::TIMER_FIRE:
                delete static_cast<TimerFirePayload*>(event.ptr); break;
            case EventType::VAR_CHANGED:
                delete static_cast<std::string*>(event.ptr); break;
            case EventType::ASYNC_HOST_COMPLETE:
                delete static_cast<AsyncCxxResult*>(event.ptr); break;
            default: break;
            }
        }
        return;
    }
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
#if HAVEL_PLATFORM_WINDOWS
    if (wakeupEvent_) {
        ::SetEvent(static_cast<HANDLE>(wakeupEvent_));
    }
#else
    if (wakeupFd_ >= 0) {
        uint64_t val = 1;
        ssize_t ret = write(wakeupFd_, &val, sizeof(val));
        if (ret < 0 && errno != EAGAIN) {
            ::havel::error("[EventQueue] wakeup write failed: {}", strerror(errno));
        }
    }
#endif
}

void EventQueue::drainWakeup() {
#if HAVEL_PLATFORM_WINDOWS
    if (wakeupEvent_) {
        // Auto-reset event: a zero-timeout wait consumes the pending signal
        // if one is set and leaves it alone otherwise. That is the direct
        // counterpart of the eventfd read-until-EAGAIN loop below, and like
        // it, draining is advisory - the real work is in events_.
        ::WaitForSingleObject(static_cast<HANDLE>(wakeupEvent_), 0);
    }
#else
    if (wakeupFd_ >= 0) {
        uint64_t val;
        while (read(wakeupFd_, &val, sizeof(val)) == sizeof(val)) {
            // drain all pending signals
        }
    }
#endif
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
    size_t processed = 0;
    while (events_.pop(event)) {
        processed++;
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
                    ::havel::debug("[EventQueue] dispatching event type={}", static_cast<int>(event.type));
                    handler_it->second(event);
                } else {
                    ::havel::debug("[EventQueue] event type={} dropped (no handler)", static_cast<int>(event.type));
                }
            }
        } catch (const std::exception& e) {
            ::havel::error("[EventQueue] Handler exception: {}", e.what());
        } catch (...) {
            ::havel::error("[EventQueue] Handler unknown exception");
        }
    }
    if (processed > 0) {
        ::havel::debug("[EventQueue] processAll: processed {} events", processed);
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
      delete static_cast<TimerFirePayload*>(ev.ptr);
    } else if (ev.type == EventType::VAR_CHANGED && ev.ptr) {
      delete static_cast<std::string*>(ev.ptr);
    } else if (ev.type == EventType::ASYNC_HOST_COMPLETE && ev.ptr) {
      delete static_cast<AsyncCxxResult*>(ev.ptr);
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
