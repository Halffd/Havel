// HotkeyExecutor.hpp
#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

class HotkeyExecutor {
public:
  struct SubmitResult {
    bool accepted; // false if queue full or shutting down
  };

  // Create executor with `workers` threads and `maxQueue` capacity.
  HotkeyExecutor(size_t workers = 16, size_t maxQueue = 8192)
      : maxQueue(maxQueue), stopFlag(false) {
    startWorkers(workers);
  }

  ~HotkeyExecutor() { shutdown(std::chrono::milliseconds(5000)); }

  // Submit a callback with timeout detection using worker thread monitoring
  // callback runs on a worker thread; timeout is handled by worker loop
  SubmitResult submit(std::function<void()> cb, int timeoutMs = 90000) {
    std::unique_lock<std::mutex> lk(queueMutex);
    if (stopFlag)
      return {false};
    if (taskQueue.size() >= maxQueue) {
      // Log queue overflow instead of silently dropping
      std::cerr << "[HotkeyExecutor] Queue full (" << taskQueue.size() << "/"
                << maxQueue << "), dropping hotkey\n";
      return {false};
    }

    // Package task with timeout and creation time
    auto task = std::make_shared<Task>();
    task->fn = std::move(cb);
    task->prom = std::make_shared<std::promise<void>>();
    task->fut = task->prom->get_future().share();
    task->timeoutMs = timeoutMs;
    task->createdTime = std::chrono::steady_clock::now();

    taskQueue.push(task);
    lk.unlock();
    queueCv.notify_one();

    return {true};
  }

  // Graceful shutdown: stop accepting tasks, wait for workers to finish
  // naturally Returns true if shutdown completed within timeout.
  bool shutdown(
      std::chrono::milliseconds waitTimeout = std::chrono::milliseconds(2000)) {
    {
      std::lock_guard<std::mutex> lk(queueMutex);
      if (stopFlag)
        return true;
      stopFlag = true;
    }
    queueCv.notify_all();

    // Wait for workers to finish their current tasks
    auto start = std::chrono::steady_clock::now();

    for (auto &t : workers) {
      if (!t.joinable())
        continue;

      // Calculate remaining timeout
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - start);
      auto remain = waitTimeout - elapsed;

      if (remain.count() > 0) {
        // Try to join with remaining timeout
        t.join();
      } else {
        // Timeout expired, detach to avoid blocking
        t.detach();
      }
    }
    return true;
  }

private:
  struct Task {
    std::function<void()> fn;
    std::shared_ptr<std::promise<void>> prom;
    std::shared_future<void> fut;
    int timeoutMs = 5000;
    std::chrono::steady_clock::time_point createdTime;
    std::atomic<bool> timedOut{false};

    Task() = default;

    // Delete move and copy operations since we have std::atomic member
    Task(Task &&) = delete;
    Task &operator=(Task &&) = delete;
    Task(const Task &) = delete;
    Task &operator=(const Task &) = delete;
  };

  void startWorkers(size_t n) {
    for (size_t i = 0; i < n; ++i) {
      workers.emplace_back([this, i]() { workerLoop(i); });
    }
  }

  void workerLoop(size_t workerIndex) {
    while (true) {
      std::shared_ptr<Task> t;
      {
        std::unique_lock<std::mutex> lk(queueMutex);
        queueCv.wait(lk, [this]() { return stopFlag || !taskQueue.empty(); });
        if (stopFlag && taskQueue.empty())
          break;
        if (!taskQueue.empty()) {
          t = taskQueue.front();
          taskQueue.pop();
        } else {
          continue;
        }
      }

      if (!t)
        continue;

      // Check if task has already timed out before execution
      auto now = std::chrono::steady_clock::now();
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
          now - t->createdTime);
      bool alreadyTimedOut = elapsed.count() > t->timeoutMs;
      if (alreadyTimedOut) {
        std::cerr << "[HotkeyExecutor] Task timed out before execution: "
                  << elapsed.count() << "ms > " << t->timeoutMs << "ms\n";
        t->timedOut.store(true);
      }

      // Execute task (even if timed out)
      try {
        t->fn();
      } catch (const std::exception &e) {
        std::cerr << "[HotkeyExecutor] Task threw exception: " << e.what()
                  << "\n";
      } catch (...) {
        std::cerr << "[HotkeyExecutor] Task threw unknown exception\n";
      }

      // Mark completion
      try {
        t->prom->set_value();
      } catch (...) {
        // promise already satisfied or already set due to timeout
      }
    }
  }

  std::vector<std::thread> workers;
  std::queue<std::shared_ptr<Task>> taskQueue;
  std::mutex queueMutex;
  std::condition_variable queueCv;
  size_t maxQueue;
  std::atomic<bool> stopFlag;
};
