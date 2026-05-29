#include "VM.hpp"
#include "VMInternals.hpp"
#include "../../../utils/Logger.hpp"
#include "../../utils/ErrorPrinter.hpp"
#include "../runtime/HostBridge.hpp"
#include "../runtime/RuntimeSupport.hpp"
#include "../../runtime/concurrency/DependencyTracker.hpp"
#include "../../runtime/concurrency/WatcherRegistry.hpp"
#include "../../runtime/concurrency/Fiber.hpp"
#include "../prototypes/PrototypeRegistry.hpp"
#include "core/config/ConfigManager.hpp"
#include <cmath>
#include <iostream>
#include <set>
#include <sstream>
#include <algorithm>
#include <numeric>
#include <regex>

namespace havel::compiler {

bool VM::execConcurrencyOp(const Instruction &instruction) {
	switch (instruction.opcode) {
  // CONCURRENCY PRIMITIVES
  // ============================================================================

case OpCode::THREAD_SPAWN: {
			Value func_val = popStack();
			if (!func_val.isClosureId() && !func_val.isFunctionObjId()) {
				COMPILER_THROW("THREAD_SPAWN expects a function");
			}
			Value result = invokeHostFunctionDirect("thread_spawn", {func_val});
			if (result.isNull()) {
				result = invokeHostFunctionDirect("thread.spawn", {func_val});
			}
			if (result.isNull()) {
				uint32_t thread_id = heap_.allocateThread();
				result = Value::makeThreadId(thread_id);
			}
			pushStack(result);
			break;
		}

  case OpCode::THREAD_JOIN: {
    // Join thread and wait for completion (non-blocking via fiber suspension)
    // Phase 3B-7: Instead of blocking thread.join(), suspend the fiber
    // and enqueue a callback to unpark when thread completes
    Value thread_val = popStack();
    if (!thread_val.isThreadId()) {
      COMPILER_THROW("THREAD_JOIN expects a thread");
    }
    
    uint32_t thread_id = thread_val.asThreadId();
    
    // Set suspension request - signals executeOneStep to suspend the fiber
    // Context pointer carries the thread_id for the callback to use
    suspension_requested_ = true;
    suspension_reason_ = static_cast<uint8_t>(SuspensionReason::THREAD_JOIN);
    suspension_context_ = reinterpret_cast<void*>(static_cast<uintptr_t>(thread_id));
    
    // The actual suspension will happen in executeOneStep after this instruction
    // For now, push null as the return value (will be used if thread already done)
    pushStack(Value::makeNull());
    break;
  }

case OpCode::THREAD_SEND: {
			Value message = popStack();
			Value thread_val = popStack();
			if (!thread_val.isThreadId()) {
				COMPILER_THROW("THREAD_SEND expects a thread");
			}
			Value result = invokeHostFunctionDirect("thread_send", {thread_val, message});
			if (result.isNull()) {
				result = invokeHostFunctionDirect("thread.send", {thread_val, message});
			}
			pushStack(result);
			break;
		}

		case OpCode::THREAD_RECEIVE: {
			Value thread_val = popStack();
			if (!thread_val.isThreadId()) {
				COMPILER_THROW("THREAD_RECEIVE expects a thread");
			}
			Value result = invokeHostFunctionDirect("thread_receive", {thread_val});
			if (result.isNull()) {
				result = invokeHostFunctionDirect("thread.receive", {thread_val});
			}
			pushStack(result);
			break;
		}

case OpCode::INTERVAL_START: {
			Value func_val = popStack();
			Value duration_val = popStack();

			if (!func_val.isClosureId() && !func_val.isFunctionObjId()) {
				COMPILER_THROW("INTERVAL_START expects a function");
			}

			if (!duration_val.isInt()) {
				COMPILER_THROW("INTERVAL_START expects duration in milliseconds");
			}

			Value result = invokeHostFunctionDirect("interval_start", {duration_val, func_val});
			if (result.isNull()) {
				result = invokeHostFunctionDirect("interval.start", {duration_val, func_val});
			}
			if (result.isNull()) {
				uint32_t interval_id = heap_.allocateInterval();
				result = Value::makeIntervalId(interval_id);
			}
			pushStack(result);
			break;
		}

		case OpCode::INTERVAL_STOP: {
			Value interval_val = popStack();

			if (!interval_val.isIntervalId()) {
				COMPILER_THROW("INTERVAL_STOP expects an interval");
			}

			Value result = invokeHostFunctionDirect("interval_stop", {interval_val});
			if (result.isNull()) {
				result = invokeHostFunctionDirect("interval.stop", {interval_val});
			}
			pushStack(result);
			break;
		}

case OpCode::TIMEOUT_START: {
			Value func_val = popStack();
			Value delay_val = popStack();

			if (!func_val.isClosureId() && !func_val.isFunctionObjId()) {
				COMPILER_THROW("TIMEOUT_START expects a function");
			}

			if (!delay_val.isInt()) {
				COMPILER_THROW("TIMEOUT_START expects delay in milliseconds");
			}

			Value result = invokeHostFunctionDirect("timeout_start", {delay_val, func_val});
			if (result.isNull()) {
				result = invokeHostFunctionDirect("timeout.start", {delay_val, func_val});
			}
			if (result.isNull()) {
				uint32_t timeout_id = heap_.allocateTimeout();
				result = Value::makeTimeoutId(timeout_id);
			}
			pushStack(result);
			break;
		}

		case OpCode::TIMEOUT_CANCEL: {
			Value timeout_val = popStack();

			if (!timeout_val.isTimeoutId()) {
				COMPILER_THROW("TIMEOUT_CANCEL expects a timeout");
			}

			Value result = invokeHostFunctionDirect("timeout_cancel", {timeout_val});
			if (result.isNull()) {
				result = invokeHostFunctionDirect("timeout.cancel", {timeout_val});
			}
			pushStack(result);
			break;
		}

  // ============================================================================
  // COROUTINES
  // ============================================================================

    case OpCode::YIELD: {
        Value yield_value = Value::makeNull();
        if (!stack.empty()) {
            yield_value = popStack();
        }

            if (current_coroutine_id_ != UINT32_MAX) {
                auto *co = heap_.coroutine(current_coroutine_id_);
                if (co) {
                    co->ip = currentFrame().ip + 1;
                    co->locals = locals;

                    // Save coroutine's current stack
                    co->stack.clear();
                    {
                        std::vector<Value> tmp;
                        while (!stack.empty()) {
                            tmp.push_back(stack.top());
                            stack.pop();
                        }
                        for (auto it = tmp.rbegin(); it != tmp.rend(); ++it) {
                            co->stack.push_back(*it);
                        }
                    }

                    co->state = GCHeap::Coroutine::Waiting;

                    // Pop caller frame from the coroutine's caller stack
                    if (!co->caller_stack.empty()) {
                        auto &caller = co->caller_stack.back();
                        frame_count_ = caller.frame_count;
                        locals = caller.locals;
                        current_coroutine_id_ = caller.coroutine_id;

                        currentFrame().ip = caller.ip;

                        stack = std::stack<Value>();
                        for (auto it = caller.stack.begin(); it != caller.stack.end(); ++it) {
                            stack.push(*it);
                        }

                        co->caller_stack.pop_back();
                    }

		pushStack(yield_value);
		return true;
                }
            }

        // Non-coroutine yield
        pushStack(yield_value);
        break;
}

    case OpCode::YIELD_RESUME: {
        // Resume yielded coroutine
        Value coroutine_val = popStack();

        if (!coroutine_val.isCoroutineId()) {
            COMPILER_THROW("YIELD_RESUME expects a coroutine");
        }

        uint32_t coroutine_id = coroutine_val.asCoroutineId();
        auto *co = heap_.coroutine(coroutine_id);

        if (!co) {
            COMPILER_THROW("YIELD_RESUME: coroutine not found");
        }

        if (co->state == GCHeap::Coroutine::Done) {
            pushStack(Value::makeNull());
            break;
        }

            // Push current caller's state onto the coroutine's caller stack
            {
                GCHeap::CallerFrame cf;
                cf.coroutine_id = current_coroutine_id_;
                cf.frame_count = frame_count_;
                cf.ip = currentFrame().ip + 1;
                cf.locals = locals;
                {
                    std::vector<Value> tmp;
                    while (!stack.empty()) {
                        tmp.push_back(stack.top());
                        stack.pop();
                    }
                    for (auto it = tmp.rbegin(); it != tmp.rend(); ++it) {
                        cf.stack.push_back(*it);
                    }
                }
                co->caller_stack.push_back(std::move(cf));
            }
            co->parent_locals_size = locals.size();

        // Switch to the coroutine
        current_coroutine_id_ = coroutine_id;

        // Restore coroutine's stack (stack[0]=bottom, [N-1]=top)
        stack = std::stack<Value>();
        for (auto it = co->stack.begin(); it != co->stack.end(); ++it) {
            stack.push(*it);
        }

        // Restore coroutine's locals
        locals = co->locals;

        // Clear stale immutable_locals_ from the previous frame that used
        // different absolute indices; coroutine's STORE_IMMUT_VAR opcodes
        // will repopulate during execution
        immutable_locals_.clear();

        // Restore coroutine's instruction pointer
        currentFrame().ip = co->ip;

        // Set state to Runnable
        co->state = GCHeap::Coroutine::Runnable;

        // Push yield values from last yield (these become the "send" values
        // passed to the coroutine on resume — currently empty for generators)
        for (const auto &val : co->yield_values) {
            pushStack(val);
        }
        co->yield_values.clear();

        break;
    }

case OpCode::GO_ASYNC: {
Value call_val = popStack();

if (!call_val.isClosureId() && !call_val.isFunctionObjId()) {
COMPILER_THROW("GO_ASYNC expects a function");
}

if (scheduler_) {
uint32_t gid = spawnGoroutine(call_val, {});
pushStack(Value::makeThreadId(gid));
} else {
pushStack(Value::makeNull());
}
break;
}

 case OpCode::FIBER_AWAIT: {
 Value awaitable = popStack();

 if (awaitable.isCoroutineId()) {
 uint32_t coId = awaitable.asCoroutineId();
 auto *co = heap_.coroutine(coId);

 if (!co) {
 pushStack(Value::makeNull());
 break;
 }

 if (co->state == GCHeap::Coroutine::Done) {
 Value result = co->stack.empty() ? Value::makeNull() : co->stack.back();
 pushStack(result);
 break;
 }

 struct VMState {
 std::stack<Value> stack;
 std::vector<Value> locals;
 size_t frame_count;
 std::vector<CallFrame> frame_arena;
 uint32_t current_coroutine_id;
 };

 VMState saved;
 saved.stack = stack;
 saved.locals = locals;
 saved.frame_count = frame_count_;
 saved.frame_arena = frame_arena_;
 saved.current_coroutine_id = current_coroutine_id_;

            current_coroutine_id_ = coId;
            {
                GCHeap::CallerFrame cf;
                cf.coroutine_id = saved.current_coroutine_id;
                cf.frame_count = frame_count_;
                cf.ip = 0;
                cf.locals = locals;
                co->caller_stack.push_back(std::move(cf));
            }
            locals = co->locals;
            immutable_locals_.clear();

 const auto *chunk = current_chunk;
 const auto *func = chunk ? chunk->getFunction(co->function_index) : nullptr;
 if (!func) {
 stack = saved.stack;
 frame_count_ = saved.frame_count;
 locals = saved.locals;
 immutable_locals_.clear();
 frame_arena_ = saved.frame_arena;
 current_coroutine_id_ = saved.current_coroutine_id;
 COMPILER_THROW("FIBER_AWAIT: function not found for coroutine");
 }

        uint32_t coroutine_stack_depth = static_cast<uint32_t>(stack.size());
    if (frame_arena_.size() <= frame_count_) {
        CallFrame cf;
        cf.function = func;
        cf.chunk = current_chunk;
        cf.ip = co->ip;
        cf.locals_base = 0;
        cf.closure_id = co->closure_id;
        cf.stack_depth = coroutine_stack_depth;
        frame_arena_.push_back(std::move(cf));
    } else {
        frame_arena_[frame_count_].function = func;
        frame_arena_[frame_count_].chunk = current_chunk;
        frame_arena_[frame_count_].ip = co->ip;
        frame_arena_[frame_count_].locals_base = 0;
        frame_arena_[frame_count_].closure_id = co->closure_id;
        frame_arena_[frame_count_].stack_depth = coroutine_stack_depth;
        }
        frame_count_++;
 co->state = GCHeap::Coroutine::Runnable;

 size_t caller_frame_count = saved.frame_count;
 size_t max_steps = 1000000;
 size_t steps = 0;

 while (co->state != GCHeap::Coroutine::Done && steps < max_steps) {
 if (frame_count_ <= caller_frame_count && co->state != GCHeap::Coroutine::Done) {
 // Coroutine yielded or slept — check if it's a timed sleep
 if (co->state == GCHeap::Coroutine::Waiting &&
 co->resume_at_time > std::chrono::steady_clock::time_point{}) {
 auto now = std::chrono::steady_clock::now();
 if (co->resume_at_time > now) {
 auto sleep_remaining = co->resume_at_time - now;
 while (std::chrono::steady_clock::now() < co->resume_at_time) {
 if (timer_check_func_) timer_check_func_();
 auto rem = co->resume_at_time - std::chrono::steady_clock::now();
 auto rem_ms = std::chrono::duration_cast<std::chrono::milliseconds>(rem);
 if (rem_ms.count() > 0) {
 std::this_thread::sleep_for(std::chrono::milliseconds(
 std::min(static_cast<int64_t>(1), rem_ms.count())));
 }
 }
 }
  // Resume coroutine after sleep
  co->state = GCHeap::Coroutine::Runnable;
  locals = co->locals;
  immutable_locals_.clear();
 const auto *resume_chunk = current_chunk;
 const auto *resume_func = resume_chunk ? resume_chunk->getFunction(co->function_index) : nullptr;
        if (resume_func) {
            uint32_t resume_stack_depth = static_cast<uint32_t>(stack.size());
        if (frame_arena_.size() <= frame_count_) {
            CallFrame cf;
            cf.function = resume_func;
            cf.chunk = resume_chunk;
            cf.ip = co->ip;
            cf.locals_base = 0;
            cf.closure_id = co->closure_id;
            cf.stack_depth = resume_stack_depth;
            frame_arena_.push_back(std::move(cf));
        } else {
            frame_arena_[frame_count_].function = resume_func;
            frame_arena_[frame_count_].chunk = resume_chunk;
            frame_arena_[frame_count_].ip = co->ip;
                frame_arena_[frame_count_].locals_base = 0;
                frame_arena_[frame_count_].closure_id = co->closure_id;
                frame_arena_[frame_count_].stack_depth = resume_stack_depth;
            }
            frame_count_++;
 pushStack(Value::makeNull());
 }
 continue;
 }
 break;
 }

 if (frame_count_ <= caller_frame_count) {
 break;
 }

 size_t afi = frame_count_ - 1;
 const auto *cur_func = frame_arena_[afi].function;
 uint32_t cur_ip = frame_arena_[afi].ip;

 if (cur_ip >= cur_func->instructions.size()) {
 executeInstruction(Instruction{OpCode::RETURN});
 } else {
 const auto &inst = cur_func->instructions[cur_ip];
 executeInstruction(inst);
 if (afi < frame_count_ && frame_arena_[afi].ip == cur_ip) {
 frame_arena_[afi].ip++;
 }
 }

 processPendingCalls();
 steps++;

 if (suspension_requested_) {
 suspension_requested_ = false;
 break;
 }
 }

 Value result;
 if (co->state == GCHeap::Coroutine::Done && !stack.empty()) {
 result = popStack();
 } else if (co->state == GCHeap::Coroutine::Done) {
 result = co->stack.empty() ? Value::makeNull() : co->stack.back();
 } else if (co->state == GCHeap::Coroutine::Waiting && !stack.empty()) {
 result = popStack();
 } else {
 result = awaitable;
 }

 stack = saved.stack;
 frame_count_ = saved.frame_count;
 locals = saved.locals;
 immutable_locals_.clear();
 frame_arena_ = saved.frame_arena;
 current_coroutine_id_ = saved.current_coroutine_id;

 pushStack(result);
 break;
 }

 // <- thread_id: join the thread, return its result
 if (awaitable.isThreadId()) {
 uint32_t tid = awaitable.asThreadId();
 auto *threadObj = heap_.thread(tid);
 if (threadObj) {
 threadObj->stop();
 }
 // Check if a result was stored for this thread
 auto it = thread_results_.find(tid);
 Value result = (it != thread_results_.end()) ? it->second : Value::makeNull();
 if (it != thread_results_.end()) thread_results_.erase(it);
 pushStack(result);
 break;
 }

 // <- interval_id: wait for next tick, return callback result
 if (awaitable.isIntervalId()) {
 uint32_t iid = awaitable.asIntervalId();
 // Wait for interval to produce a result by polling
 // In a real async VM, this would suspend the fiber.
 // For now, busy-wait with processPendingCalls
 while (interval_results_.find(iid) == interval_results_.end()) {
 if (timer_check_func_) timer_check_func_();
 std::this_thread::sleep_for(std::chrono::milliseconds(1));
 }
 auto it = interval_results_.find(iid);
 Value result = it->second;
 interval_results_.erase(it);
 pushStack(result);
 break;
 }

 // <- timeout_id: wait for timeout to fire, return callback result
 if (awaitable.isTimeoutId()) {
 uint32_t tid = awaitable.asTimeoutId();
 auto *timeoutObj = heap_.timeout(tid);
 if (timeoutObj) {
 // The timeout is already scheduled. We need to wait for it.
 // Since Timeout::cancel() joins the thread, we can call it
 // to wait for completion. If it was already cancelled, this
 // is a no-op.
 timeoutObj->cancel();
 }
 auto it = timeout_results_.find(tid);
 Value result = (it != timeout_results_.end()) ? it->second : Value::makeNull();
 if (it != timeout_results_.end()) timeout_results_.erase(it);
 pushStack(result);
 break;
 }

 // <- object with __await_result field (simulated concurrency in tests)
 if (awaitable.isObjectId()) {
 auto objRef = ObjectRef{awaitable.asObjectId(), true};
 Value result = getHostObjectField(objRef, "__await_result");
 if (!result.isNull()) {
 pushStack(result);
 break;
 }
 }

 // Non-awaitable value: push through (identity for resolved values)
 pushStack(awaitable);
 break;
 }

 case OpCode::FIBER_SLEEP: {
 Value ms_val = popStack();
 int ms = toInt(ms_val);

 if (current_coroutine_id_ != UINT32_MAX) {
 // Inside a coroutine: yield with a resume time
 auto *co = heap_.coroutine(current_coroutine_id_);
 if (co && frame_count_ > 0) {
 co->ip = currentFrame().ip + 1;
 co->locals = locals;
 co->state = GCHeap::Coroutine::Waiting;
 co->resume_at_time = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);

 auto finished = frame_arena_[frame_count_ - 1];
 frame_count_--;

 closeFrameUpvalues(static_cast<uint32_t>(finished.locals_base),
 static_cast<uint32_t>(locals.size()));
  if (locals.size() >= finished.locals_base) {
  locals.resize(finished.locals_base);
  }

  // Stale immutable_locals_ indices from the old frame are invalid
  // after locals truncation; clear for the parent frame to repopulate
  immutable_locals_.clear();

                     if (!co->caller_stack.empty()) {
                         auto &caller = co->caller_stack.back();
                         frame_count_ = caller.frame_count;
                         locals = caller.locals;
                         immutable_locals_.clear();
                         current_coroutine_id_ = caller.coroutine_id;
                        currentFrame().ip = caller.ip;
                        co->caller_stack.pop_back();
                    }

		pushStack(Value::makeNull());

		return true;
 }
 }

  // Main fiber: suspend goroutine via scheduler if available,
  // otherwise fall back to blocking sleep
  if (scheduler_) {
    suspension_requested_ = true;
    suspension_reason_ = static_cast<uint8_t>(SuspensionReason::SLEEP);
    suspension_context_ = reinterpret_cast<void*>(
        static_cast<intptr_t>(ms));
    pushStack(Value::makeNull());
    break;
  }

  // No scheduler — blocking sleep with timer processing
  {
  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  while (std::chrono::steady_clock::now() < deadline) {
  if (timer_check_func_) timer_check_func_();
  auto remaining = deadline - std::chrono::steady_clock::now();
  auto sleep_ms = std::chrono::duration_cast<std::chrono::milliseconds>(remaining);
  if (sleep_ms.count() > 0) {
  std::this_thread::sleep_for(std::chrono::milliseconds(std::min(static_cast<int64_t>(1), sleep_ms.count())));
  }
  }
  }
  pushStack(Value::makeNull());
  break;
 }

  // ============================================================================
  // CHANNELS
  // ============================================================================

case OpCode::CHANNEL_NEW: {
			Value result = invokeHostFunctionDirect("channel_new", {});
			if (result.isNull()) {
				result = invokeHostFunctionDirect("channel.new", {});
			}
			if (result.isNull()) {
				ChannelRef channel_ref = heap_.allocateChannel();
				result = Value::makeChannelId(channel_ref.id);
			}
			pushStack(result);
			break;
		}

case OpCode::CHANNEL_SEND: {
			Value value = popStack();
			Value channel_val = popStack();

			if (!channel_val.isChannelId()) {
				COMPILER_THROW("CHANNEL_SEND expects a channel");
			}

			Value result = invokeHostFunctionDirect("channel_send", {channel_val, value});
			if (result.isNull()) {
				result = invokeHostFunctionDirect("channel.send", {channel_val, value});
			}
			pushStack(result);
			break;
		}

		case OpCode::CHANNEL_RECEIVE: {
			Value channel_val = popStack();

			if (!channel_val.isChannelId()) {
				COMPILER_THROW("CHANNEL_RECEIVE expects a channel");
			}

			Value result = invokeHostFunctionDirect("channel_receive", {channel_val});
			if (result.isNull()) {
				result = invokeHostFunctionDirect("channel.receive", {channel_val});
			}
			pushStack(result);
			break;
		}

		case OpCode::CHANNEL_CLOSE: {
			Value channel_val = popStack();

			if (!channel_val.isChannelId()) {
				COMPILER_THROW("CHANNEL_CLOSE expects a channel");
			}

			Value result = invokeHostFunctionDirect("channel_close", {channel_val});
			if (result.isNull()) {
				result = invokeHostFunctionDirect("channel.close", {channel_val});
			}
			pushStack(result);
			break;
		}

	default:
		return false;
	}
	return true;
}

} // namespace havel::compiler
