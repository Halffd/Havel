#pragma once

#include <atomic>
#include <condition_variable>
#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>
#include <unordered_map>
#include <vector>

#include "../../core/Value.hpp"

namespace havel::compiler {

using havel::core::Value;

/**
 * Go-style M:N Scheduler Implementation
 *
 * M (Machine) - OS thread that executes Go runtime code
 * P (Processor) - Logical processor, VM execution context
 * G (Goroutine) - Lightweight coroutine/user task
 *
 * Model:
 * - Multiple M's (OS threads) run in the scheduler
 * - Each M tries to acquire a P to run work
 * - Each P has a runqueue of G's to execute
 * - When a G yields/blocks, the P runs the next G
 */

class Scheduler {
public:
  enum class GoroutineState {
    Runnable,      // Ready to execute
    Running,       // Currently executing
    Waiting,       // Yielded/blocked
    Done,          // Finished execution
    Created        // Created but not yet runnable
  };

  struct Goroutine {
    uint32_t id;
    std::function<Value()> task;          // Task to execute
    GoroutineState state;
    
    // Execution state for yield/resume
    uint32_t ip;                          // Instruction pointer
    std::vector<Value> stack;             // Operand stack
    std::vector<Value> locals;            // Local variables
    std::vector<Value> yield_values;      // Values yielded
    
    // Timing information
    std::chrono::steady_clock::time_point created_time;
    std::chrono::steady_clock::time_point resume_time;  // For sleep()/waiting
    
    // Metadata
    std::string name;
    uint32_t parent_id = 0;               // Parent goroutine ID
    
    Goroutine(uint32_t id_, std::function<Value()> task_)
        : id(id_), task(task_), state(GoroutineState::Created),
          created_time(std::chrono::steady_clock::now()) {}
  };

  struct Processor {
    uint32_t id;
    std::deque<std::shared_ptr<Goroutine>> runqueue;
    std::shared_ptr<Goroutine> current_g;
    bool idle = true;
    
    Processor(uint32_t id_) : id(id_) {}
  };

  // Singleton interface
  static Scheduler &instance();

  Scheduler(const Scheduler &) = delete;
  Scheduler &operator=(const Scheduler &) = delete;

  // Goroutine management
  uint32_t spawn(std::function<Value()> task, const std::string &name = "");
  void yieldG(std::shared_ptr<Goroutine> g);
  void resumeG(std::shared_ptr<Goroutine> g);
  std::shared_ptr<Goroutine> getGoroutine(uint32_t id);
  
  // Scheduler control
  void start(size_t num_processors = 0);  // 0 = default (CPU count)
  void stop();
  bool isRunning() const { return running_.load(); }
  
  // Wait for all goroutines to complete
  void waitAll();
  
  // Get current processor
  std::shared_ptr<Processor> getCurrentProcessor();
  
  // Debugging
  size_t goroutineCount() const;
  size_t processorCount() const;

private:
  Scheduler();
  ~Scheduler();

  // Machine thread worker
  void machineWorker(std::shared_ptr<Processor> p);
  
  // Processor scheduling
  std::shared_ptr<Processor> getIdleProcessor();
  void scheduleGoroutine(std::shared_ptr<Goroutine> g);
  
  // State management
  std::atomic<bool> running_{false};
  std::atomic<bool> shutdown_{false};
  
  // Goroutine management
  std::unordered_map<uint32_t, std::shared_ptr<Goroutine>> goroutines_;
  std::queue<std::shared_ptr<Goroutine>> global_runqueue_;
  mutable std::mutex goroutines_mutex_;
  uint32_t next_goroutine_id_ = 1;
  
  // Processors
  std::vector<std::shared_ptr<Processor>> processors_;
  mutable std::mutex processors_mutex_;
  
  // Machines (OS threads)
  std::vector<std::thread> machines_;
  
  // Synchronization
  std::condition_variable scheduler_cv_;
  mutable std::mutex scheduler_mutex_;
  std::condition_variable runqueue_cv_;
  
  // Thread-local current processor
  static thread_local std::shared_ptr<Processor> current_processor_;
};

} // namespace havel::compiler
