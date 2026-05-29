#include "VM.hpp"
#include "VMInternals.hpp"
#include "../../../utils/Logger.hpp"
#include "../../utils/ErrorPrinter.hpp"
#include "../../runtime/concurrency/Thread.hpp"
#include "../../runtime/concurrency/Fiber.hpp"
#include "../../runtime/concurrency/DependencyTracker.hpp"
#include "../../runtime/concurrency/WatcherRegistry.hpp"
#include "../../runtime/concurrency/Scheduler.hpp"
#include "../runtime/EventQueue.hpp"
#include "../runtime/HostBridge.hpp"
#include "../runtime/RuntimeSupport.hpp"
#include "compiler/core/ByteCompiler.hpp"
#include "../../lexer/Lexer.hpp"
#include "../../parser/Parser.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <queue>
#include "havel-lang/compiler/runtime/DebugUtils.hpp"

#include "VMApi.hpp"
#include "../../stdlib/ShellModule.hpp"
#include "core/config/ConfigManager.hpp"

namespace havel::compiler {

VM::VM() {
  tiering_enabled_ = envU64("HAVEL_TIERING", 0) != 0;
  tier1_threshold_ = envU64("HAVEL_TIER1_THRESHOLD", 1000);
  tier2_threshold_ = envU64("HAVEL_TIER2_THRESHOLD", 10000);
  tier2_flush_on_shutdown_ = envU64("HAVEL_TIER2_FLUSH", 0) != 0;
  registerDefaultHostFunctions();
}

VM::VM(const ::havel::HostContext &ctx) {
  tiering_enabled_ = envU64("HAVEL_TIERING", 0) != 0;
  tier1_threshold_ = envU64("HAVEL_TIER1_THRESHOLD", 1000);
  tier2_threshold_ = envU64("HAVEL_TIER2_THRESHOLD", 10000);
  tier2_flush_on_shutdown_ = envU64("HAVEL_TIER2_FLUSH", 0) != 0;
  // Store context for service access
  context_ = &ctx;
  registerDefaultHostFunctions();
}

void VM::setMaxCallDepth(size_t value) { max_call_depth_ = value; }

VM::~VM() {
  if (tier2_flush_on_shutdown_) {
    // Optional drain mode: let queued tier2 compiles finish before shutdown.
    for (;;) {
      bool empty = false;
      {
        std::lock_guard<std::mutex> lk(tier2_queue_mutex_);
        empty = tier2_queue_.empty() && tier2_queued_or_compiling_.empty();
      }
      if (empty) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  }
  tier2_worker_running_.store(false);
  if (tier2_worker_.joinable()) tier2_worker_.join();
  if (tiering_enabled_) {
    ::havel::info("[tiering] transitions: tier1={} tier2_enqueued={} tier2_compiled={} tier2_dup_skipped={}",
                  tier1_transition_count_.load(),
                  tier2_enqueue_count_.load(),
                  tier2_compile_count_.load(),
                  tier2_skip_duplicate_count_.load());
  }
  if (heap_.externalRootCount() > 0) {
        ::havel::warning("[VM][GC] {} external roots still pinned at VM shutdown", heap_.externalRootCount());
  }
}

template <typename T> T VM::getValue(const Value &value) {
  if constexpr (std::is_same_v<T, std::nullptr_t>) {
    return nullptr;
  } else if constexpr (std::is_same_v<T, bool>) {
    return value.asBool();
  } else if constexpr (std::is_same_v<T, int64_t>) {
    return value.asInt();
  } else if constexpr (std::is_same_v<T, double>) {
    return value.asDouble();
  } else if constexpr (std::is_same_v<T, std::string>) {
    // TODO: string pool lookup
    return "<string:" + std::to_string(value.asStringValId()) + ">";
  }

  COMPILER_THROW("Invalid type conversion");
}

const VM::CallFrame &VM::currentFrame() const {
  if (frame_count_ == 0) {
    COMPILER_THROW("No active call frame");
  }
  return frame_arena_[frame_count_ - 1];
}

VM::CallFrame &VM::currentFrame() {
  if (frame_count_ == 0) {
    COMPILER_THROW("No active call frame");
  }
  return frame_arena_[frame_count_ - 1];
}

Value VM::getConstant(uint32_t index) {
  auto& consts = currentFrame().function->constants;
  if (index >= consts.size()) {
    return Value::makeNull();
  }
  return consts[index];
}

VM::ExecutionState VM::saveState() const {
  ExecutionState state;
  state.stack = stack;
  state.locals = locals;
  state.frames = frame_arena_;
  state.frame_count = frame_count_;
  return state;
}

void VM::restoreState(const ExecutionState &state) {
  stack = state.stack;
  locals = state.locals;
  immutable_locals_.clear(); // stale indices from old locals
  frame_arena_ = state.frames;
  frame_count_ = state.frame_count;
}

void VM::scheduleCall(const Value &fn,
                      const std::vector<Value> &args,
                      Value &result, bool &completed) {
  pending_calls.push_back({fn, args, &result, &completed});
}

void VM::processPendingCalls() {
  // Process all pending calls - just doCall, let outer loop execute
  for (auto &call : pending_calls) {
    doCall(call.fn, call.args, false);
  }
  pending_calls.clear();
}

// Synchronous call for host functions - executes callback and returns result
// Minimal state isolation: just save/restore stack size
Value VM::callFunctionSync(const Value &fn,
                               const std::vector<Value> &args) {
    size_t savedStackSize = stack.size();
    size_t savedFrameCount = frame_count_;

    const BytecodeChunk *saved_chunk = current_chunk;
    if (!current_chunk && main_chunk_) {
        current_chunk = main_chunk_.get();
    }

    // Execute callback
    doCall(fn, args, false);
    runDispatchLoop(savedFrameCount);

    current_chunk = saved_chunk;


  // Get result from stack top
  Value result;
  if (stack.empty()) {
    result = nullptr;
  } else {
    result = stack.top();
    stack.pop();
  }

  // Just ensure stack is at expected size
  while (stack.size() > savedStackSize) {
    stack.pop();
  }

  return result;
}


Value VM::execute(const BytecodeChunk &chunk,
 const std::string &function_name,
 const std::vector<Value> &args) {
 const BytecodeChunk *saved_chunk = current_chunk;
 current_chunk = &chunk;

  const auto *entry = chunk.getFunction(function_name);
  if (!entry) {
    COMPILER_THROW("Function not found: " + function_name);
  }

  while (!stack.empty()) {
        stack.pop();
    }
    locals.clear();
    frame_count_ = 0;

    collectGarbage();

    open_upvalues.clear();
    has_current_exception_ = false;
    current_exception_ = nullptr;
  registerDefaultHostGlobals();
  host_globals_registered_ = true;
  std::fprintf(stderr, "DBG execute(): isArray=%d globals_size=%zu\n",
               globals.contains("isArray")?1:0, globals.size());
  if (post_reset_setup_) {
        post_reset_setup_(*this);
    }
    opcode_counts_.fill(0);
 executed_instructions_ = 0;

 if (frame_arena_.size() <= frame_count_) {
        frame_arena_.push_back(CallFrame{entry, &chunk, 0, 0, 0});
    } else {
        frame_arena_[frame_count_] = CallFrame{entry, &chunk, 0, 0, 0};
    }
    frame_count_++;
    locals.resize(entry->local_count);

 if (!args.empty()) {
 if (args.size() != entry->param_count) {
 COMPILER_THROW("Argument count mismatch for entry function '" +
 function_name + "' (expected " +
 std::to_string(entry->param_count) + ", got " +
 std::to_string(args.size()) + ")");
 }

 for (uint32_t i = 0; i < entry->param_count; ++i) {
 locals[i] = args[i];
 }
 }

if (debug_mode) {
        ::havel::debug("=== Executing function: {} ===", function_name);
    }

    runDispatchLoop(0);

 current_chunk = saved_chunk;

 if (stack.empty()) {
 return nullptr;
 }

 Value result = stack.top();
 stack.pop();
 return result;
 }

Value VM::executePersistent(const BytecodeChunk &chunk,
                             const std::string &function_name,
                             const std::vector<Value> &args) {
  const BytecodeChunk *saved_chunk = current_chunk;
  current_chunk = &chunk;

  const auto *entry = chunk.getFunction(function_name);
  if (!entry) {
    COMPILER_THROW("Function not found: " + function_name);
  }

  suspendGC();

  std::fprintf(stderr, "DBG executePersistent ENTRY: isArray=%d globals_size=%zu stack_depth=%zu main_chunk=%p current_chunk=%p\n",
               globals.contains("isArray")?1:0, globals.size(), globals_stack_.size(),
               main_chunk_ ? main_chunk_.get() : nullptr, current_chunk);

  // Save globals state (we may be inside a module closure that swapped
  // globals). The persistent execution needs root-level globals that
  // contain all host-registered globals (Type.isArray, math.PI, etc).
  auto saved_globals = globals;
  auto saved_globals_stack = globals_stack_;

  // Unwind to root globals: the bottom of the stack holds the globals
  // that were active before any module closure swapped them.
  if (!globals_stack_.empty()) {
    globals = std::move(globals_stack_.front());
    globals_stack_.erase(globals_stack_.begin());
  }

  // Clear stack and locals for this execution, but PRESERVE:
  // - globals (user-defined variables persist)
  // - heap (objects allocated by user persist)
  // - struct_type_ids (type information persists)
  while (!stack.empty()) { stack.pop(); }
  locals.clear();
  frame_count_ = 0;
  // DON'T reset heap - preserves user globals
  if (!host_globals_registered_) {
    registerDefaultHostGlobals();
    host_globals_registered_ = true;
  }
  registerDefaultPrototypes();
  open_upvalues.clear();
  has_current_exception_ = false;
  current_exception_ = nullptr;

    if (frame_arena_.size() <= frame_count_) {
        frame_arena_.push_back(CallFrame{entry, &chunk, 0, 0, 0});
    } else {
        frame_arena_[frame_count_] = CallFrame{entry, &chunk, 0, 0, 0};
    }
    frame_count_++;
    locals.resize(entry->local_count);

    if (!args.empty()) {
        if (args.size() != entry->param_count) {
            COMPILER_THROW("Argument count mismatch for entry function '" +
                           function_name + "' (expected " +
                           std::to_string(entry->param_count) + ", got " +
                           std::to_string(args.size()) + ")");
        }

        for (uint32_t i = 0; i < entry->param_count; ++i) {
            locals[i] = args[i];
        }
    }

    runDispatchLoop(0);

  // Restore globals state so the calling module context is unbroken
  globals = std::move(saved_globals);
  globals_stack_ = std::move(saved_globals_stack);

  current_chunk = saved_chunk;
  resumeGC();

  if (stack.empty()) {
    return nullptr;
  }

  Value result = stack.top();
  stack.pop();
  return result;
}

 // ============================================================================
 
// ============================================================================

bool VM::evaluateConditionBytecode(uint32_t func_index, uint32_t ip) {
  
  // 
  // Used by WatcherRegistry::onVariableChanged() to re-evaluate
  // reactive conditions when watched variables change.
  //
  // Approach:
  // 1. Create lightweight execution context (DependencyTrackerScope)
  // 2. Execute condition bytecode function
  // 3. Return top of stack as boolean
  //
  // Important: This runs in the current thread, not creating new fiber
  
  if (!current_chunk) {
    // No bytecode loaded - can't evaluate
    return false;
  }
  
  // Get function by index from current chunk
  if (func_index >= current_chunk->getFunctionCount()) {
    return false;
  }
  
  const auto *func_entry = current_chunk->getFunction(func_index);
  if (!func_entry) {
    return false;
  }
  
  
  // in VM::getGlobalThreadSafe(), so we don't need extra setup here
  
// Save current stack state (conditions shouldn't consume/modify main stack)
 std::stack<Value> saved_stack = stack;
 size_t saved_frame_count = frame_count_;
 auto saved_locals = locals;
 auto saved_frame_arena = frame_arena_;

 try {
 // Execute function and get result
 // We call the function through the normal call mechanism
 (void)ip;
 Value func_value = Value::makeFunctionObjId(func_index);
 Value result = call(func_value, {});

 // Convert result to boolean
 bool condition_result = toBool(result);

 // Restore stack
 stack = saved_stack;
 frame_count_ = saved_frame_count;
 locals = saved_locals;
 frame_arena_ = saved_frame_arena;

 return condition_result;
 } catch (...) {
 // Restore stack on error
 stack = saved_stack;
 frame_count_ = saved_frame_count;
 locals = saved_locals;
 frame_arena_ = saved_frame_arena;
 return false;
 }
}

// ============================================================================

// ============================================================================

VMExecutionResult::VMExecutionResult()
    : type(YIELD), result_value(nullptr) {}

// ============================================================================

// ============================================================================
//

// instruction in the current fiber, then returns control to the main loop.
//
// Key guarantee: No blocking. Always returns immediately after one instruction.
// 
// Integration pattern in main loop:
//   while (scheduler.hasRunnable()) {
//     result = vm.executeOneStep(scheduler.current());
//     // Handle result (YIELD/SUSPENDED/RETURNED/ERROR)
//   }
//

VMExecutionResult VM::executeOneStep(Fiber *current_fiber) {
if (!current_fiber) {
return VMExecutionResult::Error("No current fiber");
}

executing_in_fiber_ = true;

// For now, execute from current VM state
  
  // Check if we have frames to execute
  if (frame_count_ == 0) {
    return VMExecutionResult::Suspended();
  }

  try {
    // Get current frame state
    size_t active_frame_idx = frame_count_ - 1;
    const auto *function = frame_arena_[active_frame_idx].function;
    uint32_t ip = frame_arena_[active_frame_idx].ip;
    size_t entry_frame_count = frame_count_;

    // Boundary check - if IP past function end, return
    if (ip >= function->instructions.size()) {
      stack.push(nullptr);
      executeInstruction(Instruction{OpCode::RETURN});
      // After RETURN, check frame count to determine if function returned
      if (frame_count_ < entry_frame_count) {
        if (stack.empty()) {
          return VMExecutionResult::Returned(nullptr);
        }
        Value ret_val = stack.top();
        stack.pop();
        return VMExecutionResult::Returned(ret_val);
      }
      return VMExecutionResult::Yield(nullptr);
    }

    // Get and execute instruction
    const auto &instruction = function->instructions[ip];

    // Track for profiling
    if (profiling_enabled_) {
      opcode_counts_[static_cast<uint8_t>(instruction.opcode)]++;
      executed_instructions_++;
    }

    // Execute the instruction
    executeInstruction(instruction);

    // Process any pending callbacks that resulted from instruction
    processPendingCalls();

    // Check if exit was requested during this instruction (from exit() host function)
    // Stop all goroutines immediately and return so the EventLoop shuts down cleanly
    if (exit_requested_.load()) {
        if (scheduler_) scheduler_->stop();
        return VMExecutionResult::Returned(nullptr);
    }

    // Increment IP if the instruction didn't modify it (no CALL/RETURN)
    // MUST happen before suspension check so that on resume the IP already
    // points past the instruction that requested suspension.
    if (frame_count_ > 0) {
      active_frame_idx = frame_count_ - 1;
      if (frame_count_ == entry_frame_count &&
          frame_arena_[active_frame_idx].ip == ip) {
        frame_arena_[active_frame_idx].ip++;
      }
    }

    if (suspension_requested_) {
      suspension_requested_ = false;

      // Suspend the current fiber with the stored reason and context
      // The context pointer contains thread_id or other relevant data
      void* context = suspension_context_;
      SuspensionReason reason = static_cast<SuspensionReason>(suspension_reason_);
      suspension_context_ = nullptr;

      if (current_fiber) {
        current_fiber->suspend(reason, context);


        if (reason == SuspensionReason::THREAD_JOIN) {
          uint32_t thread_id = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(context));
          registerThreadWait(thread_id, current_fiber);
        }
      }

      return VMExecutionResult::Suspended();
    }

    // Return normal yield (instruction completed successfully)
    return VMExecutionResult::Yield(nullptr);

  } catch (const ScriptThrow &thrown) {
    // Handle script-thrown exceptions
    if (!handleScriptThrow(thrown.value)) {
      std::string stackTrace = buildStackTrace(frame_count_);
  uint32_t line = 0, column = 0;
  std::string srcFile;
  if (frame_count_ > 0) {
    auto &frame = frame_arena_[frame_count_ - 1];
    if (frame.function && frame.ip < frame.function->instruction_locations.size()) {
      const auto loc = nearestSourceLocation(*frame.function, frame.ip);
      line = loc.line;
      column = loc.column;
      srcFile = loc.filename;
    }
  }
  std::string errorMsg = "Uncaught exception: " + toString(thrown.value);
  if (line > 0 && !srcFile.empty()) {
    errorMsg = ::havel::ErrorPrinter::formatErrorFromFile("Runtime Error", errorMsg, srcFile, (size_t)line, (size_t)column, 0);
  } else if (line > 0) {
    errorMsg += " at line " + std::to_string(line);
  }
      return VMExecutionResult::Error(errorMsg);
    }
    // Exception was caught and handled
    return VMExecutionResult::Yield(nullptr);

  } catch (const std::runtime_error &e) {
    std::string msg = e.what();
    if (frame_count_ > 0) {
      auto &frame = frame_arena_[frame_count_ - 1];
      if (frame.function && frame.ip < frame.function->instruction_locations.size()) {
        const auto loc = nearestSourceLocation(*frame.function, frame.ip);
        if (loc.line > 0) {
          if (!loc.filename.empty()) {
            msg = ::havel::ErrorPrinter::formatErrorFromFile("Runtime Error", msg, loc.filename, (size_t)loc.line, (size_t)loc.column, (size_t)loc.length);
          } else {
            msg += " at " + std::to_string(loc.line) + ":" + std::to_string(loc.column);
          }
        }
      }
    }
    return VMExecutionResult::Error(msg);
  } catch (const std::exception &e) {
    return VMExecutionResult::Error(std::string("VM exception: ") + e.what());
  }

  return VMExecutionResult::Yield(nullptr);
}

// ============================================================================

// ============================================================================

/**
 * loadFiberState - Copy fiber's suspended state into VM's global state
 * 
 * Called before executeOneStep() to restore a fiber that is resuming.
 * @param fiber The Fiber being resumed (must have suspended state)
 */
void VM::loadFiberState(Fiber *fiber) {
  if (!fiber) {
    return;  // No-op for null fiber
  }

  // STEP 1: Clear VM's current execution state
  // These will be repopulated from the fiber
  while (!stack.empty()) {
    stack.pop();
  }
  locals.clear();
  immutable_locals_.clear();
  frame_count_ = 0;

  // STEP 2: Restore operand stack from fiber's stack
  // FiberStack uses a data vector and size_t sp (stack pointer)
  // We need to copy all pushed values onto the VM's stack
  const auto &fiber_stack_data = fiber->stack.data();
  const size_t fiber_sp = fiber->stack.size();
  for (size_t i = 0; i < fiber_sp; ++i) {
    stack.push(fiber_stack_data[i]);
  }

  // STEP 3: Restore locals from fiber's map into VM's vector
  // VM locals is a vector indexed by absolute position
  // We iterate fiber's map and build the locals vector
  if (!fiber->locals.empty()) {
    // Find the maximum local index to know how big to make locals vector
    size_t max_local = 0;
    for (const auto &[name, value] : fiber->locals) {
      // For now, we just copy all values into a sequential vector
      // TODO: If fiber->locals uses string keys, we may need a mapping
      max_local++;
    }
    
    locals.reserve(max_local);
    for (const auto &[name, value] : fiber->locals) {
      locals.push_back(value);
    }
  }

  // STEP 4: Restore call stack from fiber's call_stack
  // Copy each CallFrame from Fiber to VM's frame arena
  for (const auto &fiber_frame : fiber->call_stack) {
    // Allocate a CallFrame in VM's frame arena
    if (frame_arena_.size() <= frame_count_) {
      frame_arena_.push_back(CallFrame());
    }

    auto &vm_frame = frame_arena_[frame_count_];

    // Resolve function pointer from function_id using the saved chunk
    const BytecodeChunk *resolve_chunk = fiber_frame.chunk_ptr;
    if (fiber_frame.function_id == 0xFFFFFFFF) {
      vm_frame.function = nullptr; // Special marker for hotkey action
    } else if (resolve_chunk && fiber_frame.function_id < resolve_chunk->getFunctionCount()) {
      vm_frame.function = resolve_chunk->getFunction(fiber_frame.function_id);
      vm_frame.chunk = resolve_chunk;
    } else if (current_chunk && fiber_frame.function_id < current_chunk->getFunctionCount()) {
      // Fallback: resolve from current chunk if no saved chunk
      vm_frame.function = current_chunk->getFunction(fiber_frame.function_id);
      vm_frame.chunk = current_chunk;
      ::havel::debug("[VM] loadFiberState: No saved chunk, resolved function {} from current chunk", fiber_frame.function_id);
    }

    if (!vm_frame.function && fiber_frame.function_id != 0xFFFFFFFF) {
      ::havel::warn("[VM] loadFiberState: Could not resolve function {} (chunk_ptr={})", fiber_frame.function_id, (void*)resolve_chunk);
    }

    vm_frame.ip = fiber_frame.ip;
    vm_frame.locals_base = fiber_frame.locals_base;
    vm_frame.closure_id = fiber_frame.closure_id;
    vm_frame.stack_depth = fiber_frame.stack_depth;
    vm_frame.owns_globals = fiber_frame.owns_globals;

    // Convert try_stack: both have same structure but different types
    vm_frame.try_stack.clear();
    for (const auto& handler : fiber_frame.try_stack) {
      vm_frame.try_stack.push_back(VM::TryHandler{
        handler.catch_ip,
        handler.finally_ip,
        handler.finally_return_ip,
        handler.stack_depth
      });
    }

    frame_count_++;
  }

  // STEP 5: Restore current_chunk from the topmost frame's chunk
  // This is critical for LOAD_GLOBAL and other chunk-dependent operations
  if (frame_count_ > 0) {
    const auto &top_frame = frame_arena_[frame_count_ - 1];
    if (top_frame.chunk) {
      current_chunk = top_frame.chunk;
    }

    // The fiber->ip tells us where to resume in the current chunk
    // This was saved during the previous suspension
    frame_arena_[frame_count_ - 1].ip = fiber->ip;
  }
}

/**
 * saveFiberState - Copy VM's current execution state back to fiber
 * 
 * Called after executeOneStep() to persist the fiber's progress.
 * Preserves all state so the fiber can be resumed later.
 * @param fiber The Fiber being suspended (receives current VM state)
 */
void VM::saveFiberState(Fiber *fiber) {
  if (!fiber) {
    return;  // No-op for null fiber
  }

  // STEP 1: Save operand stack from VM back to fiber's stack
  fiber->stack.clear();
  
  // Convert VM's std::stack<Value> to fiber's FiberStack
  // std::stack is LIFO, so we need to extract in reverse order
  std::vector<Value> temp_values;
  auto temp_stack = stack;  // Copy the stack
  while (!temp_stack.empty()) {
    temp_values.push_back(temp_stack.top());
    temp_stack.pop();
  }
  // Now push in correct order (reverse of extraction)
  for (auto it = temp_values.rbegin(); it != temp_values.rend(); ++it) {
    fiber->stack.push(*it);
  }

  // STEP 2: Save locals from VM's vector back to fiber's map
  fiber->locals.clear();
  for (size_t i = 0; i < locals.size(); ++i) {
    
    std::string key = "_local_" + std::to_string(i);
    fiber->locals[key] = locals[i];
  }

  // STEP 3: Save call stack from VM back to fiber's call_stack  
  fiber->call_stack.clear();
  for (size_t i = 0; i < frame_count_; ++i) {
    const auto &vm_frame = frame_arena_[i];
    
    // We need to create instances of the Fiber CallFrame type
    // The type in fiber->call_stack is havel::compiler::CallFrame (from Fiber.hpp)
    // We're currently inside VM class scope, so we need to explicitly qualify
    
    // Get the correct CallFrame type from what fiber expects
    // by using a typedef based on fiber's vector
    using FiberCallFrameType = typename decltype(fiber->call_stack)::value_type;
    using TryHandlerType = typename FiberCallFrameType::TryHandler;
    
    FiberCallFrameType fiber_cf;
    fiber_cf.ip = vm_frame.ip;
    fiber_cf.locals_base = vm_frame.locals_base;
    fiber_cf.closure_id = vm_frame.closure_id;
    fiber_cf.arg_count = 0;
    fiber_cf.stack_depth = vm_frame.stack_depth;
    fiber_cf.owns_globals = vm_frame.owns_globals;
    fiber_cf.chunk_ptr = vm_frame.chunk;
    if (vm_frame.function && vm_frame.chunk) {
      fiber_cf.function_id = vm_frame.chunk->getFunctionIndex(vm_frame.function);
    } else {
      fiber_cf.function_id = 0;
    }
    
    // Convert try_stack: both have same structure but different types
    fiber_cf.try_stack.clear();
    for (const auto& vm_handler : vm_frame.try_stack) {
      fiber_cf.try_stack.push_back(TryHandlerType{
          vm_handler.catch_ip,
          vm_handler.finally_ip,
          vm_handler.finally_return_ip,
          vm_handler.stack_depth
      });
    }
    
    fiber->call_stack.push_back(fiber_cf);
  }

  // STEP 4: Save current instruction pointer
  if (frame_count_ > 0) {
    fiber->ip = frame_arena_[frame_count_ - 1].ip;
  }

  // STEP 5: Update fiber state if needed
  // Don't change the suspended_reason - that was set when suspension occurred
  // Just ensure the fiber's state reflects current execution point
}

bool VM::startGoroutineCall(uint32_t function_id, uint32_t closure_id,
                            const std::vector<Value> &args) {
    // Clear VM state for fresh goroutine context
    while (!stack.empty()) stack.pop();
    locals.clear();
    immutable_locals_.clear();
    frame_count_ = 0;

    // Resolve chunk: closures carry their defining chunk
    const BytecodeChunk *resolve_chunk = current_chunk;
    if (closure_id > 0) {
        auto *closure = heap_.closure(closure_id);
        if (closure && closure->chunk) {
            resolve_chunk = closure->chunk;
        }
    }

    if (!resolve_chunk) {
        ::havel::error("[VM] startGoroutineCall: no chunk available for function {}", function_id);
        return false;
    }

    const auto *func = resolve_chunk->getFunction(function_id);
    if (!func) {
        ::havel::error("[VM] startGoroutineCall: function {} not found in chunk ({} functions)",
                       function_id, resolve_chunk->getFunctionCount());
        return false;
    }

    current_chunk = resolve_chunk;

    // Push args onto VM stack
    for (const auto &arg : args) {
        stack.push(arg);
    }

    // Set up locals with room for params + locals
    size_t needed = std::max(func->local_count, func->param_count);
    locals.resize(needed, nullptr);

    // Copy args into local slots
    for (uint32_t i = 0; i < func->param_count && i < args.size(); ++i) {
        locals[i] = args[i];
    }

    // Set up the initial call frame
    size_t stack_depth = stack.size();
    CallFrame cf;
    cf.function = func;
    cf.chunk = resolve_chunk;
    cf.ip = 0;
    cf.locals_base = 0;
    cf.closure_id = closure_id;
    cf.stack_depth = static_cast<uint32_t>(stack_depth);

    if (frame_arena_.size() <= frame_count_) {
        frame_arena_.push_back(std::move(cf));
    } else {
        frame_arena_[frame_count_] = std::move(cf);
    }
    frame_count_++;

    return true;
}

// ============================================================================

// ============================================================================

void VM::registerThreadWait(uint32_t thread_id, Fiber *fiber) {
  if (!fiber) {
    return;
  }
  
  std::unique_lock<std::shared_mutex> lock(thread_wait_mutex_);
  thread_wait_map_[thread_id] = fiber;
}

// ============================================================================
// CALLER INFO - Access call stack for reflection
// ============================================================================

VM::CallerInfo VM::getCallerInfo(int depth) const {
  CallerInfo info;
  // Frame 0 is current function, so caller is at frame_count_ - 2 - depth
  if (frame_count_ < 2) return info;

  int targetFrame = static_cast<int>(frame_count_) - 2 - depth;
  if (targetFrame < 0 || static_cast<size_t>(targetFrame) >= frame_count_) {
    return info;
  }

  const auto& frame = frame_arena_[static_cast<size_t>(targetFrame)];
  if (frame.function) {
    info.function = frame.function->name;
    uint32_t ip = frame.ip;
    if (ip < frame.function->instruction_locations.size()) {
      const auto& loc = frame.function->instruction_locations[ip];
      info.line = loc.line;
      info.column = loc.column;
    }
    info.file = frame.function->source_file;
  }
  return info;
}

Fiber* VM::getThreadWaitingFiber(uint32_t thread_id) const {
  std::shared_lock<std::shared_mutex> lock(thread_wait_mutex_);
  auto it = thread_wait_map_.find(thread_id);
  return it != thread_wait_map_.end() ? it->second : nullptr;
}

void VM::unregisterThreadWait(uint32_t thread_id) {
  std::unique_lock<std::shared_mutex> lock(thread_wait_mutex_);
  thread_wait_map_.erase(thread_id);
}

std::vector<uint32_t> VM::getWaitingThreadIds() const {
  std::shared_lock<std::shared_mutex> lock(thread_wait_mutex_);
  std::vector<uint32_t> result;
  result.reserve(thread_wait_map_.size());
  for (const auto& [thread_id, fiber] : thread_wait_map_) {
    if (fiber) {
      result.push_back(thread_id);
    }
  }
  return result;
}

void VM::runDispatchLoop(size_t stop_frame_depth) {
executing_in_fiber_ = false;
while (frame_count_ > stop_frame_depth) {
        // Check if exit was requested (from exit() host function)
        if (exit_requested_.load()) {
            break;
        }

    // Instruction limit check - prevents infinite loops
    if (max_instructions_ > 0) {
        executed_instructions_++;
        if (executed_instructions_ > max_instructions_) {
            throw std::runtime_error("VM instruction limit exceeded (" +
                std::to_string(max_instructions_) + ") - possible infinite loop");
        }
    }

        // Periodically check for expired timers
        if (timer_check_func_) {
            instructions_since_timer_check_++;
            if (instructions_since_timer_check_ >= TIMER_CHECK_INTERVAL) {
                timer_check_func_();
                instructions_since_timer_check_ = 0;
            }
        }
    
    // CRITICAL: Capture ALL frame data by value BEFORE any mutation!
    // doCall() may cause vector reallocation, invalidating all
    // references/indices.
    size_t active_frame_idx = frame_count_ - 1;

    // Capture frame data by value - do NOT keep references!
    const auto *function = frame_arena_[active_frame_idx].function;
    uint32_t ip = frame_arena_[active_frame_idx].ip;
    size_t entry_frame_count = frame_count_;

        if (ip >= function->instructions.size()) {
            stack.push(nullptr);
            executeInstruction(Instruction{OpCode::RETURN});
            continue;
  }

  const auto &instruction = function->instructions[ip];

        try {
            if (profiling_enabled_) {
                opcode_counts_[static_cast<uint8_t>(instruction.opcode)]++;
                executed_instructions_++;
            }
            if (trace_execution_ && current_chunk) {
                auto funcName = function->name.empty() ? std::string("<anon>") : function->name;
                BytecodeDisassembler::Options opts;
                opts.showLineNumbers = false;
                opts.showSourceLocations = true;
                opts.showConstantPool = false;
                opts.showFunctionInfo = false;
                opts.useLabels = true;
                auto disasm = BytecodeDisassembler(*current_chunk).formatInstruction(
                    ip, instruction, opts);
                std::fprintf(stderr, "\033[2m[trace] %3u %s:%u  %s\033[0m\n",
                             static_cast<unsigned>(frame_count_), funcName.c_str(),
                             static_cast<unsigned>(ip), disasm.c_str());
            }
            executeInstruction(instruction);
      if (exit_requested_.load()) {
        break;
      }
      if (suspension_requested_) {
        // SUSPENDED or SLEEP was requested (e.g. by sleep() host function).
        // In runDispatchLoop mode, handle SLEEP as blocking sleep since there's
        // no scheduler event loop to resume us. Other suspension reasons
        // (channel/thread) are not applicable in dispatch loop mode.
        uint8_t reason = suspension_reason_;
        void* ctx = suspension_context_;
        suspension_requested_ = false;
        suspension_context_ = nullptr;
        if (reason == static_cast<uint8_t>(SuspensionReason::SLEEP)) {
          int64_t ms = reinterpret_cast<intptr_t>(ctx);
          if (ms > 0) {
            const int CHUNK = 10;
            auto deadline = std::chrono::steady_clock::now() +
                            std::chrono::milliseconds(ms);
            while (std::chrono::steady_clock::now() < deadline) {
              if (exit_requested_.load()) break;
              if (timer_check_func_) timer_check_func_();
              auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                  deadline - std::chrono::steady_clock::now());
              auto chunk = std::min(static_cast<int>(remaining.count()), CHUNK);
              if (chunk > 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(chunk));
            }
          }
          // IP already incremented by auto-increment below — continue.
        } else {
          // Unknown suspension — break out so caller can handle
          break;
        }
      }
    } catch (const ScriptThrow &thrown) {
      if (!handleScriptThrow(thrown.value)) {
        // Build stack trace for uncaught exception
        std::string stackTrace = buildStackTrace(frame_count_);

        // Get line number from current instruction
        uint32_t line = 0;
        uint32_t column = 0;
        if (frame_count_ > 0) {
          auto &frame = frame_arena_[frame_count_ - 1];
          if (frame.function &&
              frame.ip < frame.function->instruction_locations.size()) {
            const auto loc = nearestSourceLocation(*frame.function, frame.ip);
            line = loc.line;
            column = loc.column;
          }
        }

        std::string errorMsg =
            "Uncaught exception: " + toString(thrown.value);
        if (line > 0) {
          errorMsg += " at line " + std::to_string(line);
          if (column > 0) {
            errorMsg += ":" + std::to_string(column);
          }
        }

        throw ScriptError(thrown.value, errorMsg, stackTrace, line, column);
      }
      continue;
    } catch (const std::runtime_error &e) {
      // Convert runtime errors to script exceptions so they can be caught
      // by script-level try/catch blocks
    std::string msg = e.what();
    uint32_t line = 0;
    uint32_t column = 0;
    if (frame_count_ > 0) {
      auto &frame = frame_arena_[frame_count_ - 1];
      if (frame.function &&
          frame.ip < frame.function->instruction_locations.size()) {
        const auto loc = nearestSourceLocation(*frame.function, frame.ip);
        line = loc.line;
        column = loc.column;
        if (loc.line > 0) {
          if (!loc.filename.empty()) {
            msg = ::havel::ErrorPrinter::formatErrorFromFile("Runtime Error", std::string(e.what()), loc.filename, (size_t)loc.line, (size_t)loc.column, (size_t)loc.length);
          } else {
            msg += " at " + std::to_string(loc.line) + ":" + std::to_string(loc.column);
          }
        }
      }
    }
      // Try to handle as script exception first
      Value exceptionValue = Value::makeStringId(heap_.allocateString(msg).id);
      if (handleScriptThrow(exceptionValue)) {
        continue;  // Exception was caught by script-level handler
      }
      // No script handler found - treat as uncaught runtime error
      throw std::runtime_error(msg);
    }

    processPendingCalls();
      if (exit_requested_.load()) {
        break;
      }

        // CRITICAL: Re-fetch frame AFTER executeInstruction (vector may have
        // reallocated). Only increment IP if the frame count didn't change
        // (no CALL/RETURN) and the instruction didn't modify IP itself.
        if (frame_count_ > stop_frame_depth) {
            active_frame_idx = frame_count_ - 1;
            if (frame_count_ == entry_frame_count && frame_arena_[active_frame_idx].ip == ip) {
                frame_arena_[active_frame_idx].ip++;
            }
        }
    }
}

