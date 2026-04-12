#include "Scheduler.hpp"
#include <algorithm>
#include <thread>

namespace havel::compiler {

// Singleton instance
static Scheduler* g_scheduler_instance = nullptr;

Scheduler& Scheduler::instance() {
    if (!g_scheduler_instance) {
        g_scheduler_instance = new Scheduler();
    }
    return *g_scheduler_instance;
}

Scheduler::Scheduler() {
}

Scheduler::~Scheduler() {
    stop();
}

// ===== GOROUTINE LIFECYCLE =====

uint32_t Scheduler::spawn(uint32_t function_id, const std::vector<Value>& args,
                          const std::string& name) {
    auto g = std::make_unique<Scheduler::Goroutine>(next_goroutine_id_++, name);
    g->function_id = function_id;
    g->state = GoroutineState::Created;
    
    // Store arguments in locals
    // Caller responsible for setting up locals correctly
    g->locals = args;
    
    uint32_t g_id = g->id;
    
    {
        std::lock_guard<std::mutex> lock(goroutines_mutex_);
        goroutines_[g_id] = std::move(g);
    }
    
    // Make it runnable immediately
    {
        std::lock_guard<std::mutex> lock(runnable_mutex_);
        runnable_.push_back(goroutines_[g_id].get());
    }
    
    return g_id;
}

Scheduler::Goroutine* Scheduler::current() {
    return current_;
}

Scheduler::Goroutine* Scheduler::get(uint32_t id) {
    std::lock_guard<std::mutex> lock(goroutines_mutex_);
    auto it = goroutines_.find(id);
    if (it != goroutines_.end()) {
        return it->second.get();
    }
    return nullptr;
}

// ===== EXECUTION CONTROL =====

Scheduler::Goroutine* Scheduler::pickNext() {
    std::lock_guard<std::mutex> lock(runnable_mutex_);
    
    if (runnable_.empty()) {
        return nullptr;  // No runnable goroutines (all sleeping/suspended)
    }
    
    // FIFO - take from front
    Scheduler::Goroutine* g = runnable_.front();
    runnable_.pop_front();
    
    // Mark as running (but don't execute - VM does that)
    g->state = GoroutineState::Running;
    current_ = g;
    
    return g;
}

void Scheduler::suspend(Scheduler::Goroutine* g, SuspensionReason reason) {
    if (!g) return;
    
    // Mark as suspended
    g->state = GoroutineState::Suspended;
    g->suspension_reason = reason;
    
    // Note: We don't remove from runnable here because pickNext() already
    // popped it. The goroutine stays in goroutines_ map but is not in
    // runnable queue until unpark() is called.
}

void Scheduler::unpark(Scheduler::Goroutine* g) {
    if (!g) return;
    
    if (g->state != GoroutineState::Suspended) {
        return;  // Can't unpark non-suspended goroutines
    }
    
    // Transition: Suspended → Runnable
    g->state = GoroutineState::Runnable;
    g->suspension_reason = SuspensionReason::None;
    
    {
        std::lock_guard<std::mutex> lock(runnable_mutex_);
        runnable_.push_back(g);
    }
}

// ===== SCHEDULER LIFECYCLE =====

void Scheduler::start() {
    running_.store(true);
    shutdown_.store(false);
}

void Scheduler::stop() {
    shutdown_.store(true);
    running_.store(false);
    
    // Mark all as done (cleanup)
    {
        std::lock_guard<std::mutex> lock(goroutines_mutex_);
        for (auto& [id, g] : goroutines_) {
            g->state = GoroutineState::Done;
        }
    }
}

void Scheduler::waitAll() {
    while (running_.load()) {
        // Count non-done goroutines
        size_t active = 0;
        {
            std::lock_guard<std::mutex> lock(goroutines_mutex_);
            for (const auto& [id, g] : goroutines_) {
                if (g->state != GoroutineState::Done) {
                    active++;
                }
            }
        }
        
        if (active == 0) {
            break;
        }
        
        // Sleep briefly and check again
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

// ===== DIAGNOSTICS =====

size_t Scheduler::goroutineCount() const {
    std::lock_guard<std::mutex> lock(goroutines_mutex_);
    return goroutines_.size();
}

size_t Scheduler::runnableCount() const {
    std::lock_guard<std::mutex> lock(runnable_mutex_);
    return runnable_.size();
}

size_t Scheduler::suspendedCount() const {
    std::lock_guard<std::mutex> lock(goroutines_mutex_);
    size_t count = 0;
    for (const auto& [id, g] : goroutines_) {
        if (g->state == GoroutineState::Suspended) {
            count++;
        }
    }
    return count;
}

}  // namespace havel::compiler
