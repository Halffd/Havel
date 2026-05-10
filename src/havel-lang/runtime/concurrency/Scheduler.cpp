#include "Scheduler.hpp"
#include "Fiber.hpp"
#include "../../../utils/Logger.hpp"
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

Scheduler::Goroutine::~Goroutine() {
    if (fiber) {
        delete fiber;
    }
}

// ===== GOROUTINE LIFECYCLE =====

uint32_t Scheduler::spawn(uint32_t function_id, const std::vector<Value>& args,
                          uint32_t closure_id, const std::string& name) {
    auto g = std::make_unique<Scheduler::Goroutine>(next_goroutine_id_++, name);
    g->function_id = function_id;
    g->closure_id = closure_id;
    g->state = GoroutineState::Created;
    
    
    // This is the actual execution context that will be loaded into the VM
    g->fiber = new Fiber(g->id, function_id, 0, name);
    if (closure_id > 0) {
        auto& frame = g->fiber->currentFrame();
        frame.closure_id = closure_id;
        frame.arg_count = static_cast<uint32_t>(args.size());
    }
    
    // Store arguments in locals
    // Caller responsible for setting up locals correctly
    g->locals = args;
    
    // Also push arguments to fiber stack for consistency
    for (const auto& arg : args) {
        g->fiber->stack.push(arg);
    }
    
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

    while (!runnable_.empty()) {
        auto* g = runnable_.front();
        runnable_.pop_front();

        if (!g) {
            continue;
        }

        if (g->state == GoroutineState::Done) {
            continue;
        }

        if (g->fiber && g->fiber->state == FiberState::DONE) {
            g->state = GoroutineState::Done;
            continue;
        }

        g->state = GoroutineState::Running;
        current_ = g;
        return g;
    }

    return nullptr;
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

void Scheduler::attachFiber(uint32_t goroutine_id, Fiber* fiber) {
	std::lock_guard<std::mutex> lock(goroutines_mutex_);
	auto it = goroutines_.find(goroutine_id);
	if (it != goroutines_.end()) {
		it->second->fiber = fiber;
	}
}
void Scheduler::yield(Goroutine* g) {
    g->state = GoroutineState::Runnable;

    std::lock_guard<std::mutex> lock(runnable_mutex_);
    runnable_.push_back(g);
}
void Scheduler::addActionFiber(Fiber* fiber) {
    if (!fiber) return;
    
    ::havel::debug("[Scheduler] Adding action Fiber {} with name '{}'", fiber->id, fiber->name);
    ::havel::debug("[Scheduler] addActionFiber called on instance: {}", (void*)this);

    // Create a goroutine wrapper
    auto g = std::make_unique<Scheduler::Goroutine>(fiber->id, fiber->name);
    
    // Set the state
    g->function_id = fiber->current_function_id; 
    g->state = GoroutineState::Runnable;
    g->fiber = fiber;
    
    // Add to goroutines map and runnable queue
    uint32_t g_id = g->id;
    {
        std::lock_guard<std::mutex> lock(goroutines_mutex_);
        goroutines_[g_id] = std::move(g);
        ::havel::debug("[Scheduler] Fiber {} added to goroutines map", g_id);
    }
    {
        std::lock_guard<std::mutex> lock(runnable_mutex_);
        runnable_.push_back(goroutines_[g_id].get());
        ::havel::debug("[Scheduler] Fiber {} added to runnable queue (runnable count: {})", g_id, runnable_.size());
    }
}

bool Scheduler::hasRunnableFibers() const {
    std::lock_guard<std::mutex> lock(runnable_mutex_);
    return !runnable_.empty();
}

} // namespace havel::compiler