bool VM::handleScriptThrow(const Value &value) {
  has_current_exception_ = true;
  current_exception_ = value;

  while (frame_count_ > 0) {
    auto &frame = frame_arena_[frame_count_ - 1];
    // Defensive check: ensure frame is valid
    if (!frame.function) {
      frame_count_--;
      continue;
    }
    if (!frame.try_stack.empty()) {
      const auto handler = frame.try_stack.back();
      frame.try_stack.pop_back();

      // Defensive check: ensure stack_depth is not larger than current stack
      // If it is, something went wrong - reset to empty stack
      size_t target_depth = handler.stack_depth;
      if (target_depth > stack.size()) {
        target_depth = 0;  // Reset to empty if corrupted
      }
      while (stack.size() > target_depth) {
        stack.pop();
      }

      // Jump to catch block (finally is compiled into the catch block if it
      // exists)
      frame.ip = handler.catch_ip;
      return true;
    }

    auto finished = frame;
    frame_count_--;

    closeFrameUpvalues(static_cast<uint32_t>(finished.locals_base),
                       static_cast<uint32_t>(locals.size()));
    if (locals.size() >= finished.locals_base) {
      locals.resize(finished.locals_base);
    }
  }

  // No handler found - exception is uncaught
  return false;
}

std::string VM::buildStackTrace(size_t frame_count) const {
  std::string trace;
  if (frame_count == 0) {
    return trace;
  }

  trace = "Stack trace:\n";
  for (size_t i = 0; i < frame_count; ++i) {
    const auto &frame = frame_arena_[i];
    if (!frame.function) {
      continue;
    }

    // Get function name if available
    std::string funcName = "<anonymous>";
    if (!frame.function->name.empty()) {
      funcName = frame.function->name;
    }

    // Get line/column from instruction location
    uint32_t line = 0;
    uint32_t column = 0;
    if (frame.ip < frame.function->instruction_locations.size()) {
      const auto &loc = frame.function->instruction_locations[frame.ip];
      line = loc.line;
      column = loc.column;
    }

    trace += "  at " + funcName;
    if (line > 0) {
      trace += " (line " + std::to_string(line);
      if (column > 0) {
        trace += ":" + std::to_string(column);
      }
      trace += ")";
    }
    trace += "\n";
  }

  return trace;
}

