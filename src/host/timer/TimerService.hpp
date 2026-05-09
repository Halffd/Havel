/*
 * TimerService.hpp - Timer management service
 *
 * Tracks active intervals/timeouts and provides lifecycle management.
 * Actual timer execution is handled by VM's Interval/Timeout objects.
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <unordered_set>

namespace havel::host {

class TimerService {
public:
  TimerService() = default;
  ~TimerService() = default;

  void trackInterval(uint32_t id);
  void trackTimeout(uint32_t id);
  void untrackInterval(uint32_t id);
  void untrackTimeout(uint32_t id);

  size_t activeIntervalCount() const;
  size_t activeTimeoutCount() const;
  size_t activeCount() const;

  void clearAllIntervals();
  void clearAllTimeouts();
  void clearAll();

  bool isIntervalActive(uint32_t id) const;
  bool isTimeoutActive(uint32_t id) const;

private:
  mutable std::mutex mutex_;
  std::unordered_set<uint32_t> active_intervals_;
  std::unordered_set<uint32_t> active_timeouts_;
};

} // namespace havel::host
