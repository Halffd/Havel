#include "ExecutionEngine.hpp"
#include <iostream>

namespace havel {

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
    
    // STEP 2: Pick next runnable fiber
    // The scheduler maintains a queue of RUNNABLE fibers
    Scheduler::Goroutine* goroutine = scheduler_->pickNext();
    if (!goroutine) {
      // No runnable fiber - idle
      if (debug_mode_) {
        std::cout << "[ExecutionEngine] No runnable fibers, idle" << std::endl;
      }
      return false;
    }
    
    // STEP 3: Execute one instruction in this fiber
    // This is non-blocking - always returns immediately
    compiler::VMExecutionResult result = vm_->executeOneStep(nullptr);  // TODO: Pass actual Fiber*
    
    stats_.instructions_executed++;
    
    // STEP 4: Handle execution result
    switch (result.type) {
      case compiler::VMExecutionResult::YIELD:
        // Instruction completed normally, fiber yields
        // Return it to scheduler's runnable queue
        handleYield(nullptr);  // TODO: Pass actual Fiber*
        break;
        
      case compiler::VMExecutionResult::SUSPENDED:
        // Fiber blocked on external event (channel, timer, thread)
        // Scheduler already marked it SUSPENDED
        // EventQueue will unpark it when condition is met
        handleSuspended(nullptr);  // TODO: Pass actual Fiber*
        break;
        
      case compiler::VMExecutionResult::RETURNED:
        // Function returned - mark fiber DONE
        handleReturned(nullptr);  // TODO: Pass actual Fiber*
        stats_.fibers_completed++;
        break;
        
      case compiler::VMExecutionResult::ERROR:
        // Exception occurred
        handleError(nullptr, result.error_message);  // TODO: Pass actual Fiber*
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
  // TODO: Check if scheduler has any runnable or suspended fibers
  return false;  // Not implemented yet
}

void ExecutionEngine::shutdown() {
  running_ = false;
  // TODO: Clean up any remaining fibers
}

// ============================================================================
// RESULT HANDLERS
// ============================================================================

void ExecutionEngine::handleYield(Fiber* fiber) {
  if (debug_mode_) {
    std::cout << "[ExecutionEngine] Fiber yielded, returning to runnable queue" << std::endl;
  }
  // Return fiber to scheduler's runnable queue
  if (fiber) {
    scheduler_->unpark(fiber);
  }
}

void ExecutionEngine::handleSuspended(Fiber* fiber) {
  if (debug_mode_) {
    std::cout << "[ExecutionEngine] Fiber suspended, waiting for external event" << std::endl;
  }
  // Fiber is already marked SUSPENDED by the suspension operation
  // EventQueue will unpark it when the waiting condition is met
}

void ExecutionEngine::handleReturned(Fiber* fiber) {
  if (debug_mode_) {
    std::cout << "[ExecutionEngine] Fiber completed execution" << std::endl;
  }
  // Mark fiber DONE
  if (fiber) {
    // fiber->state = FiberState::DONE;
  }
}

void ExecutionEngine::handleError(Fiber* fiber, const std::string& msg) {
  if (debug_mode_) {
    std::cout << "[ExecutionEngine] Fiber error: " << msg << std::endl;
  }
  // Mark fiber DONE with error
  if (fiber) {
    // fiber->had_error = true;
    // fiber->error_message = msg;
    // fiber->state = FiberState::DONE;
  }
}

} // namespace havel
