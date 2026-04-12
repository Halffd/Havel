#pragma once

#include <atomic>
#include <chrono>
#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "../../core/Value.hpp"

namespace havel::compiler {

using havel::core::Value;

/**
 * Scheduler - Cooperative Goroutine Scheduler for Havel VM
 *
 * DIFFERS from traditional M:N (Go) scheduler:
 * - No OS threads spawned
 * - Single VM thread drives execution
 * - Goroutines are bytecode execution contexts (not arbitrary functions)
 * - Suspension-based (not preemptive)
 *
 * Design:
 * - VM calls scheduler->pickNext() to get next goroutine to run
 * - VM executes ONE instruction, then yields back to scheduler
 * - Scheduler parks goroutines on channel/timer/thread waits
 * - Event queue wakes parked goroutines via unpark()
 */

class Scheduler {
public:
  enum class GoroutineState {
    Created,         // Just spawned, not yet runnable
    Runnable,        // Ready to execute next instruction
    Running,         // Currently executing (in VM)
    Suspended,       // Parked (waiting on channel/timer/thread/sleep)
    Done             // Execution finished
  };

  enum class SuspensionReason {
    None = 0,
    ChannelWait,     // Suspended on recv() from empty channel
    ThreadWait,      // Suspended on thread.join()
    SleepWait,       // Suspended on sleep()
    TimerWait        // Suspended on timeout
  };

  /**
   * Goroutine - Bytecode execution context
   *
   * Each goroutine is an execution state for a single function/task.
   * The VM executes bytecode instructions for the current goroutine,
   * then yields back to scheduler to pick the next runnable goroutine.
   */
  struct Goroutine {
    // Identity
    uint32_t id;
    std::string name;

    // Bytecode execution state
    uint32_t function_id;              // Which function's bytecode (required)
    uint32_t chunk_index;              // Which bytecode chunk (for module support)
    uint32_t ip;                       // Instruction pointer (resume position)
    std::vector<Value> stack;          // VM operand stack
    std::vector<Value> locals;         // Local variable storage
    
    // Goroutine state machine
    GoroutineState state;
    SuspensionReason suspension_reason;
    
    // Suspension context (what we're waiting for)
    uint32_t waiting_for_channel;      // Channel ID (if waiting on recv)
    uint32_t waiting_for_thread;       // Thread ID (if waiting on join)
    std::chrono::steady_clock::time_point resume_at_time;  // Time-based waits

    // Timing information
    std::chrono::steady_clock::time_point created_time;
    
    // Metadata
    uint32_t parent_id;                // Parent goroutine ID (if spawned by another)

    explicit Goroutine(uint32_t id_, const std::string& name_ = "")
        : id(id_), name(name_), function_id(0), chunk_index(0), ip(0),
          state(GoroutineState::Created), suspension_reason(SuspensionReason::None),
          waiting_for_channel(0), waiting_for_thread(0),
          created_time(std::chrono::steady_clock::now()), parent_id(0) {}
  };

  // Singleton interface
  static Scheduler& instance();

  Scheduler(const Scheduler&) = delete;
  Scheduler& operator=(const Scheduler&) = delete;

  // ===== Goroutine Lifecycle =====

  /**
   * Spawn a new goroutine to execute bytecode
   *
   * @param function_id Index of function to execute (into bytecode chunk)
   * @param args Arguments to pass (stored in locals[])
   * @param name Optional name for debugging
   * @return Goroutine ID (for tracking and .join())
   */
  uint32_t spawn(uint32_t function_id, const std::vector<Value>& args,
                 const std::string& name = "");

  /**
   * Get the currently executing goroutine
   *
   * Only valid during VM execution (between VM.executeStep() calls).
   * Returns nullptr if no goroutine is running.
   */
  Goroutine* current();

  /**
   * Get a goroutine by ID
   *
   * @param id Goroutine ID
   * @return Pointer to goroutine (or nullptr if not found)
   */
  Goroutine* get(uint32_t id);

  // ===== Execution Control =====

  /**
   * Pick the next runnable goroutine
   *
   * Called by VM at start of each cycle to decide what to run next.
   * - Returns nullptr if no runnable goroutines (all sleeping/suspended)
   * - Does NOT modify state (VM must set Running)
   *
   * @return Next goroutine to run (or nullptr)
   */
  Goroutine* pickNext();

  /**
   * Suspend the current goroutine
   *
   * Called by VM when goroutine hits a blocking point:
   * - recv() on empty channel → suspend with ChannelWait
   * - thread.join() → suspend with ThreadWait
   * - sleep(ms) → suspend with SleepWait
   *
   * MUST be followed by yielding back to event loop.
   * VM will pick different goroutine on next step.
   *
   * @param g Goroutine to suspend (usually = current())
   * @param reason Why it's suspended
   */
  void suspend(Goroutine* g, SuspensionReason reason);

  /**
   * Wake a suspended goroutine
   *
   * Called from event queue by:
   * - Timer callbacks (sleep completion)
   * - Channel senders (message arrived)
   * - Thread completion callbacks
   *
   * Moves goroutine from Suspended → Runnable.
   * VM will pick it up on next pickNext().
   *
   * @param g Goroutine to wake (must be Suspended)
   */
  void unpark(Goroutine* g);

  // ===== Scheduler Lifecycle =====

  void start();
  void stop();
  bool isRunning() const { return running_.load(); }

  /**
   * Wait for all goroutines to complete
   *
   * Blocks calling thread until all goroutines are Done.
   * Used for shutdown synchronization.
   */
  void waitAll();

  // ===== Diagnostics =====

  size_t goroutineCount() const;
  size_t runnableCount() const;
  size_t suspendedCount() const;

private:
  Scheduler();
  ~Scheduler();

  // State management
  std::atomic<bool> running_{false};
  std::atomic<bool> shutdown_{false};

  // Goroutine storage and queues
  std::unordered_map<uint32_t, std::unique_ptr<Goroutine>> goroutines_;
  mutable std::mutex goroutines_mutex_;
  uint32_t next_goroutine_id_ = 1;

  // Runnable queue (ready to execute)
  std::deque<Goroutine*> runnable_;
  mutable std::mutex runnable_mutex_;

  // Current goroutine (the one VM is executing)
  Goroutine* current_ = nullptr;
};

}  // namespace havel::compiler
