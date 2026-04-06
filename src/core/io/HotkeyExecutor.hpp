// HotkeyExecutor.hpp
#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <future>
#include <iostream>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include "../../havel-lang/compiler/vm/VM.hpp"

class HotkeyExecutor {
public:
  struct SubmitResult {
    bool accepted; // false if queue full or shutting down
  };

  // Callback types
  using SimpleCallback = std::function<void()>;
  using VMCallback = std::function<void(havel::compiler::VM &)>;
  using ExecutionContextCallback =
      std::function<void(havel::compiler::VM::VMExecutionContext &)>;

  // Create executor with `workers` threads and `maxQueue` capacity.
  HotkeyExecutor(size_t workers = 16, size_t maxQueue = 8192)
      : maxQueue(maxQueue), stopFlag(false) {
    startWorkers(workers);
  }

  ~HotkeyExecutor() { shutdown(std::chrono::milliseconds(5000)); }

  // Submit a simple callback (legacy API)
  SubmitResult submit(std::function<void()> cb, int timeoutMs = 90000) {
    return submitInternal([cb = std::move(cb)]() mutable { cb(); }, timeoutMs);
  }

  // Submit a callback with VM context access (legacy - prefer execution
  // context)
  SubmitResult submitVM(havel::compiler::VM &vm, VMCallback cb,
                        int timeoutMs = 90000) {
    return submitInternal([cb = std::move(cb), &vm]() mutable { cb(vm); },
                          timeoutMs);
  }

  // Submit a callback with isolated execution context (RECOMMENDED for hotkeys)
  // This ensures thread-safe execution with isolated stack/locals
  SubmitResult submitExecutionContext(havel::compiler::VM &vm,
                                      ExecutionContextCallback cb,
                                      int timeoutMs = 90000) {
    return submitInternal(
        [cb = std::move(cb), &vm]() mutable {
          auto ctx = vm.createExecutionContext();
          cb(ctx);
        },
        timeoutMs);
  }

private:
  std::atomic<bool> timedOut{false};

  struct Task {
    std::function<void()> fn;
    std::shared_ptr<std::promise<void>> prom;
    std::shared_future<void> fut;
    int timeoutMs = 5000;
    std::chrono::steady_clock::time_point createdTime;
    std::atomic<bool> timedOut{false};
    
    // Error tracking
    std::string errorMessage;
    std::function<void(const std::string&)> errorCallback;

    Task() = default;
    Task(Task &&) = delete;
    Task &operator=(Task &&) = delete;
    Task(const Task &) = delete;
    Task &operator=(const Task &) = delete;
  };

  SubmitResult submitInternal(std::function<void()> cb, int timeoutMs) {
    std::unique_lock<std::mutex> lk(queueMutex);
    if (stopFlag)
      return {false};
    if (taskQueue.size() >= maxQueue) {
      std::cerr << "[HotkeyExecutor] Queue full (" << taskQueue.size() << "/"
                << maxQueue << "), dropping hotkey\n";
      return {false};
    }

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

  void startWorkers(size_t n) {
    for (size_t i = 0; i < n; ++i) {
      workers.emplace_back([this, i]() { workerLoop(i); });
    }
  }

  void workerLoop(size_t workerIndex) {
    (void)workerIndex;
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

      auto now = std::chrono::steady_clock::now();
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
          now - t->createdTime);
      bool alreadyTimedOut = elapsed.count() > t->timeoutMs;
      if (alreadyTimedOut) {
        std::cerr << "[HotkeyExecutor] Task timed out before execution: "
                  << elapsed.count() << "ms > " << t->timeoutMs << "ms\n";
        t->timedOut.store(true);
      }

      try {
        t->fn();
      } catch (const havel::compiler::ScriptError &e) {
        // Capture error message
        t->errorMessage = std::string("ScriptError: ") + e.what();
        
        // Call custom error callback if set
        if (t->errorCallback) {
          t->errorCallback(t->errorMessage);
        } else {
          // Default: print to stderr
          std::cerr << "[HotkeyExecutor] " << t->errorMessage << std::endl;
        }
      } catch (const std::exception &e) {
        // Capture error message
        t->errorMessage = std::string("Exception: ") + e.what();
        
        if (t->errorCallback) {
          t->errorCallback(t->errorMessage);
        } else {
          std::cerr << "[HotkeyExecutor] " << t->errorMessage << std::endl;
        }
      } catch (...) {
        t->errorMessage = "Unknown exception in hotkey execution";
        
        if (t->errorCallback) {
          t->errorCallback(t->errorMessage);
        } else {
          std::cerr << "[HotkeyExecutor] " << t->errorMessage << std::endl;
        }
      }

      try {
        t->prom->set_value();
      } catch (...) {
        // promise already satisfied
      }
    }
  }

  std::vector<std::thread> workers;
  std::queue<std::shared_ptr<Task>> taskQueue;
  std::mutex queueMutex;
  std::condition_variable queueCv;
  size_t maxQueue;
  std::atomic<bool> stopFlag;

public:
  void shutdown(std::chrono::milliseconds timeout) {
    {
      std::unique_lock<std::mutex> lk(queueMutex);
      stopFlag.store(true);
    }
    queueCv.notify_all();
    for (auto &worker : workers) {
      if (worker.joinable()) {
        worker.join();
      }
    }
    workers.clear();
  }
};
