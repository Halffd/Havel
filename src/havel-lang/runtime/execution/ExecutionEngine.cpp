#include "ExecutionEngine.hpp"
#include "../../../utils/Logger.hpp"
#include "core/hotkey/HotkeyActionWrapper.hpp"
#include "../../common/Debug.hpp"
#include "../concurrency/Fiber.hpp"
#include "../concurrency/DependencyTracker.hpp"
#include <iostream>

namespace havel::compiler {

// ============================================================================
// SuspensionReason conversion: Fiber::SuspensionReason → Scheduler::SuspensionReason
// These enums have DIFFERENT ordinal values (historical divergence).
// Direct static_cast produces wrong values — must map explicitly.
// ============================================================================
static Scheduler::SuspensionReason toSchedulerReason(uint8_t fiberReason) {
  using F = SuspensionReason;
  using S = Scheduler::SuspensionReason;
  switch (static_cast<F>(fiberReason)) {
    case F::NONE:          return S::None;
    case F::YIELD:         return S::None;
    case F::CHANNEL_RECV:  return S::ChannelWait;
    case F::CHANNEL_SEND:  return S::ChannelSendWait;
    case F::THREAD_JOIN:   return S::ThreadWait;
    case F::TIMER:         return S::TimerWait;
    case F::SLEEP:         return S::SleepWait;
    case F::EXTERNAL:      return S::None;
    case F::HOTKEY_WAIT:   return S::HotkeyWait;
    case F::AWAIT:         return S::None; // AWAIT handled via WaitHandle type, not suspension_reason
    case F::COROUTINE_WAIT: return S::CoroutineWait;
    default:               return S::None;
  }
}

ExecutionEngine::ExecutionEngine(VM* vm, Scheduler* sched, EventQueue* eq)
    : vm_(vm), scheduler_(sched), event_queue_(eq), running_(true) {
  if (!vm || !sched || !eq) {
    throw std::invalid_argument("ExecutionEngine requires non-null VM, Scheduler, and EventQueue");
  }
  
  
  vm_->setEventQueue(eq);
  
  
  watcher_registry_ = std::make_unique<WatcherRegistry>();
  
  
  if (event_queue_) {
    event_queue_->onEvent(EventType::THREAD_COMPLETE, 
        [this](const Event& event) { onThreadComplete(event); });
    
    
event_queue_->onEvent(EventType::VAR_CHANGED,
[this](const Event& event) { onVariableChanged(event); });

        event_queue_->onEvent(EventType::TIMER_FIRE,
            [this](const Event& event) { onTimerFire(event); });

        event_queue_->onEvent(EventType::CHANNEL_RECV,
            [this](const Event& event) { onChannelRecv(event); });

        event_queue_->onEvent(EventType::CHANNEL_SEND,
            [this](const Event& event) { onChannelSend(event); });
    }
}

ExecutionEngine::~ExecutionEngine() {
  shutdown();
}

// ============================================================================

// ============================================================================

bool ExecutionEngine::executeFrame() {
  if (!running_) {
    ::havel::debug("[ExecutionEngine] executeFrame: running_=false, returning early");
    return false;
  }

  // Do not execute goroutines while the main thread is still running the
  // initial script (VM::execute).  Both paths touch the same VM state
  // (stack, locals, heap) so concurrent access would corrupt memory.
  if (!script_ready_.load(std::memory_order_acquire)) {
    // Still drain deferred callbacks (e.g. from IO thread) so they don't
    // accumulate, but skip goroutine scheduling/execution.
    ::havel::debug("[ExecutionEngine] executeFrame: script_ready_=false, draining deferred only");
    scheduler_->drainDeferredCallbacks();
    return false;
  }

  try {
    if (debug_mode_) {
      std::cerr << "[ExecutionEngine] Entering executeFrame\n";
    }
    // STEP 1: Process all pending events
    // Events include: thread completions, timer fires, variable changes, etc.
    // Event handlers (registered in constructor) process each event
    if (event_queue_) {
      event_queue_->processAll();
        size_t processed = event_queue_->getEventsCount();
            if (processed > 0 && debug_mode_) {
                std::cerr << "[ExecutionEngine] Processed " << processed << " events from event_queue_\n";
            }
    }
        vm_->garbageCollectionSafePoint();
        vm_->drainFinalizers();

    // STEP 1.5: Drain deferred callbacks from non-VM threads
    // These are queued via scheduler->deferToVM() and must run on the VM thread
    // (e.g. clipboard change callbacks, external event callbacks)
    size_t deferred = scheduler_->drainDeferredCallbacks();
            if (deferred > 0 && debug_mode_) {
                std::cerr << "[ExecutionEngine] Drained " << deferred << " deferred callbacks\n";
            }

	// STEP 1.6: Wake sleeping goroutines whose sleep timer has expired
	scheduler_->wakeSleepingGoroutines();

// STEP 2: Pick next runnable goroutine
    // The scheduler maintains a queue of RUNNABLE goroutines
    ::havel::debug("[ExecutionEngine] executeFrame: calling pickNext");
    Scheduler::Goroutine* g = scheduler_->pickNext();
    if (!g) {
::havel::debug("[ExecutionEngine] executeFrame: pickNext returned null (idle)");
// No runnable goroutine - idle
return false;
}
if (g->persistent || debug_mode_) {
::havel::debug("[ExecutionEngine] Picked goroutine gid={} persistent={} state={} fn={} closure={}",
g->id, g->persistent, static_cast<int>(g->state.load()), g->function_id, g->closure_id);
}

if (g->persistent && g->state == Scheduler::GoroutineState::Created
    && g->hotkey_condition_callback_id != 0) {
    auto condVal = vm_->externalRootValue(g->hotkey_condition_callback_id);
    if (condVal) {
        bool conditionMet = false;
        try {
            Value result = vm_->call(*condVal, {});
            conditionMet = vm_->toBool(result);
        } catch (...) {
            conditionMet = false;
        }
        if (!conditionMet) {
            g->state = Scheduler::GoroutineState::Suspended;
            g->suspension_reason.store(Scheduler::SuspensionReason::HotkeyWait, std::memory_order_release);
            if (g->fiber) {
                g->fiber->state = FiberState::SUSPENDED;
                g->fiber->suspended_reason = SuspensionReason::HOTKEY_WAIT;
            }
            return scheduler_->hasRunnableFibers() || scheduler_->suspendedCount() > 0;
        }
    }
}

// STEP 3: Load fiber state into VM's global execution state
// For newly-created goroutines, use startGoroutineCall which resolves
// the correct chunk (from closure if needed) and sets up the call frame.
// For resuming goroutines, use loadFiberState which restores suspended state.
if (g->state == Scheduler::GoroutineState::Created) {
// DirectCallThunk fast path: if the hotkey callback only calls
// host functions with pre-resolved args, skip VM dispatch entirely.
if (g->persistent && g->hotkey_direct_thunk) {
auto thunk = vm_->getDirectCallThunk(g->hotkey_callback_id);
if (!thunk.calls.empty()) {
vm_->executeDirectCallThunk(thunk);
if (g->fiber) {
vm_->saveFiberState(g->fiber);
}
handleReturned(g);
stats_.goroutines_completed++;
stats_.frames_executed++;
vm_->garbageCollectionSafePoint();
return scheduler_->hasRunnableFibers() || scheduler_->suspendedCount() > 0;
}
}

auto call_result = vm_->startGoroutineCall(g->function_id, g->closure_id, g->locals);
if (call_result == VM::GoroutineCallResult::Failed) {
handleReturned(g);
stats_.goroutines_completed++;
stats_.frames_executed++;
return scheduler_->hasRunnableFibers() || scheduler_->suspendedCount() > 0;
}
if (call_result == VM::GoroutineCallResult::JITExecuted) {
if (g->fiber) {
vm_->saveFiberState(g->fiber);
}
handleReturned(g);
stats_.goroutines_completed++;
stats_.frames_executed++;
vm_->garbageCollectionSafePoint();
return scheduler_->hasRunnableFibers() || scheduler_->suspendedCount() > 0;
}
g->state = Scheduler::GoroutineState::Runnable;
if (g->fiber) {
vm_->saveFiberState(g->fiber);
}
} else if (g->fiber) {
            vm_->loadFiberState(g->fiber);
            // If resuming from an await suspension, replace the placeholder null
            // on the stack with the actual resume_value from the WaitHandle
            if (g->wait_handle.type != Scheduler::AwaitableType::NONE &&
                g->wait_handle.type != Scheduler::AwaitableType::SLEEP) {
                vm_->replaceStackTop(g->wait_handle.resume_value);
                g->wait_handle.clear();
            }
        }


    // State is Running for the duration of this frame's execution.
    // Cross-thread consumers (wakeHotkey from IO thread) check
    // isPending which includes Running.
    g->state = Scheduler::GoroutineState::Running;

    // If this Fiber is a hotkey action (special marker function_id), execute the callback
    // instead of bytecode
    VMExecutionResult result;
		if (g->fiber && g->fiber->current_function_id == HotkeyActionWrapper::HOTKEY_ACTION_FUNCTION_ID) {

            if (debug_mode_) {
                std::cerr << "[ExecutionEngine] Executing hotkey action Fiber " << g->fiber->id << "\n";
            }

			auto* action = HotkeyActionWrapper::getCallback(g->fiber->id);
			if (action && *action) {
                if (debug_mode_) {
                    std::cerr << "[ExecutionEngine] Found hotkey callback for fiber " << g->fiber->id << "\n";
                }
				try {
					(*action)();
				} catch (const std::exception& e) {
                    if (debug_mode_) {
                        std::cerr << "[ExecutionEngine] Exception in hotkey action: " << e.what() << "\n";
                    }
					g->fiber->had_error = true;
					g->fiber->error_message = std::string("Hotkey action error: ") + e.what();
					result.type = VMExecutionResult::ERROR;
					result.error_message = g->fiber->error_message;
				}
        } else {
                if (debug_mode_) {
                    std::cerr << "[ExecutionEngine] No callback registered for hotkey fiber " << g->fiber->id << "\n";
                }
			}

 if (result.type != VMExecutionResult::ERROR) {
 result.type = VMExecutionResult::RETURNED;
 }
 g->fiber->state = FiberState::DONE;
    } else {
        // STEP 4: Run goroutine's instruction budget in a tight loop.
        // Each executeOneStep() runs one bytecode instruction and returns YIELD
        // (normal completion), SUSPENDED (sleep/channel/timer), RETURNED, or ERROR.
        // Previously we ran ONE instruction per executeFrame() call, which meant
        // every instruction paid the cost of saveFiberState/loadFiberState/handleYield/
        // requeue — making a 50-instruction hotkey callback take ~50 round trips
        // through the event loop. Now we loop until the goroutine needs a real
        // scheduler action or exhausts its time slice.

        // CRITICAL: Set fiber state to RUNNING before execution.
        // Fiber::suspend() requires state == RUNNING, otherwise it throws
        // "Cannot suspend non-RUNNING fiber" and the goroutine silently dies.
        // Previously this was never set, making all suspension code dead.
        if (g->fiber) {
            g->fiber->state = FiberState::RUNNING;
        }

        for (;;) {
          // Before running the next step, check if we're close to the tick
          // budget limit. If so, request the JIT to yield at its next
          // backedge check so long-running JIT functions don't monopolize
          // the scheduler.
          if (g->instructions_executed + 1 >= g->max_instructions_per_tick) {
            vm_->requestJitYield();
          }

          result = vm_->executeOneStep(g->fiber);

            stats_.instructions_executed++;
            g->instructions_executed++;

            if (result.type != VMExecutionResult::YIELD) {
                break;
            }

            if (g->instructions_executed >= g->max_instructions_per_tick) {
                g->instructions_executed = 0;
                if (debug_mode_) {
                    std::cerr << "[ExecutionEngine] Goroutine " << g->id << " time slice expired ("
                              << g->max_instructions_per_tick << " instructions), yielding\n";
                }
                break;
            }
        }
    }

// STEP 5: Save VM state back to fiber
// CRITICAL: If exit was requested during executeOneStep, the scheduler's
// stop() has already destroyed all goroutines (including g). Do NOT
// touch g->fiber — it's freed memory.
if (vm_->exit_requested_.load()) {
stats_.frames_executed++;
return false;
}
if (g->fiber) {
vm_->saveFiberState(g->fiber);
}

// STEP 6: Handle execution result
switch (result.type) {
    case VMExecutionResult::YIELD:
        // Budget expired mid-execution — yield to scheduler, will resume next tick
        handleYield(g);
        break;

  case VMExecutionResult::SUSPENDED: {
      // Fiber already suspended by VM::executeOneStep() — it called
      // fiber->suspend() which set state=SUSPENDED and saved reason/context.
      // Do NOT call fiber->suspend() again — that would throw since the
      // fiber is already SUSPENDED.
      //
      // NOTE: We read reason/context from g->fiber, NOT from vm_->isSuspensionRequested().
      // executeOneStep clears suspension_requested_ before returning (VM.cpp:575),
      // so vm_->isSuspensionRequested() is always false here — the old code was dead.
      auto fiber_reason = g->fiber ? g->fiber->suspended_reason : SuspensionReason::NONE;
      void* context = g->fiber ? g->fiber->suspension_context : nullptr;
      uint8_t reason = static_cast<uint8_t>(fiber_reason);

      scheduler_->suspend(g, toSchedulerReason(reason));

      if (fiber_reason == SuspensionReason::SLEEP) {
        int64_t ms = reinterpret_cast<intptr_t>(context);
        g->wait_handle.type = Scheduler::AwaitableType::SLEEP;
        g->wait_handle.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
      }
      if (fiber_reason == SuspensionReason::COROUTINE_WAIT) {
        uint32_t co_id = static_cast<uint32_t>(reinterpret_cast<intptr_t>(context));
        g->wait_handle.type = Scheduler::AwaitableType::COROUTINE;
        g->wait_handle.target_id = co_id;
      }
      if (fiber_reason == SuspensionReason::THREAD_JOIN) {
        uint32_t tid = static_cast<uint32_t>(reinterpret_cast<intptr_t>(context));
        g->wait_handle.type = Scheduler::AwaitableType::THREAD_JOIN;
        g->wait_handle.target_id = tid;
      }
      if (fiber_reason == SuspensionReason::CHANNEL_RECV) {
        uint32_t ch_id = static_cast<uint32_t>(reinterpret_cast<intptr_t>(context));
        g->wait_handle.type = Scheduler::AwaitableType::CHANNEL_RECV;
        g->wait_handle.target_id = ch_id;
      }
      if (fiber_reason == SuspensionReason::TIMER) {
        uint32_t timer_id = static_cast<uint32_t>(reinterpret_cast<intptr_t>(context));
        g->wait_handle.type = Scheduler::AwaitableType::TIMER_WAIT;
        g->wait_handle.target_id = timer_id;
      }

      handleSuspended(g);
      break;
    }
        
      case VMExecutionResult::RETURNED:
        // Function returned - mark goroutine DONE
        handleReturned(g);
        stats_.goroutines_completed++;
        break;
        
      case VMExecutionResult::ERROR:
        // Exception occurred
        handleError(g, result.error_message);
        break;
    }
    
	stats_.frames_executed++;
	vm_->garbageCollectionSafePoint();
	return scheduler_->hasRunnableFibers() || scheduler_->suspendedCount() > 0;
    
  } catch (const std::exception& e) {
    ::havel::error("[ExecutionEngine] Exception in executeFrame: {}", e.what());
    return false;
  }
}

bool ExecutionEngine::isDone() const {
  
  // This means all spawned goroutines have completed (are in Done state).
  if (!scheduler_) {
    return true;  // No scheduler = no goroutines = done
  }
  
  size_t runnable = scheduler_->runnableCount();
  size_t suspended = scheduler_->suspendedCount();
  
  // Done when there are no waiting or ready goroutines
  return (runnable == 0 && suspended == 0);
}

void ExecutionEngine::shutdown() {
  
  running_ = false;
  
  // Stop the scheduler which will clean up goroutines
  if (scheduler_) {
    scheduler_->stop();
  }
  
  // Clear watcher registry to prevent further condition checks
  if (watcher_registry_) {
    watcher_registry_.reset();
  }
}

// ============================================================================
// RESULT HANDLERS
// ============================================================================
void ExecutionEngine::handleYield(Scheduler::Goroutine* g) {
    if (debug_mode_) {
        std::cerr << "[ExecutionEngine] Goroutine yielded, returning to runnable queue\n";
    }
    // Fiber was RUNNING, now returning to scheduler queue — set RUNNABLE
    if (g->fiber && g->fiber->state == FiberState::RUNNING) {
        g->fiber->state = FiberState::RUNNABLE;
    }
    scheduler_->yield(g);
}

void ExecutionEngine::handleSuspended(Scheduler::Goroutine* g) {
    if (debug_mode_) {
        std::cerr << "[ExecutionEngine] Goroutine suspended, waiting for external event\n";
    }
  // Goroutine is already marked SUSPENDED by the suspension operation
  // EventQueue will unpark it when the waiting condition is met
}

void ExecutionEngine::handleReturned(Scheduler::Goroutine* g) {
  if (debug_mode_) {
    std::cerr << "[ExecutionEngine] Goroutine completed execution\n";
  }
  if (!g) return;

  // Persistent goroutines (hotkey system): re-suspend instead of Done.
  // The goroutine/fiber are recycled on next trigger via resetAndRequeuePersistent.
  if (g->persistent) {
        ::havel::debug("[ExecutionEngine] handleReturned: gid={} persistent retrigger={}", g->id, g->hotkey_retrigger.load(std::memory_order_acquire));
        if (g->hotkey_retrigger.load(std::memory_order_acquire)) {
     g->hotkey_retrigger.store(false, std::memory_order_release);
     scheduler_->requeueFront(g);
     if (scheduler_->current() == g) {
       scheduler_->clearCurrent();
     }
     return;
   }
     g->state = Scheduler::GoroutineState::Suspended;
     g->suspension_reason = Scheduler::SuspensionReason::HotkeyWait;
     if (g->fiber) {
         g->fiber->state = FiberState::SUSPENDED;
         g->fiber->suspended_reason = SuspensionReason::HOTKEY_WAIT;
     }
     if (scheduler_->current() == g) {
         scheduler_->clearCurrent();
     }
     return;
 }

 g->state = Scheduler::GoroutineState::Done;
 if (g->fiber) {
 g->fiber->state = FiberState::DONE;
 }
 if (g->fiber && g->fiber->current_function_id == HotkeyActionWrapper::HOTKEY_ACTION_FUNCTION_ID) {
 HotkeyActionWrapper::unregisterCallback(g->fiber->id);
 }
 if (scheduler_->current() == g) {
 scheduler_->clearCurrent();
 }
}

void ExecutionEngine::handleError(Scheduler::Goroutine* g, const std::string& msg) {
    if (debug_mode_) {
        std::cerr << "[ExecutionEngine] Goroutine error: " << msg << "\n";
    }
 if (g) {
 g->state = Scheduler::GoroutineState::Done;
 if (g->fiber) {
 g->fiber->state = FiberState::DONE;
 }
 if (g->fiber && g->fiber->current_function_id == HotkeyActionWrapper::HOTKEY_ACTION_FUNCTION_ID) {
 HotkeyActionWrapper::unregisterCallback(g->fiber->id);
 }
 }
 if (scheduler_->current() == g) {
 scheduler_->clearCurrent();
 }
}

// ============================================================================

// ============================================================================

void ExecutionEngine::onThreadComplete(const Event& event) {
    // Event payload: data1 = thread_id that completed
    uint32_t thread_id = event.data1;

    if (!vm_) {
        return;
    }

    if (debug_mode_) {
        std::cerr << "[ExecutionEngine] Thread " << thread_id << " completed (event-driven)\n";
    }

    // Check if a goroutine is waiting on this thread via WaitHandle
    if (scheduler_) {
        Scheduler::Goroutine* g = scheduler_->findGoroutineByWaitTarget(
            Scheduler::AwaitableType::THREAD_JOIN, thread_id);
        if (g) {
            if (debug_mode_) {
                std::cerr << "[ExecutionEngine] Unparking goroutine " << g->id
                          << " waiting on thread " << thread_id << "\n";
            }
            // Store the thread result as resume_value
            g->wait_handle.resume_value = vm_->getThreadResult(thread_id);
            scheduler_->unpark(g);
        }
    }

    // Legacy: also handle fiber-only (non-goroutine) thread wait
    Fiber* waiting_fiber = vm_->getThreadWaitingFiber(thread_id);
    if (waiting_fiber) {
        if (debug_mode_) {
            std::cerr << "[ExecutionEngine] Unparking fiber waiting on thread " << thread_id << "\n";
        }
        waiting_fiber->state = FiberState::RUNNABLE;
    }

    vm_->unregisterThreadWait(thread_id);
}

// ============================================================================

// ============================================================================

void ExecutionEngine::onVariableChanged(const Event& event) {
  if (!event.ptr || !watcher_registry_) {
    return;
  }

  auto *name_ptr = static_cast<std::string*>(event.ptr);
  const std::string var_name = *name_ptr;
  delete name_ptr;
  
    if (debug_mode_) {
        std::cerr << "[ExecutionEngine] Variable '" << var_name << "' changed, checking "
                  << watcher_registry_->getWatcherCount() << " watchers\n";
    }
  
  
  // Returns list of fibers whose watchers fired (false→true edge)
  std::vector<Fiber*> fired_fibers = watcher_registry_->onVariableChanged(
      var_name,
      [this](uint32_t watcher_id) -> bool {
        
        return evaluateCondition(watcher_id);
      }
  );
  
  // Resume all fibers whose watchers fired
  for (Fiber* fiber : fired_fibers) {
    if (fiber && scheduler_) {
            if (debug_mode_) {
                std::cerr << "[ExecutionEngine] Resuming fiber for fired watcher\n";
            }
      fiber->state = FiberState::RUNNABLE;
    }
  }

  if (vm_) {
      vm_->processSignalBindings(var_name);
  }
}

bool ExecutionEngine::evaluateCondition(uint32_t watcher_id) {
  
  // 
  // Called when a watched variable changes to check if condition now fires
  // 
  // Process:
  // 1. Get watcher's condition bytecode reference
  // 2. Create DependencyTrackerScope to track accessed variables
  // 3. Execute condition bytecode
  // 4. Return result
  
  if (!vm_ || !watcher_registry_) {
    return false;
  }
  
  
  const auto* watcher = watcher_registry_->getWatcher(watcher_id);
  if (!watcher) {
    return false;
  }
  
  // Create scope to track global variable accesses
  // This will record which variables the condition depends on
  auto tracker = std::make_shared<DependencyTracker>();
  DependencyTrackerScope scope(tracker);
  
  // Evaluate the condition bytecode
  bool result = vm_->evaluateConditionBytecode(
      watcher->condition_func_id,
      watcher->condition_ip
  );
  
    if (debug_mode_) {
        std::cerr << "[ExecutionEngine::evaluateCondition] Watcher " << watcher_id
                  << " condition evaluated to: " << (result ? "true" : "false") << "\n";
    }
  
  return result;
}

void ExecutionEngine::onTimerFire(const Event& event) {
    auto *payload = static_cast<std::pair<Value, uint32_t>*>(event.ptr);
    if (!payload) return;

    Value closure = payload->first;
    uint32_t timer_id = payload->second;
    bool is_timeout = (event.data1 == 1);
    delete payload;

    if (!vm_) return;

    // Check if a goroutine is waiting on this timer via WaitHandle
    if (scheduler_) {
        Scheduler::Goroutine* g = scheduler_->findGoroutineByWaitTarget(
            Scheduler::AwaitableType::TIMER_WAIT, timer_id);
        if (g) {
            if (debug_mode_) {
                std::cerr << "[ExecutionEngine] Unparking goroutine " << g->id
                          << " waiting on timer " << timer_id << "\n";
            }
            // Store the timer result as resume_value
            if (is_timeout) {
                g->wait_handle.resume_value = vm_->getTimeoutResult(timer_id);
            } else {
                g->wait_handle.resume_value = vm_->getIntervalResult(timer_id);
            }
            scheduler_->unpark(g);
        }
    }

    // Also call the timer callback for non-await usage
    try {
        Value result = vm_->callFunction(closure, {});
        if (is_timeout) {
            vm_->addTimeoutResult(timer_id, result);
        } else {
            vm_->addIntervalResult(timer_id, result);
        }
    } catch (const std::exception& e) {
        ::havel::error("[ExecutionEngine] Timer callback exception: {}", e.what());
    }
}

void ExecutionEngine::onChannelRecv(const Event& event) {
    // Event payload: data1 = channel_id that now has data available
    uint32_t channel_id = event.data1;

    if (!scheduler_) return;

    if (debug_mode_) {
        std::cerr << "[ExecutionEngine] Channel " << channel_id << " has data available\n";
    }

    // Find goroutine waiting for data on this channel
    Scheduler::Goroutine* g = scheduler_->findGoroutineByWaitTarget(
        Scheduler::AwaitableType::CHANNEL_RECV, channel_id);
    if (g) {
        if (debug_mode_) {
            std::cerr << "[ExecutionEngine] Unparking goroutine " << g->id
                      << " waiting on channel " << channel_id << " recv\n";
        }
        // Resume value will be fetched from channel when goroutine resumes
        scheduler_->unpark(g);
    }
}

void ExecutionEngine::onChannelSend(const Event& event) {
    // Event payload: data1 = channel_id that now has space available
    uint32_t channel_id = event.data1;

    if (!scheduler_) return;

    if (debug_mode_) {
        std::cerr << "[ExecutionEngine] Channel " << channel_id << " has space available\n";
    }

    // Find goroutine waiting to send on this channel
    Scheduler::Goroutine* g = scheduler_->findGoroutineByWaitTarget(
        Scheduler::AwaitableType::CHANNEL_SEND, channel_id);
    if (g) {
        if (debug_mode_) {
            std::cerr << "[ExecutionEngine] Unparking goroutine " << g->id
                      << " waiting on channel " << channel_id << " send\n";
        }
        scheduler_->unpark(g);
    }
}

} // namespace havel::compiler
