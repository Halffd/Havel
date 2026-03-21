/*
 * TimerService.cpp
 *
 * Timer service implementation.
 */
#include "TimerService.hpp"
#include <thread>
#include <mutex>
#include <atomic>
#include <unordered_map>
#include <chrono>

namespace havel::host {

struct TimerService::Impl {
    struct Timer {
        int id;
        std::thread thread;
        std::atomic<bool> cancelled{false};
        std::atomic<bool> running{true};
    };
    
    std::unordered_map<int, std::unique_ptr<Timer>> timers;
    std::mutex mutex;
    std::atomic<int> nextId{1};
};

TimerService::TimerService() : m_impl(std::make_unique<Impl>()) {
}

TimerService::~TimerService() {
    clearAll();
}

int TimerService::setTimeout(std::function<void()> fn, int delayMs) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    int timerId = m_impl->nextId++;
    auto timer = std::make_unique<Impl::Timer>();
    timer->id = timerId;
    
    timer->thread = std::thread([this, fn = std::move(fn), timerId, timer = timer.get(), delayMs]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        
        if (!timer->cancelled) {
            try {
                fn();
            } catch (...) {
                // Ignore exceptions
            }
        }
        
        // Remove from timers
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        m_impl->timers.erase(timerId);
    });
    
    m_impl->timers[timerId] = std::move(timer);
    return timerId;
}

bool TimerService::clearTimeout(int timerId) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    auto it = m_impl->timers.find(timerId);
    if (it == m_impl->timers.end()) {
        return false;
    }
    
    it->second->cancelled = true;
    if (it->second->thread.joinable()) {
        it->second->thread.detach();  // Can't interrupt, just detach
    }
    m_impl->timers.erase(it);
    return true;
}

int TimerService::setInterval(std::function<void()> fn, int intervalMs) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    int timerId = m_impl->nextId++;
    auto timer = std::make_unique<Impl::Timer>();
    timer->id = timerId;
    
    timer->thread = std::thread([this, fn = std::move(fn), timerId, timer = timer.get(), intervalMs]() {
        while (!timer->cancelled) {
            std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
            
            if (timer->cancelled) break;
            
            try {
                fn();
            } catch (...) {
                // Ignore exceptions
                break;
            }
        }
        
        // Remove from timers
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        m_impl->timers.erase(timerId);
    });
    
    m_impl->timers[timerId] = std::move(timer);
    return timerId;
}

bool TimerService::clearInterval(int timerId) {
    return clearTimeout(timerId);  // Same implementation
}

bool TimerService::isActive(int timerId) const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return m_impl->timers.find(timerId) != m_impl->timers.end();
}

std::vector<int> TimerService::getActiveTimers() const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    std::vector<int> ids;
    for (const auto& [id, timer] : m_impl->timers) {
        ids.push_back(id);
    }
    return ids;
}

void TimerService::clearAll() {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    for (auto& [id, timer] : m_impl->timers) {
        timer->cancelled = true;
        if (timer->thread.joinable()) {
            timer->thread.detach();
        }
    }
    m_impl->timers.clear();
}

} // namespace havel::host
