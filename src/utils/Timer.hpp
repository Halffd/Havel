#pragma once
#include "Logger.hpp"
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
namespace havel {
using TimerTask = std::shared_ptr<std::atomic<bool>>;
class TimerManager {
private:
  struct TimerInfo {
    std::atomic<bool> running{true};
    std::thread thread;
    std::chrono::milliseconds interval;
    std::function<void()> callback;
    bool repeating;
  };

  // Use the atomic<bool> pointer as key, not the atomic itself
  static std::unordered_map<std::shared_ptr<std::atomic<bool>>,
                            std::shared_ptr<TimerInfo>>
      activeTimers;
  // Use a shared_mutex for read-write lock
  static std::shared_mutex timersMutex;

public:
  static std::shared_ptr<std::atomic<bool>>
  SetTimer(int milliseconds, const std::function<void()> &func,
           bool repeating) {

    auto running = std::make_shared<std::atomic<bool>>(true);
    auto timerInfo = std::make_shared<TimerInfo>();

    timerInfo->interval = std::chrono::milliseconds(std::abs(milliseconds));
    timerInfo->callback = func;
    timerInfo->repeating = repeating;

    // Store before starting thread
    {
      std::unique_lock<std::shared_mutex> lock(timersMutex);
      activeTimers[running] = timerInfo;
    }

    timerInfo->thread = std::thread([running, timerInfo]() {
      try {
        if (timerInfo->repeating) {
          // Repeating timer
          while (running->load()) {
            auto start = std::chrono::steady_clock::now();

            // Wait for interval with interruption check
            while (std::chrono::steady_clock::now() - start <
                   timerInfo->interval) {
              if (!running->load())
                return;
              std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            if (running->load()) {
              timerInfo->callback();
            }
          }
        } else {
          // One-time timer
          auto start = std::chrono::steady_clock::now();

          // Wait for interval with interruption check
          while (std::chrono::steady_clock::now() - start <
                 timerInfo->interval) {
            if (!running->load())
              return;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
          }

          if (running->load()) {
            timerInfo->callback();
          }
        }
      } catch (const std::exception &e) {
        error("Timer callback threw exception: {}", e.what());
      }

      // Clean up when done
      std::unique_lock<std::shared_mutex> lock(timersMutex);
      activeTimers.erase(running);
    });

    timerInfo->thread.detach();
    return running;
  }

  static void StopTimer(std::shared_ptr<std::atomic<bool>> timer) {
    if (!timer)
      return;

    // Signal stop
    timer->store(false);

    // Clean up the timer entry
    std::unique_lock<std::shared_mutex> lock(timersMutex);
    auto it = activeTimers.find(timer);
    if (it != activeTimers.end()) {
      // If the thread is still joinable, we should wait for it to finish
      // but since we detach, the thread will clean itself up
      activeTimers.erase(it);
    }
  }

  // Optional: Clean up all timers
  static void CleanupAllTimers() {
    std::unique_lock<std::shared_mutex> lock(timersMutex);
    for (auto &[timer, info] : activeTimers) {
      timer->store(false);
    }
    activeTimers.clear();
  }
};
inline std::shared_ptr<std::atomic<bool>>
SetTimer(int milliseconds, const std::function<void()> &func) {
  bool repeating = milliseconds >= 0;
  return TimerManager::SetTimer(milliseconds, func, repeating);
}

inline std::shared_ptr<std::atomic<bool>>
SetInterval(int milliseconds, const std::function<void()> &func) {
  return TimerManager::SetTimer(milliseconds, func, true);
}

inline std::shared_ptr<std::atomic<bool>>
SetTimeout(int milliseconds, const std::function<void()> &func) {
  return TimerManager::SetTimer(milliseconds, func, false);
}

inline void StopTimer(std::shared_ptr<std::atomic<bool>> timer) {
  TimerManager::StopTimer(timer);
}

inline void StopInterval(std::shared_ptr<std::atomic<bool>> timer) {
  TimerManager::StopTimer(timer);
}

inline void StopTimeout(std::shared_ptr<std::atomic<bool>> timer) {
  TimerManager::StopTimer(timer);
}

// Optional: Global cleanup function
inline void CleanupAllTimers() { TimerManager::CleanupAllTimers(); }
} // namespace havel