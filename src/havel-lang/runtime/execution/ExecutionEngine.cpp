#include "ExecutionEngine.hpp"
#include "../concurrency/Fiber.hpp"
#include <iostream>

namespace havel::compiler {

ExecutionEngine::ExecutionEngine(VM* vm, Scheduler* sched, EventQueue* eq)
    : vm_(vm), scheduler_(sched), event_queue_(eq), running_(false) {
  if (!vm || !sched || !eq) {
    throw std::invalid_argument("ExecutionEngine requires non-null VM, Scheduler, and EventQueue");
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
    // STEP 1: Drain event queue
    // Process all pending callbacks (channel sends, timer completions, etc)
    if (event_queue_) {
      event_queue_->processAll();
    }
    
    // Phase 3B-7: Check for thread completions and unpark waiting fibers
    checkThreadCompletions();
    
    // STEP 2: Pick next runnable goroutine
    // The scheduler maintains a queue of RUNNABLE goroutines
    Scheduler::Goroutine* g = scheduler_->pickNext();
    if (!g) {
      // No runnable goroutine - idle
      if (debug_mode_) {
        std::cout << "[ExecutionEngine] No runnable goroutines, idle" << std::endl;
      }
      return false;
    }
    
    // STEP 3: Load fiber state into VM's global execution state
    // This synchronizes the VM with the fiber's suspended state
    // Required before executeOneStep() to restore the executing fiber
    if (g->fiber) {
      vm_->loadFiberState(g->fiber);
    }
    
    // STEP 4: Execute one instruction in this goroutine
    // This is non-blocking - always returns immediately
    VMExecutionResult result = vm_->executeOneStep(g->fiber);
    
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
        // Goroutine blocked on external event (channel, timer, thread)
        // Scheduler already marked it SUSPENDED
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
    return true;  // Work remains
    
  } catch (const std::exception& e) {
    std::cerr << "[ExecutionEngine] Exception in executeFrame: " << e.what() << std::endl;
    running_ = false;
    return false;
  }
}

bool ExecutionEngine::isDone() const {
  // TODO: Check if scheduler has any runnable or suspended goroutines
  return false;  // Not implemented yet
}

void ExecutionEngine::shutdown() {
  running_ = false;
  // TODO: Clean up any remaining goroutines
}

// ============================================================================
// RESULT HANDLERS
// ============================================================================

void ExecutionEngine::handleYield(Scheduler::Goroutine* g) {
  if (debug_mode_) {
    std::cout << "[ExecutionEngine] Goroutine yielded, returning to runnable queue" << std::endl;
  }
  // Return goroutine to scheduler's runnable queue
  if (g) {
    scheduler_->unpark(g);
  }
}

void ExecutionEngine::handleSuspended(Scheduler::Goroutine* g) {
  if (debug_mode_) {
    std::cout << "[ExecutionEngine] Goroutine suspended, waiting for external event" << std::endl;
  }
  // Goroutine is already marked SUSPENDED by the suspension operation
  // EventQueue will unpark it when the waiting condition is met
}

void ExecutionEngine::handleReturned(Scheduler::Goroutine* g) {
  if (debug_mode_) {
    std::cout << "[ExecutionEngine] Goroutine completed execution" << std::endl;
  }
  // Mark goroutine DONE
  if (g) {
    g->state = Scheduler::GoroutineState::Done;
  }
}

void ExecutionEngine::handleError(Scheduler::Goroutine* g, const std::string& msg) {
  if (debug_mode_) {
    std::cout << "[ExecutionEngine] Goroutine error: " << msg << std::endl;
  }
  // Mark goroutine DONE with error
  if (g) {
    g->state = Scheduler::GoroutineState::Done;
  }
}

// ============================================================================
// PHASE 3B-7: Thread Completion Handling
// ============================================================================

void ExecutionEngine::checkThreadCompletions() {
  if (!vm_ || !scheduler_ || !concurrency_bridge_) {
    return;
  }

  // Get all thread IDs with waiting fibers
  auto waiting_thread_ids = vm_->getWaitingThreadIds();
  
  if (debug_mode_) {
    std::cout << "[ExecutionEngine] Checking " << waiting_thread_ids.size() 
              << " waiting threads..." << std::endl;
  }

  // Check each waiting thread for completion
  for (uint32_t thread_id : waiting_thread_ids) {
    // Check if thread is now complete
    if (concurrency_bridge_->isThreadCompleted(thread_id)) {
      // Get the fiber that was waiting
      Fiber* waiting_fiber = vm_->getThreadWaitingFiber(thread_id);
      
      if (waiting_fiber) {
        if (debug_mode_) {
          std::cout << "[ExecutionEngine] Thread " << thread_id 
                    << " completed, unparking fiber " << waiting_fiber->id << std::endl;
        }
        
        // Mark fiber as RUNNABLE so scheduler will pick it up
        waiting_fiber->state = FiberState::RUNNABLE;
        
        // Unpark the fiber (return it to runnable queue)
        scheduler_->unpark(waiting_fiber);
      }
      
      // Unregister the wait tracking
      vm_->unregisterThreadWait(thread_id);
    }
  }
  
  // Cleanup any completed threads (join and remove from active tracking)
  int cleaned = concurrency_bridge_->cleanupCompletedThreads();
  if (debug_mode_ && cleaned > 0) {
    std::cout << "[ExecutionEngine] Cleaned up " << cleaned << " completed threads" << std::endl;
  }
}

} // namespace havel::compiler
