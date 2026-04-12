#include "ExecutionEngine.hpp"
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
    
    // STEP 3: Execute one instruction in this goroutine
    // This is non-blocking - always returns immediately
    VMExecutionResult result = vm_->executeOneStep(nullptr);  // TODO: Pass actual Fiber* or map g->id
    
    stats_.instructions_executed++;
    
    // STEP 4: Handle execution result
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

} // namespace havel::compiler