Value VM::call(const Value &callee_value,
 const std::vector<Value> &args) {
 std::lock_guard<std::recursive_mutex> lock(execution_mutex_);
 if (!current_chunk) {
 COMPILER_THROW(
 "VM::call requires an active bytecode chunk (run execute first)");
 }

 const size_t base_depth = frame_count_;
 doCall(callee_value, args, false);
 runDispatchLoop(base_depth);

 if (stack.empty()) {
 return nullptr;
 }
 Value result = stack.top();
 stack.pop();
 return result;
}

void VM::setDebugMode(bool enabled) { debug_mode = enabled; }

void VM::doCall(Value callee_value, std::vector<Value> args,
 bool advance_caller_ip) {
 tail_call_depth_ = 0;

    // Handle host function call directly
if (callee_value.isHostFuncId()) {
uint32_t host_func_idx = callee_value.asHostFuncId();
if (host_func_idx >= host_function_names_.size()) {
COMPILER_THROW("Host function index out of range: " +
std::to_string(host_func_idx));
}
const std::string &name = host_function_names_[host_func_idx];
auto it = host_functions.find(name);
    if (it == host_functions.end()) {
      COMPILER_THROW("Host function not found: " + name);
    }
    gc_suspend_counter_++;
    Value result = it->second(args);
    gc_suspend_counter_--;
    pushStack(result);
    maybeCollectGarbage();
    return;
  }

  if (frame_count_ >= max_call_depth_) {
    COMPILER_THROW("Stack overflow: maximum call depth " +
                             std::to_string(max_call_depth_) + " reached");
	}

	// Handle coroutine resume
  if (callee_value.isCoroutineId()) {
    uint32_t coId = callee_value.asCoroutineId();
    auto *co = heap_.coroutine(coId);
    if (!co) {
      COMPILER_THROW("Coroutine not found: " + std::to_string(coId));
    }
    if (co->state == GCHeap::Coroutine::Done) {
      pushStack(Value::makeNull());
      return;
    }

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
            current_coroutine_id_ = coId;

            // Push a frame for coroutine execution on the existing stack
      const auto *chunk = current_chunk;
      const auto *func = chunk ? chunk->getFunction(co->function_index) : nullptr;
      if (!func) {
        COMPILER_THROW("Function not found for coroutine");
      }

      // Restore coroutine's locals for execution
      locals = co->locals;

      // Coroutine switched into execution — its val declarations
      // will repopulate immutable_locals_ during execution
      immutable_locals_.clear();

      size_t coroutine_stack_depth = stack.size();
    if (frame_arena_.size() <= frame_count_) {
        frame_arena_.push_back(CallFrame{func, current_chunk, co->ip, 0, co->closure_id});
    } else {
        frame_arena_[frame_count_] = CallFrame{func, current_chunk, co->ip, 0, co->closure_id};
    }
      frame_arena_[frame_count_].stack_depth = coroutine_stack_depth;
      frame_count_++;

    co->state = GCHeap::Coroutine::Runnable;
    return;
  }

uint32_t function_index = 0;
uint32_t closure_id = 0;
const BytecodeChunk *resolve_chunk = current_chunk;
std::shared_ptr<std::unordered_map<std::string, Value>> closure_globals;
if (callee_value.isFunctionObjId()) {
function_index = callee_value.asFunctionObjId();
if (resolve_chunk && !resolve_chunk->getFunction(function_index)) {
if (main_chunk_ && main_chunk_->getFunction(function_index)) {
resolve_chunk = main_chunk_.get();
}
}
} else if (callee_value.isClosureId()) {
closure_id = callee_value.asClosureId();
auto *closure = heap_.closure(closure_id);
 function_index = closure->function_index;
 if (closure->chunk) {
 resolve_chunk = closure->chunk;
 }
 if (closure->module_globals) {
 closure_globals = closure->module_globals;
 }
 } else {
            // Debug: identify what type the value actually is
        std::string typeInfo = "unknown";
        if (callee_value.isNull()) typeInfo = "null";
        else if (callee_value.isInt()) typeInfo = "int";
        else if (callee_value.isDouble()) typeInfo = "double";
        else if (callee_value.isBool()) typeInfo = "bool";
else if (callee_value.isStringValId()) {
		typeInfo = current_chunk ? std::string("string_val_id='") + current_chunk->getString(callee_value.asStringValId()) + "'"
		                         : std::string("string_val_id=<") + std::to_string(callee_value.asStringValId()) + ">";
	}
	else if (callee_value.isStringId()) {
		auto *sp = heap_.string(callee_value.asStringId());
		typeInfo = sp ? std::string("string_id='") + *sp + "'" : std::string("string_id=<") + std::to_string(callee_value.asStringId()) + ">";
	}
	else if (callee_value.isObjectId()) typeInfo = "object_id";
	else if (callee_value.isArrayId()) typeInfo = "array_id";
	else if (callee_value.isHostFuncId()) typeInfo = "host_func_id";
	else if (callee_value.isFunctionObjId()) typeInfo = "function_obj_id";
	else if (callee_value.isClosureId()) typeInfo = "closure_id (unexpected)";
	else if (callee_value.isCoroutineId()) typeInfo = "coroutine_id (should have been caught)";
	// Dump call stack for debugging
	std::string frameInfo;
	for (int fi = static_cast<int>(frame_count_) - 1; fi >= 0 && fi >= static_cast<int>(frame_count_) - 8; --fi) {
		auto &fr = frame_arena_[fi];
		std::string fname = fr.function ? fr.function->name : "<anon>";
		frameInfo += "  frame[" + std::to_string(fi) + "] " + fname + " ip=" + std::to_string(fr.ip) + "\n";
	}
	// Dump instructions around the failing IP
	std::string instrInfo;
	auto &cf = currentFrame();
	if (cf.function && cf.ip < cf.function->instructions.size()) {
		uint32_t start = cf.ip > 5 ? cf.ip - 5 : 0;
		uint32_t end = std::min(cf.function->instructions.size(), static_cast<size_t>(cf.ip + 5));
		for (uint32_t ii = start; ii < end; ++ii) {
			auto &inst = cf.function->instructions[ii];
			std::string marker = (ii == cf.ip) ? " >>> " : "     ";
			instrInfo += marker + std::to_string(ii) + ": op=" + std::to_string(static_cast<int>(inst.opcode));
			for (size_t oi = 0; oi < inst.operands.size(); ++oi) {
				instrInfo += " op" + std::to_string(oi) + "=";
				if (inst.operands[oi].isStringValId() && resolve_chunk) {
					instrInfo += "'" + resolve_chunk->getString(inst.operands[oi].asStringValId()) + "'";
				} else {
					instrInfo += inst.operands[oi].toString();
				}
			}
			instrInfo += "\n";
		}
	}
	COMPILER_THROW("CALL expects function or closure as callee (got " + typeInfo + ") [callee_bits=" + std::to_string(callee_value.rawBits()) + ", ip=" + std::to_string(currentFrame().ip) + "]\nCall stack:\n" + frameInfo + "Instructions:\n" + instrInfo);
    }

    if (!resolve_chunk) {
        COMPILER_THROW("No chunk available for function call");
    }

const auto *callee = resolve_chunk->getFunction(function_index);
if (!callee) {
    COMPILER_THROW("Function index not found: " +
        std::to_string(function_index));
}


callee->execution_count++;
 if (callee->execution_count == 1000 && hot_func_cb_) {
 hot_func_cb_(*callee);
 }
 
 
 if (callee->jit_compiled && jit_compiler_) {
 uint32_t prev_jit_closure = setJITActiveClosurePublic(closure_id);
 try {
   Value result = jit_compiler_->executeCompiled(this, callee->name, args);
   setJITActiveClosurePublic(prev_jit_closure);
   pushStack(result);
   return;
 } catch (...) {
   setJITActiveClosurePublic(prev_jit_closure);
   throw;
 }
 }
 
 
  // If so, create a coroutine object and return it instead of executing
if (callee->is_generator) {
// Create coroutine object for this generator function
uint32_t coId = heap_.allocateCoroutine(function_index, 0);
auto *co = heap_.coroutine(coId);
co->state = GCHeap::Coroutine::Runnable;
co->closure_id = closure_id;

            // Save the caller's locals so nested generators can access outer frames' values
            {
                GCHeap::CallerFrame cf;
                cf.coroutine_id = current_coroutine_id_;
                cf.frame_count = frame_count_;
                cf.ip = 0;
                cf.locals = locals;
                co->caller_stack.push_back(std::move(cf));
            }

// Generators should NOT copy parent's locals into their own locals.
// The generator's locals are for its own variables only.
// Upvalues are accessed through the closure, not through copied locals.
 co->locals.resize(std::max(callee->local_count, callee->param_count), nullptr);

// Close all open upvalues in the closure by copying their current values.
// For generators, upvalues must be closed because the generator runs in its own
// isolated locals context and can't access outer frames' locals directly.
                    // Special case for nested generators: if the upvalue points to an outer frame,
                    // use the caller coroutine's caller_stack locals to access the value.
if (closure_id != 0) {
    auto *closure = heap_.closure(closure_id);
    if (closure) {
        for (auto &cell : closure->upvalues) {
            if (cell && cell->is_open) {
                uint32_t abs_index = cell->locals_base + cell->open_index;
                
                        std::vector<Value>* target_locals = &locals;
                        if (current_coroutine_id_ != UINT32_MAX) {
                            auto* caller_co = heap_.coroutine(current_coroutine_id_);
                            if (caller_co && abs_index >= locals.size() && !caller_co->caller_stack.empty()) {
                                target_locals = &caller_co->caller_stack.back().locals;
                            }
                        }
                
                if (abs_index < target_locals->size()) {
                    cell->closed_value = (*target_locals)[abs_index];
                }
                cell->is_open = false;
                open_upvalues.erase(abs_index);
            }
        }
    }
}

co->ip = 0;

    // Copy args into coroutine locals (same as normal call)
    size_t base = 0;
    for (uint32_t i = 0; i < callee->param_count; i++) {
      if (i < args.size()) {
        co->locals[base + i] = std::move(args[i]);
      } else if (i < callee->default_values.size() && callee->default_values[i]) {
        co->locals[base + i] = (*callee->default_values[i]);
      }
    }

    Value coroutineValue = Value::makeCoroutineId(coId);
    pushStack(coroutineValue);
    return;
  }

  // Debug
  (void)callee_value;

    // Allow fewer arguments than parameters (for default parameters)
    // For variadic functions, allow MORE arguments than parameters
    // Silently drop excess args for non-variadic functions (callback-safe)
    if (callee->variadic_param_index == UINT32_MAX && args.size() > callee->param_count) {
        args.resize(callee->param_count);
    }

  // For variadic functions, require at least as many args as non-variadic
  // params
  if (callee->variadic_param_index != UINT32_MAX &&
      args.size() < callee->variadic_param_index) {
    COMPILER_THROW("Argument count mismatch calling function index " +
                             std::to_string(function_index) +
                             " (expected at least " +
                             std::to_string(callee->variadic_param_index) +
                             ", got " + std::to_string(args.size()) + ")");
  }

    // Advance caller IP now so RETURN resumes at the next instruction.
    if (advance_caller_ip && frame_count_ > 0) {
        currentFrame().ip++;
    }

 current_chunk = resolve_chunk;

    bool frame_owns_globals = false;
    if (closure_globals) {
        globals_stack_.push_back(std::move(globals));
        globals = *closure_globals;
        frame_owns_globals = true;
    }

 size_t base = locals.size();
 size_t stack_depth = stack.size(); // Save current stack depth
 size_t needed_locals = std::max(callee->local_count, callee->param_count);
 locals.resize(base + needed_locals, nullptr);
 {
 CallFrame cf;
 cf.function = callee;
 cf.chunk = resolve_chunk;
 cf.ip = 0;
 cf.locals_base = base;
 cf.closure_id = closure_id;
 cf.owns_globals = frame_owns_globals;
 cf.stack_depth = static_cast<uint32_t>(stack_depth);
        if (frame_arena_.size() <= frame_count_) {
            frame_arena_.push_back(std::move(cf));
        } else {
            frame_arena_[frame_count_] = std::move(cf);
        }
    }
    frame_count_++;

    // Clear per-frame immutable_locals_ on function entry.
    // Each function's STORE_IMMUT_VAR opcodes will repopulate
    // this set with absolute indices valid for this frame's locals.
    immutable_locals_.clear();

  // Initialize parameter slots: provided args first, then defaults
  // Handle variadic parameters: pack extra args into array
  
  // Check if last arg is a kwargs object
  bool has_kwargs = false;
  auto *kwargs_obj = heap_.object(0);
  if (!args.empty() && args.back().isObjectId()) {
    kwargs_obj = heap_.object(args.back().asObjectId());
    if (kwargs_obj) {
            auto itEnd = kwargs_obj->find("end");
            if (itEnd != kwargs_obj->end()) {
                has_kwargs = true;
            } else {
                auto itDelim = kwargs_obj->find("delim");
                if (itDelim != kwargs_obj->end()) {
                    has_kwargs = true;
                }
            }
      if (has_kwargs) {
        args.pop_back();
      } else {
        kwargs_obj = heap_.object(0);
      }
    }
    }

    for (uint32_t i = 0; i < callee->param_count; i++) {
        if (callee->variadic_param_index != UINT32_MAX &&
            i == callee->variadic_param_index) {
            // Variadic parameter: pack remaining args into array
      auto arrRef = heap_.allocateArray();
      auto *arr = heap_.array(arrRef.id);
      for (size_t j = i; j < args.size(); j++) {
        arr->push_back(std::move(args[j]));
      }
      locals[base + i] = Value::makeArrayId(arrRef.id);
    } else if (i < args.size()) {
      locals[base + i] = std::move(args[i]);
    } else if (has_kwargs && i < callee->param_names.size() && kwargs_obj) {
      auto it = kwargs_obj->find(callee->param_names[i]);
      if (it != kwargs_obj->end()) {
        locals[base + i] = it->second;
      } else if (i < callee->default_values.size() &&
                 callee->default_values[i].has_value()) {
        const auto &dv = callee->default_values[i].value();
        // Sentinel: bool(true) means "fresh empty array" for arr=[] defaults
        if (dv.isBool() && dv.asBool()) {
          locals[base + i] = Value::makeArrayId(heap_.allocateArray().id);
        } else {
          locals[base + i] = dv;
        }
      } else {
        locals[base + i] = nullptr;
      }
    } else if (i < callee->default_values.size() &&
               callee->default_values[i].has_value()) {
      const auto &dv = callee->default_values[i].value();
      // Sentinel: bool(true) means "fresh empty array" for arr=[] defaults
      if (dv.isBool() && dv.asBool()) {
        locals[base + i] = Value::makeArrayId(heap_.allocateArray().id);
      } else {
        locals[base + i] = dv;
      }
    } else {
      locals[base + i] = nullptr; // No arg provided, no default
    }
  }
}

