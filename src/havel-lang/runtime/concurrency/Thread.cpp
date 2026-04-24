// Thread.cpp - Implementation of Thread, Interval, Timeout, and TimeRange
#include "Thread.hpp"
#include "../../../utils/Logger.hpp"
#include <chrono>
#include <iostream>
#include <thread>

namespace havel {

// ============================================================================
// Thread - Message-passing concurrency
// ============================================================================

void Thread::start(MessageHandler handler) {
  if (running.load()) {
    return; // Already running
  }
  
  running.store(true);
  stopped.store(false);
  paused.store(false);
  
  thread = std::thread(&Thread::messageLoop, this, std::move(handler));
}

void Thread::send(const Message &msg) {
  {
    std::lock_guard<std::mutex> lock(queueMutex);
    messageQueue.push(msg);
  }
  queueCV.notify_one();
}

std::optional<Thread::Message> Thread::receive() {
  std::unique_lock<std::mutex> lock(queueMutex);
  
  // Wait for message or stop signal
  queueCV.wait_for(lock, std::chrono::milliseconds(100), [this] {
    return !messageQueue.empty() || stopped.load();
  });
  
  if (messageQueue.empty()) {
    return std::nullopt;
  }
  
  Message msg = std::move(messageQueue.front());
  messageQueue.pop();
  return msg;
}

void Thread::pause() {
  paused.store(true);
}

void Thread::resume() {
  paused.store(false);
  queueCV.notify_one();
}

void Thread::stop() {
  if (!running.load()) {
    return;
  }
  
  stopped.store(true);
  running.store(false);
  queueCV.notify_one();
  
  if (thread.joinable()) {
    thread.join();
  }
}

void Thread::messageLoop(MessageHandler handler) {
  while (running.load() && !stopped.load()) {
    // Check if paused
    if (paused.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }
    
    // Wait for message
    std::unique_lock<std::mutex> lock(queueMutex);
    queueCV.wait_for(lock, std::chrono::milliseconds(100), [this] {
      return !messageQueue.empty() || stopped.load();
    });
    
    if (stopped.load()) {
      break;
    }
    
    if (!messageQueue.empty()) {
      Message msg = std::move(messageQueue.front());
      messageQueue.pop();
      lock.unlock();
      
      // Call handler
      try {
        handler(msg);
      } catch (const std::exception &e) {
        // Log error but continue processing
        havel::error("[Thread] Handler exception: {}", e.what());
      }
    }
  }
  
  running.store(false);
}

// ============================================================================
// Interval - Repeating timer
// ============================================================================

Interval::Interval(int intervalMs, std::function<void()> callback)
    : intervalMs(intervalMs), callback(std::move(callback)) {
  thread = std::thread(&Interval::timerLoop, this);
}

Interval::~Interval() {
  stop();
}

void Interval::pause() {
  paused.store(true);
}

void Interval::resume() {
  paused.store(false);
}

void Interval::stop() {
  if (!running.load()) {
    return;
  }
  
  stopped.store(true);
  running.store(false);
  
  if (thread.joinable()) {
    thread.join();
  }
}

void Interval::timerLoop() {
  while (running.load() && !stopped.load()) {
    if (paused.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }
    
    // Sleep for interval duration
    std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
    
    if (stopped.load() || paused.load()) {
      continue;
    }
    
    // Execute callback
    try {
      callback();
    } catch (const std::exception &e) {
        havel::error("[Interval] Callback exception: {}", e.what());
    }
  }
  
  running.store(false);
}

// ============================================================================
// Timeout - One-shot delayed execution
// ============================================================================

Timeout::Timeout(int timeoutMs, std::function<void()> callback)
    : timeoutMs(timeoutMs), callback(std::move(callback)) {
  thread = std::thread(&Timeout::timerLoop, this);
}

Timeout::~Timeout() {
  if (thread.joinable()) {
    thread.join();
  }
}

void Timeout::cancel() {
  if (cancelled.load()) {
    return;
  }
  
  cancelled.store(true);
  
  if (thread.joinable()) {
    thread.join();
  }
}

void Timeout::timerLoop() {
  // Wait for timeout duration
  std::this_thread::sleep_for(std::chrono::milliseconds(timeoutMs));
  
  if (cancelled.load()) {
    return;
  }
  
  // Execute callback once
  try {
    callback();
  } catch (const std::exception &e) {
        havel::error("[Timeout] Callback exception: {}", e.what());
  }
}

// ============================================================================
// TimeRange - Time range utilities
// ============================================================================

TimeRange::TimeRange(int start, int end) : start(start), end(end) {}

bool TimeRange::contains(int value) const {
  if (start <= end) {
    return value >= start && value <= end;
  }
  // Wraparound (e.g., 22..6 for night hours)
  return value >= start || value <= end;
}

} // namespace havel
