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
	uint32_t closure_id, const std::string& name) {
	auto g = std::make_unique<Scheduler::Goroutine>(next_goroutine_id_++, name);
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

Scheduler::Goroutine* Scheduler::pickNext() {
	Goroutine* result = nullptr;

	{
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

			if (g->state != GoroutineState::Runnable && g->state != GoroutineState::Created) {
				continue;
			}

			result = g;
			break;
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
		std::lock_guard<std::mutex> lock(runnable_mutex_);
		runnable_.push_back(g);
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
	if (!g) return;
	if (g->state == GoroutineState::Done) return;
	if (g->fiber && g->fiber->state == FiberState::DONE) {
		g->state = GoroutineState::Done;
		return;
	}
	g->state = GoroutineState::Runnable;

	std::lock_guard<std::mutex> lock(runnable_mutex_);
	runnable_.push_back(g);
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
		std::lock_guard<std::mutex> lock(runnable_mutex_);
		runnable_.push_back(g);
	}
	clearCurrent();
}

void Scheduler::clearCurrent() {
	current_ = nullptr;
}

void Scheduler::addActionFiber(Fiber* fiber) {
	if (!fiber) return;

	auto g = std::make_unique<Scheduler::Goroutine>(fiber->id, fiber->name);

	g->function_id = fiber->current_function_id;
	g->state = GoroutineState::Runnable;
	g->fiber = fiber;

	uint32_t g_id = g->id;
	{
		std::lock_guard<std::mutex> lock(goroutines_mutex_);
		goroutines_[g_id] = std::move(g);
	}
	{
		std::lock_guard<std::mutex> lock(runnable_mutex_);
		runnable_.push_front(goroutines_[g_id].get());
	}
}

bool Scheduler::hasRunnableFibers() const {
	std::lock_guard<std::mutex> lock(runnable_mutex_);
	if (runnable_.empty()) return false;
	for (const auto* g : runnable_) {
		if (g && g->state != GoroutineState::Done) {
			if (g->fiber && g->fiber->state == FiberState::DONE) {
				continue;
			}
			return true;
		}
	}
	return false;
}

} // namespace havel::compiler
