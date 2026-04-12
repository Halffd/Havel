#include "ConcurrencyBridge.hpp"
#include "../vm/VM.hpp"

#include <chrono>
#include <iostream>

namespace havel::compiler {

ConcurrencyBridge::ConcurrencyBridge(const ::havel::HostContext &ctx) : ctx_(&ctx), vm_(ctx.vm) {
  initThreadPool();
}

ConcurrencyBridge::~ConcurrencyBridge() {
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

  // Clean up timers (timer queue is automatically cleaned up when vector is destroyed)
  std::lock_guard<std::mutex> lock(timers_mutex_);
  timers_.clear();
}

void ConcurrencyBridge::initThreadPool(size_t pool_size) {
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

void ConcurrencyBridge::install(PipelineOptions &options) {
  // Thread operations
  options.host_functions["thread_spawn"] = [this](const std::vector<Value> &args) {
    return threadSpawn(args);
  };
  options.host_functions["thread_join"] = [this](const std::vector<Value> &args) {
    return threadJoin(args);
  };
  options.host_functions["thread_send"] = [this](const std::vector<Value> &args) {
    return threadSend(args);
  };
  options.host_functions["thread_receive"] = [this](const std::vector<Value> &args) {
    return threadReceive(args);
  };

  // Interval operations
  options.host_functions["interval_start"] = [this](const std::vector<Value> &args) {
    return intervalStart(args);
  };
  options.host_functions["interval_stop"] = [this](const std::vector<Value> &args) {
    return intervalStop(args);
  };

  // Timeout operations
  options.host_functions["timeout_start"] = [this](const std::vector<Value> &args) {
    return timeoutStart(args);
  };
  options.host_functions["timeout_cancel"] = [this](const std::vector<Value> &args) {
    return timeoutCancel(args);
  };

  // Channel operations
  options.host_functions["channel_new"] = [this](const std::vector<Value> &args) {
    return channelNew(args);
  };
  options.host_functions["channel_send"] = [this](const std::vector<Value> &args) {
    return channelSend(args);
  };
  options.host_functions["channel_receive"] = [this](const std::vector<Value> &args) {
    return channelReceive(args);
  };
  options.host_functions["channel_close"] = [this](const std::vector<Value> &args) {
    return channelClose(args);
  };
}

Value ConcurrencyBridge::threadSpawn(const std::vector<Value> &args) {
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

Value ConcurrencyBridge::threadJoin(const std::vector<Value> &args) {
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

Value ConcurrencyBridge::threadSend(const std::vector<Value> &args) {
  if (args.size() < 2 || !args[0].isThreadId()) {
    return Value::makeNull();
  }

  uint32_t thread_id = args[0].asThreadId();
  Value message = args[1];

  std::lock_guard<std::mutex> lock(threads_mutex_);
  thread_mailboxes_[thread_id].push_back(message);

  return Value::makeNull();
}

Value ConcurrencyBridge::threadReceive(const std::vector<Value> &args) {
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

Value ConcurrencyBridge::intervalStart(const std::vector<Value> &args) {
  if (args.size() < 2 || !args[0].isInt() || !args[1].isClosureId() && !args[1].isFunctionObjId()) {
    return Value::makeNull();
  }

  int64_t interval_ms = args[0].asInt();
  Value callback = args[1];
  
  std::lock_guard<std::mutex> lock(timers_mutex_);
  uint32_t timer_id = next_timer_id_++;
  
  Timer timer;
  timer.id = timer_id;
  timer.next_run = std::chrono::steady_clock::now() + std::chrono::milliseconds(interval_ms);
  timer.interval_ms = interval_ms;
  timer.callback = callback;
  timer.active = true;
  
  timers_.push_back(timer);

  return Value::makeIntervalId(timer_id);
}

Value ConcurrencyBridge::intervalStop(const std::vector<Value> &args) {
  if (args.empty() || !args[0].isIntervalId()) {
    return Value::makeNull();
  }

  uint32_t interval_id = args[0].asIntervalId();

  std::lock_guard<std::mutex> lock(timers_mutex_);
  for (auto &timer : timers_) {
    if (timer.id == interval_id && timer.active) {
      timer.active = false;
      break;
    }
  }

  return Value::makeNull();
}

Value ConcurrencyBridge::timeoutStart(const std::vector<Value> &args) {
  if (args.size() < 2 || !args[0].isInt() || !args[1].isClosureId() && !args[1].isFunctionObjId()) {
    return Value::makeNull();
  }

  int64_t delay_ms = args[0].asInt();
  Value callback = args[1];
  
  std::lock_guard<std::mutex> lock(timers_mutex_);
  uint32_t timer_id = next_timer_id_++;
  
  Timer timer;
  timer.id = timer_id;
  timer.next_run = std::chrono::steady_clock::now() + std::chrono::milliseconds(delay_ms);
  timer.interval_ms = 0;  // 0 means one-shot (timeout)
  timer.callback = callback;
  timer.active = true;
  
  timers_.push_back(timer);

  return Value::makeTimeoutId(timer_id);
}

Value ConcurrencyBridge::timeoutCancel(const std::vector<Value> &args) {
  if (args.empty() || !args[0].isTimeoutId()) {
    return Value::makeNull();
  }

  uint32_t timeout_id = args[0].asTimeoutId();

  std::lock_guard<std::mutex> lock(timers_mutex_);
  for (auto &timer : timers_) {
    if (timer.id == timeout_id && timer.active) {
      timer.active = false;
      break;
    }
  }

  return Value::makeNull();
}

Value ConcurrencyBridge::channelNew(const std::vector<Value> &args) {
  (void)args; // No arguments needed

  uint32_t channel_id;
  {
    std::lock_guard<std::mutex> lock(channels_mutex_);
    channel_id = next_channel_id_++;
    channels_[channel_id] = std::make_unique<Channel>();
  }

  return Value::makeChannelId(channel_id);
}

Value ConcurrencyBridge::channelSend(const std::vector<Value> &args) {
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

Value ConcurrencyBridge::channelReceive(const std::vector<Value> &args) {
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

Value ConcurrencyBridge::channelClose(const std::vector<Value> &args) {
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

void ConcurrencyBridge::checkTimers() {
  std::lock_guard<std::mutex> lock(timers_mutex_);
  auto now = std::chrono::steady_clock::now();
  
  for (auto &timer : timers_) {
    if (timer.active && timer.next_run <= now) {
      // Execute the callback
      // TODO: Execute the callback via VM
      // For now, print a debug message
      std::cout << "Timer " << timer.id << " triggered" << std::endl;
      
      if (timer.interval_ms > 0) {
        // Interval timer - schedule next run
        timer.next_run = now + std::chrono::milliseconds(timer.interval_ms);
      } else {
        // One-shot timeout timer - deactivate
        timer.active = false;
      }
    }
  }
  
  // Remove inactive timers
  timers_.erase(std::remove_if(timers_.begin(), timers_.end(),
                            [](const Timer &t) { return !t.active; }),
               timers_.end());
}

} // namespace havel::compiler