void VM::doTailCall(Value callee_value,
 std::vector<Value> args) {
 tail_call_depth_++;
 if (frame_count_ + tail_call_depth_ >= max_call_depth_) {
 tail_call_depth_ = 0;
 COMPILER_THROW("Stack overflow: maximum call depth " +
 std::to_string(max_call_depth_) + " reached");
 }

    if (callee_value.isCoroutineId()) {
        doCall(callee_value, std::move(args), false);
        return;
    }

    if (callee_value.isHostFuncId()) {
        Value result = callHostFunction(callee_value, args);
        pushStack(result);
        this->doReturn();
        return;
    }

    // Handle bound methods (lightweight BoundMethod struct)
    if (callee_value.isBoundMethodId()) {
        auto *bm = heap_.boundMethod(callee_value.asBoundMethodId());
        if (bm && (bm->fn.isHostFuncId() || bm->fn.isFunctionObjId() || bm->fn.isClosureId())) {
            std::vector<Value> boundArgs;
            boundArgs.push_back(bm->self);
            boundArgs.insert(boundArgs.end(), args.begin(), args.end());
            doCall(bm->fn, std::move(boundArgs), false);
            this->doReturn();
            return;
        }
    }

    // Handle callable objects (Lua-style __call / op_call)
    if (callee_value.isObjectId()) {
        auto *obj = heap_.object(callee_value.asObjectId());
        if (obj) {
            Value callFn = Value::makeNull();
            auto* search = obj;
            while (search) {
                auto* val = search->get("__call");
                if (!val) val = search->get("op_call");
                if (val) {
                    callFn = *val;
                    break;
                }
                auto* parentVal = search->get("__proto");
                if (!parentVal) parentVal = search->get("__class");
                if (!parentVal) parentVal = search->get("__parent");
                if (parentVal && parentVal->isObjectId()) {
                    search = heap_.object(parentVal->asObjectId());
                } else {
                    break;
                }
            }
            if (!callFn.isNull() && (callFn.isFunctionObjId() || callFn.isClosureId() || callFn.isHostFuncId())) {
                std::vector<Value> callArgs;
                callArgs.push_back(callee_value);
                callArgs.insert(callArgs.end(), args.begin(), args.end());
                doCall(callFn, std::move(callArgs), false);
                this->doReturn();
                return;
            }
            // Handle bound method objects
            auto fnIt = obj->find("fn");
            auto selfIt = obj->find("self");
            if (fnIt != obj->end() && selfIt != obj->end() &&
                (fnIt->second.isHostFuncId() || fnIt->second.isFunctionObjId() || fnIt->second.isClosureId())) {
                std::vector<Value> boundArgs;
                boundArgs.push_back(selfIt->second);
                boundArgs.insert(boundArgs.end(), args.begin(), args.end());
                doCall(fnIt->second, std::move(boundArgs), false);
                this->doReturn();
                return;
            }
        }
        COMPILER_THROW("TAIL_CALL: object is not callable");
    }

 const BytecodeChunk *resolve_chunk = current_chunk;
 uint32_t function_index = 0;
 uint32_t closure_id = 0;
 std::shared_ptr<std::unordered_map<std::string, Value>> tail_closure_globals;
 if (callee_value.isFunctionObjId()) {
 function_index = callee_value.asFunctionObjId();
 } else if (callee_value.isClosureId()) {
 closure_id = callee_value.asClosureId();
 auto *closure = heap_.closure(closure_id);
 if (!closure) {
 COMPILER_THROW("Closure not found: " +
 std::to_string(closure_id));
 }
 function_index = closure->function_index;
 if (closure->chunk) {
 resolve_chunk = closure->chunk;
 }
 if (closure->module_globals) {
 tail_closure_globals = closure->module_globals;
 }
 } else {
 std::string typeInfo = "unknown";
 if (callee_value.isNull()) typeInfo = "null";
        else if (callee_value.isInt()) typeInfo = "int";
        else if (callee_value.isDouble()) typeInfo = "double";
        else if (callee_value.isBool()) typeInfo = "bool";
else if (callee_value.isStringValId()) {
		typeInfo = current_chunk ? std::string("string_val_id='") + current_chunk->getString(callee_value.asStringValId()) + "'"
		                         : std::string("string_val_id=<") + std::to_string(callee_value.asStringValId()) + ">";
	}
	else if (callee_value.isStringId()) {
		auto *sp = heap_.string(callee_value.asStringId());
		typeInfo = sp ? std::string("string_id='") + *sp + "'" : std::string("string_id=<") + std::to_string(callee_value.asStringId()) + ">";
	}
	else if (callee_value.isArrayId()) typeInfo = "array_id";
	else if (callee_value.isEnumId()) typeInfo = "enum_id";
	COMPILER_THROW("TAIL_CALL expects function, closure, or callable object as callee (got " + typeInfo + ")");
    }

    if (!resolve_chunk) {
        COMPILER_THROW("No chunk available for tail call");
    }

    const auto *callee = resolve_chunk->getFunction(function_index);
    if (!callee) {
        COMPILER_THROW("Function index not found: " +
                       std::to_string(function_index));
  }

  // Allow fewer arguments than parameters (for default parameters)
  // For variadic functions, allow MORE arguments than parameters
  if (callee->variadic_param_index == UINT32_MAX &&
      args.size() > callee->param_count) {
    COMPILER_THROW(
        "Argument count mismatch for tail call to function index " +
        std::to_string(function_index) + " (expected at most " +
        std::to_string(callee->param_count) + ", got " +
        std::to_string(args.size()) + ")");
  }

  // For variadic functions, require at least as many args as non-variadic
  // params
  if (callee->variadic_param_index != UINT32_MAX &&
      args.size() < callee->variadic_param_index) {
    COMPILER_THROW(
        "Argument count mismatch for tail call to function index " +
        std::to_string(function_index) + " (expected at least " +
        std::to_string(callee->variadic_param_index) + ", got " +
        std::to_string(args.size()) + ")");
  }

    // TCO: Reuse current frame - update function, reset IP, adjust locals
    auto &current_frame = currentFrame();
    size_t old_base = current_frame.locals_base;

    // Close open upvalues for current frame before reusing it
    // Otherwise closures capturing locals from this frame see corrupted data
    // when the new function overwrites those local slots
    closeFrameUpvalues(static_cast<uint32_t>(old_base),
                       static_cast<uint32_t>(locals.size()));

 // Update frame to point to new function
 current_frame.function = callee;
 current_frame.chunk = resolve_chunk;
 current_frame.ip = 0;
 current_frame.closure_id = closure_id;
 current_chunk = resolve_chunk;
 if (tail_closure_globals) {
 if (!current_frame.owns_globals) {
 globals_stack_.push_back(std::move(globals));
 current_frame.owns_globals = true;
 }
 globals = *tail_closure_globals;
 }
 // Keep same locals base

  // Resize locals if needed (reuse existing space)
  size_t new_locals_needed = old_base + callee->local_count;
  if (locals.size() < new_locals_needed) {
    locals.resize(new_locals_needed, nullptr);
  }

  // Tail-call reuses the same frame but a different function; the new
  // function's val declarations will repopulate immutable_locals_
  immutable_locals_.clear();

  // Set up arguments in the reused frame (at old_base): provided args first,
  // then defaults Handle variadic parameters: pack extra args into array
  for (uint32_t i = 0; i < callee->param_count; i++) {
    if (callee->variadic_param_index != UINT32_MAX &&
        i == callee->variadic_param_index) {
      // Variadic parameter: pack remaining args into array
      auto arrRef = heap_.allocateArray();
      auto *arr = heap_.array(arrRef.id);
      for (size_t j = i; j < args.size(); j++) {
        arr->push_back(std::move(args[j]));
      }
      locals[old_base + i] = Value::makeArrayId(arrRef.id);
    } else if (i < args.size()) {
      locals[old_base + i] = std::move(args[i]);
    } else if (i < callee->default_values.size() &&
               callee->default_values[i].has_value()) {
      locals[old_base + i] = callee->default_values[i].value();
    } else {
      locals[old_base + i] = nullptr;
    }
  }

  // Clear remaining locals from old function
  // Start after callee's params (which may include defaults set above),
  // not after the provided args count
  for (size_t i = old_base + callee->param_count; i < new_locals_needed; i++) {
    locals[i] = nullptr;
  }
}

