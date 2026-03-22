#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>
#include <variant>
#include <string>

namespace havel {

/**
 * Thread - Lightweight actor-based concurrency
 *
 * Usage:
 *   let worker = thread {
 *     on message(msg) {
 *       print(msg)
 *     }
 *   }
 *   worker.send("hello")
 */
class Thread {
public:
  using Message = std::variant<std::string, int, double>;
  using MessageHandler = std::function<void(const Message &)>;

  Thread() = default;
  ~Thread() = default;

  void start(MessageHandler) {}
  void send(const Message &) {}
  std::optional<Message> receive() { return std::nullopt; }
  void pause() {}
  void resume() {}
  void stop() {}
  bool isRunning() const { return false; }
  bool isPaused() const { return paused.load(); }

private:
  void messageLoop(MessageHandler handler);

  std::thread thread;
  std::queue<Message> messageQueue;
  std::mutex queueMutex;
  std::atomic<bool> running{false};
  std::atomic<bool> paused{false};
  std::atomic<bool> stopped{false};
};

/**
 * Interval - Repeating timer with control
 *
 * Usage:
 *   let timer = interval 1000 {
 *     print("tick")
 *   }
 *   timer.pause()
 *   timer.resume()
 *   timer.stop()
 */
class Interval {
public:
  Interval(int intervalMs, std::function<void()> callback);
  ~Interval();

  void pause();
  void resume();
  void stop();

  bool isRunning() const { return running.load(); }

private:
  void timerLoop();

  std::thread thread;
  int intervalMs;
  std::function<void()> callback;
  std::atomic<bool> running{true};
  std::atomic<bool> paused{false};
  std::atomic<bool> stopped{false};
};

/**
 * Timeout - One-shot delayed execution
 *
 * Usage:
 *   timeout 5000 {
 *     print("5 seconds later")
 *   }
 */
class Timeout {
public:
  Timeout(int timeoutMs, std::function<void()> callback);
  ~Timeout();

  void cancel();

private:
  void timerLoop();

  std::thread thread;
  int timeoutMs;
  std::function<void()> callback;
  std::atomic<bool> cancelled{false};
};

/**
 * TimeRange - First-class time range type
 *
 * Usage:
 *   if time.hour in (8..18) { ... }
 *   schedule (8..18) { ... }
 */
class TimeRange {
public:
  TimeRange(int start, int end);

  bool contains(int value) const;
  int getStart() const { return start; }
  int getEnd() const { return end; }

private:
  int start;
  int end;
};

} // namespace havel
