#include "AsyncBridge.hpp"
#include "../vm/VM.hpp"

#include <chrono>

namespace havel::compiler {

AsyncBridge::AsyncBridge(const ::havel::HostContext &ctx) : ctx_(&ctx) {
  initThreadPool();
}

AsyncBridge::~AsyncBridge() {
  shutdown_ = true;
  queue_cv_.notify_all();
  
  for (auto &thread : thread_pool_) {
    if (thread.joinable()) {
      thread.join();
    }
  }

  // Clean up active threads
  {
    std::lock_guard<std::mutex> lock(threads_mutex_);
    for (auto &[id, thread] : active_threads_) {
      if (thread.joinable()) {
        thread.join();
      }
    }
  }

  // Clean up intervals
  {
    std::lock_guard<std::mutex> lock(intervals_mutex_);
    for (auto &[id, timer] : intervals_) {
      timer->running = false;
      if (timer->timer_thread.joinable()) {
        timer->timer_thread.join();
      }
    }
  }

  // Clean up timeouts
  {
    std::lock_guard<std::mutex> lock(timeouts_mutex_);
    for (auto &[id, timer] : timeouts_) {
      timer->running = false;
      if (timer->timer_thread.joinable()) {
        timer->timer_thread.join();
      }
    }
  }
}

void AsyncBridge::initThreadPool(size_t pool_size) {
  for (size_t i = 0; i < pool_size; ++i) {
    thread_pool_.emplace_back([this] {
      while (true) {
        ThreadTask task;
        {
          std::unique_lock<std::mutex> lock(queue_mutex_);
          queue_cv_.wait(lock, [this] {
            return shutdown_ || !task_queue_.empty();
          });
          
          if (shutdown_ && task_queue_.empty()) {
            return;
          }
          
          if (!task_queue_.empty()) {
            task = std::move(task_queue_.front());
            task_queue_.pop();
          }
        }
        
        if (task.task) {
          task.task();
        }
      }
    });
  }
}

void AsyncBridge::install(PipelineOptions &options) {
  // Thread operations
  options.host_functions["thread.spawn"] = [this](const std::vector<Value> &args) {
    return threadSpawn(args);
  };
  options.host_functions["thread.join"] = [this](const std::vector<Value> &args) {
    return threadJoin(args);
  };
  options.host_functions["thread.send"] = [this](const std::vector<Value> &args) {
    return threadSend(args);
  };
  options.host_functions["thread.receive"] = [this](const std::vector<Value> &args) {
    return threadReceive(args);
  };

  // Interval operations
  options.host_functions["interval.start"] = [this](const std::vector<Value> &args) {
    return intervalStart(args);
  };
  options.host_functions["interval.stop"] = [this](const std::vector<Value> &args) {
    return intervalStop(args);
  };

  // Timeout operations
  options.host_functions["timeout.start"] = [this](const std::vector<Value> &args) {
    return timeoutStart(args);
  };
  options.host_functions["timeout.cancel"] = [this](const std::vector<Value> &args) {
    return timeoutCancel(args);
  };

  // Channel operations
  options.host_functions["channel.new"] = [this](const std::vector<Value> &args) {
    return channelNew(args);
  };
  options.host_functions["channel.send"] = [this](const std::vector<Value> &args) {
    return channelSend(args);
  };
  options.host_functions["channel.receive"] = [this](const std::vector<Value> &args) {
    return channelReceive(args);
  };
  options.host_functions["channel.close"] = [this](const std::vector<Value> &args) {
    return channelClose(args);
  };
}

Value AsyncBridge::threadSpawn(const std::vector<Value> &args) {
  if (args.empty() || !args[0].isClosureId() && !args[0].isFunctionObjId()) {
    return Value::makeNull();
  }

  // For now, return a thread ID placeholder
  // In a full implementation, this would spawn a thread to execute the closure
  uint32_t thread_id;
  {
    std::lock_guard<std::mutex> lock(threads_mutex_);
    thread_id = next_thread_id_++;
  }

  return Value::makeThreadId(thread_id);
}

Value AsyncBridge::threadJoin(const std::vector<Value> &args) {
  if (args.empty() || !args[0].isThreadId()) {
    return Value::makeNull();
  }

  uint32_t thread_id = args[0].asThreadId();

  // Wait for thread to complete
  std::lock_guard<std::mutex> lock(threads_mutex_);
  auto it = active_threads_.find(thread_id);
  if (it != active_threads_.end() && it->second.joinable()) {
    it->second.join();
    active_threads_.erase(it);
  }

  return Value::makeNull();
}

Value AsyncBridge::threadSend(const std::vector<Value> &args) {
  if (args.size() < 2 || !args[0].isThreadId()) {
    return Value::makeNull();
  }

  uint32_t thread_id = args[0].asThreadId();
  Value message = args[1];

  std::lock_guard<std::mutex> lock(threads_mutex_);
  thread_mailboxes_[thread_id].push_back(message);

  return Value::makeNull();
}

Value AsyncBridge::threadReceive(const std::vector<Value> &args) {
  if (args.empty() || !args[0].isThreadId()) {
    return Value::makeNull();
  }

  uint32_t thread_id = args[0].asThreadId();

  std::lock_guard<std::mutex> lock(threads_mutex_);
  auto &mailbox = thread_mailboxes_[thread_id];
  
  if (mailbox.empty()) {
    return Value::makeNull();
  }

  Value message = mailbox.front();
  mailbox.erase(mailbox.begin());
  
  return message;
}

Value AsyncBridge::intervalStart(const std::vector<Value> &args) {
  if (args.size() < 2 || !args[0].isInt() || !args[1].isClosureId() && !args[1].isFunctionObjId()) {
    return Value::makeNull();
  }

  int64_t interval_ms = args[0].asInt();
  uint32_t interval_id;
  
  {
    std::lock_guard<std::mutex> lock(intervals_mutex_);
    interval_id = next_interval_id_++;
  }

  // Create interval timer
  auto timer = std::make_unique<IntervalTimer>();
  timer->running = true;
  
  timer->timer_thread = std::thread([this, interval_id, interval_ms] {
    while (true) {
      std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
      
      std::lock_guard<std::mutex> lock(intervals_mutex_);
      auto it = intervals_.find(interval_id);
      if (it == intervals_.end() || !it->second->running) {
        break;
      }
      
      // Execute the interval callback
      // TODO: Execute the closure/function
    }
  });

  {
    std::lock_guard<std::mutex> lock(intervals_mutex_);
    intervals_[interval_id] = std::move(timer);
  }

  return Value::makeIntervalId(interval_id);
}

Value AsyncBridge::intervalStop(const std::vector<Value> &args) {
  if (args.empty() || !args[0].isIntervalId()) {
    return Value::makeNull();
  }

  uint32_t interval_id = args[0].asIntervalId();

  std::lock_guard<std::mutex> lock(intervals_mutex_);
  auto it = intervals_.find(interval_id);
  if (it != intervals_.end()) {
    it->second->running = false;
    if (it->second->timer_thread.joinable()) {
      it->second->timer_thread.join();
    }
    intervals_.erase(it);
  }

  return Value::makeNull();
}

Value AsyncBridge::timeoutStart(const std::vector<Value> &args) {
  if (args.size() < 2 || !args[0].isInt() || !args[1].isClosureId() && !args[1].isFunctionObjId()) {
    return Value::makeNull();
  }

  int64_t delay_ms = args[0].asInt();
  uint32_t timeout_id;
  
  {
    std::lock_guard<std::mutex> lock(timeouts_mutex_);
    timeout_id = next_timeout_id_++;
  }

  // Create timeout timer
  auto timer = std::make_unique<TimeoutTimer>();
  timer->running = true;
  
  timer->timer_thread = std::thread([this, timeout_id, delay_ms] {
    std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    
    std::lock_guard<std::mutex> lock(timeouts_mutex_);
    auto it = timeouts_.find(timeout_id);
    if (it != timeouts_.end() && it->second->running) {
      // Execute the timeout callback
      // TODO: Execute the closure/function
      timeouts_.erase(it);
    }
  });

  {
    std::lock_guard<std::mutex> lock(timeouts_mutex_);
    timeouts_[timeout_id] = std::move(timer);
  }

  return Value::makeTimeoutId(timeout_id);
}

Value AsyncBridge::timeoutCancel(const std::vector<Value> &args) {
  if (args.empty() || !args[0].isTimeoutId()) {
    return Value::makeNull();
  }

  uint32_t timeout_id = args[0].asTimeoutId();

  std::lock_guard<std::mutex> lock(timeouts_mutex_);
  auto it = timeouts_.find(timeout_id);
  if (it != timeouts_.end()) {
    it->second->running = false;
    if (it->second->timer_thread.joinable()) {
      it->second->timer_thread.join();
    }
    timeouts_.erase(it);
  }

  return Value::makeNull();
}

Value AsyncBridge::channelNew(const std::vector<Value> &args) {
  (void)args; // No arguments needed

  uint32_t channel_id;
  {
    std::lock_guard<std::mutex> lock(channels_mutex_);
    channel_id = next_channel_id_++;
    channels_[channel_id] = std::make_unique<Channel>();
  }

  return Value::makeChannelId(channel_id);
}

Value AsyncBridge::channelSend(const std::vector<Value> &args) {
  if (args.size() < 2 || !args[0].isChannelId()) {
    return Value::makeNull();
  }

  uint32_t channel_id = args[0].asChannelId();
  Value value = args[1];

  std::lock_guard<std::mutex> lock(channels_mutex_);
  auto it = channels_.find(channel_id);
  if (it == channels_.end() || it->second->closed) {
    return Value::makeBool(false);
  }

  it->second->queue.push(value);
  it->second->cv.notify_one();

  return Value::makeBool(true);
}

Value AsyncBridge::channelReceive(const std::vector<Value> &args) {
  if (args.empty() || !args[0].isChannelId()) {
    return Value::makeNull();
  }

  uint32_t channel_id = args[0].asChannelId();

  std::unique_lock<std::mutex> lock(channels_mutex_);
  auto it = channels_.find(channel_id);
  if (it == channels_.end()) {
    return Value::makeNull();
  }

  // Wait for a value to be available or channel to be closed
  it->second->cv.wait(lock, [&] {
    return !it->second->queue.empty() || it->second->closed;
  });

  if (it->second->queue.empty() && it->second->closed) {
    return Value::makeNull();
  }

  Value value = it->second->queue.front();
  it->second->queue.pop();

  return value;
}

Value AsyncBridge::channelClose(const std::vector<Value> &args) {
  if (args.empty() || !args[0].isChannelId()) {
    return Value::makeNull();
  }

  uint32_t channel_id = args[0].asChannelId();

  std::lock_guard<std::mutex> lock(channels_mutex_);
  auto it = channels_.find(channel_id);
  if (it != channels_.end()) {
    it->second->closed = true;
    it->second->cv.notify_all();
  }

  return Value::makeNull();
}

} // namespace havel::compiler
