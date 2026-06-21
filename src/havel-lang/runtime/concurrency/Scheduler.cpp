#include "Scheduler.hpp"
#include "Fiber.hpp"
#include "../../../utils/Logger.hpp"
#include "utils/DebugFlags.hpp"
#include <algorithm>
#include <thread>
#ifndef _WIN32
#include <sys/eventfd.h>
#include <unistd.h>
#include <cstring>
#endif

namespace havel::compiler {

static Scheduler* g_scheduler_instance = nullptr;
static std::once_flag g_scheduler_once;

Scheduler& Scheduler::instance() {
  std::call_once(g_scheduler_once, []() {
    g_scheduler_instance = new Scheduler();
  });
  return *g_scheduler_instance;
}

Scheduler::Scheduler() {
#ifndef _WIN32
  deferred_wakeup_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (deferred_wakeup_fd_ < 0) {
    havel::warning("[Scheduler] Failed to create deferred wakeup eventfd: {}", strerror(errno));
  }
#endif
}

Scheduler::~Scheduler() {
#ifndef _WIN32
  if (deferred_wakeup_fd_ >= 0) {
    close(deferred_wakeup_fd_);
    deferred_wakeup_fd_ = -1;
  }
#endif
  stop();
}

Scheduler::Goroutine::~Goroutine() {
	if (fiber) {
		delete fiber;
	}
}

uint32_t Scheduler::spawn(uint32_t function_id, const std::vector<Value>& args,
 	uint32_t closure_id, const std::string& name, FiberPriority priority) {
    auto g = std::make_unique<Scheduler::Goroutine>(next_goroutine_id_++, name, priority);
    g->function_id = function_id;
    g->closure_id = closure_id;
    g->state = GoroutineState::Created;
    g->max_instructions_per_tick = (priority == FiberPriority::HOTKEY) ? hotkey_tick_instructions_ : default_tick_instructions_;

    if (debugging::debug_io) ::havel::debug("[Scheduler] SPAWN: gid={} name='{}' func_id={} closure_id={} priority={}", 
                  g->id, name, function_id, closure_id, (int)priority);

 	g->fiber = new Fiber(g->id, function_id, 0, name);
	if (closure_id > 0) {
		auto& frame = g->fiber->currentFrame();
		frame.closure_id = closure_id;
		frame.arg_count = static_cast<uint32_t>(args.size());
	}

	g->locals = args;

	for (const auto& arg : args) {
		g->fiber->stack.push(arg);
	}

	uint32_t g_id = g->id;

	{
		std::lock_guard<std::mutex> lock(goroutines_mutex_);
		goroutines_[g_id] = std::move(g);
	}

	{
		std::lock_guard<std::mutex> lock(priority_mutex_);
		if (priority == FiberPriority::HOTKEY) {
			hotkey_queue_.push_front(goroutines_[g_id].get());
            if (debugging::debug_io) ::havel::debug("[Scheduler] [PUSH FRONT] gid={} name='{}' priority=HOTKEY", g_id, name);
		} else if (priority == FiberPriority::BACKGROUND) {
			background_queue_.push_back(goroutines_[g_id].get());
            if (debugging::debug_io) ::havel::debug("[Scheduler] [PUSH BACK] gid={} name='{}' priority=BACKGROUND", g_id, name);
		} else {
			runnable_queue_.push_back(goroutines_[g_id].get());
            if (debugging::debug_io) ::havel::debug("[Scheduler] [PUSH BACK] gid={} name='{}' priority=NORMAL", g_id, name);
		}
	}

	return g_id;
}

Scheduler::Goroutine* Scheduler::current() {
    return current_.load(std::memory_order_acquire);
}

Scheduler::Goroutine* Scheduler::get(uint32_t id) {
	std::lock_guard<std::mutex> lock(goroutines_mutex_);
	auto it = goroutines_.find(id);
	if (it != goroutines_.end()) {
		return it->second.get();
	}
	return nullptr;
}

Scheduler::Goroutine* Scheduler::pickNext() {
  Goroutine* result = nullptr;

  {
    std::lock_guard<std::mutex> lock(priority_mutex_);

    if (debugging::debug_io) {
      ::havel::debug("[Scheduler] pickNext: hotkey_queue={} runnable_queue={} bg_queue={}",
        hotkey_queue_.size(), runnable_queue_.size(), background_queue_.size());
    }

    // Priority order: hotkey → normal → background
    // Hotkey queue: immediate execution (keyboard/mouse interrupts)
    while (!hotkey_queue_.empty()) {
      auto* g = hotkey_queue_.front();
      hotkey_queue_.pop_front();

      if (!g) {
        if (debugging::debug_io) ::havel::debug("[Scheduler] pickNext: null goroutine in hotkey_queue, skipping");
        continue;
      }
      if (g->state == GoroutineState::Done) {
        if (debugging::debug_io) ::havel::debug("[Scheduler] pickNext: gid={} state=Done, skipping", g->id);
        continue;
      }
      if (g->fiber && g->fiber->state == FiberState::DONE) {
        if (debugging::debug_io) ::havel::debug("[Scheduler] pickNext: gid={} fiber=DONE, marking Done", g->id);
        g->state = GoroutineState::Done;
        continue;
      }
      if (g->state == GoroutineState::Runnable || g->state == GoroutineState::Created) {
        result = g;
        if (debugging::debug_io) ::havel::debug("[Scheduler] pickNext: selected HOTKEY gid={} state={}",
          g->id, static_cast<int>(g->state.load()));
        break;
      }
      if (debugging::debug_io) ::havel::debug("[Scheduler] pickNext: gid={} state={} not runnable, continuing",
        g->id, static_cast<int>(g->state.load()));
    }

		// Normal queue: standard cooperative tasks
		if (!result) {
			while (!runnable_queue_.empty()) {
				auto* g = runnable_queue_.front();
				runnable_queue_.pop_front();

				if (!g) continue;
				if (g->state == GoroutineState::Done) continue;
				if (g->fiber && g->fiber->state == FiberState::DONE) {
					g->state = GoroutineState::Done;
					continue;
				}
				if (g->state == GoroutineState::Runnable || g->state == GoroutineState::Created) {
					result = g;
                    if (debugging::debug_io) ::havel::debug("[Scheduler] pickNext: selected RUNNABLE gid={} state={}", g->id, (int)g->state.load());
					break;
				}
			}
		}

		// Background queue: low-priority work
		if (!result) {
			while (!background_queue_.empty()) {
				auto* g = background_queue_.front();
				background_queue_.pop_front();

				if (!g) continue;
				if (g->state == GoroutineState::Done) continue;
				if (g->fiber && g->fiber->state == FiberState::DONE) {
					g->state = GoroutineState::Done;
					continue;
				}
				if (g->state == GoroutineState::Runnable || g->state == GoroutineState::Created) {
					result = g;
                    if (debugging::debug_io) ::havel::debug("[Scheduler] pickNext: selected BACKGROUND gid={} state={}", g->id, (int)g->state.load());
					break;
				}
			}
		}
	}

	if (result) {
		{
			std::lock_guard<std::mutex> wlock(result->wait_handle_mutex_);
			result->wait_handle.clear();  // Clear stale suspension context before running
		}
        current_.store(result, std::memory_order_release);
        if (debugging::debug_io) ::havel::debug("[Scheduler] [RUN] gid={} name='{}' state={}", 
                      result->id, result->name, (int)result->state.load());
	} else {
		// No runnable goroutines found. Periodic cleanup while idle.
		cleanupDoneGoroutines();
	}

	return result;
}

void Scheduler::suspend(Scheduler::Goroutine* g, SuspensionReason reason) {
	if (!g) return;

	g->state = GoroutineState::Suspended;
	g->suspension_reason.store(reason, std::memory_order_release);
    ::havel::debug("[Scheduler] [YIELD] gid={} name='{}' reason={}", 
                  g->id, g->name, (int)reason);
}

void Scheduler::unpark(Scheduler::Goroutine* g) {
	if (!g) return;

	if (g->state != GoroutineState::Suspended) {
		return;
	}

	g->state = GoroutineState::Runnable;
	g->suspension_reason.store(SuspensionReason::None, std::memory_order_release);
	{
		std::lock_guard<std::mutex> wlock(g->wait_handle_mutex_);
		g->wait_handle.clear();  // Clear stale suspension context
	}

	{
		std::lock_guard<std::mutex> lock(priority_mutex_);
		if (g->priority == FiberPriority::HOTKEY) {
			hotkey_queue_.push_back(g);
            ::havel::debug("[Scheduler] [UNPARK] gid={} name='{}' to HOTKEY queue", g->id, g->name);
		} else if (g->priority == FiberPriority::BACKGROUND) {
			background_queue_.push_back(g);
            ::havel::debug("[Scheduler] [UNPARK] gid={} name='{}' to BACKGROUND queue", g->id, g->name);
		} else {
			runnable_queue_.push_back(g);
            ::havel::debug("[Scheduler] [UNPARK] gid={} name='{}' to RUNNABLE queue", g->id, g->name);
		}
    }
}

Scheduler::Goroutine* Scheduler::findGoroutineByWaitTarget(AwaitableType type, uint32_t target_id) {
    std::lock_guard<std::mutex> lock(goroutines_mutex_);
    for (auto& [id, g] : goroutines_) {
        if (g->state == GoroutineState::Suspended &&
            g->wait_handle.type == type &&
            g->wait_handle.target_id == target_id) {
            return g.get();
        }
    }
    return nullptr;
}

Scheduler::Goroutine* Scheduler::findGoroutineByFiber(Fiber* fiber) {
    if (!fiber) return nullptr;
    std::lock_guard<std::mutex> lock(goroutines_mutex_);
    for (auto& [id, g] : goroutines_) {
        if (g->fiber == fiber) {
            return g.get();
        }
    }
    return nullptr;
}

void Scheduler::forEachConditionalHotkey(std::function<void(Goroutine*)> fn) {
    std::lock_guard<std::mutex> lock(goroutines_mutex_);
    for (auto& [id, g] : goroutines_) {
        if (g && g->hotkey_condition_callback_id != 0) {
            fn(g.get());
        }
    }
}

void Scheduler::start() {
	running_.store(true);
	shutdown_.store(false);
}

void Scheduler::stop() {
	shutdown_.store(true);
	running_.store(false);

	{
		std::lock_guard<std::mutex> lock(goroutines_mutex_);
		for (auto& [id, g] : goroutines_) {
			g->state = GoroutineState::Done;
		}
	}
	
	// Clean up all Done goroutines
	cleanupDoneGoroutines();
}

void Scheduler::waitAll() {
	while (running_.load()) {
		size_t active = 0;
		{
			std::lock_guard<std::mutex> lock(goroutines_mutex_);
			for (const auto& [id, g] : goroutines_) {
				if (g->state != GoroutineState::Done) {
					active++;
				}
			}
		}

		if (active == 0) {
			break;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
}

size_t Scheduler::goroutineCount() const {
	std::lock_guard<std::mutex> lock(goroutines_mutex_);
	return goroutines_.size();
}

size_t Scheduler::runnableCount() const {
  std::lock_guard<std::mutex> lock(priority_mutex_);
  size_t count = 0;
  for (auto* g : hotkey_queue_) {
    if (g && (g->state == GoroutineState::Runnable || g->state == GoroutineState::Created))
      count++;
  }
  for (auto* g : runnable_queue_) {
    if (g && (g->state == GoroutineState::Runnable || g->state == GoroutineState::Created))
      count++;
  }
  for (auto* g : background_queue_) {
    if (g && (g->state == GoroutineState::Runnable || g->state == GoroutineState::Created))
      count++;
  }
  return count;
}

size_t Scheduler::suspendedCount() const {
	std::lock_guard<std::mutex> lock(goroutines_mutex_);
	size_t count = 0;
	for (const auto& [id, g] : goroutines_) {
		if (g->state == GoroutineState::Suspended) {
			count++;
		}
	}
	return count;
}

void Scheduler::attachFiber(uint32_t goroutine_id, Fiber* fiber) {
	std::lock_guard<std::mutex> lock(goroutines_mutex_);
	auto it = goroutines_.find(goroutine_id);
	if (it != goroutines_.end()) {
		it->second->fiber = fiber;
	}
}

void Scheduler::yield(Goroutine* g) {
	if (!g) return;
	if (g->state == GoroutineState::Done) return;
	if (g->fiber && g->fiber->state == FiberState::DONE) {
		g->state = GoroutineState::Done;
		return;
	}
	g->state = GoroutineState::Runnable;

	std::lock_guard<std::mutex> lock(priority_mutex_);
	if (g->priority == FiberPriority::HOTKEY) {
		hotkey_queue_.push_back(g);
	} else if (g->priority == FiberPriority::BACKGROUND) {
		background_queue_.push_back(g);
	} else {
		runnable_queue_.push_back(g);
	}
}

void Scheduler::yieldCurrentAndCheckTimers() {
    Goroutine* g = current_.load(std::memory_order_acquire);
	if (!g) return;
	if (g->state == GoroutineState::Done) return;
	if (g->fiber && g->fiber->state == FiberState::DONE) {
		g->state = GoroutineState::Done;
		clearCurrent();
		return;
	}
	g->state = GoroutineState::Runnable;

	{
		std::lock_guard<std::mutex> lock(priority_mutex_);
		if (g->priority == FiberPriority::HOTKEY) {
			hotkey_queue_.push_back(g);
		} else if (g->priority == FiberPriority::BACKGROUND) {
			background_queue_.push_back(g);
		} else {
			runnable_queue_.push_back(g);
		}
	}
	clearCurrent();
}

void Scheduler::clearCurrent() {
    current_.store(nullptr, std::memory_order_release);
}

void Scheduler::addActionFiber(Fiber* fiber, FiberPriority priority) {
	if (!fiber) return;

	auto g = std::make_unique<Scheduler::Goroutine>(fiber->id, fiber->name, priority);

	g->function_id = fiber->current_function_id;
	g->state = GoroutineState::Runnable;
	g->fiber = fiber;

	uint32_t g_id = g->id;
	{
		std::lock_guard<std::mutex> lock(goroutines_mutex_);
		goroutines_[g_id] = std::move(g);
	}
	{
		std::lock_guard<std::mutex> lock(priority_mutex_);
		// Hotkey fibers are prepended for immediate execution
		if (priority == FiberPriority::HOTKEY) {
			hotkey_queue_.push_front(goroutines_[g_id].get());
		} else if (priority == FiberPriority::BACKGROUND) {
			background_queue_.push_back(goroutines_[g_id].get());
		} else {
			runnable_queue_.push_back(goroutines_[g_id].get());
		}
	}
}

void Scheduler::requeueFront(Goroutine* g) {
  if (!g) return;
  g->state = GoroutineState::Created;
  g->suspension_reason.store(SuspensionReason::None, std::memory_order_release);
  g->hotkey_retrigger.store(false, std::memory_order_release);
    {
        std::lock_guard<std::mutex> wlock(g->wait_handle_mutex_);
        g->wait_handle.clear();
    }
    g->ip = 0;
    g->stack.clear();
    g->locals.clear();
    if (g->persistent && !g->hotkey_args.empty()) {
        g->locals = g->hotkey_args;
        for (const auto& arg : g->hotkey_args) {
            g->stack.push_back(arg);
        }
    g->function_id = g->hotkey_function_id;
    g->closure_id = g->hotkey_closure_id;
  }
  if (g->fiber) {
    g->fiber->stack.clear();
    g->fiber->call_stack.clear();
    if (g->persistent && !g->hotkey_args.empty()) {
      for (const auto& arg : g->hotkey_args) {
        g->fiber->stack.push(arg);
      }
      g->fiber->pushCall(g->hotkey_function_id,
        static_cast<uint32_t>(g->hotkey_args.size()),
        g->hotkey_chunk);
      auto& frame = g->fiber->currentFrame();
      frame.closure_id = g->hotkey_closure_id;
    }
    g->fiber->state = FiberState::CREATED;
    g->fiber->suspended_reason = ::havel::compiler::SuspensionReason::NONE;
  }
    ::havel::debug("[Scheduler] requeueFront: gid={} persistent={} fn={} closure={} priority={}",
        g->id, g->persistent, g->function_id, g->closure_id, static_cast<int>(g->priority));
    {
    std::lock_guard<std::mutex> lock(priority_mutex_);
    if (g->priority == FiberPriority::HOTKEY) {
      hotkey_queue_.push_front(g);
      ::havel::debug("[Scheduler] requeueFront: gid={} pushed to HOTKEY queue (size={})",
        g->id, hotkey_queue_.size());
    } else if (g->priority == FiberPriority::BACKGROUND) {
      background_queue_.push_front(g);
      ::havel::debug("[Scheduler] requeueFront: gid={} pushed to BACKGROUND queue (size={})",
        g->id, background_queue_.size());
    } else {
      runnable_queue_.push_front(g);
      ::havel::debug("[Scheduler] requeueFront: gid={} pushed to RUNNABLE queue (size={})",
        g->id, runnable_queue_.size());
    }
    }
}

// Wake a persistent hotkey goroutine on trigger
//
// This function implements the hotkey scheduling policy and the hotkey_retrigger flag contract.
// The flag is a cross-thread signaling mechanism:
//   - Scheduler thread: sets flag when new trigger arrives during execution
//   - VM thread: reads and clears flag at end of each hotkey body iteration
//
// See Goroutine::hotkey_retrigger documentation for full contract details.
//
// @param g Persistent goroutine to wake
// @param newArgs Optional new arguments for the trigger
// @return true if successfully queued (g->persistent || idle state), false if dropped
bool Scheduler::wakeHotkey(Goroutine* g, const std::vector<Value>& newArgs) {
  if (!g) return false;

  bool isPending = (g->state == GoroutineState::Runnable ||
                    g->state == GoroutineState::Created ||
                    g->state == GoroutineState::Running);

  ::havel::debug("[Scheduler] wakeHotkey: gid={} state={} policy={} isPending={}",
                 g->id, static_cast<int>(g->state.load()), static_cast<int>(g->hotkey_policy), isPending);

  switch (g->hotkey_policy) {
  case HotkeyPolicy::Drop:
    if (isPending) return g->persistent;
    break;
  case HotkeyPolicy::Replace:
    if (isPending) {
      if (!newArgs.empty()) {
        g->hotkey_args = newArgs;
      }
      requeueFront(g);
      return true;
    }
    break;
  case HotkeyPolicy::Queue:
    if (isPending) {
      if (!newArgs.empty()) {
        g->hotkey_args = newArgs;
      }
      requeueFront(g);
      return true;
    }
    break;
  case HotkeyPolicy::Coalesce:
    // Coalesce: merge multiple triggers into one execution
    // If pending: update args if provided, set retrigger flag, and return
    // If suspended/idle: proceed to normal wake-up below
    if (isPending) {
      // Update args if new ones provided (otherwise keep existing)
      if (!newArgs.empty()) {
        g->hotkey_args = newArgs;
        g->locals = newArgs;
        if (g->fiber) {
          g->fiber->stack.clear();
          for (const auto& arg : newArgs) {
            g->fiber->stack.push(arg);
          }
        }
      }
      g->hotkey_retrigger.store(true, std::memory_order_release);
      return true;
    }
    break;
  }

  if (!newArgs.empty()) {
    g->hotkey_args = newArgs;
  }

  // NOTE: State may change between this check and requeueFront() due to concurrent VM thread.
  // This is safe by design: the hotkey_retrigger flag ensures re-execution happens.
  // If state changes from Suspended→Running before requeueFront(), the flag will be checked
  // by the VM and the goroutine will retrigger at the appropriate point.
  if (g->state == GoroutineState::Suspended) {
    if (g->suspension_reason.load(std::memory_order_acquire) == SuspensionReason::HotkeyWait) {
      // Idle/parked goroutine waiting for next trigger — wake it up
      requeueFront(g);
    } else {
      // Mid-sleep or other suspension — set retrigger flag so the
      // goroutine re-runs after its current suspension resolves
      g->hotkey_retrigger.store(true, std::memory_order_release);
    }
    return true;
  }

  requeueFront(g);
  return true;
}

bool Scheduler::wakeHotkeyByAlias(const std::string& alias) {
    std::vector<Goroutine*> toWake;
    {
        std::lock_guard<std::mutex> lock(goroutines_mutex_);
        for (auto& [id, g] : goroutines_) {
            if (g && g->persistent && g->hotkey_alias == alias) {
                toWake.push_back(g.get());
            }
        }
    }
    ::havel::debug("[Scheduler] wakeHotkeyByAlias('{}'): found {} persistent goroutines", alias, toWake.size());
    bool found = false;
    for (auto* g : toWake) {
        if (wakeHotkey(g)) found = true;
    }
    return found;
}

bool Scheduler::hasRunnableFibers() const {
  std::lock_guard<std::mutex> lock(priority_mutex_);
  size_t hk = hotkey_queue_.size(), rn = runnable_queue_.size(), bg = background_queue_.size();
  if (debugging::debug_io)
    ::havel::debug("[Scheduler] hasRunnableFibers: hotkey={} runnable={} bg={}", hk, rn, bg);
  for (auto* g : hotkey_queue_) {
    if (g && (g->state == GoroutineState::Runnable || g->state == GoroutineState::Created))
      return true;
  }
  for (auto* g : runnable_queue_) {
    if (g && (g->state == GoroutineState::Runnable || g->state == GoroutineState::Created))
      return true;
  }
  for (auto* g : background_queue_) {
    if (g && (g->state == GoroutineState::Runnable || g->state == GoroutineState::Created))
      return true;
  }
  return false;
}

size_t Scheduler::wakeSleepingGoroutines() {
    auto now = std::chrono::steady_clock::now();
    size_t woken = 0;

    std::lock_guard<std::mutex> lock(goroutines_mutex_);
    for (auto& [id, g] : goroutines_) {
        if (g->state != GoroutineState::Suspended) continue;
        if (g->suspension_reason.load(std::memory_order_acquire) != SuspensionReason::SleepWait) continue;
        {
            std::lock_guard<std::mutex> wlock(g->wait_handle_mutex_);
            if (g->wait_handle.type != AwaitableType::SLEEP) continue;
            if (g->wait_handle.deadline == std::chrono::steady_clock::time_point{}) continue;
            if (now < g->wait_handle.deadline) continue;
        }

        g->state = GoroutineState::Runnable;
        g->suspension_reason.store(SuspensionReason::None, std::memory_order_release);
        {
            std::lock_guard<std::mutex> wlock(g->wait_handle_mutex_);
            g->wait_handle.clear();  // Clear stale sleep deadline and context
        }
        woken++;

        std::lock_guard<std::mutex> plock(priority_mutex_);
        if (g->priority == FiberPriority::HOTKEY) {
            hotkey_queue_.push_back(g.get());
        } else if (g->priority == FiberPriority::BACKGROUND) {
            background_queue_.push_back(g.get());
        } else {
            runnable_queue_.push_back(g.get());
        }
    }

    return woken;
}

 void Scheduler::schedule(DeferredAction fn, FiberPriority priority) {
   {
     std::lock_guard<std::mutex> lock(deferred_mutex_);
     switch (priority) {
       case FiberPriority::HOTKEY:
         deferred_hotkey_.push_back(std::move(fn));
         break;
       case FiberPriority::BACKGROUND:
         deferred_background_.push_back(std::move(fn));
         break;
       default:
         deferred_normal_.push_back(std::move(fn));
         break;
     }
   }
 #ifndef _WIN32
   if (deferred_wakeup_fd_ >= 0) {
     uint64_t val = 1;
     ssize_t ret = write(deferred_wakeup_fd_, &val, sizeof(val));
     (void)ret;
   }
 #endif
 }

 void Scheduler::deferToVM(DeferredAction fn) {
   schedule(std::move(fn), FiberPriority::NORMAL);
 }

 size_t Scheduler::drainDeferredCallbacks(FiberPriority upTo) {
   if (vm_thread_id_ == std::thread::id()) {
     vm_thread_id_ = std::this_thread::get_id();
   }

 #ifndef _WIN32
   if (deferred_wakeup_fd_ >= 0) {
     uint64_t val;
     while (read(deferred_wakeup_fd_, &val, sizeof(val)) == sizeof(val)) {}
   }
 #endif

   size_t drained = 0;

   auto drainOneQueue = [&](std::deque<DeferredAction>& queue) {
     std::deque<DeferredAction> acts;
     {
       std::lock_guard<std::mutex> lock(deferred_mutex_);
       acts = std::move(queue);
     }
     for (auto& fn : acts) {
       try {
         fn();
         ++drained;
       } catch (const std::exception& e) {
         ::havel::warn("Scheduler: deferred action threw: {}", e.what());
       }
     }
   };

   if (static_cast<int>(upTo) >= static_cast<int>(FiberPriority::HOTKEY))
     drainOneQueue(deferred_hotkey_);
   if (static_cast<int>(upTo) >= static_cast<int>(FiberPriority::NORMAL))
     drainOneQueue(deferred_normal_);
   if (static_cast<int>(upTo) >= static_cast<int>(FiberPriority::BACKGROUND))
     drainOneQueue(deferred_background_);

   return drained;
 }

size_t Scheduler::cleanupDoneGoroutines() {
  std::lock_guard<std::mutex> lock(goroutines_mutex_);
  size_t removed = 0;
  
  // Find all Done goroutines and remove them
  for (auto it = goroutines_.begin(); it != goroutines_.end(); ) {
    auto& [id, g] = *it;
    if (g && g->state == GoroutineState::Done) {
      if (debugging::debug_io) {
        ::havel::debug("[Scheduler] Cleanup: removing Done goroutine gid={} name='{}'", 
                      g->id, g->name);
      }
      // unique_ptr will auto-delete the Goroutine and its Fiber
      it = goroutines_.erase(it);
      removed++;
    } else {
      ++it;
    }
  }
  
  return removed;
}

std::vector<uint64_t> Scheduler::collectUpdateCallbackIds() {
  std::lock_guard<std::mutex> lock(goroutines_mutex_);
  std::vector<uint64_t> ids;
  for (auto& [id, g] : goroutines_) {
    if (g && g->update_callback_id != 0) {
      ids.push_back(g->update_callback_id);
      g->update_callback_id = 0;
    }
  }
  return ids;
}

Scheduler::Goroutine* Scheduler::getHotkeyByAlias(const std::string& alias) {
    std::lock_guard<std::mutex> lock(goroutines_mutex_);
    for (auto& [id, g] : goroutines_) {
        if (g && g->persistent && g->hotkey_alias == alias) {
            return g.get();
        }
    }
    return nullptr;
}

void Scheduler::setHotkeyPolicy(Goroutine* g, HotkeyPolicy policy) {
    if (!g) return;
    g->hotkey_policy = policy;
}

HotkeyPolicy Scheduler::getHotkeyPolicy(Goroutine* g) const {
  if (!g) return HotkeyPolicy::Drop;
  return g->hotkey_policy;
}

size_t Scheduler::hotkeyCount() const {
  std::lock_guard<std::mutex> lock(goroutines_mutex_);
  size_t count = 0;
  for (const auto& [id, g] : goroutines_) {
    if (g && g->persistent) count++;
  }
  return count;
}

size_t Scheduler::activeHotkeyCount() const {
  std::lock_guard<std::mutex> lock(goroutines_mutex_);
  size_t count = 0;
  for (const auto& [id, g] : goroutines_) {
    if (g && g->persistent &&
        (g->state == GoroutineState::Running ||
         g->state == GoroutineState::Runnable ||
         g->state == GoroutineState::Created)) {
      count++;
    }
  }
  return count;
}

size_t Scheduler::suspendedHotkeyCount() const {
  std::lock_guard<std::mutex> lock(goroutines_mutex_);
  size_t count = 0;
  for (const auto& [id, g] : goroutines_) {
    if (g && g->persistent && g->state == GoroutineState::Suspended) {
      count++;
    }
  }
  return count;
}

std::vector<std::string> Scheduler::getHotkeyAliases() const {
  std::lock_guard<std::mutex> lock(goroutines_mutex_);
  std::vector<std::string> result;
  for (const auto& [id, g] : goroutines_) {
    if (g && g->persistent && !g->hotkey_alias.empty()) {
      result.push_back(g->hotkey_alias);
    }
  }
  return result;
}

} // namespace havel::compiler