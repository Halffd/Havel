#include "Scheduler.hpp"

#include <algorithm>
#include <iostream>
#include <thread>

namespace havel::compiler {

thread_local std::shared_ptr<Scheduler::Processor> Scheduler::current_processor_;

Scheduler &Scheduler::instance() {
  static Scheduler instance_;
  return instance_;
}

Scheduler::Scheduler() = default;

Scheduler::~Scheduler() {
  stop();
}

uint32_t Scheduler::spawn(std::function<Value()> task, const std::string &name) {
  auto g = std::make_shared<Goroutine>(next_goroutine_id_++, task);
  g->name = name.empty() ? "goroutine_" + std::to_string(g->id) : name;
  
  {
    std::lock_guard<std::mutex> lock(goroutines_mutex_);
    goroutines_[g->id] = g;
  }
  
  // Add to global runqueue for scheduling
  {
    std::lock_guard<std::mutex> lock(scheduler_mutex_);
    global_runqueue_.push(g);
  }
  
  runqueue_cv_.notify_one();
  return g->id;
}

void Scheduler::yieldG(std::shared_ptr<Goroutine> g) {
  if (!g) return;
  
  g->state = GoroutineState::Waiting;
  
  // Return to runqueue to be scheduled again
  if (auto p = getCurrentProcessor()) {
    p->current_g = nullptr;
    {
      std::lock_guard<std::mutex> lock(scheduler_mutex_);
      p->runqueue.push_back(g);
    }
    runqueue_cv_.notify_one();
  }
}

void Scheduler::resumeG(std::shared_ptr<Goroutine> g) {
  if (!g) return;
  
  if (g->state == GoroutineState::Waiting || g->state == GoroutineState::Created) {
    g->state = GoroutineState::Runnable;
    scheduleGoroutine(g);
  }
}

std::shared_ptr<Scheduler::Goroutine> Scheduler::getGoroutine(uint32_t id) {
  std::lock_guard<std::mutex> lock(goroutines_mutex_);
  auto it = goroutines_.find(id);
  if (it != goroutines_.end()) {
    return it->second;
  }
  return nullptr;
}

void Scheduler::start(size_t num_processors) {
  if (running_.load()) {
    return;
  }
  
  // Default to CPU count
  if (num_processors == 0) {
    num_processors = std::thread::hardware_concurrency();
    if (num_processors == 0) num_processors = 4;
  }
  
  running_ = true;
  shutdown_ = false;
  
  // Create processors and start machine workers
  {
    std::lock_guard<std::mutex> lock(processors_mutex_);
    processors_.clear();
    
    for (size_t i = 0; i < num_processors; ++i) {
      auto p = std::make_shared<Processor>(i);
      processors_.push_back(p);
      
      // Start machine worker for this processor
      machines_.emplace_back(&Scheduler::machineWorker, this, p);
    }
  }
}

void Scheduler::stop() {
  if (!running_.load()) {
    return;
  }
  
  running_ = false;
  shutdown_ = true;
  
  runqueue_cv_.notify_all();
  
  for (auto &m : machines_) {
    if (m.joinable()) {
      m.join();
    }
  }
  
  machines_.clear();
  {
    std::lock_guard<std::mutex> lock(processors_mutex_);
    processors_.clear();
  }
}

void Scheduler::waitAll() {
  while (running_) {
    {
      std::lock_guard<std::mutex> lock(goroutines_mutex_);
      
      // Count non-done goroutines
      size_t active_count = 0;
      for (const auto &[id, g] : goroutines_) {
        if (g->state != GoroutineState::Done) {
          active_count++;
        }
      }
      
      if (active_count == 0) {
        break;
      }
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

std::shared_ptr<Scheduler::Processor> Scheduler::getCurrentProcessor() {
  return current_processor_;
}

size_t Scheduler::goroutineCount() const {
  std::lock_guard<std::mutex> lock(goroutines_mutex_);
  return goroutines_.size();
}

size_t Scheduler::processorCount() const {
  std::lock_guard<std::mutex> lock(processors_mutex_);
  return processors_.size();
}

std::shared_ptr<Scheduler::Processor> Scheduler::getIdleProcessor() {
  std::lock_guard<std::mutex> lock(processors_mutex_);
  
  for (auto &p : processors_) {
    if (p->idle && (p->runqueue.empty() || !p->current_g)) {
      return p;
    }
  }
  
  // If no idle processor, return least busy one
  if (!processors_.empty()) {
    auto least_busy = std::min_element(
        processors_.begin(), processors_.end(),
        [](const auto &a, const auto &b) {
          return a->runqueue.size() < b->runqueue.size();
        });
    return *least_busy;
  }
  
  return nullptr;
}

void Scheduler::scheduleGoroutine(std::shared_ptr<Goroutine> g) {
  if (!g) return;
  
  auto p = getIdleProcessor();
  if (p) {
    {
      std::lock_guard<std::mutex> lock(scheduler_mutex_);
      p->runqueue.push_back(g);
    }
    runqueue_cv_.notify_one();
  }
}

void Scheduler::machineWorker(std::shared_ptr<Processor> p) {
  current_processor_ = p;
  
  while (!shutdown_) {
    std::shared_ptr<Goroutine> g;
    
    // Try to get a goroutine from this processor's runqueue
    {
      std::unique_lock<std::mutex> lock(scheduler_mutex_);
      
      // Wait for work or shutdown signal
      runqueue_cv_.wait_for(lock, std::chrono::milliseconds(10), [this, p]() {
        return shutdown_ || !p->runqueue.empty() || !global_runqueue_.empty();
      });
      
      // Try processor's local runqueue first
      if (!p->runqueue.empty()) {
        g = p->runqueue.front();
        p->runqueue.pop_front();
      }
      // Try global runqueue if local is empty
      else if (!global_runqueue_.empty()) {
        g = global_runqueue_.front();
        global_runqueue_.pop();
      }
    }
    
    if (g && g->state != GoroutineState::Done) {
      p->current_g = g;
      p->idle = false;
      g->state = GoroutineState::Running;
      
      // Execute the goroutine task
      try {
        Value result = g->task();
        g->state = GoroutineState::Done;
      } catch (const std::exception &e) {
        g->state = GoroutineState::Done;
        // Log error but don't crash
      }
      
      p->current_g = nullptr;
    } else if (shutdown_) {
      break;
    } else {
      p->idle = true;
    }
  }
  
  current_processor_ = nullptr;
}

} // namespace havel::compiler
