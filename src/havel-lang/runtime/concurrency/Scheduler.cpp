#include "Scheduler.hpp"
#include "Fiber.hpp"
#include "../../../utils/Logger.hpp"
#include <algorithm>
#include <thread>

namespace havel::compiler {

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

uint32_t Scheduler::spawn(uint32_t function_id, const std::vector<Value>& args,
	uint32_t closure_id, const std::string& name, FiberPriority priority) {
	auto g = std::make_unique<Scheduler::Goroutine>(next_goroutine_id_++, name, priority);
	g->function_id = function_id;
	g->closure_id = closure_id;
	g->state = GoroutineState::Created;

	g->fiber = new Fiber(g->id, function_id, 0, name);
	if (closure_id > 0) {
		auto& frame = g->fiber->currentFrame();
		frame.closure_id = closure_id;
		frame.arg_count = static_cast<uint32_t>(args.size());
	}

	g->locals = args;

	for (const auto& arg : args) {
		g->fiber->stack.push(arg);
	}

	uint32_t g_id = g->id;

	{
		std::lock_guard<std::mutex> lock(goroutines_mutex_);
		goroutines_[g_id] = std::move(g);
	}

	{
		std::lock_guard<std::mutex> lock(priority_mutex_);
		if (priority == FiberPriority::HOTKEY) {
			hotkey_queue_.push_front(goroutines_[g_id].get());
		} else if (priority == FiberPriority::BACKGROUND) {
			background_queue_.push_back(goroutines_[g_id].get());
		} else {
			runnable_queue_.push_back(goroutines_[g_id].get());
		}
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

Scheduler::Goroutine* Scheduler::pickNext() {
	Goroutine* result = nullptr;

	{
		std::lock_guard<std::mutex> lock(priority_mutex_);

		// Priority order: hotkey → normal → background
		// Hotkey queue: immediate execution (keyboard/mouse interrupts)
		while (!hotkey_queue_.empty()) {
			auto* g = hotkey_queue_.front();
			hotkey_queue_.pop_front();

			if (!g) continue;
			if (g->state == GoroutineState::Done) continue;
			if (g->fiber && g->fiber->state == FiberState::DONE) {
				g->state = GoroutineState::Done;
				continue;
			}
			if (g->state == GoroutineState::Runnable || g->state == GoroutineState::Created) {
				result = g;
				break;
			}
		}

		// Normal queue: standard cooperative tasks
		if (!result) {
			while (!runnable_queue_.empty()) {
				auto* g = runnable_queue_.front();
				runnable_queue_.pop_front();

				if (!g) continue;
				if (g->state == GoroutineState::Done) continue;
				if (g->fiber && g->fiber->state == FiberState::DONE) {
					g->state = GoroutineState::Done;
					continue;
				}
				if (g->state == GoroutineState::Runnable || g->state == GoroutineState::Created) {
					result = g;
					break;
				}
			}
		}

		// Background queue: low-priority work
		if (!result) {
			while (!background_queue_.empty()) {
				auto* g = background_queue_.front();
				background_queue_.pop_front();

				if (!g) continue;
				if (g->state == GoroutineState::Done) continue;
				if (g->fiber && g->fiber->state == FiberState::DONE) {
					g->state = GoroutineState::Done;
					continue;
				}
				if (g->state == GoroutineState::Runnable || g->state == GoroutineState::Created) {
					result = g;
					break;
				}
			}
		}
	}

	if (result) {
		result->state = GoroutineState::Running;
		current_ = result;
	}

	return result;
}

void Scheduler::suspend(Scheduler::Goroutine* g, SuspensionReason reason) {
	if (!g) return;

	g->state = GoroutineState::Suspended;
	g->suspension_reason = reason;
}

void Scheduler::unpark(Scheduler::Goroutine* g) {
	if (!g) return;

	if (g->state != GoroutineState::Suspended) {
		return;
	}

	g->state = GoroutineState::Runnable;
	g->suspension_reason = SuspensionReason::None;

	{
		std::lock_guard<std::mutex> lock(priority_mutex_);
		if (g->priority == FiberPriority::HOTKEY) {
			hotkey_queue_.push_back(g);
		} else if (g->priority == FiberPriority::BACKGROUND) {
			background_queue_.push_back(g);
		} else {
			runnable_queue_.push_back(g);
		}
	}
}

void Scheduler::start() {
	running_.store(true);
	shutdown_.store(false);
}

void Scheduler::stop() {
	shutdown_.store(true);
	running_.store(false);

	{
		std::lock_guard<std::mutex> lock(goroutines_mutex_);
		for (auto& [id, g] : goroutines_) {
			g->state = GoroutineState::Done;
		}
	}
}

void Scheduler::waitAll() {
	while (running_.load()) {
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

		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
}

size_t Scheduler::goroutineCount() const {
	std::lock_guard<std::mutex> lock(goroutines_mutex_);
	return goroutines_.size();
}

size_t Scheduler::runnableCount() const {
	std::lock_guard<std::mutex> lock(priority_mutex_);
	return hotkey_queue_.size() + runnable_queue_.size() + background_queue_.size();
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
	if (!g) return;
	if (g->state == GoroutineState::Done) return;
	if (g->fiber && g->fiber->state == FiberState::DONE) {
		g->state = GoroutineState::Done;
		return;
	}
	g->state = GoroutineState::Runnable;

	std::lock_guard<std::mutex> lock(priority_mutex_);
	if (g->priority == FiberPriority::HOTKEY) {
		hotkey_queue_.push_back(g);
	} else if (g->priority == FiberPriority::BACKGROUND) {
		background_queue_.push_back(g);
	} else {
		runnable_queue_.push_back(g);
	}
}

void Scheduler::yieldCurrentAndCheckTimers() {
	Goroutine* g = current_;
	if (!g) return;
	if (g->state == GoroutineState::Done) return;
	if (g->fiber && g->fiber->state == FiberState::DONE) {
		g->state = GoroutineState::Done;
		clearCurrent();
		return;
	}
	g->state = GoroutineState::Runnable;

	{
		std::lock_guard<std::mutex> lock(priority_mutex_);
		if (g->priority == FiberPriority::HOTKEY) {
			hotkey_queue_.push_back(g);
		} else if (g->priority == FiberPriority::BACKGROUND) {
			background_queue_.push_back(g);
		} else {
			runnable_queue_.push_back(g);
		}
	}
	clearCurrent();
}

void Scheduler::clearCurrent() {
	current_ = nullptr;
}

void Scheduler::addActionFiber(Fiber* fiber, FiberPriority priority) {
	if (!fiber) return;

	auto g = std::make_unique<Scheduler::Goroutine>(fiber->id, fiber->name, priority);

	g->function_id = fiber->current_function_id;
	g->state = GoroutineState::Runnable;
	g->fiber = fiber;

	uint32_t g_id = g->id;
	{
		std::lock_guard<std::mutex> lock(goroutines_mutex_);
		goroutines_[g_id] = std::move(g);
	}
	{
		std::lock_guard<std::mutex> lock(priority_mutex_);
		// Hotkey fibers are prepended for immediate execution
		if (priority == FiberPriority::HOTKEY) {
			hotkey_queue_.push_front(goroutines_[g_id].get());
		} else if (priority == FiberPriority::BACKGROUND) {
			background_queue_.push_back(goroutines_[g_id].get());
		} else {
			runnable_queue_.push_back(goroutines_[g_id].get());
		}
	}
}

bool Scheduler::hasRunnableFibers() const {
    std::lock_guard<std::mutex> lock(priority_mutex_);
    if (!hotkey_queue_.empty()) return true;
    if (!runnable_queue_.empty()) return true;
    if (!background_queue_.empty()) return true;
    return false;
}

size_t Scheduler::wakeSleepingGoroutines() {
    auto now = std::chrono::steady_clock::now();
    size_t woken = 0;

    std::lock_guard<std::mutex> lock(goroutines_mutex_);
    for (auto& [id, g] : goroutines_) {
        if (g->state != GoroutineState::Suspended) continue;
        if (g->suspension_reason != SuspensionReason::SleepWait) continue;
        if (g->resume_at_time == std::chrono::steady_clock::time_point{}) continue;
        if (now < g->resume_at_time) continue;

        g->state = GoroutineState::Runnable;
        g->suspension_reason = SuspensionReason::None;
        woken++;

        std::lock_guard<std::mutex> plock(priority_mutex_);
        if (g->priority == FiberPriority::HOTKEY) {
            hotkey_queue_.push_back(g.get());
        } else if (g->priority == FiberPriority::BACKGROUND) {
            background_queue_.push_back(g.get());
        } else {
            runnable_queue_.push_back(g.get());
        }
    }

    return woken;
}

void Scheduler::deferToVM(DeferredAction fn) {
  std::lock_guard<std::mutex> lock(deferred_mutex_);
  deferred_actions_.push_back(std::move(fn));
}

size_t Scheduler::drainDeferredCallbacks() {
  std::deque<DeferredAction> acts;
  {
    std::lock_guard<std::mutex> lock(deferred_mutex_);
    acts = std::move(deferred_actions_);
  }

  for (auto& fn : acts) {
    try {
      fn();
    } catch (const std::exception& e) {
        ::havel::warn("Scheduler: deferred action threw: {}", e.what());
    }
  }

  return acts.size();
}

} // namespace havel::compiler