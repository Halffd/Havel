#pragma once

#include "../../runtime/HostContext.hpp"
#include "../core/BytecodeIR.hpp"

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
 * AsyncBridge - Host functions for concurrency primitives
 *
 * Implements thread pool management for:
 * - thread.spawn, thread.join, thread.send, thread.receive
 * - interval.start, interval.stop
 * - timeout.start, timeout.cancel
 * - channel.new, channel.send, channel.receive, channel.close
 */
class AsyncBridge {
public:
  explicit AsyncBridge(const ::havel::HostContext &ctx);
  ~AsyncBridge();

  void install(PipelineOptions &options);

private:
  const ::havel::HostContext *ctx_;

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

  // Interval timers
  struct IntervalTimer {
    std::thread timer_thread;
    bool running;
  };
  std::unordered_map<uint32_t, std::unique_ptr<IntervalTimer>> intervals_;
  std::mutex intervals_mutex_;
  uint32_t next_interval_id_ = 1;

  // Timeout timers
  struct TimeoutTimer {
    std::thread timer_thread;
    bool running;
  };
  std::unordered_map<uint32_t, std::unique_ptr<TimeoutTimer>> timeouts_;
  std::mutex timeouts_mutex_;
  uint32_t next_timeout_id_ = 1;

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
