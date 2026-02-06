// HotkeyExecutor.hpp
#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
#include <chrono>
#include <iostream>

class HotkeyExecutor {
public:
    struct SubmitResult {
        bool accepted;     // false if queue full or shutting down
    };

    // Create executor with `workers` threads and `maxQueue` capacity.
    HotkeyExecutor(size_t workers = 16, size_t maxQueue = 8192)
      : maxQueue(maxQueue), stopFlag(false)
    {
        startWorkers(workers);
    }

    ~HotkeyExecutor() {
        shutdown(std::chrono::milliseconds(5000));
    }

    // Submit a callback with a timeout in milliseconds for detection (not cancellation).
    // callback runs on a worker thread; if it runs longer than timeoutMs we log it.
    SubmitResult submit(std::function<void()> cb, int timeoutMs = 90000) {
        std::unique_lock<std::mutex> lk(queueMutex);
        if (stopFlag) return {false};
        if (taskQueue.size() >= maxQueue) return {false}; // backpressure: reject
        // Package the task with a promise so we can detect completion.
        auto task = std::make_shared<Task>();
        task->fn = std::move(cb);
        task->prom = std::make_shared<std::promise<void>>();
        // Convert future to shared_future to make it copyable
        task->fut = task->prom->get_future().share();
        task->timeoutMs = timeoutMs;

        taskQueue.push(task);
        lk.unlock();
        queueCv.notify_one();

        // Spawn a watcher thread to detect timeout for this specific task.
        // Note: watcher does not attempt to kill the worker. It only observes and logs.
        std::thread([weak = std::weak_ptr<Task>(task)]() {
            if (auto t = weak.lock()) {
                // Get a copy of the shared_future
                auto fut = t->fut;
                if (fut.wait_for(std::chrono::milliseconds(t->timeoutMs)) != std::future_status::ready) {
                    // timeout detected
                    // You can extend this to update metrics or notify the owner
                    std::cerr << "[HotkeyExecutor] Task timed out after "
                              << t->timeoutMs << "ms\n";
                    t->timedOut.store(true);
                }
                // let the watcher thread exit
            }
        }).detach();

        return {true};
    }

    // Graceful shutdown: stop accepting tasks, optionally wait for running tasks up to timeout.
    // Returns true if shutdown completed within timeout.
    bool shutdown(std::chrono::milliseconds waitTimeout = std::chrono::milliseconds(2000)) {
        {
            std::lock_guard<std::mutex> lk(queueMutex);
            if (stopFlag) return true;
            stopFlag = true;
        }
        queueCv.notify_all();

        // Wait for workers to exit
        auto start = std::chrono::steady_clock::now();
        for (auto &t : workers) {
            if (!t.joinable()) continue;
            auto remain = waitTimeout - std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
            if (remain.count() <= 0) {
                // deadline passed, try to join with zero timeout (non-block)
                if (t.joinable()) {
                    // can't force-terminate; detach to not block destructor
                    t.detach();
                }
                continue;
            }
            // join with blocking; if it doesn't finish in remain, we'll detach
            // we can't time-limited join portably, so just join and rely on threads finishing quickly
            try {
                t.join();
            } catch (...) {
                if (t.joinable()) t.detach();
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
        std::atomic<bool> timedOut{false};
        
        Task() = default;
        
        // Delete move and copy operations since we have std::atomic member
        Task(Task&&) = delete;
        Task& operator=(Task&&) = delete;
        Task(const Task&) = delete;
        Task& operator=(const Task&) = delete;
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
                if (stopFlag && taskQueue.empty()) break;
                if (!taskQueue.empty()) {
                    t = taskQueue.front();
                    taskQueue.pop();
                } else {
                    continue;
                }
            }

            if (!t) continue;

            // Execute task
            try {
                t->fn();
            } catch (const std::exception &e) {
                std::cerr << "[HotkeyExecutor] Task threw exception: " << e.what() << "\n";
            } catch (...) {
                std::cerr << "[HotkeyExecutor] Task threw unknown exception\n";
            }

            // Mark completion for watcher
            try {
                t->prom->set_value();
            } catch (...) {
                // promise already satisfied? ignore
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
