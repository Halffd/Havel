#pragma once

#include "../../compiler/vm/VM.hpp"
#include "../../compiler/runtime/EventQueue.hpp"
#include "../../compiler/runtime/ConcurrencyBridge.hpp"
#include "../concurrency/Scheduler.hpp"

#include <memory>
#include <cstdint>

namespace havel::compiler {

/**
 * ExecutionEngine - Phase 3 Main Loop
 * 
 * Central coordinator for single-threaded cooperative goroutine execution.
 * 
 * Responsibilities:
 * 1. Main loop: executeFrame() - called repeatedly by event loop
 * 2. Scheduler integration - picks next runnable goroutine
 * 3. Event queue handling - drains callbacks before each frame
 * 4. Single-step execution - VM::executeOneStep()
 * 5. Result handling - manages goroutine state transitions
 * 
 * Non-blocking: All operations return immediately, no blocking waits.
 * 
 * CRITICAL INVARIANT: Single-threaded execution
 * - Only one goroutine runs at a time
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
   * Executes one instruction in the next runnable goroutine, then returns.
   * 
   * Algorithm:
   * 1. Drain all pending callbacks (EventQueue::processAll)
   * 2. Pick next runnable goroutine (Scheduler::pickNext)
   * 3. If no work, return idle
   * 4. Execute one instruction (VM::executeOneStep)
   * 5. Handle result (YIELD/SUSPENDED/RETURNED/ERROR)
   * 
   * @return true if work remains, false if all goroutines suspended/done
   */
  bool executeFrame();
  
  /**
   * isDone - Check if all goroutines have completed
   * 
   * @return true if no runnable goroutines and no suspended goroutines
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
    uint64_t goroutines_spawned = 0;
    uint64_t goroutines_completed = 0;
    uint64_t instructions_executed = 0;
  };
  Stats getStats() const { return stats_; }

  // ========== DEBUG ==========
  void setDebugMode(bool enabled) { debug_mode_ = enabled; }
  bool getDebugMode() const { return debug_mode_; }
  
  // ========== PHASE 3B-7: THREAD MANAGEMENT ==========
  void setConcurrencyBridge(ConcurrencyBridge* bridge) { concurrency_bridge_ = bridge; }
  ConcurrencyBridge* getConcurrencyBridge() const { return concurrency_bridge_; }

private:
  // ========== CORE COMPONENTS ==========
  VM* vm_;                    // Bytecode virtual machine
  Scheduler* scheduler_;      // Goroutine scheduler
  EventQueue* event_queue_;   // Event callback queue
  ConcurrencyBridge* concurrency_bridge_ = nullptr;  // Thread management (Phase 3B-7)
  
  // ========== STATE ==========
  bool running_ = false;
  Stats stats_;
  bool debug_mode_ = false;
  
  // ========== HELPER METHODS ==========
  void handleYield(Scheduler::Goroutine* g);
  void handleSuspended(Scheduler::Goroutine* g);
  void handleReturned(Scheduler::Goroutine* g);
  void handleError(Scheduler::Goroutine* g, const std::string& msg);
  
  // Phase 3B-7: Check for thread completions and unpark waiting fibers
  void checkThreadCompletions();
};

} // namespace havel::compiler

