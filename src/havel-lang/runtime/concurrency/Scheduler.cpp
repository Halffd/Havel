#include "Scheduler.hpp"
#include "Fiber.hpp"
#include "../../../utils/Logger.hpp"
#include "utils/DebugFlags.hpp"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>
#ifndef _WIN32
#include <sys/eventfd.h>
#include <unistd.h>
#include <cstring>
#endif

namespace havel::compiler {

static Scheduler* g_scheduler_instance = nullptr;
static std::once_flag g_scheduler_once;

static thread_local bool g_in_conditional_hotkey_eval = false;

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

    // Lock in consistent order: priority_mutex_ first, then goroutines_mutex_
    {
        std::lock_guard plock(priority_mutex_);
        std::lock_guard glock(goroutines_mutex_);
        goroutines_[g_id] = std::move(g);
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
	auto it = goroutines_.find(id);
	if (it != goroutines_.end()) {
		return it->second.get();
	}
	return nullptr;
}

Scheduler::Goroutine* Scheduler::pickNext() {
  Goroutine* result = nullptr;

  static const bool trace_cycle = std::getenv("HAVEL_TRACE_CYCLE");

{

    if (debugging::debug_io) {
      ::havel::debug("[Scheduler] pickNext: hotkey_queue={} runnable_queue={} bg_queue={}",
        hotkey_queue_.size(), runnable_queue_.size(), background_queue_.size());
    }
    if (trace_cycle) {
      fprintf(stderr, "[CYCLE] pickNext: queues hk=%zu run=%zu bg=%zu suspended=%zu\n",
              hotkey_queue_.size(), runnable_queue_.size(), background_queue_.size(), suspendedCount());
    }

    // Priority order: hotkey → normal → background
    // Each queue: rotate non-runnable entries to back instead of discarding.
    // This prevents goroutines from being lost if the state changes between
    // enqueue and pickNext (e.g. suspend() while still in queue).
    auto popRunnable = [&](std::deque<Goroutine*>& q, const char* label) -> Goroutine* {
      size_t limit = q.size();
      for (size_t i = 0; i < limit && !q.empty(); i++) {
        auto* g = q.front();
        q.pop_front();
        if (!g) continue;
        // UAF defense: cleanupDoneGoroutines purges queue entries before
        // erasing the Goroutine. If we ever pop a Done goroutine here, a
        // purge was skipped — log loudly with timestamp so we can match
        // it against [ERASE] entries to confirm queue poisoning.
        if (g->state == GoroutineState::Done) {
          auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
              std::chrono::steady_clock::now().time_since_epoch()).count();
          ::havel::warning("[Scheduler] [POP-DONE] gid={} t={} from {} queue (queue poisoning suspected)",
                           g->id, now_ns, label);
          continue;
        }
        if (g->fiber && g->fiber->state == FiberState::DONE) {
          g->state = GoroutineState::Done;
          continue;
        }
        if (g->state == GoroutineState::Runnable || g->state == GoroutineState::Created) {
          if (debugging::debug_io) ::havel::debug("[Scheduler] pickNext: selected {} gid={} state={}",
            label, g->id, static_cast<int>(g->state.load()));
          return g;
        }
        // Non-runnable but not garbage: rotate to back
        q.push_back(g);
      }
      return nullptr;
    };

    {
      std::lock_guard lock(priority_mutex_);
      result = popRunnable(hotkey_queue_, "HOTKEY");
      if (!result) result = popRunnable(runnable_queue_, "RUNNABLE");
      if (!result) result = popRunnable(background_queue_, "BACKGROUND");
    }
	}

	if (result) {
		if (result->state == GoroutineState::Done) {
			// Raced with markGoroutineDone — skip. popRunnable already
			// filters Done entries (lines 145-150), but the state can
			// flip between its check and here.
			result = nullptr;
		} else {
			{
				std::lock_guard glock(goroutines_mutex_);
				assert(goroutines_.count(result->id) == 1);
			}
			result->wait_handle.clear();
			current_.store(result, std::memory_order_release);
			if (debugging::debug_io) ::havel::debug("[Scheduler] [RUN] gid={} name='{}' state={}",
						  result->id, result->name, (int)result->state.load());
			if (trace_cycle) {
			  fprintf(stderr, "[CYCLE] pickNext -> gid=%u state=%d\n",
					  result->id, (int)result->state.load());
			}
		}
	}

