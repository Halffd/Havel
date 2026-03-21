/*
 * TimerService.hpp
 *
 * Timer service for delayed and repeated execution.
 * 
 * Pure C++ implementation with std::thread and std::chrono.
 */
#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <functional>

namespace havel::host {

/**
 * TimerService - Timer management
 * 
 * Provides:
 * - setTimeout(fn, delay): Run function after delay
 * - setInterval(fn, interval): Run function repeatedly
 * - clearTimeout/clearInterval: Cancel timers
 */
class TimerService {
public:
    TimerService();
    ~TimerService();

    // =========================================================================
    // One-shot timers
    // =========================================================================

    /// Schedule function to run after delay
    /// @param fn Function to execute
    /// @param delayMs Delay in milliseconds
    /// @return Timer ID
    int setTimeout(std::function<void()> fn, int delayMs);

    /// Cancel a timeout
    /// @param timerId Timer ID
    /// @return true if cancelled
    bool clearTimeout(int timerId);

    // =========================================================================
    // Repeating timers
    // =========================================================================

    /// Schedule function to run repeatedly
    /// @param fn Function to execute
    /// @param intervalMs Interval in milliseconds
    /// @return Timer ID
    int setInterval(std::function<void()> fn, int intervalMs);

    /// Cancel an interval
    /// @param timerId Timer ID
    /// @return true if cancelled
    bool clearInterval(int timerId);

    // =========================================================================
    // Status
    // =========================================================================

    /// Check if timer is active
    bool isActive(int timerId) const;

    /// Get list of active timer IDs
    std::vector<int> getActiveTimers() const;

    /// Clear all timers
    void clearAll();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace havel::host
