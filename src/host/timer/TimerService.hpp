/*
 * TimerService.hpp - Timer service (merged header-only)
 * setTimeout/setInterval with std::thread
 */
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>
#include <unordered_map>
#include <chrono>

#include "../../havel-lang/runtime/Environment.hpp"

namespace havel::host {

using havel::HavelValue;
using havel::HavelRuntimeError;
// BuiltinFunction is already available from Environment.hpp

class TimerService {
    struct Timer {
        int id;
        std::thread thread;
        std::atomic<bool> cancelled{false};
        std::atomic<bool> running{true};
    };
    
    std::unordered_map<int, std::unique_ptr<Timer>> m_timers;
    mutable std::mutex m_mutex;
    std::atomic<int> m_nextId{1};

public:
    TimerService() = default;
    ~TimerService() { clearAll(); }

    int setTimeout(std::function<void()> fn, int delayMs) {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        int timerId = m_nextId++;
        auto timer = std::make_unique<Timer>();
        timer->id = timerId;
        
        timer->thread = std::thread([this, fn = std::move(fn), timerId, timer = timer.get(), delayMs]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
            
            if (!timer->cancelled) {
                try { fn(); } catch (...) {}
            }
            
            std::lock_guard<std::mutex> lock(m_mutex);
            m_timers.erase(timerId);
        });
        
        m_timers[timerId] = std::move(timer);
        return timerId;
    }

    bool clearTimeout(int timerId) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_timers.find(timerId);
        if (it == m_timers.end()) return false;
        
        it->second->cancelled = true;
        if (it->second->thread.joinable()) it->second->thread.detach();
        m_timers.erase(it);
        return true;
    }

    int setInterval(std::function<void()> fn, int intervalMs) {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        int timerId = m_nextId++;
        auto timer = std::make_unique<Timer>();
        timer->id = timerId;
        
        timer->thread = std::thread([this, fn = std::move(fn), timerId, timer = timer.get(), intervalMs]() {
            while (!timer->cancelled) {
                std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
                if (timer->cancelled) break;
                try { fn(); } catch (...) { break; }
            }
            std::lock_guard<std::mutex> lock(m_mutex);
            m_timers.erase(timerId);
        });
        
        m_timers[timerId] = std::move(timer);
        return timerId;
    }

    bool clearInterval(int timerId) { return clearTimeout(timerId); }
    bool isActive(int timerId) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_timers.find(timerId) != m_timers.end();
    }
    
    std::vector<int> getActiveTimers() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<int> ids;
        for (const auto& [id, timer] : m_timers) ids.push_back(id);
        return ids;
    }
    
    void clearAll() {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& [id, timer] : m_timers) {
            timer->cancelled = true;
            if (timer->thread.joinable()) timer->thread.detach();
        }
        m_timers.clear();
    }
};

// Module registration (co-located)
class Environment;
class IHostAPI;
struct HavelValue;
struct HavelRuntimeError;
template<typename T> struct BuiltinFunction;

inline void registerTimerModule(havel::Environment& env, std::shared_ptr<havel::IHostAPI>) {
    auto timerService = std::make_shared<TimerService>();
    
    auto timerObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
    
    (*timerObj)["setTimeout"] = HavelValue(::havel::BuiltinFunction(
        [timerService](const std::vector<HavelValue>& args) -> HavelResult {
            if (args.size() < 2) return HavelRuntimeError("setTimeout requires (fn, delayMs)");
            if (!args[0].isFunction()) return HavelRuntimeError("First argument must be a function");
            
            int delayMs = static_cast<int>(args[1].asNumber());
            auto fn = [func = args[0].get<::havel::BuiltinFunction>()]() { func({}); };
            
            return HavelValue(static_cast<double>(timerService->setTimeout(fn, delayMs)));
        }));
    
    (*timerObj)["clearTimeout"] = HavelValue(::havel::BuiltinFunction(
        [timerService](const std::vector<HavelValue>& args) -> HavelResult {
            if (args.empty()) return HavelRuntimeError("clearTimeout requires timerId");
            int timerId = static_cast<int>(args[0].asNumber());
            return HavelValue(timerService->clearTimeout(timerId));
        }));
    
    (*timerObj)["setInterval"] = HavelValue(::havel::BuiltinFunction(
        [timerService](const std::vector<HavelValue>& args) -> HavelResult {
            if (args.size() < 2) return HavelRuntimeError("setInterval requires (fn, intervalMs)");
            if (!args[0].isFunction()) return HavelRuntimeError("First argument must be a function");
            
            int intervalMs = static_cast<int>(args[1].asNumber());
            auto fn = [func = args[0].get<::havel::BuiltinFunction>()]() { func({}); };
            
            return HavelValue(static_cast<double>(timerService->setInterval(fn, intervalMs)));
        }));
    
    (*timerObj)["clearInterval"] = HavelValue(::havel::BuiltinFunction(
        [timerService](const std::vector<HavelValue>& args) -> HavelResult {
            if (args.empty()) return HavelRuntimeError("clearInterval requires timerId");
            int timerId = static_cast<int>(args[0].asNumber());
            return HavelValue(timerService->clearInterval(timerId));
        }));
    
    env.Define("timer", HavelValue(timerObj));
}

} // namespace havel::host
