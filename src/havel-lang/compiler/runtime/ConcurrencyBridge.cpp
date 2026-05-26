#include "ConcurrencyBridge.hpp"
#include "../../../utils/Logger.hpp"
#include "../vm/VM.hpp"
#include "../../runtime/concurrency/Fiber.hpp"
#include "../../runtime/concurrency/Scheduler.hpp"
#include "../../runtime/concurrency/Thread.hpp"
#include "EventQueue.hpp"
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

options.host_functions["interval_pause"] = [this](const std::vector<Value> &args) {
return intervalPause(args);
};
options.host_functions["interval.pause"] = options.host_functions["interval_pause"];

options.host_functions["interval_resume"] = [this](const std::vector<Value> &args) {
return intervalResume(args);
};
options.host_functions["interval.resume"] = options.host_functions["interval_resume"];

// Timeout operations (snake_case + dot aliases)
  options.host_functions["timeout_start"] = [this](const std::vector<Value> &args) {
    return timeoutStart(args);
  };
  options.host_functions["timeout.start"] = options.host_functions["timeout_start"];

options.host_functions["timeout_cancel"] = [this](const std::vector<Value> &args) {
return timeoutCancel(args);
};
options.host_functions["timeout.cancel"] = options.host_functions["timeout_cancel"];
options.host_functions["timeout.stop"] = options.host_functions["timeout_cancel"];

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

  
  // If scheduler is available, spawn as a goroutine
  if (vm_ && vm_->scheduler_) {
    uint32_t gid = vm_->spawnGoroutine(args[0], {});
    return Value::makeThreadId(gid);
  }

  // No scheduler: execute synchronously via vm_->call
  // This runs the closure inline and returns immediately.
  // The returned thread will be in "completed" state for threadJoin.
  uint32_t thread_id;
  {
    std::lock_guard<std::mutex> lock(threads_mutex_);
    thread_id = next_thread_id_++;
    sync_threads_.insert(thread_id);
  }

  Value result = vm_->call(args[0], {});
  (void)result;

  return Value::makeThreadId(thread_id);
}