void VM::closeFrameUpvalues(uint32_t locals_base, uint32_t locals_end) {
  if (locals_end < locals_base) {
    return;
  }

  std::vector<uint32_t> to_close;
  to_close.reserve(open_upvalues.size());
  for (const auto &[index, _] : open_upvalues) {
    if (index >= locals_base && index < locals_end) {
      to_close.push_back(index);
    }
  }

  for (uint32_t index : to_close) {
    auto it = open_upvalues.find(index);
    if (it == open_upvalues.end() || !it->second) {
      continue;
    }
      auto &cell = it->second;
    if (index < locals.size()) {
      cell->closed_value = locals[index];
    } else {
      cell->closed_value = nullptr;
    }
    cell->is_open = false;
    open_upvalues.erase(it);
  }
}

std::vector<Value> VM::stackValuesForRoots() const {
  std::vector<Value> values;
  std::stack<Value> copy = stack;
  values.reserve(copy.size());
  while (!copy.empty()) {
    values.push_back(copy.top());
    copy.pop();
  }
  return values;
}

std::vector<uint32_t> VM::activeClosureIdsForRoots() const {
  std::vector<uint32_t> closure_ids;
  closure_ids.reserve(frame_count_);
  for (size_t i = 0; i < frame_count_; ++i) {
    const auto &frame = frame_arena_[i];
    if (frame.closure_id != 0) {
      closure_ids.push_back(frame.closure_id);
    }
  }
  return closure_ids;
}

