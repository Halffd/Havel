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

  // Thread management
  std::unordered_map<uint32_t, std::thread> active_threads_;
  std::unordered_map<uint32_t, std::vector<Value>> thread_mailboxes_;
  std::mutex threads_mutex_;
  uint32_t next_thread_id_ = 1;

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
