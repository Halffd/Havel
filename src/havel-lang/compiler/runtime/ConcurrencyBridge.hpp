#pragma once

#include "../../runtime/HostContext.hpp"
#include "../core/BytecodeIR.hpp"
#include "../core/Pipeline.hpp"
#include "EventQueue.hpp"

#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <queue>
#include <condition_variable>
#include <unordered_map>

namespace havel::compiler {

/**
 * ConcurrencyBridge - Host functions for concurrency primitives
 *
 * Implements thread pool management for:
 * - thread.spawn, thread.join, thread.send, thread.receive
 * - interval.start, interval.stop
 * - timeout.start, timeout.cancel
 * - channel.new, channel.send, channel.receive, channel.close
 */
class ConcurrencyBridge {
public:
  explicit ConcurrencyBridge(const ::havel::HostContext &ctx);
  ~ConcurrencyBridge();

  void install(PipelineOptions &options);

  // Check for expired timers (to be called from main event loop)
  void checkTimers();
  
  // Process all enqueued callbacks from timers, threads, channels
  // Should be called from main event loop
  void processEventQueue() {
    if (event_queue_) {
      event_queue_->processAll();
    }
  }
  
  // Get event queue for enqueuing callbacks from worker threads
  EventQueue* eventQueue() { return event_queue_.get(); }

  // Phase 3B-7: Check if a thread has completed
  // Used by ExecutionEngine to detect thread completion and unpark waiting fibers
  bool isThreadCompleted(uint32_t thread_id);
  
  // Phase 3B-7: Mark a thread as completed
  // Called when a thread finishes execution (or when removed from active_threads)
  void markThreadCompleted(uint32_t thread_id);
  
  // Phase 3B-7: Consume completion notification (clears the flag)
  void clearThreadCompletion(uint32_t thread_id);
  
  // Phase 3B-7: Get all completed thread IDs (for ExecutionEngine iteration)
  std::vector<uint32_t> getCompletedThreadIds();
  
  // Phase 3B-7: Cleanup and join a completed thread
  // Safely joins the OS thread and removes it from tracking
  // Returns true if thread was cleaned up, false if not found or still running
  bool cleanupThread(uint32_t thread_id);
  
  // Phase 3B-7: Cleanup all completed threads (called from main loop)
  // Returns count of threads cleaned up
  int cleanupCompletedThreads();

private:
  const ::havel::HostContext *ctx_;
  class compiler::VM *vm_;

  // Thread pool management
  struct ThreadTask {
    std::function<void()> task;
  };

  std::vector<std::thread> thread_pool_;
  std::queue<ThreadTask> task_queue_;
  std::mutex queue_mutex_;
  std::condition_variable queue_cv_;
  bool shutdown_ = false;

  // Phase 3B-7: Enhanced thread state tracking
  enum class ThreadState : uint8_t {
    CREATED,      // Thread spawned but not started
    RUNNING,      // Thread is executing
    COMPLETED,    // Thread finished execution
    JOINED        // Thread has been joined/cleaned up
  };

  struct ManagedThread {
    uint32_t id;
    ThreadState state;
    std::thread* thread_ptr;  // Owned by active_threads_, just reference here
    std::chrono::steady_clock::time_point created_at;
    std::chrono::steady_clock::time_point completed_at;
  };

  // Thread management
  std::unordered_map<uint32_t, std::thread> active_threads_;
  std::unordered_map<uint32_t, std::vector<Value>> thread_mailboxes_;
  std::mutex threads_mutex_;
  uint32_t next_thread_id_ = 1;

  // Phase 3B-7: Thread completion tracking
  // Tracks which threads have completed (for unparking waiting fibers)
  std::unordered_set<uint32_t> completed_threads_;
  std::mutex completed_threads_mutex_;

  // Phase 3B-7: Enhanced thread info tracking
  std::unordered_map<uint32_t, ManagedThread> thread_info_;
  
  // Timer queue for main event loop
  struct Timer {
    uint32_t id;
    std::chrono::steady_clock::time_point next_run;
    int64_t interval_ms;  // 0 for one-shot (timeout), >0 for repeating (interval)
    Value callback;
    bool active;
  };
  std::vector<Timer> timers_;
  std::mutex timers_mutex_;
  uint32_t next_timer_id_ = 1;

  // Channels
  struct Channel {
    std::queue<Value> queue;
    std::mutex mutex;
    std::condition_variable cv;
    bool closed = false;
  };
  std::unordered_map<uint32_t, std::unique_ptr<Channel>> channels_;
  std::mutex channels_mutex_;
  uint32_t next_channel_id_ = 1;

  // Event queue for non-blocking callback distribution
  std::unique_ptr<EventQueue> event_queue_;

  // Thread pool initialization
  void initThreadPool(size_t pool_size = 4);

  // Host function implementations
  Value threadSpawn(const std::vector<Value> &args);
  Value threadJoin(const std::vector<Value> &args);
  Value threadSend(const std::vector<Value> &args);
  Value threadReceive(const std::vector<Value> &args);

  Value intervalStart(const std::vector<Value> &args);
  Value intervalStop(const std::vector<Value> &args);

  Value timeoutStart(const std::vector<Value> &args);
  Value timeoutCancel(const std::vector<Value> &args);

  Value channelNew(const std::vector<Value> &args);
  Value channelSend(const std::vector<Value> &args);
  Value channelReceive(const std::vector<Value> &args);
  Value channelClose(const std::vector<Value> &args);
};

} // namespace havel::compiler
