#include "ConcurrencyBridge.hpp"
#include "../../../utils/Logger.hpp"
#include "../vm/VM.hpp"

#include <chrono>

namespace havel::compiler {

ConcurrencyBridge::ConcurrencyBridge(const ::havel::HostContext &ctx) : ctx_(&ctx), vm_(ctx.vm) {
  event_queue_ = std::make_unique<EventQueue>();
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
  // Thread operations (snake_case + dot aliases)
  options.host_functions["thread_spawn"] = [this](const std::vector<Value> &args) {
    return threadSpawn(args);
  };
  options.host_functions["thread.spawn"] = options.host_functions["thread_spawn"];

  options.host_functions["thread_join"] = [this](const std::vector<Value> &args) {
    return threadJoin(args);
  };
  options.host_functions["thread.join"] = options.host_functions["thread_join"];

  options.host_functions["thread_send"] = [this](const std::vector<Value> &args) {
    return threadSend(args);
  };
  options.host_functions["thread.send"] = options.host_functions["thread_send"];

  options.host_functions["thread_receive"] = [this](const std::vector<Value> &args) {
    return threadReceive(args);
  };
  options.host_functions["thread.receive"] = options.host_functions["thread_receive"];

  // Interval operations (snake_case + dot aliases)
  options.host_functions["interval_start"] = [this](const std::vector<Value> &args) {
    return intervalStart(args);
  };
  options.host_functions["interval.start"] = options.host_functions["interval_start"];

  options.host_functions["interval_stop"] = [this](const std::vector<Value> &args) {
    return intervalStop(args);
  };
  options.host_functions["interval.stop"] = options.host_functions["interval_stop"];

  // Timeout operations (snake_case + dot aliases)
  options.host_functions["timeout_start"] = [this](const std::vector<Value> &args) {
    return timeoutStart(args);
  };
  options.host_functions["timeout.start"] = options.host_functions["timeout_start"];

  options.host_functions["timeout_cancel"] = [this](const std::vector<Value> &args) {
    return timeoutCancel(args);
  };
  options.host_functions["timeout.cancel"] = options.host_functions["timeout_cancel"];

  // Channel operations (snake_case + dot aliases)
  options.host_functions["channel_new"] = [this](const std::vector<Value> &args) {
    return channelNew(args);
  };
  options.host_functions["channel.new"] = options.host_functions["channel_new"];

  options.host_functions["channel_send"] = [this](const std::vector<Value> &args) {
    return channelSend(args);
  };
  options.host_functions["channel.send"] = options.host_functions["channel_send"];

  options.host_functions["channel_receive"] = [this](const std::vector<Value> &args) {
    return channelReceive(args);
  };
  options.host_functions["channel.receive"] = options.host_functions["channel_receive"];

  options.host_functions["channel_close"] = [this](const std::vector<Value> &args) {
    return channelClose(args);
  };
  options.host_functions["channel.close"] = options.host_functions["channel_close"];
}

Value ConcurrencyBridge::threadSpawn(const std::vector<Value> &args) {
  if (args.empty() || !args[0].isClosureId() && !args[0].isFunctionObjId()) {
    return Value::makeNull();
  }

  uint32_t thread_id;
  {
    std::lock_guard<std::mutex> lock(threads_mutex_);
    thread_id = next_thread_id_++;
  }

  // Phase 3B-7: Spawn thread that pushes event when complete (not polling)
  std::thread t([this, thread_id]() {
    // Thread is now running
    // TODO: Execute the Havel closure in a safe execution context
    // For now, just sleep briefly
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // CRITICAL: Push event instead of setting flag
    // This wakes up the scheduler without polling
    if (event_queue_) {
      event_queue_->push(Event(EventType::THREAD_COMPLETE, thread_id));
    }
  });

  // Store thread in active map and initialize thread_info
  {
    std::lock_guard<std::mutex> lock(threads_mutex_);
    active_threads_[thread_id] = std::move(t);
    
    // Initialize thread_info with RUNNING state
    thread_info_[thread_id] = ManagedThread{
        thread_id,
        ThreadState::RUNNING,
        &active_threads_[thread_id],
        std::chrono::steady_clock::now(),
        std::chrono::steady_clock::time_point()
    };
  }

  return Value::makeThreadId(thread_id);
}

Value ConcurrencyBridge::threadJoin(const std::vector<Value> &args) {
  if (args.empty() || !args[0].isThreadId()) {
    return Value::makeNull();
  }

  uint32_t thread_id = args[0].asThreadId();

  // Phase 3B-7: Non-blocking join via event system
  // If thread already completed, clean up and return
  // Otherwise, caller's fiber will be suspended via THREAD_JOIN opcode
  // and unparked when THREAD_COMPLETE event fires

  std::lock_guard<std::mutex> lock(threads_mutex_);
  
  auto it = active_threads_.find(thread_id);
  if (it == active_threads_.end()) {
    return Value::makeNull();  // Thread not found or already cleaned up
  }

  // If thread is joinable and we're here, it means it hasn't finished yet
  // The caller's fiber will suspend and wait for event-driven wakeup
  // Just return null for now - ExecutionEngine event handler will do the actual cleanup
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
      // Execute the callback via VM if available
      if (vm_) {
        try {
          // Register callback and invoke it
          CallbackId cbId = vm_->registerCallback(timer.callback);
          vm_->invokeCallback(cbId, {});
          vm_->releaseCallback(cbId);
        } catch (const std::exception &e) {
          ::havel::error("Error executing timer callback: {}", e.what());
        }
      }
      
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

// ============================================================================
// PHASE 3B-7: THREAD COMPLETION TRACKING
// ============================================================================

bool ConcurrencyBridge::isThreadCompleted(uint32_t thread_id) {
  std::lock_guard<std::mutex> lock(completed_threads_mutex_);
  return completed_threads_.count(thread_id) > 0;
}

void ConcurrencyBridge::markThreadCompleted(uint32_t thread_id) {
  std::lock_guard<std::mutex> lock(completed_threads_mutex_);
  completed_threads_.insert(thread_id);
  
  // Also update thread_info if exists
  {
    std::lock_guard<std::mutex> info_lock(threads_mutex_);
    auto it = thread_info_.find(thread_id);
    if (it != thread_info_.end()) {
      it->second.state = ThreadState::COMPLETED;
      it->second.completed_at = std::chrono::steady_clock::now();
    }
  }
}

void ConcurrencyBridge::clearThreadCompletion(uint32_t thread_id) {
  std::lock_guard<std::mutex> lock(completed_threads_mutex_);
  completed_threads_.erase(thread_id);
}

std::vector<uint32_t> ConcurrencyBridge::getCompletedThreadIds() {
  std::lock_guard<std::mutex> lock(completed_threads_mutex_);
  std::vector<uint32_t> result(completed_threads_.begin(), completed_threads_.end());
  return result;
}

bool ConcurrencyBridge::cleanupThread(uint32_t thread_id) {
  std::lock_guard<std::mutex> lock(threads_mutex_);
  
  auto it = active_threads_.find(thread_id);
  if (it == active_threads_.end()) {
    return false;  // Thread not found
  }

  // Verify it's completed
  {
    std::lock_guard<std::mutex> completed_lock(completed_threads_mutex_);
    if (completed_threads_.count(thread_id) == 0) {
      return false;  // Thread not completed yet
    }
  }

  // Join and remove the thread
  if (it->second.joinable()) {
    it->second.join();
  }
  active_threads_.erase(it);

  // Update thread_info
  auto info_it = thread_info_.find(thread_id);
  if (info_it != thread_info_.end()) {
    info_it->second.state = ThreadState::JOINED;
  }

  // Remove mailbox
  thread_mailboxes_.erase(thread_id);

  // Clear completion tracking
  {
    std::lock_guard<std::mutex> completed_lock(completed_threads_mutex_);
    completed_threads_.erase(thread_id);
  }

  return true;
}

int ConcurrencyBridge::cleanupCompletedThreads() {
  std::lock_guard<std::mutex> lock(threads_mutex_);
  
  int cleaned = 0;
  std::vector<uint32_t> to_cleanup;

  // Get list of completed threads to clean
  {
    std::lock_guard<std::mutex> completed_lock(completed_threads_mutex_);
    for (uint32_t thread_id : completed_threads_) {
      auto it = active_threads_.find(thread_id);
      if (it != active_threads_.end()) {
        to_cleanup.push_back(thread_id);
      }
    }
  }

  // Clean up each thread
  for (uint32_t thread_id : to_cleanup) {
    auto it = active_threads_.find(thread_id);
    if (it != active_threads_.end() && it->second.joinable()) {
      it->second.join();
      active_threads_.erase(it);
      cleaned++;

      // Update thread_info
      auto info_it = thread_info_.find(thread_id);
      if (info_it != thread_info_.end()) {
        info_it->second.state = ThreadState::JOINED;
      }

      // Remove mailbox
      thread_mailboxes_.erase(thread_id);
    }
  }

  // Clear all from completed set
  {
    std::lock_guard<std::mutex> completed_lock(completed_threads_mutex_);
    completed_threads_.clear();
  }

  return cleaned;
}

} // namespace havel::compiler
