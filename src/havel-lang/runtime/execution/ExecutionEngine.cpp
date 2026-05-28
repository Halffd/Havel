#include "ExecutionEngine.hpp"
#include "../../../utils/Logger.hpp"
#include "core/hotkey/HotkeyActionWrapper.hpp"
#include "../../common/Debug.hpp"
#include "../concurrency/Fiber.hpp"
#include "../concurrency/DependencyTracker.hpp"
#include <iostream>

namespace havel::compiler {

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
    Scheduler::Goroutine* g = scheduler_->pickNext();
    if (!g) {
      // No runnable goroutine - idle
      return false;
    }
    
            if (debug_mode_) {
                std::cerr << "[ExecutionEngine] Picked goroutine " << g->id << " (fiber " << (g->fiber ? g->fiber->id : 0) << ")\n";
            }
    
    // STEP 3: Load fiber state into VM's global execution state
    // For newly-created goroutines, use startGoroutineCall which resolves
    // the correct chunk (from closure if needed) and sets up the call frame.
    // For resuming goroutines, use loadFiberState which restores suspended state.
    if (g->state == Scheduler::GoroutineState::Created) {
        bool ok = vm_->startGoroutineCall(g->function_id, g->closure_id, g->locals);
        if (!ok) {
            handleReturned(g);
            stats_.goroutines_completed++;
            stats_.frames_executed++;
            return scheduler_->hasRunnableFibers() || scheduler_->suspendedCount() > 0;
        }
        g->state = Scheduler::GoroutineState::Runnable;
        // Sync VM state to fiber immediately so the fiber's call_stack
        // has the correct chunk_ptr. The Fiber constructor initializes
        // call_stack via pushCall() with chunk_ptr=nullptr; without this
        // sync, the first loadFiberState would see null chunk_ptr.
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
      // STEP 4: Execute one instruction in this goroutine
      // This is non-blocking - always returns immediately
      result = vm_->executeOneStep(g->fiber);
    }
    
// STEP 5: Save VM state back to fiber
    // This persists any progress made by the instruction
    // Preserves IP for resumption on next iteration
    if (g->fiber) {
        vm_->saveFiberState(g->fiber);
    }

    stats_.instructions_executed++;
    g->instructions_executed++;

    // Time slice enforcement: auto-yield when goroutine exceeds instruction budget
    // Prevents a single goroutine from monopolizing the scheduler
    // Hotkeys get a smaller budget to prevent infinite loops from blocking UI
    bool budget_exceeded = (g->instructions_executed >= g->max_instructions_per_tick);

    // STEP 6: Handle execution result
    switch (result.type) {
      case VMExecutionResult::YIELD:
        // Instruction completed normally, goroutine yields
        // Return it to scheduler's runnable queue
        if (budget_exceeded) {
            // Time slice expired - yield and reset budget for next run
            g->instructions_executed = 0;
                if (debug_mode_) {
                    std::cerr << "[ExecutionEngine] Goroutine " << g->id << " time slice expired ("
                              << g->max_instructions_per_tick << " instructions), yielding\n";
                }
        }
        handleYield(g);
        break;
        
      case VMExecutionResult::SUSPENDED:
        // Handle suspension requested via VM (e.g. from host function)
        if (vm_->isSuspensionRequested()) {
          uint8_t reason = vm_->getSuspensionReason();
          void* context = vm_->getSuspensionContext();
          
          scheduler_->suspend(g, static_cast<Scheduler::SuspensionReason>(reason));
          if (g->fiber) {
            g->fiber->suspend(static_cast<SuspensionReason>(reason), context);
          }
          
                // Special handling for SLEEP - set deadline via WaitHandle
                if (reason == static_cast<uint8_t>(SuspensionReason::SLEEP)) {
                    // Context for sleep is the duration in milliseconds
                    int64_t ms = reinterpret_cast<intptr_t>(context);
                    g->wait_handle.type = Scheduler::AwaitableType::SLEEP;
                    g->wait_handle.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
                }
                // Handle COROUTINE_WAIT - set WaitHandle for coroutine await
                if (reason == static_cast<uint8_t>(SuspensionReason::COROUTINE_WAIT)) {
                    uint32_t co_id = static_cast<uint32_t>(reinterpret_cast<intptr_t>(context));
                    g->wait_handle.type = Scheduler::AwaitableType::COROUTINE;
                    g->wait_handle.target_id = co_id;
                }
                // Handle THREAD_JOIN - set WaitHandle for thread join
                if (reason == static_cast<uint8_t>(SuspensionReason::THREAD_JOIN)) {
                    uint32_t tid = static_cast<uint32_t>(reinterpret_cast<intptr_t>(context));
                    g->wait_handle.type = Scheduler::AwaitableType::THREAD_JOIN;
                    g->wait_handle.target_id = tid;
                }
                // Handle CHANNEL_RECV - set WaitHandle for channel receive
                if (reason == static_cast<uint8_t>(SuspensionReason::CHANNEL_RECV)) {
                    uint32_t ch_id = static_cast<uint32_t>(reinterpret_cast<intptr_t>(context));
                    g->wait_handle.type = Scheduler::AwaitableType::CHANNEL_RECV;
                    g->wait_handle.target_id = ch_id;
                }
                // Handle TIMER - set WaitHandle for timer/interval/timeout
                if (reason == static_cast<uint8_t>(SuspensionReason::TIMER)) {
                    uint32_t timer_id = static_cast<uint32_t>(reinterpret_cast<intptr_t>(context));
                    g->wait_handle.type = Scheduler::AwaitableType::TIMER_WAIT;
                    g->wait_handle.target_id = timer_id;
                }
          
          vm_->clearSuspensionRequest();
        }
        
        // Goroutine blocked on external event (channel, timer, thread)
        // EventQueue will unpark it when condition is met
        handleSuspended(g);
        break;
        
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
    running_ = false;
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