void VM::maybeCollectGarbage() {
 if (gc_suspend_counter_ > 0) return;
 heap_.maybeCollectGarbage(
 stackValuesForRoots(), locals, globals, activeClosureIdsForRoots(),
 [this](uint32_t index) -> std::optional<Value> {
 if (index >= locals.size()) {
 return std::nullopt;
 }
 return locals[index];
 });
 }

void VM::drainFinalizers() {
    auto finalizers = heap_.drainFinalizers();
    for (auto &[objId, entry] : finalizers) {
        auto *val = entry.get("op_destructor");
        if (val && (val->isFunctionObjId() || val->isClosureId() || val->isHostFuncId())) {
            try {
                callFunctionSync(*val, {Value::makeObjectId(objId)});
            } catch (...) {
            }
        }
    }
}

void VM::collectGarbage() {
  heap_.collectGarbage(stackValuesForRoots(), locals, globals,
                       activeClosureIdsForRoots(),
                       [this](uint32_t index) -> std::optional<Value> {
                         if (index >= locals.size()) {
                           return std::nullopt;
                         }
                         return locals[index];
                       });
}

void VM::stepGarbageCollection(size_t work_budget) {
  heap_.stepGarbageCollection(
      stackValuesForRoots(), locals, globals, activeClosureIdsForRoots(),
      [this](uint32_t index) -> std::optional<Value> {
        if (index >= locals.size()) {
          return std::nullopt;
        }
        return locals[index];
      },
      work_budget);
}

void VM::garbageCollectionSafePoint(size_t work_budget) {
  if (active_hotkey_executions_.load(std::memory_order_acquire) != 0) {
    return;
  }
  stepGarbageCollection(work_budget);
}

// ============================================================================
// Stack helpers - extracted from executeInstruction to reduce stack frame size
// ============================================================================

Value VM::popStack() {
  if (stack.empty()) {
    if (frame_count_ > 0) {
      const auto &frame = currentFrame();
      ::havel::error("Stack underflow in function '{}' at IP {}", frame.function->name, frame.ip);
      if (frame.ip < frame.function->instructions.size()) {
        const auto &instr = frame.function->instructions[frame.ip];
        ::havel::error("Offending Instruction: IP {} OpCode {}", frame.ip, static_cast<int>(instr.opcode));
      }
    }
    COMPILER_THROW("Stack underflow");
  }
  Value value = stack.top();
  stack.pop();
  return value;
}

void VM::pushStack(Value value) { stack.push(std::move(value)); }

uint32_t VM::toAbsoluteLocal(uint32_t local_index) {
  size_t base = frame_count_ > 0 ? currentFrame().locals_base : 0;
  size_t result = base + local_index;
  // Defensive: check for overflow or excessive index
  if (result > 1000000 || result > UINT32_MAX) {
    COMPILER_THROW("Local variable index overflow: base=" + std::to_string(base) +
                   ", local=" + std::to_string(local_index));
  }
  return static_cast<uint32_t>(result);
}

void VM::ensureLocalIndex(uint32_t absolute_index) {
  if (absolute_index >= locals.size()) {
    // Defensive: prevent overflow when absolute_index is near UINT32_MAX
    // Also prevent excessive resizing (sanity check: max 1M locals)
    if (absolute_index > 1000000) {
      COMPILER_THROW("Local variable index too large: " + std::to_string(absolute_index));
    }
    size_t new_size = static_cast<size_t>(absolute_index) + 1;
    locals.resize(new_size, nullptr);
  }
}

void VM::doReturn() {
    tail_call_depth_ = 0;
    if (frame_count_ == 0) {
        return;
    }

    Value ret = nullptr;
    if (!stack.empty()) {
        ret = popStack();
    }

    // Materialize chunk-relative StringValId values into heap-stable StringId
    // while current_chunk still points to the callee's chunk. StringValId is
    // a chunk-local index — after restoring the parent frame's chunk it would
    // resolve to the wrong string. Deep walk handles objects/arrays too.
    if (current_chunk && (ret.isStringValId() || ret.isObjectId() || ret.isArrayId())) {
        ret = deepMaterializeStrings(ret, current_chunk);
    }

 auto finished = frame_arena_[frame_count_ - 1];
 frame_count_--;

 // Restore current_chunk from parent frame
 if (frame_count_ > 0) {
 current_chunk = frame_arena_[frame_count_ - 1].chunk;
 }

 // Restore globals if this frame swapped them (module closure call)
    if (finished.owns_globals && !globals_stack_.empty()) {
        globals = std::move(globals_stack_.back());
        globals_stack_.pop_back();
    }

 closeFrameUpvalues(static_cast<uint32_t>(finished.locals_base),
 static_cast<uint32_t>(locals.size()));

if (locals.size() >= finished.locals_base) {
    locals.resize(finished.locals_base);
}

// immutable_locals_ stores absolute indices into the locals vector.
// After returning from a frame and truncating locals, those indices
// are stale — clear them so the parent frame's val declarations
// can be re-established on next function entry.
immutable_locals_.clear();

// Restore expression stack to the depth at call time, preserving return value
while (stack.size() > finished.stack_depth) {
    popStack();
}

            if (current_coroutine_id_ != UINT32_MAX) {
                auto *co = heap_.coroutine(current_coroutine_id_);
                if (co) {
                    co->state = GCHeap::Coroutine::Done;
                    if (!co->caller_stack.empty()) {
                        auto &caller = co->caller_stack.back();
                        frame_count_ = caller.frame_count;
                        locals = caller.locals;
                        immutable_locals_.clear();
                        current_coroutine_id_ = caller.coroutine_id;

                        currentFrame().ip = caller.ip;

                        stack = std::stack<Value>();
                        for (auto it = caller.stack.begin(); it != caller.stack.end(); ++it) {
                            stack.push(*it);
                        }

                        co->caller_stack.pop_back();
                    }
                }
            }

    pushStack(ret);
}




void VM::emitVariableChanged(const std::string& var_name) {
  if (!event_queue_ || !event_queue_->hasHandler(EventType::VAR_CHANGED)) return;
  uint32_t var_hash = std::hash<std::string>{}(var_name);
  Event change_event(EventType::VAR_CHANGED, var_hash, new std::string(var_name));
  event_queue_->push(change_event);
}

void VM::throwError(const std::string &msg) {
  COMPILER_THROW(msg);
}


Value VM::deepMaterializeStrings(Value value, const BytecodeChunk* chunk) {
    std::unordered_set<uint32_t> visited;
    return deepMaterializeStrings(value, chunk, visited);
}

Value VM::deepMaterializeStrings(Value value, const BytecodeChunk* chunk, std::unordered_set<uint32_t>& visited) {
    if (!chunk) return value;

if (value.isStringValId()) {
auto strRef = heap_.allocateString(chunk->getString(value.asStringValId()));
return Value::makeStringId(strRef.id);
}

if (value.isFunctionObjId() && chunk != current_chunk) {
auto closureRef = heap_.allocateClosure(GCHeap::RuntimeClosure{
.function_index = value.asFunctionObjId(),
.chunk_index = 0,
.chunk = chunk,
.module_globals = nullptr,
.upvalues = {}});
return Value::makeClosureId(closureRef.id);
}

    if (value.isObjectId()) {
        auto* obj = heap_.object(value.asObjectId());
        if (!obj) return value;
        uint32_t objId = value.asObjectId();
        if (visited.count(objId)) return value;
        visited.insert(objId);
        std::vector<std::pair<std::string, Value>> entries;
        for (auto& [k, v] : *obj) {
            Value mat_key_v = deepMaterializeStrings(v, chunk, visited);
            entries.emplace_back(k, mat_key_v);
        }
        for (auto& [k, v] : entries) {
            (*obj)[k] = std::move(v);
        }
        return value;
    }

    if (value.isArrayId()) {
        auto* arr = heap_.array(value.asArrayId());
        if (!arr) return value;
        uint32_t arrId = value.asArrayId();
        if (visited.count(arrId)) return value;
        visited.insert(arrId);
        for (auto& elem : *arr) {
            elem = deepMaterializeStrings(elem, chunk, visited);
        }
        return value;
    }

    return value;
}