Value ConcurrencyBridge::threadJoin(const std::vector<Value> &args) {
  if (args.empty() || !args[0].isThreadId()) {
    return Value::makeNull();
  }

  uint32_t thread_id = args[0].asThreadId();

  
  // Check if this was a synchronous thread (no scheduler)
  {
    std::lock_guard<std::mutex> lock(threads_mutex_);
    if (sync_threads_.count(thread_id)) {
      sync_threads_.erase(thread_id);
      return Value::makeNull();
    }
  }

  // Check scheduler for goroutine state
  if (vm_ && vm_->scheduler_) {
    auto *sched = vm_->scheduler_;
    auto *g = sched->get(thread_id);
    if (!g || g->state == Scheduler::GoroutineState::Done) {
      return Value::makeNull();
    }

    
    // If we're inside a goroutine (ExecutionEngine mode), request fiber suspension
    // The ExecutionEngine will unpark when goroutine completes
    if (sched->current() != nullptr) {
      vm_->requestSuspension(
          static_cast<uint8_t>(SuspensionReason::THREAD_JOIN),
          reinterpret_cast<void *>(static_cast<uintptr_t>(thread_id)));
    } else {
      // Main script (headless or full mode): execute goroutine synchronously
      // This drives the goroutine to completion via vm_->call()
      if (g->state == Scheduler::GoroutineState::Created) {
        Value callee;
        if (g->closure_id > 0) {
          callee = Value::makeClosureId(g->closure_id);
        } else {
          callee = Value::makeFunctionObjId(g->function_id);
        }
        vm_->call(callee, g->locals);
        g->state = Scheduler::GoroutineState::Done;
        if (g->fiber) {
          g->fiber->state = FiberState::DONE;
        }
      }
    }
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
if (args.size() < 2 || !args[1].isClosureId() && !args[1].isFunctionObjId()) {
return Value::makeNull();
}

if (!vm_) return Value::makeNull();

int64_t interval_ms = 0;
auto parsed = vm_->parseDuration(args[0]);
if (!parsed) return Value::makeNull();
interval_ms = *parsed;

Value callback = args[1];
auto intervalIdPtr = std::make_shared<uint32_t>(0);

        auto vm_cb = [vm = vm_, callback, intervalIdPtr]() {
            auto *eq = vm->getEventQueue();
            if (eq && !eq->isShutdown()) {
                auto *payload = new std::pair<Value, uint32_t>(callback, *intervalIdPtr);
                eq->push(Event(EventType::TIMER_FIRE, 0, payload));
            }
        };

auto intervalObj = std::make_shared<Interval>(static_cast<int>(interval_ms), std::move(vm_cb));
auto intervalRef = vm_->getHeap().allocateIntervalObj(intervalObj);
*intervalIdPtr = intervalRef.id;
return Value::makeIntervalId(intervalRef.id);
}

Value ConcurrencyBridge::intervalStop(const std::vector<Value> &args) {
if (args.empty() || !args[0].isIntervalId()) {
return Value::makeNull();
}

if (!vm_) return Value::makeNull();

uint32_t interval_id = args[0].asIntervalId();
auto *intervalObj = vm_->getHeap().interval(interval_id);
if (intervalObj) {
intervalObj->stop();
}

return Value::makeNull();
}

Value ConcurrencyBridge::intervalPause(const std::vector<Value> &args) {
if (args.empty() || !args[0].isIntervalId()) {
return Value::makeNull();
}

if (!vm_) return Value::makeNull();

uint32_t interval_id = args[0].asIntervalId();
auto *intervalObj = vm_->getHeap().interval(interval_id);
if (intervalObj) {
intervalObj->pause();
}

return Value::makeNull();
}

Value ConcurrencyBridge::intervalResume(const std::vector<Value> &args) {
if (args.empty() || !args[0].isIntervalId()) {
return Value::makeNull();
}

if (!vm_) return Value::makeNull();

uint32_t interval_id = args[0].asIntervalId();
auto *intervalObj = vm_->getHeap().interval(interval_id);
if (intervalObj) {
intervalObj->resume();
}

return Value::makeNull();
}

Value ConcurrencyBridge::timeoutStart(const std::vector<Value> &args) {
if (args.size() < 2 || !args[1].isClosureId() && !args[1].isFunctionObjId()) {
return Value::makeNull();
}

if (!vm_) return Value::makeNull();

auto parsed = vm_->parseDuration(args[0]);
if (!parsed) return Value::makeNull();

int ms = static_cast<int>(*parsed);
Value callback = args[1];
auto timeoutIdPtr = std::make_shared<uint32_t>(0);

        auto vm_cb = [vm = vm_, callback, timeoutIdPtr]() {
            auto *eq = vm->getEventQueue();
            if (eq && !eq->isShutdown()) {
                auto *payload = new std::pair<Value, uint32_t>(callback, *timeoutIdPtr);
                eq->push(Event(EventType::TIMER_FIRE, 1, payload));
            }
        };

auto timeoutObj = std::make_shared<Timeout>(ms, std::move(vm_cb));
auto timeoutRef = vm_->getHeap().allocateTimeoutObj(timeoutObj);
*timeoutIdPtr = timeoutRef.id;
return Value::makeTimeoutId(timeoutRef.id);
}

Value ConcurrencyBridge::timeoutCancel(const std::vector<Value> &args) {
if (args.empty() || !args[0].isTimeoutId()) {
return Value::makeNull();
}

if (!vm_) return Value::makeNull();

uint32_t timeout_id = args[0].asTimeoutId();
auto *timeoutObj = vm_->getHeap().timeout(timeout_id);
if (timeoutObj) {
timeoutObj->cancel();
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

    // Emit CHANNEL_RECV event so the ExecutionEngine can unpark
    // any goroutine that is suspended waiting for data on this channel
    if (event_queue_) {
        event_queue_->push(Event(EventType::CHANNEL_RECV, channel_id));
    }

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

    // If data is available, return it immediately
    if (!it->second->queue.empty()) {
        Value value = it->second->queue.front();
        it->second->queue.pop();
        return value;
    }

    // Channel closed and empty
    if (it->second->closed) {
        return Value::makeNull();
    }

    // No data available — check if we're in a goroutine context
    // If so, suspend instead of blocking
    auto* vm = getVM();
    auto* sched = vm ? vm->getScheduler() : nullptr;
    if (sched) {
        auto* current_g = sched->current();
        if (current_g && current_g->state == Scheduler::GoroutineState::Running) {
            // Suspend the goroutine — the EventQueue will unpark us
            // when data arrives on this channel
            current_g->wait_handle.type = Scheduler::AwaitableType::CHANNEL_RECV;
            current_g->wait_handle.target_id = channel_id;
            current_g->wait_handle.resume_value = Value::makeNull();
            vm->requestSuspension(
                static_cast<uint8_t>(SuspensionReason::CHANNEL_RECV),
                reinterpret_cast<void*>(static_cast<intptr_t>(channel_id)));
            return Value::makeNull(); // placeholder, replaced on resume
        }
    }

    // No scheduler — blocking wait (legacy path)
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
    // Clean up closed channels with empty queues
    for (auto ci = channels_.begin(); ci != channels_.end(); ) {
      if (ci->second->closed && ci->second->queue.empty()) {
        ci = channels_.erase(ci);
      } else {
        ++ci;
      }
    }
  }

  return Value::makeNull();
}

void ConcurrencyBridge::checkTimers() {
  std::lock_guard<std::mutex> lock(timers_mutex_);
  auto now = std::chrono::steady_clock::now();
  
for (auto &timer : timers_) {
if (timer.active && !timer.paused && timer.next_run <= now) {
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

// ============================================================================

bool ConcurrencyBridge::isThreadCompleted(uint32_t thread_id) {
  std::lock_guard<std::mutex> lock(completed_threads_mutex_);
  return completed_threads_.count(thread_id) > 0;
}

void ConcurrencyBridge::markThreadCompleted(uint32_t thread_id) {
  std::lock_guard<std::mutex> lock(threads_mutex_);
  std::lock_guard<std::mutex> completed_lock(completed_threads_mutex_);
  completed_threads_.insert(thread_id);

  auto it = thread_info_.find(thread_id);
  if (it != thread_info_.end()) {
    it->second.state = ThreadState::COMPLETED;
    it->second.completed_at = std::chrono::steady_clock::now();
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
  thread_info_.erase(thread_id);

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
