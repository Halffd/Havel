#pragma once

#include "../../compiler/vm/VM.hpp"
#include "../concurrency/Scheduler.hpp"
#include "../concurrency/Fiber.hpp"
#include "../concurrency/EventQueue.hpp"

#include <memory>
#include <cstdint>

namespace havel {

// Forward declarations
class VM;
class Scheduler;
class EventQueue;
class Fiber;

/**
 * ExecutionEngine - Phase 3 Main Loop
 * 
 * Central coordinator for single-threaded cooperative fiber execution.
 * 
 * Responsibilities:
 * 1. Main loop: executeFrame() - called repeatedly by event loop
 * 2. Scheduler integration - picks next runnable fiber
 * 3. Event queue handling - drains callbacks before each frame
 * 4. Single-step execution - VM::executeOneStep()
 * 5. Result handling - manages fiber state transitions
 * 
 * Non-blocking: All operations return immediately, no blocking waits.
 * 
 * CRITICAL INVARIANT: Single-threaded execution
 * - Only one fiber runs at a time
 * - Main loop runs in application's event thread
 * - No locks needed in executeFrame()
 */
class ExecutionEngine {
public:
  ExecutionEngine(VM* vm, Scheduler* sched, EventQueue* eq);
  ~ExecutionEngine();

  // ========== MAIN LOOP ==========
  
  /**
   * executeFrame - Core Phase 3 main loop
   * 
   * Called repeatedly by application's event loop (e.g., 60x/second).
   * Executes one instruction in the next runnable fiber, then returns.
   * 
   * Algorithm:
   * 1. Drain all pending callbacks (EventQueue::processAll)
   * 2. Pick next runnable fiber (Scheduler::pickNext)
   * 3. If no work, return idle
   * 4. Execute one instruction (VM::executeOneStep)
   * 5. Handle result (YIELD/SUSPENDED/RETURNED/ERROR)
   * 
   * @return true if work remains, false if all fibers suspended/done
   */
  bool executeFrame();
  
  /**
   * isDone - Check if all fibers have completed
   * 
   * @return true if no runnable fibers and no suspended fibers
   */
  bool isDone() const;
  
  /**
   * shutdown - Gracefully shutdown execution
   */
  void shutdown();

  // ========== STATUS INSPECTION ==========
  
  /**
   * getStats - Get execution statistics
   */
  struct Stats {
    uint64_t frames_executed = 0;
    uint64_t fibers_spawned = 0;
    uint64_t fibers_completed = 0;
    uint64_t instructions_executed = 0;
  };
  Stats getStats() const { return stats_; }

  // ========== DEBUG ==========
  void setDebugMode(bool enabled) { debug_mode_ = enabled; }
  bool getDebugMode() const { return debug_mode_; }

private:
  // ========== CORE COMPONENTS ==========
  VM* vm_;                    // Bytecode virtual machine
  Scheduler* scheduler_;      // Fiber scheduler
  EventQueue* event_queue_;   // Event callback queue
  
  // ========== STATE ==========
  bool running_ = false;
  Stats stats_;
  bool debug_mode_ = false;
  
  // ========== HELPER METHODS ==========
  void handleYield(Fiber* fiber);
  void handleSuspended(Fiber* fiber);
  void handleReturned(Fiber* fiber);
  void handleError(Fiber* fiber, const std::string& msg);
};

} // namespace havel