Value VM::deepWrapModuleFunctions(Value value, std::shared_ptr<BytecodeChunk> chunk,
    const std::unordered_map<std::string, Value>& moduleGlobals,
    const std::string& canonicalKey, const std::string& fieldPath, int depth,
    std::unordered_set<uint32_t>* visitedPtr) {
  if (depth > 64) return value;
  bool suspendedGc = false;
  if (depth == 0) { suspendGC(); suspendedGc = true; }
  auto resumeGcGuard = [&]() { if (suspendedGc) { resumeGC(); suspendedGc = false; } };
  if (value.isFunctionObjId() && chunk) {
        uint32_t funcIdx = value.asFunctionObjId();
        const auto* moduleFunc = chunk->getFunction(funcIdx);
        uint32_t paramCount = moduleFunc ? moduleFunc->param_count : 0;
    auto moduleChunk = chunk;
auto wrapperName = "$module_fn_" + canonicalKey + "_" + fieldPath;
std::string fnCapturedKey = canonicalKey;
std::string fnCapturedField = fieldPath;
    registerHostFunction(wrapperName,
    [this, funcIdx, moduleChunk, paramCount, moduleGlobals, wrapperName, fnCapturedKey, fnCapturedField](const std::vector<Value>& args) -> Value {
    std::vector<Value> callArgs = args;
    if (callArgs.size() > paramCount && paramCount > 0) {
    callArgs.erase(callArgs.begin());
    }
    auto* savedChunk = current_chunk;
                    auto savedGlobals = globals;
                    auto savedMirrorId = globals_mirror_object_id_;
                    Value savedG = globals["_G"];
                    globals = moduleGlobals;
                    current_chunk = moduleChunk.get();
    const auto* callee = moduleChunk->getFunction(funcIdx);
    if (!callee) {
    globals = std::move(savedGlobals);
    globals_mirror_object_id_ = savedMirrorId;
    globals["_G"] = savedG;
    current_chunk = savedChunk;
    return Value::makeNull();
    }
    size_t base = locals.size();
                    size_t savedLocalsSize = base;
                    locals.resize(base + callee->local_count, nullptr);
                    uint32_t frame_stack_depth = static_cast<uint32_t>(stack.size());
                    if (frame_arena_.size() <= frame_count_) {
                        CallFrame cf;
                        cf.function = callee;
                        cf.chunk = moduleChunk.get();
                        cf.ip = 0;
                        cf.locals_base = base;
                        cf.stack_depth = frame_stack_depth;
                        frame_arena_.push_back(std::move(cf));
                    } else {
                        frame_arena_[frame_count_].function = callee;
                        frame_arena_[frame_count_].chunk = moduleChunk.get();
                        frame_arena_[frame_count_].ip = 0;
                        frame_arena_[frame_count_].locals_base = base;
                        frame_arena_[frame_count_].stack_depth = frame_stack_depth;
                    }
                    frame_count_++;
        for (uint32_t i = 0; i < callee->param_count; i++) {
            if (i < callArgs.size()) {
                Value argVal = callArgs[i];
                if (savedChunk) {
                    argVal = deepMaterializeStrings(argVal, savedChunk);
                }
                locals[base + i] = std::move(argVal);
            } else if (i < callee->default_values.size() &&
                       callee->default_values[i].has_value()) {
                const auto &dv = callee->default_values[i].value();
                if (dv.isBool() && dv.asBool()) {
                    locals[base + i] = Value::makeArrayId(heap_.allocateArray().id);
                } else {
                    locals[base + i] = dv;
                }
            } else {
                locals[base + i] = Value::makeNull();
            }
        }
        runDispatchLoop(frame_count_ - 1);
        Value result = popStack();
        if (locals.size() > savedLocalsSize) {
            locals.resize(savedLocalsSize);
        }
        if (bc_execute_depth_ == 0) {
            result = deepWrapModuleFunctions(deepMaterializeStrings(result, current_chunk),
                moduleChunk, moduleGlobals, fnCapturedKey, fnCapturedField + "_ret");
        }
        globals = std::move(savedGlobals);
        globals_mirror_object_id_ = savedMirrorId;
        globals["_G"] = savedG;
        current_chunk = savedChunk;
        return result;
    });
        uint32_t hostIdx = host_function_globals_[wrapperName].asHostFuncId();
        resumeGcGuard();
        return Value::makeHostFuncId(hostIdx);
    }

    if (value.isClosureId() && chunk) {
        uint32_t closureId = value.asClosureId();
        auto* rc = heap_.closure(closureId);
        if (!rc || !rc->chunk) { resumeGcGuard(); return value; }
        if (rc->chunk != chunk.get()) { resumeGcGuard(); return value; }

        // Pin the closure as a GC root so the host function wrapper
        // can safely reference it by ID across GC cycles.
        pinExternalRoot(Value::makeClosureId(closureId));

    uint32_t funcIdx = rc->function_index;
    auto moduleChunk = chunk;
    auto closureGlobals = rc->module_globals
        ? rc->module_globals
        : std::make_shared<std::unordered_map<std::string, Value>>(moduleGlobals);
    auto wrapperName = "$module_closure_" + canonicalKey + "_" + fieldPath;
    std::string capturedKey = canonicalKey;
    std::string capturedField = fieldPath;
    registerHostFunction(wrapperName,
        [this, closureId, funcIdx, moduleChunk, closureGlobals, wrapperName, capturedKey, capturedField](const std::vector<Value>& args) -> Value {
            auto* rc2 = heap_.closure(closureId);
            if (!rc2 || !rc2->chunk) return Value::makeNull();

            auto* savedChunk = current_chunk;
            auto savedGlobals = globals;
            auto savedMirrorId = globals_mirror_object_id_;
            Value savedG = globals["_G"];
            globals = *closureGlobals;
            current_chunk = moduleChunk.get();

            const auto* callee = moduleChunk->getFunction(funcIdx);
            if (!callee) {
                globals = std::move(savedGlobals);
                globals_mirror_object_id_ = savedMirrorId;
                globals["_G"] = savedG;
                current_chunk = savedChunk;
                return Value::makeNull();
            }

            size_t base = locals.size();
            locals.resize(base + callee->local_count, nullptr);
            uint32_t frame_stack_depth = static_cast<uint32_t>(stack.size());
            if (frame_arena_.size() <= frame_count_) {
                CallFrame cf;
                cf.function = callee;
                cf.chunk = moduleChunk.get();
                cf.ip = 0;
                cf.locals_base = base;
                cf.closure_id = closureId;
                cf.stack_depth = frame_stack_depth;
                cf.owns_globals = true;
                frame_arena_.push_back(std::move(cf));
            } else {
                frame_arena_[frame_count_].function = callee;
                frame_arena_[frame_count_].chunk = moduleChunk.get();
                frame_arena_[frame_count_].ip = 0;
                frame_arena_[frame_count_].locals_base = base;
                frame_arena_[frame_count_].closure_id = closureId;
                frame_arena_[frame_count_].stack_depth = frame_stack_depth;
                frame_arena_[frame_count_].owns_globals = true;
            }
            frame_count_++;

        for (uint32_t i = 0; i < callee->param_count; i++) {
            if (i < args.size()) {
                Value argVal = args[i];
                if (savedChunk) {
                    argVal = deepMaterializeStrings(argVal, savedChunk);
                }
                locals[base + i] = std::move(argVal);
            } else {
                locals[base + i] = Value::makeNull();
            }
        }

 runDispatchLoop(frame_count_ - 1);
 Value result = popStack();
 if (bc_execute_depth_ == 0) {
 result = deepWrapModuleFunctions(deepMaterializeStrings(result, current_chunk),
 moduleChunk, *closureGlobals, capturedKey, capturedField + "_ret");
 }
 globals = std::move(savedGlobals);
 globals_mirror_object_id_ = savedMirrorId;
 globals["_G"] = savedG;
 current_chunk = savedChunk;
 return result;
 });
        uint32_t hostIdx = host_function_globals_[wrapperName].asHostFuncId();
        resumeGcGuard();
        return Value::makeHostFuncId(hostIdx);
    }

  if (value.isObjectId()) {
    uint32_t objId = value.asObjectId();
    if (visitedPtr && visitedPtr->count(objId)) { resumeGcGuard(); return value; }
    auto* srcObj = heap_.object(objId);
    if (!srcObj) { resumeGcGuard(); return value; }
    bool anyWrapped = false;
    std::vector<std::pair<std::string, Value>> entries;
    entries.reserve(srcObj->size());
    std::unordered_set<uint32_t> localVisited;
    if (!visitedPtr) visitedPtr = &localVisited;
    visitedPtr->insert(objId);
    for (const auto& [k, v] : *srcObj) {
      Value wrapped = deepWrapModuleFunctions(v, chunk, moduleGlobals, canonicalKey,
        fieldPath.empty() ? k : (fieldPath + "." + k), depth + 1, visitedPtr);
      if (!(wrapped == v)) anyWrapped = true;
      entries.emplace_back(k, std::move(wrapped));
    }
    if (!anyWrapped) {
      visitedPtr->erase(objId);
      resumeGcGuard();
      return value;
    }
    ObjectRef copyRef = createHostObject();
    auto* copyObj = heap_.object(copyRef.id);
    for (auto& [k, v] : entries) {
      (*copyObj)[k] = std::move(v);
    }
    visitedPtr->erase(objId);
    resumeGcGuard();
    return Value::makeObjectId(copyRef.id);
  }

  if (value.isArrayId()) {
    uint32_t arrId = value.asArrayId();
    if (visitedPtr && visitedPtr->count(arrId)) { resumeGcGuard(); return value; }
    auto* srcArr = heap_.array(arrId);
    if (!srcArr) { resumeGcGuard(); return value; }
    bool anyWrapped = false;
    std::vector<Value> elements;
    elements.reserve(srcArr->size());
    std::unordered_set<uint32_t> localVisited;
    if (!visitedPtr) visitedPtr = &localVisited;
    visitedPtr->insert(arrId);
    for (size_t i = 0; i < srcArr->size(); i++) {
      Value wrapped = deepWrapModuleFunctions((*srcArr)[i], chunk, moduleGlobals, canonicalKey,
        fieldPath + "[" + std::to_string(i) + "]", depth + 1, visitedPtr);
      if (!(wrapped == (*srcArr)[i])) anyWrapped = true;
      elements.push_back(std::move(wrapped));
    }
    if (!anyWrapped) {
      visitedPtr->erase(arrId);
      resumeGcGuard();
      return value;
    }
    ArrayRef copyRef = createHostArray();
    auto* copyArr = heap_.array(copyRef.id);
    copyArr->reserve(elements.size());
    for (auto& elem : elements) {
      copyArr->push_back(std::move(elem));
    }
    visitedPtr->erase(arrId);
    resumeGcGuard();
    return Value::makeArrayId(copyRef.id);
  }

    resumeGcGuard();
    return value;
}

