#include "ExecutionEngine.hpp"
#include "../../../utils/Logger.hpp"
#include "../concurrency/Fiber.hpp"
#include "../concurrency/DependencyTracker.hpp"  // Phase 2E: For tracking dependencies
#include <iostream>

namespace havel::compiler {

ExecutionEngine::ExecutionEngine(VM* vm, Scheduler* sched, EventQueue* eq)
    : vm_(vm), scheduler_(sched), event_queue_(eq), running_(false) {
  if (!vm || !sched || !eq) {
    throw std::invalid_argument("ExecutionEngine requires non-null VM, Scheduler, and EventQueue");
  }
  
  // Phase 2A: Give VM reference to event queue for variable change notifications
  vm_->setEventQueue(eq);
  
  // Phase 2C: Initialize watcher registry
  watcher_registry_ = std::make_unique<WatcherRegistry>();
  
  // Phase 1: Register event handlers for unified event system
  if (event_queue_) {
    event_queue_->onEvent(EventType::THREAD_COMPLETE, 
        [this](const Event& event) { onThreadComplete(event); });
    
    // Phase 2A: Register handler for variable changes
    event_queue_->onEvent(EventType::VAR_CHANGED,
        [this](const Event& event) { onVariableChanged(event); });
    
    // TODO: Register handlers for other event types
    // TIMER_FIRE, CHANNEL_SEND, etc.
  }
}

ExecutionEngine::~ExecutionEngine() {
  shutdown();
}

// ============================================================================
// PHASE 3: Main Loop Implementation
// ============================================================================

bool ExecutionEngine::executeFrame() {
  if (!running_) {
    return false;
  }

  try {
    // STEP 1: Process all pending events
    // Events include: thread completions, timer fires, variable changes, etc.
    // Event handlers (registered in constructor) process each event
    if (event_queue_) {
      event_queue_->processAll();
    }
    vm_->garbageCollectionSafePoint();
    
    // STEP 2: Pick next runnable goroutine
    // The scheduler maintains a queue of RUNNABLE goroutines
    Scheduler::Goroutine* g = scheduler_->pickNext();
    if (!g) {
      // No runnable goroutine - idle
      if (debug_mode_) {
            ::havel::debug("[ExecutionEngine] No runnable goroutines, idle");
      }
      return false;
    }
    
    // STEP 3: Load fiber state into VM's global execution state
    // This synchronizes the VM with the fiber's suspended state
    // Required before executeOneStep() to restore the executing fiber
    if (g->fiber) {
      vm_->loadFiberState(g->fiber);
    }
    
    // STEP 3B: Phase 2I Integration - Handle hotkey action Fibers
    // If this Fiber is a hotkey action (special marker function_id), execute the callback
    // instead of bytecode
    VMExecutionResult result;
    if (g->fiber && g->fiber->current_function_id == 0xFFFFFFFF) {  // HOTKEY_ACTION_FUNCTION_ID
      // Phase 2I: This is a hotkey action Fiber
      // Get the registered callback and execute it
      if (debug_mode_) {
                ::havel::debug("[ExecutionEngine] Executing hotkey action Fiber {}", g->fiber->id);
      }
      
      // Call the registered hotkey action callback if available
      if (hotkey_action_callback_) {
        try {
          hotkey_action_callback_(g->fiber->id);  // Execute the hotkey action
        } catch (const std::exception& e) {
          // Handle exceptions from hotkey actions
          if (debug_mode_) {
                    ::havel::debug("[ExecutionEngine] Exception in hotkey action: {}", e.what());
          }
          g->fiber->had_error = true;
          g->fiber->error_message = std::string("Hotkey action error: ") + e.what();
          result.type = VMExecutionResult::ERROR;
          result.error_message = g->fiber->error_message;
        }
      }
      
      // Mark hotkey action Fiber as completed
      result.type = VMExecutionResult::RETURNED;
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
    
    // STEP 6: Handle execution result
    switch (result.type) {
      case VMExecutionResult::YIELD:
        // Instruction completed normally, goroutine yields
        // Return it to scheduler's runnable queue
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
          
          // Special handling for SLEEP - set resume time
          if (reason == static_cast<uint8_t>(SuspensionReason::SLEEP)) {
            // Context for sleep is the duration in milliseconds
            int64_t ms = reinterpret_cast<intptr_t>(context);
            g->resume_at_time = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
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
    return true;  // Work remains
    
  } catch (const std::exception& e) {
        ::havel::error("[ExecutionEngine] Exception in executeFrame: {}", e.what());
    running_ = false;
    return false;
  }
}

bool ExecutionEngine::isDone() const {
  // Phase 3: ExecutionEngine is done when no goroutines are runnable or suspended.
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
  // Phase 3: Graceful shutdown of ExecutionEngine
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
        ::havel::debug("[ExecutionEngine] Goroutine yielded, returning to runnable queue");
  }
  // Return goroutine to scheduler's runnable queue
  if (g) {
    scheduler_->unpark(g);
  }
}

void ExecutionEngine::handleSuspended(Scheduler::Goroutine* g) {
  if (debug_mode_) {
        ::havel::debug("[ExecutionEngine] Goroutine suspended, waiting for external event");
  }
  // Goroutine is already marked SUSPENDED by the suspension operation
  // EventQueue will unpark it when the waiting condition is met
}

void ExecutionEngine::handleReturned(Scheduler::Goroutine* g) {
  if (debug_mode_) {
        ::havel::debug("[ExecutionEngine] Goroutine completed execution");
  }
  // Mark goroutine DONE
  if (g) {
    g->state = Scheduler::GoroutineState::Done;
  }
}

void ExecutionEngine::handleError(Scheduler::Goroutine* g, const std::string& msg) {
  if (debug_mode_) {
        ::havel::debug("[ExecutionEngine] Goroutine error: {}", msg);
  }
  // Mark goroutine DONE with error
  if (g) {
    g->state = Scheduler::GoroutineState::Done;
  }
}

// ============================================================================
// PHASE 1: Event Handler for Thread Completion
// ============================================================================

void ExecutionEngine::onThreadComplete(const Event& event) {
  // Event payload: data1 = thread_id that completed
  uint32_t thread_id = event.data1;
  
  if (!vm_) {
    return;
  }

  if (debug_mode_) {
        ::havel::debug("[ExecutionEngine] Thread {} completed (event-driven)", thread_id);
  }

  // Get the fiber that was waiting on this thread
  Fiber* waiting_fiber = vm_->getThreadWaitingFiber(thread_id);
  
  if (waiting_fiber) {
    if (debug_mode_) {
                    ::havel::debug("[ExecutionEngine] Unparking fiber waiting on thread {}", thread_id);
    }
    
    // Mark fiber as runnable (no longer suspended on thread.join())
    // Note: In Phase 3B, the scheduler integration is still being developed
    // For now, we directly manipulate fiber state
    waiting_fiber->state = FiberState::RUNNABLE;
  }
  
  // Unregister the wait tracking
  vm_->unregisterThreadWait(thread_id);
}

// ============================================================================
// PHASE 2A: Variable Change Event Handler
// ============================================================================

void ExecutionEngine::onVariableChanged(const Event& event) {
  // Event payload:
  //   data1: hash of variable name
  //   ptr: unsafe pointer to variable name string (must be copied)
  
  if (!event.ptr || !watcher_registry_) {
    return;
  }
  
  const std::string var_name = static_cast<const char*>(event.ptr);
  
  if (debug_mode_) {
        ::havel::debug("[ExecutionEngine] Variable '{}' changed, checking {} watchers",
                     var_name, watcher_registry_->getWatcherCount());
  }
  
  // Phase 2C: Notify watcher registry and evaluate watchers that depend on this variable
  // Returns list of fibers whose watchers fired (false→true edge)
  std::vector<Fiber*> fired_fibers = watcher_registry_->onVariableChanged(
      var_name,
      [this](uint32_t watcher_id) -> bool {
        // Phase 2D: Re-evaluate condition for this watcher
        return evaluateCondition(watcher_id);
      }
  );
  
  // Resume all fibers whose watchers fired
  for (Fiber* fiber : fired_fibers) {
    if (fiber && scheduler_) {
      if (debug_mode_) {
                    ::havel::debug("[ExecutionEngine] Resuming fiber for fired watcher");
      }
      fiber->state = FiberState::RUNNABLE;
    }
  }
}

bool ExecutionEngine::evaluateCondition(uint32_t watcher_id) {
  // Phase 2E: Evaluate a condition bytecode for a watcher
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
  
  // Phase 2E: Get watcher's condition bytecode info
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
        ::havel::debug("[ExecutionEngine::evaluateCondition] Watcher {} condition evaluated to: {}",
                     watcher_id, result ? "true" : "false");
  }
  
  return result;
}

} // namespace havel::compiler
