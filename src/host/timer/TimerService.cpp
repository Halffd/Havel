/*
 * TimerService.cpp - Timer management service implementation
 */
#include "TimerService.hpp"

namespace havel::host {

void TimerService::trackInterval(uint32_t id) {
  std::lock_guard<std::mutex> lock(mutex_);
  active_intervals_.insert(id);
}

void TimerService::trackTimeout(uint32_t id) {
  std::lock_guard<std::mutex> lock(mutex_);
  active_timeouts_.insert(id);
}

void TimerService::untrackInterval(uint32_t id) {
  std::lock_guard<std::mutex> lock(mutex_);
  active_intervals_.erase(id);
}

void TimerService::untrackTimeout(uint32_t id) {
  std::lock_guard<std::mutex> lock(mutex_);
  active_timeouts_.erase(id);
}

size_t TimerService::activeIntervalCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return active_intervals_.size();
}

size_t TimerService::activeTimeoutCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return active_timeouts_.size();
}

size_t TimerService::activeCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return active_intervals_.size() + active_timeouts_.size();
}

void TimerService::clearAllIntervals() {
  std::lock_guard<std::mutex> lock(mutex_);
  active_intervals_.clear();
}

void TimerService::clearAllTimeouts() {
  std::lock_guard<std::mutex> lock(mutex_);
  active_timeouts_.clear();
}

void TimerService::clearAll() {
  std::lock_guard<std::mutex> lock(mutex_);
  active_intervals_.clear();
  active_timeouts_.clear();
}

bool TimerService::isIntervalActive(uint32_t id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return active_intervals_.count(id) > 0;
}

bool TimerService::isTimeoutActive(uint32_t id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return active_timeouts_.count(id) > 0;
}

} // namespace havel::host