Value VM::loadModule(const std::string& path) {
    // Check cache via canonical ModuleLoader
    if (moduleLoader_.isCached(path)) {
        Value cachedVal;
        if (moduleLoader_.getCached(path, &cachedVal)) {
            return cachedVal;
        }
    }

    // Resolve the module path
    auto resolved = moduleLoader_.resolve(path, current_script_dir_);
    if (resolved) {
        std::string canonicalKey = resolved->canonicalPath;
        
        // Check cache by resolved path
        if (moduleLoader_.isCached(canonicalKey)) {
            Value cachedVal;
            if (moduleLoader_.getCached(canonicalKey, &cachedVal)) {
                Value exports = cachedVal;
                // Also cache under the original key for faster lookup next time
                moduleLoader_.putCache(path, exports);
                return exports;
            }
        }
    }

    if (!resolved) {
        std::string prefix = path + ".";
        bool hasNamespace = false;
        for (const auto& [name, value] : host_function_globals_) {
            if (name.rfind(prefix, 0) == 0) { hasNamespace = true; break; }
        }
        if (hasNamespace || (context_ && context_->hostBridge && context_->hostBridge->loadModule(path))) {
            auto exportsObj = createHostObject();
            auto *obj = heap_.object(exportsObj.id);
            for (const auto& [name, value] : host_function_globals_) {
                if (name.rfind(prefix, 0) == 0) {
                    std::string localName = name.substr(prefix.size());
                    (*obj)[localName] = value;
                }
            }
Value exports = Value::makeObjectId(exportsObj.id);
    moduleLoader_.putCache(path, exports);
            return exports;
        }
        // Fallback: try modules/ directory relative to executable
        {
            std::filesystem::path modulesPath;
            std::error_code ec;
            auto exePath = std::filesystem::read_symlink("/proc/self/exe", ec);
            if (!ec && !exePath.empty()) {
                modulesPath = exePath.parent_path() / ".." / "modules" / (path + ".hv");
            } else {
                modulesPath = std::filesystem::path("modules") / (path + ".hv");
            }
            if (std::filesystem::exists(modulesPath, ec)) {
                resolved = ModuleLoader::ResolvedModule{
                    ModuleLoader::ResolvedModule::UserSource,
                    std::filesystem::canonical(modulesPath, ec).string(),
                    path
                };
            }
        }
        if (!resolved) {
            COMPILER_THROW("Module not found: " + path);
        }
    }

  std::string canonicalKey = resolved->canonicalPath;

  // Circular dependency detection
  if (modules_loading_.count(canonicalKey)) {
    COMPILER_THROW("Circular dependency detected: " + path);
  }
  modules_loading_.insert(canonicalKey);

  std::string prev_script_dir = current_script_dir_;
  std::shared_ptr<BytecodeChunk> chunk;

  if (resolved->type == ModuleLoader::ResolvedModule::BytecodeCache) {
    // Load pre-compiled .hvc bytecode
    std::ifstream file(resolved->canonicalPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
      modules_loading_.erase(canonicalKey);
      COMPILER_THROW("Failed to open bytecode file: " + resolved->canonicalPath);
    }
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> buffer(size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
      modules_loading_.erase(canonicalKey);
      COMPILER_THROW("Failed to read bytecode file: " + resolved->canonicalPath);
    }
    ValueSerializer serializer;
    auto deserialized = serializer.deserializeChunk(buffer);
    if (!deserialized) {
      modules_loading_.erase(canonicalKey);
      COMPILER_THROW("Failed to deserialize bytecode: " + resolved->canonicalPath);
    }
    chunk = std::make_shared<BytecodeChunk>(std::move(*deserialized));
    current_script_dir_ = std::filesystem::path(resolved->canonicalPath).parent_path().string();
  } else {
    // Read source file and compile
    std::ifstream file(resolved->canonicalPath);
    if (!file.is_open()) {
      modules_loading_.erase(canonicalKey);
      COMPILER_THROW("Failed to open module file: " + resolved->canonicalPath);
    }
    std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    // Set script directory for relative imports within the module
    current_script_dir_ = std::filesystem::path(resolved->canonicalPath).parent_path().string();

    // Compile the module source using the real parser + ByteCompiler pipeline
    // (CompilationPipeline is a stub — we must use the same path as runBytecodePipeline)
    parser::Parser parser{{}};
    std::unique_ptr<ast::Program> program;
    try {
      program = parser.produceAST(source);
    } catch (const ::havel::LexError &e) {
      modules_loading_.erase(canonicalKey);
      current_script_dir_ = prev_script_dir;
      COMPILER_THROW("Module " + path + " lexer error: " + e.what());
    } catch (const ::havel::parser::ParseError &e) {
      modules_loading_.erase(canonicalKey);
      current_script_dir_ = prev_script_dir;
      COMPILER_THROW("Module " + path + " parse error: " + e.what());
    }
    if (!program || parser.hasErrors()) {
      modules_loading_.erase(canonicalKey);
      current_script_dir_ = prev_script_dir;
      std::string errors;
      if (parser.hasErrors()) {
        for (const auto &err : parser.getErrors()) errors += err.message + "\n";
      }
      COMPILER_THROW("Module " + path + " failed to parse: " + errors);
    }

    ByteCompiler compiler;

    try {
      chunk = std::shared_ptr<BytecodeChunk>(compiler.compile(*program).release());
    } catch (const std::exception &e) {
      modules_loading_.erase(canonicalKey);
      current_script_dir_ = prev_script_dir;
      COMPILER_THROW("Module " + path + " compilation error: " + std::string(e.what()));
    }
    if (!chunk) {
      modules_loading_.erase(canonicalKey);
      current_script_dir_ = prev_script_dir;
      COMPILER_THROW("Module " + path + " compiler returned null chunk");
    }
  }

    // Execute the module in a sandboxed globals context
    // Save current globals state
    globals_stack_.push_back(globals);
    auto saved_immutable_globals = immutable_globals_;
    auto old_mirror_id = globals_mirror_object_id_;
    Value old_g = globals["_G"];

    // Save caller's execution state (stack, locals, frames, chunk, exception)
    auto saved_stack = stack;
    auto saved_locals = locals;
    auto saved_frame_count = frame_count_;
    auto saved_frames = frame_arena_;
    const BytecodeChunk *saved_chunk = current_chunk;
    bool saved_exception = has_current_exception_;
    Value saved_exception_val = current_exception_;

    // Fresh globals for the module — populate with host globals so
    // the module can call print(), len(), str, etc.
    std::unordered_set<std::string> inheritedGlobalNames;
    std::unordered_map<std::string, Value> inheritedGlobalValues;
    globals.clear();
    // Register host function globals into sandbox (print, len, str, etc.)
    for (const auto& [name, value] : host_function_globals_) {
        globals[name] = value;
        inheritedGlobalNames.insert(name);
        inheritedGlobalValues[name] = value;
    }
    // Also carry over namespace objects (fs, sys, math, etc.) from the
    // caller's globals so module code can call fs.read(), sys.cwd(), etc.
    auto &callerGlobals = globals_stack_.back();
    for (const auto& [name, value] : callerGlobals) {
        if (name.empty() || name[0] == '_') continue;
        if (globals.count(name)) continue; // don't overwrite host function globals
        if (value.isObjectId()) {
            globals[name] = value;
            inheritedGlobalNames.insert(name);
            inheritedGlobalValues[name] = value;
        }
    }
    auto g_obj = createHostObject();
    globals_mirror_object_id_ = g_obj.id;
    globals["_G"] = Value::makeObjectId(g_obj.id);
    // Also register the _G mirror with host function entries
    for (const auto& [name, value] : host_function_globals_) {
        setHostObjectField(g_obj, name, value);
    }

    // Set up the module's execution context WITHOUT resetting the heap.
    // execute() would call heap_.reset() which destroys the caller's objects.
    // Instead, we set up the call frame directly (like executePersistent).
    current_chunk = chunk.get();
    const auto *entry = chunk->getFunction("__main__");
    if (!entry) {
        // Restore everything on error
        globals = std::move(globals_stack_.back());
        globals_stack_.pop_back();
        globals["_G"] = old_g;
        globals_mirror_object_id_ = old_mirror_id;
        immutable_globals_ = saved_immutable_globals;
        stack = std::move(saved_stack);
        locals = std::move(saved_locals);
        immutable_locals_.clear();
        frame_count_ = saved_frame_count;
        frame_arena_ = std::move(saved_frames);
        current_chunk = saved_chunk;
        has_current_exception_ = saved_exception;
        current_exception_ = saved_exception_val;
        current_script_dir_ = prev_script_dir;
        modules_loading_.erase(canonicalKey);
        COMPILER_THROW("Module " + path + " has no __main__ function");
    }

    while (!stack.empty()) stack.pop();
    locals.clear();
    frame_count_ = 0;
    open_upvalues.clear();
    has_current_exception_ = false;
    current_exception_ = nullptr;

    if (frame_arena_.size() <= frame_count_) {
        frame_arena_.push_back(CallFrame{entry, chunk.get(), 0, 0, 0});
    } else {
        frame_arena_[frame_count_] = CallFrame{entry, chunk.get(), 0, 0, 0};
    }
    frame_count_++;
    locals.resize(entry->local_count);

    // Execute the module's bytecode (same heap, sandboxed globals)
    Value exec_result;
    try {
        runDispatchLoop(0);
        if (!stack.empty()) {
            exec_result = stack.top();
            stack.pop();
        }
    } catch (...) {
        // Restore caller's globals and execution state on error
        globals = std::move(globals_stack_.back());
        globals_stack_.pop_back();
        globals["_G"] = old_g;
        globals_mirror_object_id_ = old_mirror_id;
        immutable_globals_ = saved_immutable_globals;
        stack = std::move(saved_stack);
        locals = std::move(saved_locals);
        immutable_locals_.clear();
        frame_count_ = saved_frame_count;
        frame_arena_ = std::move(saved_frames);
        current_chunk = saved_chunk;
        has_current_exception_ = saved_exception;
        current_exception_ = saved_exception_val;
        current_script_dir_ = prev_script_dir;
        modules_loading_.erase(canonicalKey);
        throw;
    }

    // Keep module chunk alive so exported functions can reference it
    module_chunks_[canonicalKey] = chunk;

    // Materialize chunk-relative values into heap-stable values before
    // restoring the caller's chunk. StringValId and FunctionObjId are
    // indices into the *module's* chunk — they'd resolve against the
    // caller's chunk after restore, producing garbage.
    auto exportsObj = createHostObject();
    auto *obj = heap_.object(exportsObj.id);
    auto moduleGlobalsSnapshot = globals;
    int exportCount = 0;
    for (const auto& [name, value] : globals) {
        if (name.empty() || name[0] == '_') continue;
        // Skip inherited globals UNLESS the module redefined them
        // (i.e., the value is different from what was inherited)
        if (inheritedGlobalNames.count(name)) {
            auto it = inheritedGlobalValues.find(name);
            if (it != inheritedGlobalValues.end()) {
                const auto& inheritedVal = it->second;
                // If value is identical to what was inherited, skip it
                // Use raw comparison: same type + same ID/index
                bool same = false;
                if (inheritedVal.isHostFuncId() && value.isHostFuncId() && inheritedVal.asHostFuncId() == value.asHostFuncId()) same = true;
                else if (inheritedVal.isObjectId() && value.isObjectId() && inheritedVal.asObjectId() == value.asObjectId()) same = true;
                else if (inheritedVal.isInt() && value.isInt() && inheritedVal.asInt() == value.asInt()) same = true;
                else if (inheritedVal.isStringId() && value.isStringId() && inheritedVal.asStringId() == value.asStringId()) same = true;
                else if (inheritedVal.isNull() && value.isNull()) same = true;
                if (same) continue;
            }
        }
        Value materialized = deepMaterializeStrings(value, current_chunk);
        materialized = deepWrapModuleFunctions(materialized, chunk, moduleGlobalsSnapshot,
            canonicalKey, name);
        (*obj)[name] = materialized;
        exportCount++;
    }
    Value exports = Value::makeObjectId(exportsObj.id);

    // Merge C++ host module globals (e.g., math.ceil, math.sqrt) into exports
    // when a .hv module shadows a native module. The .hv module's own exports
    // take priority; host functions are only added for missing keys.
    {
        std::string prefix = path + ".";
        for (const auto& [name, value] : host_function_globals_) {
            if (name.rfind(prefix, 0) != 0) continue;
            std::string localName = name.substr(prefix.size());
            if (!obj->get(localName)) {
                (*obj)[localName] = value;
            }
        }
    }

    // Restore caller's globals and execution state
    globals = std::move(globals_stack_.back());
    globals_stack_.pop_back();
    globals["_G"] = old_g;
    globals_mirror_object_id_ = old_mirror_id;
    immutable_globals_ = saved_immutable_globals;
    stack = std::move(saved_stack);
    locals = std::move(saved_locals);
    immutable_locals_.clear();
    frame_count_ = saved_frame_count;
    frame_arena_ = std::move(saved_frames);
    current_chunk = saved_chunk;
    has_current_exception_ = saved_exception;
    current_exception_ = saved_exception_val;
    current_script_dir_ = prev_script_dir;

    // Cache under both keys via canonical ModuleLoader
    moduleLoader_.putCache(path, exports);
    moduleLoader_.putCache(canonicalKey, exports);
    modules_loading_.erase(canonicalKey);

    return exports;
}

Value VM::runInContext(const std::string& source, Value context) {
  globals_stack_.push_back(globals);
  auto old_mirror_id = globals_mirror_object_id_;
  Value old_g = globals["_G"];

  if (context.isNull()) {
    globals.clear();
    auto g_obj = createHostObject();
    globals_mirror_object_id_ = g_obj.id;
    globals["_G"] = Value::makeObjectId(g_obj.id);
  } else if (context.isObjectId()) {
    globals.clear();
    globals_mirror_object_id_ = context.asObjectId();
    globals["_G"] = context;
  } else {
    globals_stack_.pop_back();
    throwError("runInContext: context must be null or object");
    return Value::makeNull();
  }

 parser::Parser parser{{}};
 std::unique_ptr<ast::Program> program;
 try {
 program = parser.produceAST(source);
 } catch (const ::havel::LexError &) {
 globals = std::move(globals_stack_.back());
 globals_stack_.pop_back();
 globals["_G"] = old_g;
 globals_mirror_object_id_ = old_mirror_id;
 return Value::makeNull();
 } catch (const ::havel::parser::ParseError &) {
 globals = std::move(globals_stack_.back());
 globals_stack_.pop_back();
 globals["_G"] = old_g;
 globals_mirror_object_id_ = old_mirror_id;
 return Value::makeNull();
 }
 if (!program || parser.hasErrors()) {
 globals = std::move(globals_stack_.back());
 globals_stack_.pop_back();
 globals["_G"] = old_g;
 globals_mirror_object_id_ = old_mirror_id;
 return Value::makeNull();
 }

	ByteCompiler compiler;

	std::shared_ptr<BytecodeChunk> chunk;
 try {
 chunk = std::shared_ptr<BytecodeChunk>(compiler.compile(*program).release());
 } catch (const std::exception &) {
 globals = std::move(globals_stack_.back());
 globals_stack_.pop_back();
 globals["_G"] = old_g;
 globals_mirror_object_id_ = old_mirror_id;
 return Value::makeNull();
 }
 if (!chunk) {
 globals = std::move(globals_stack_.back());
 globals_stack_.pop_back();
 globals["_G"] = old_g;
 globals_mirror_object_id_ = old_mirror_id;
 return Value::makeNull();
 }

 Value exec_result = execute(*chunk, "__main__");

  globals = std::move(globals_stack_.back());
  globals_stack_.pop_back();
  globals["_G"] = old_g;
  globals_mirror_object_id_ = old_mirror_id;

  return exec_result;
}

void VM::setGlobalThreadSafe(const std::string &name, Value value) {
  std::unique_lock lock(globals_mutex_);
  globals[name] = std::move(value);
}

std::optional<Value> VM::getGlobalThreadSafe(const std::string &name) const {
  std::shared_lock lock(globals_mutex_);
  auto it = globals.find(name);
  if (it != globals.end()) return it->second;
  return std::nullopt;
}

} // namespace havel::compiler