	if (!result) {
		// No runnable goroutines found. Periodic cleanup while idle.
		size_t removed = cleanupDoneGoroutines();
		if (trace_cycle) {
		  fprintf(stderr, "[CYCLE] pickNext -> nullptr (cleanup removed=%zu)\n", removed);
		}
	}

	return result;
}

void Scheduler::suspend(Scheduler::Goroutine* g, SuspensionReason reason) {
  if (!g) return;

  if (g->state == GoroutineState::Done) return;

  g->state = GoroutineState::Suspended;
  g->suspension_reason.store(reason, std::memory_order_release);
  ::havel::debug("[Scheduler] [YIELD] gid={} name='{}' reason={}", 
                g->id, g->name, (int)reason);

  // Remove from queues immediately since it's no longer runnable
  removeFromQueues(g);
}

void Scheduler::unpark(Scheduler::Goroutine* g) {
	if (!g) return;

	if (g->state != GoroutineState::Suspended) {
		return;
	}

	g->state = GoroutineState::Runnable;
	g->suspension_reason.store(SuspensionReason::None, std::memory_order_release);
	{
		g->wait_handle.clear();  // Clear stale suspension context
	}

	{
		std::lock_guard lock(priority_mutex_);
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
    std::lock_guard lock(goroutines_mutex_);
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
    std::lock_guard lock(goroutines_mutex_);
    for (auto& [id, g] : goroutines_) {
        if (g->fiber == fiber) {
            return g.get();
        }
    }
    return nullptr;
}

void Scheduler::forEachConditionalHotkey(std::function<void(Goroutine*)> fn) {
    if (g_in_conditional_hotkey_eval) {
        return;
    }
    g_in_conditional_hotkey_eval = true;
    // RAII reset: any exception (or early return path inside fn) MUST release
    // the flag, otherwise wakeSleepingGoroutines() early-returns forever
    // (line in wakeSleepingGoroutines checking g_in_conditional_hotkey_eval)
    // and sleeping goroutines are never promoted back to Runnable. This
    // produces the "ready-but-unpromoted sleeper" stall where the scheduler
    // spins pickNext->nullptr->cleanup->0 between event ticks while a
    // sleeper's deadline has already passed.
    auto reset_flag = [](bool* flag) { *flag = false; };
    std::unique_ptr<bool, decltype(reset_flag)> guard(&g_in_conditional_hotkey_eval, reset_flag);

    std::vector<Goroutine*> candidates;
    {
        std::lock_guard lock(goroutines_mutex_);
        for (auto& [id, g] : goroutines_) {
            if (g && g->hotkey_condition_callback_id != 0) {
                candidates.push_back(g.get());
            }
        }
    }
    for (auto* g : candidates) {
        fn(g);
    }
}

void Scheduler::forEachGoroutine(std::function<void(Goroutine*)> fn) {
    std::lock_guard lock(goroutines_mutex_);
    for (auto& [id, g] : goroutines_) {
        if (g) fn(g.get());
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
		std::lock_guard lock(goroutines_mutex_);
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
			std::lock_guard lock(goroutines_mutex_);
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
	std::lock_guard lock(goroutines_mutex_);
	return goroutines_.size();
}

size_t Scheduler::runnableCount() const {
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
	std::lock_guard lock(goroutines_mutex_);
	size_t count = 0;
	for (const auto& [id, g] : goroutines_) {
		if (g->state == GoroutineState::Suspended) {
			count++;
		}
	}
	return count;
}

void Scheduler::attachFiber(uint32_t goroutine_id, Fiber* fiber) {
	std::lock_guard lock(goroutines_mutex_);
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

	{
		std::lock_guard lock(priority_mutex_);
		if (g->priority == FiberPriority::HOTKEY) {
			hotkey_queue_.push_back(g);
		} else if (g->priority == FiberPriority::BACKGROUND) {
			background_queue_.push_back(g);
		} else {
			runnable_queue_.push_back(g);
		}
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
		std::lock_guard lock(priority_mutex_);
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
		std::lock_guard lock(goroutines_mutex_);
		goroutines_[g_id] = std::move(g);
	}
	{
		// Hotkey fibers are prepended for immediate execution
		std::lock_guard lock(priority_mutex_);
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
  // Defense in depth: a Done goroutine must not be resurrected. Persistent
  // hotkey goroutines cycle Ready->Running->Suspended(HotkeyWait) and never
  // reach Done; a Done goroutine here means a non-persistent goroutine whose
  // fiber has exited. Re-arming it would push a stale pointer into the run
  // queues. The primary guard is in wakeHotkey(); this is a backstop.
  if (g->state == GoroutineState::Done) {
    ::havel::warn("[Scheduler] requeueFront: gid={} state=Done -> refusing to requeue", g->id);
    return;
  }
  g->state = GoroutineState::Created;
  g->suspension_reason.store(SuspensionReason::None, std::memory_order_release);
  g->hotkey_retrigger.store(false, std::memory_order_release);
    {
        g->wait_handle.clear();
    }
    g->ip = 0;
    g->stack.clear();
    g->locals.clear();
    g->stack.reserve(64);
    if (g->persistent && !g->hotkey_args.empty()) {
        g->locals.reserve(g->hotkey_args.size());
        g->locals = g->hotkey_args;
        g->stack.reserve(g->hotkey_args.size());
        for (const auto& arg : g->hotkey_args) {
            g->stack.push_back(arg);
        }
    g->function_id = g->hotkey_function_id;
    g->closure_id = g->hotkey_closure_id;
  }
  if (g->fiber) {
    g->fiber->stack.clear();
    g->fiber->call_stack.clear();
    g->fiber->stack.reserve(64);
    g->fiber->call_stack.reserve(4);
    if (g->persistent && !g->hotkey_args.empty()) {
      g->fiber->stack.reserve(g->hotkey_args.size());
      for (const auto& arg : g->hotkey_args) {
        g->fiber->stack.push(arg);
      }
      g->fiber->pushCall(g->hotkey_function_id,
        static_cast<uint32_t>(g->hotkey_args.size()),
        g->hotkey_chunk.get());
      auto& frame = g->fiber->currentFrame();
      frame.closure_id = g->hotkey_closure_id;
    }
    g->fiber->state = FiberState::CREATED;
    g->fiber->suspended_reason = ::havel::compiler::SuspensionReason::NONE;
  }
    ::havel::debug("[Scheduler] requeueFront: gid={} persistent={} fn={} closure={} priority={}",
        g->id, g->persistent, g->function_id, g->closure_id, static_cast<int>(g->priority));
{
    std::lock_guard lock(priority_mutex_);
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

  // Done goroutines must never be re-enqueued. Persistent hotkey goroutines
  // never reach Done (handleReturned re-suspends them as HotkeyWait), so a
  // Done state here means a non-persistent goroutine that finished and is
  // awaiting cleanupDoneGoroutines(). Re-enqueuing it would poison the run
  // queues and trip the UAF assertion in pickNext(). See Scheduler.cpp:173.
  if (g->state == GoroutineState::Done) {
    ::havel::debug("[Scheduler] wakeHotkey: gid={} state=Done -> ignoring trigger (awaiting cleanup)", g->id);
    return false;
  }

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
        std::lock_guard lock(goroutines_mutex_);
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

bool Scheduler::removeHotkeyByAlias(const std::string& alias) {
    std::lock_guard lock(goroutines_mutex_);
    for (auto it = goroutines_.begin(); it != goroutines_.end(); ++it) {
        auto* g = it->second.get();
        if (g && g->persistent && g->hotkey_alias == alias) {
            // Mark as Done so it won't be picked up by the scheduler
            g->state = GoroutineState::Done;
            if (g->fiber) {
                g->fiber->state = FiberState::DONE;
            }
            // Remove from goroutines map
            goroutines_.erase(it);
            ::havel::debug("[Scheduler] removeHotkeyByAlias: removed persistent goroutine gid={} alias='{}'", g->id, alias);
            return true;
        }
    }
    return false;
}

bool Scheduler::hasRunnableFibers() const {
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
    static const bool trace_cycle = std::getenv("HAVEL_TRACE_CYCLE");
    // Skip if called from inside conditional hotkey evaluation to prevent
    // re-entrant lock on goroutines_mutex_ (which is held by forEachConditionalHotkey
    // while collecting candidates, then released before calling the callback).
    // If the callback yields, processGoroutinesInline() may call us.
    if (g_in_conditional_hotkey_eval) {
        if (trace_cycle) {
            fprintf(stderr, "[CYCLE] wakeSleepingGoroutines: SKIP (g_in_conditional_hotkey_eval=true)\n");
        }
        return 0;
    }

    auto now = std::chrono::steady_clock::now();
    size_t woken = 0;

    // Collect woken goroutines first under goroutines_mutex_,
    // then enqueue under priority_mutex_ separately to avoid
    // lock ordering violation (priority_mutex_ must not be
    // acquired while holding goroutines_mutex_).
    std::vector<Goroutine*> toWake;
    {
        std::lock_guard lock(goroutines_mutex_);
        for (auto& [id, g] : goroutines_) {
            assert(g != nullptr);
            // Skip goroutines that are already done
            auto state = g->state.load(std::memory_order_acquire);
            if (state == GoroutineState::Done) continue;
            if (state != GoroutineState::Suspended) {
                if (trace_cycle) {
                    fprintf(stderr, "[CYCLE]   gid=%u skip state=%d (not Suspended)\n",
                            id, static_cast<int>(state));
                }
                continue;
            }
            auto sr = g->suspension_reason.load(std::memory_order_acquire);
            if (sr != SuspensionReason::SleepWait) {
                if (trace_cycle) {
                    fprintf(stderr, "[CYCLE]   gid=%u skip reason=%d (not SleepWait)\n",
                            id, static_cast<int>(sr));
                }
                continue;
            }
            {
                std::lock_guard wlock(g->wait_handle_mutex_);
                if (g->wait_handle.type != AwaitableType::SLEEP) {
                    if (trace_cycle) {
                        fprintf(stderr, "[CYCLE]   gid=%u skip wait_handle.type=%d (not SLEEP)\n",
                                id, static_cast<int>(g->wait_handle.type));
                    }
                    continue;
                }
                if (g->wait_handle.deadline == std::chrono::steady_clock::time_point{}) {
                    if (trace_cycle) {
                        fprintf(stderr, "[CYCLE]   gid=%u skip deadline=zero\n", id);
                    }
                    continue;
                }
                // Add 1ms tolerance: when remaining is <1ms the sleep loop
                // in processGoroutines would sleep_for(0ms) (busy-wait), so
                // waking slightly early is better than spinning.
                if (now + std::chrono::milliseconds(1) < g->wait_handle.deadline) {
                    if (trace_cycle) {
                        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   g->wait_handle.deadline - now).count();
                        fprintf(stderr, "[CYCLE]   gid=%u skip deadline in %ldms\n", id, ms);
                    }
                    continue;
                }
            }

            // Atomically transition from Suspended to Runnable only if not Done
            auto expected = GoroutineState::Suspended;
            if (!g->state.compare_exchange_strong(expected, GoroutineState::Runnable,
                std::memory_order_acq_rel, std::memory_order_acquire)) {
                // State changed (likely to Done), skip this goroutine
                continue;
            }

            g->suspension_reason.store(SuspensionReason::None, std::memory_order_release);
            {
                std::lock_guard wlock(g->wait_handle_mutex_);
                g->wait_handle.clear();
            }
            toWake.push_back(g.get());
            woken++;
        }
    }

    if (!toWake.empty()) {
        std::lock_guard plock(priority_mutex_);
        for (auto* g : toWake) {
            // Set resume value for sleep (returns null)
            g->wait_handle.resume_value = Value::makeNull();
            if (g->priority == FiberPriority::HOTKEY) {
                hotkey_queue_.push_back(g);
            } else if (g->priority == FiberPriority::BACKGROUND) {
                background_queue_.push_back(g);
            } else {
                runnable_queue_.push_back(g);
            }
        }
    }

    if (trace_cycle) {
        auto next = nextSleepDeadline();
        long ms = -1;
        if (next) {
            ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                   *next - std::chrono::steady_clock::now()).count();
        }
        fprintf(stderr, "[CYCLE] wakeSleepingGoroutines: woken=%zu nextDeadlineMs=%ld\n",
                woken, ms);
    }
    return woken;
}

std::optional<std::chrono::steady_clock::time_point> Scheduler::nextSleepDeadline() const {
    std::optional<std::chrono::steady_clock::time_point> earliest;
    std::lock_guard lock(goroutines_mutex_);
    for (const auto& [id, g] : goroutines_) {
        if (g->state != GoroutineState::Suspended) continue;
        if (g->suspension_reason.load(std::memory_order_acquire) != SuspensionReason::SleepWait) continue;
        if (g->wait_handle.type != AwaitableType::SLEEP) continue;
        auto d = g->wait_handle.deadline;
        if (d == std::chrono::steady_clock::time_point{}) continue;
        if (!earliest || d < *earliest) {
            earliest = d;
        }
    }
    return earliest;
}

 void Scheduler::schedule(DeferredAction fn, FiberPriority priority) {
   {
     std::lock_guard lock(deferred_mutex_);
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

    std::deque<DeferredAction> hotkey_acts;
    std::deque<DeferredAction> normal_acts;
    std::deque<DeferredAction> background_acts;
    {
      std::lock_guard lock(deferred_mutex_);
      if (static_cast<int>(upTo) >= static_cast<int>(FiberPriority::HOTKEY))
        hotkey_acts = std::move(deferred_hotkey_);
      if (static_cast<int>(upTo) >= static_cast<int>(FiberPriority::NORMAL))
        normal_acts = std::move(deferred_normal_);
      if (static_cast<int>(upTo) >= static_cast<int>(FiberPriority::BACKGROUND))
        background_acts = std::move(deferred_background_);
    }

    auto drainOneQueue = [&](std::deque<DeferredAction>& acts) {
     for (auto& fn : acts) {
       try {
         fn();
       } catch (const std::exception& e) {
         ::havel::warn("Scheduler: deferred action threw: {}", e.what());
       }
       // Count every callback that was dequeued and attempted, regardless of
       // whether it threw. Callers (e.g. ExecutionEngine's event loop) use this
       // count to detect "any work happened"; counting only successes would
       // hide runaway exceptions as "idle" and starve the scheduler.
       ++drained;
     }
   };

   drainOneQueue(hotkey_acts);
   drainOneQueue(normal_acts);
   drainOneQueue(background_acts);

   return drained;
 }

size_t Scheduler::cleanupDoneGoroutines() {
  // Three-phase cleanup, each phase holding a single lock in declared order
  // (priority_mutex_ BEFORE goroutines_mutex_) to avoid lock-order inversion.
  // Phase 1: collect Done goroutines under goroutines_mutex_ only.
  // Phase 2: remove from queues under priority_mutex_ only.
  // Phase 3: erase from map under goroutines_mutex_ only.
  auto t_start = std::chrono::steady_clock::now();
  auto t_start_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
      t_start.time_since_epoch()).count();
  std::thread::id caller = std::this_thread::get_id();

  // Phase 1: collect Done goroutines under goroutines_mutex_ only
  std::vector<Goroutine*> done_list;
  {
    std::lock_guard glock(goroutines_mutex_);
    for (auto& [id, g] : goroutines_) {
      if (g && g->state == GoroutineState::Done) {
        done_list.push_back(g.get());
      }
    }
  }

  // Phase 2: remove from queues under priority_mutex_ only
  {
    std::lock_guard plock(priority_mutex_);
    for (auto* g : done_list) {
      removeFromQueues(g);
    }
  }

  // Phase 3: erase from map under goroutines_mutex_ only
  size_t removed = 0;
  {
    std::lock_guard glock(goroutines_mutex_);
    for (auto it = goroutines_.begin(); it != goroutines_.end();) {
      if (it->second && it->second->state == GoroutineState::Done) {
        it = goroutines_.erase(it);
        ++removed;
      } else {
        ++it;
      }
    }
  }

  // Diagnostic logging gated by HAVEL_TRACE_CLEANUP. Bypasses the spdlog
  // debug-level filter so traces are visible even at INFO log level.
  if (std::getenv("HAVEL_TRACE_CLEANUP")) {
    auto t_end = std::chrono::steady_clock::now();
    auto dur_us = std::chrono::duration_cast<std::chrono::microseconds>(
        t_end - t_start).count();
    fprintf(stderr, "[Scheduler] [CLEANUP] removed=%zu remaining=%zu dur=%lldus caller_tid=0x%llx t=%lld\n",
            removed, goroutines_.size(), (long long)dur_us,
            (unsigned long long)*reinterpret_cast<uint64_t*>(&caller),
            (long long)t_start_ns);
  }
  return removed;
}

void Scheduler::removeFromQueues(Goroutine* g) {
  // Caller must hold priority_mutex_.
  if (!g) return;
  assert(priority_mutex_owned() && "removeFromQueues requires priority_mutex_");
  auto purge = [&](std::deque<Goroutine*>& q) {
    q.erase(std::remove(q.begin(), q.end(), g), q.end());
  };
  purge(hotkey_queue_);
  purge(runnable_queue_);
  purge(background_queue_);
}

bool Scheduler::priority_mutex_owned() const {
#if defined(NDEBUG)
  return true; // Can't check in release
#else
  // In debug, we can't easily check mutex ownership without lockdep.
  // But we document the requirement.
  return true;
#endif
}

void Scheduler::markGoroutineDone(Goroutine* g) {
  if (!g) return;
  // Set state first so any concurrent pickNext() popRunnable sees Done and
  // rotates past us; the subsequent removeFromQueues then reclaims the slot.
  g->state.store(GoroutineState::Done, std::memory_order_release);
  std::lock_guard lock(priority_mutex_);
  removeFromQueues(g);
}

std::vector<uint64_t> Scheduler::collectUpdateCallbackIds() {
  std::lock_guard lock(goroutines_mutex_);
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
    std::lock_guard lock(goroutines_mutex_);
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
  std::lock_guard lock(goroutines_mutex_);
  size_t count = 0;
  for (const auto& [id, g] : goroutines_) {
    if (g && g->persistent) count++;
  }
  return count;
}

size_t Scheduler::activeHotkeyCount() const {
  std::lock_guard lock(goroutines_mutex_);
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
  std::lock_guard lock(goroutines_mutex_);
  size_t count = 0;
  for (const auto& [id, g] : goroutines_) {
    if (g && g->persistent && g->state == GoroutineState::Suspended) {
      count++;
    }
  }
  return count;
}

std::vector<std::string> Scheduler::getHotkeyAliases() const {
  std::lock_guard lock(goroutines_mutex_);
  std::vector<std::string> result;
  for (const auto& [id, g] : goroutines_) {
    if (g && g->persistent && !g->hotkey_alias.empty()) {
      result.push_back(g->hotkey_alias);
    }
  }
  return result;
}

std::vector<Scheduler::GoroutineInfo> Scheduler::getGoroutineList() const {
  std::lock_guard lock(goroutines_mutex_);
  std::vector<GoroutineInfo> result;
  result.reserve(goroutines_.size());
  for (const auto& [id, g] : goroutines_) {
    if (!g) continue;
    GoroutineInfo info;
    info.id = g->id;
    info.name = g->name;
    info.state = goroutineStateString(g->state.load());
    info.suspension_reason = suspensionReasonString(g->suspension_reason.load());
    info.priority = fiberPriorityString(g->priority);
    {
      info.wait_type = awaitableTypeString(g->wait_handle.type);
      info.wait_target_id = g->wait_handle.target_id;
    }
    info.persistent = g->persistent;
    info.hotkey_alias = g->hotkey_alias;
    info.hotkey_policy = hotkeyPolicyString(g->hotkey_policy);
    info.parent_id = g->parent_id;
    info.instructions_executed = g->instructions_executed;
    info.ip = g->ip;
    info.has_fiber = (g->fiber != nullptr);
    if (g->fiber) {
      info.fiber_id = g->fiber->id;
      info.fiber_state = g->fiber->stateString();
      info.fiber_suspension_reason = g->fiber->suspensionReasonString();
      info.fiber_call_stack_depth = g->fiber->call_stack.size();
      info.fiber_stack_size = g->fiber->stack.size();
      info.fiber_had_error = g->fiber->had_error;
      info.fiber_error_message = g->fiber->error_message;
    }
    result.push_back(std::move(info));
  }
  return result;
}

Scheduler::SchedulerSummary Scheduler::getSchedulerSummary() const {
  SchedulerSummary s;
  s.running = running_.load();
  s.goroutine_count = goroutineCount();
  s.runnable_count = runnableCount();
  s.suspended_count = suspendedCount();
  s.hotkey_count = hotkeyCount();
  s.active_hotkey_count = activeHotkeyCount();
  s.suspended_hotkey_count = suspendedHotkeyCount();
  s.default_tick_instructions = default_tick_instructions_;
  s.hotkey_tick_instructions = hotkey_tick_instructions_;
  {
    auto* cur = current_.load();
    s.current_goroutine_id = cur ? cur->id : 0;
  }
  {
    std::lock_guard lock(goroutines_mutex_);
    for (const auto& [id, g] : goroutines_) {
      if (!g) continue;
      auto st = g->state.load();
      if (st == GoroutineState::Done) s.done_count++;
      else if (st == GoroutineState::Created) s.created_count++;
      else if (st == GoroutineState::Running) s.running_count++;
    }
  }
{
    s.hotkey_queue_size = hotkey_queue_.size();
    s.normal_queue_size = runnable_queue_.size();
    s.background_queue_size = background_queue_.size();
  }
  {
    s.deferred_hotkey_count = deferred_hotkey_.size();
    s.deferred_normal_count = deferred_normal_.size();
    s.deferred_background_count = deferred_background_.size();
  }
  return s;
}

Scheduler::GoroutineInfo Scheduler::getGoroutineInfoById(uint32_t id) const {
  std::lock_guard lock(goroutines_mutex_);
  auto it = goroutines_.find(id);
  if (it == goroutines_.end() || !it->second) {
    return GoroutineInfo{};
  }
  auto* g = it->second.get();
  GoroutineInfo info;
  info.id = g->id;
  info.name = g->name;
  info.state = goroutineStateString(g->state.load());
  info.suspension_reason = suspensionReasonString(g->suspension_reason.load());
  info.priority = fiberPriorityString(g->priority);
  {
    info.wait_type = awaitableTypeString(g->wait_handle.type);
    info.wait_target_id = g->wait_handle.target_id;
  }
  info.persistent = g->persistent;
  info.hotkey_alias = g->hotkey_alias;
  info.hotkey_policy = hotkeyPolicyString(g->hotkey_policy);
  info.parent_id = g->parent_id;
  info.instructions_executed = g->instructions_executed;
  info.ip = g->ip;
  info.has_fiber = (g->fiber != nullptr);
  if (g->fiber) {
    info.fiber_id = g->fiber->id;
    info.fiber_state = g->fiber->stateString();
    info.fiber_suspension_reason = g->fiber->suspensionReasonString();
    info.fiber_call_stack_depth = g->fiber->call_stack.size();
    info.fiber_stack_size = g->fiber->stack.size();
    info.fiber_had_error = g->fiber->had_error;
    info.fiber_error_message = g->fiber->error_message;
  }
  return info;
}

} // namespace havel::compiler