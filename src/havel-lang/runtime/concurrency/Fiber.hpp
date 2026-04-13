#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <stack>
#include <stdexcept>
#include <string>
#include <vector>

#include "../../core/Value.hpp"

namespace havel::compiler {

using ::havel::core::Value;

// ============================================================================
// CALL FRAME: Function invocation context
// ============================================================================

/**
 * CallFrame - Tracks a single function call on the fiber's call stack
 *
 * When a function is called, a CallFrame is pushed. When it returns, popped.
 * Unlike traditional stacks where frames are hidden, here we explicitly store
 * each frame so suspension can preserve the entire call chain.
 */
struct CallFrame {
    // ===== FUNCTION IDENTITY =====
    uint32_t function_id;      // Which function is executing
    uint32_t chunk_index;      // Which bytecode chunk (for modules)
    
    // ===== EXECUTION CONTEXT =====
    uint32_t ip;               // Instruction pointer in this function's bytecode
    uint32_t locals_base;      // Where this frame's locals start in fiber->locals
    uint32_t arg_count;        // Number of arguments passed to function
    
    // ===== CLOSURE CONTEXT =====
    uint32_t closure_id;       // Closure context (for upvalues)
    
    // ===== ERROR HANDLING =====
    struct TryHandler {
        uint32_t catch_ip;
        uint32_t finally_ip;
        uint32_t finally_return_ip;
        size_t stack_depth;
    };
    std::vector<TryHandler> try_stack;
    
    // ===== CONSTRUCTOR =====
    CallFrame() 
        : function_id(0), chunk_index(0), ip(0), locals_base(0),
          arg_count(0), closure_id(0) {}
    
    explicit CallFrame(uint32_t func_id, uint32_t chunk_idx = 0, uint32_t local_base = 0)
        : function_id(func_id), chunk_index(chunk_idx), ip(0),
          locals_base(local_base), arg_count(0), closure_id(0) {}
};

// ============================================================================
// FIBER STACK: Per-fiber operand stack
// ============================================================================

/**
 * FiberStack - Independent operand stack for a single fiber
 *
 * Each fiber has its own stack, not the global VM stack.
 * This is critical: when a fiber suspends, its stack values survive.
 * When it resumes, the stack is exactly as it was.
 */
class FiberStack {
    std::vector<Value> data_;
    size_t sp_;  // Stack pointer
    
public:
    FiberStack() : sp_(0) {}
    
    // Stack operations
    void push(const Value& v) {
        if (data_.size() <= sp_) {
            data_.resize(sp_ + 1);  // Grow as needed
        }
        data_[sp_] = v;
        sp_++;
    }
    
    Value pop() {
        if (sp_ == 0) {
            throw std::runtime_error("Stack underflow in fiber");
        }
        sp_--;
        return data_[sp_];
    }
    
    Value peek(size_t depth = 1) const {
        if (sp_ < depth) {
            throw std::runtime_error("Stack peek out of bounds");
        }
        return data_[sp_ - depth];
    }
    
    void set(size_t depth, const Value& v) {
        if (sp_ < depth) {
            throw std::runtime_error("Stack set out of bounds");
        }
        data_[sp_ - depth] = v;
    }
    
    // Introspection
    bool empty() const { return sp_ == 0; }
    size_t size() const { return sp_; }
    
    // Debug
    const std::vector<Value>& data() const { return data_; }
    
    // Clearing
    void clear() {
        sp_ = 0;
        data_.clear();
    }
    
    // Reserve space
    void reserve(size_t capacity) {
        data_.reserve(capacity);
    }
};

// ============================================================================
// FIBER STATE MACHINE
// ============================================================================

enum class FiberState : uint8_t {
    CREATED,     // Spawned but not yet scheduled
    RUNNABLE,    // Ready to execute (in scheduler's ready queue)
    RUNNING,     // Currently executing (in VM)
    SUSPENDED,   // Paused (waiting for external event)
    DONE         // Finished execution
};

enum class SuspensionReason : uint8_t {
    NONE = 0,
    YIELD,           // Voluntary yield (user called yield)
    CHANNEL_RECV,    // Waiting to receive on channel
    CHANNEL_SEND,    // Waiting to send on channel
    THREAD_JOIN,     // Waiting for thread to complete
    TIMER,           // Waiting for timer to fire
    SLEEP,           // Waiting for sleep to complete
    EXTERNAL         // External system parked this fiber
};

// ============================================================================
// FIBER: Complete bytecode execution context
// ============================================================================

/**
 * Fiber - Independent bytecode execution context for a goroutine
 *
 * A Fiber is to a goroutine as a stack frame is to a function call.
 * Each fiber has:
 * - Complete bytecode position (function_id + ip)
 * - Independent operand stack
 * - Independent local variables
 * - Complete call chain (vector of CallFrames)
 * - Suspension context (what it's waiting for)
 * - Execution state machine
 *
 * CRITICAL INVARIANT: When a fiber suspends, all its state is frozen.
 * When it resumes, all state is restored exactly as it was.
 * No shared global VM state (except for heap/GC, which is carefully managed).
 */
class Fiber {
public:
    // ========== IDENTITY ==========
    uint32_t id;               // Unique fiber ID
    std::string name;          // Debug name
    
    // ========== BYTECODE POSITION (CRITICAL FOR RESUMPTION) ==========
    // These define where execution resumes after suspension
    uint32_t current_function_id;
    uint32_t current_chunk_index;
    uint32_t ip;               // Instruction pointer in current chunk
    
    // ========== CALL STACK (TRACKS NESTED FUNCTIONS) ==========
    // Each element is a function call (main = one frame, foo() = two frames, etc)
    std::vector<CallFrame> call_stack;
    
    // ========== VM STATE (MUST BE PER-FIBER) ==========
    // These are independent for each fiber - NOT shared with global VM
    FiberStack stack;          // Operand stack (values being computed)
    std::map<std::string, Value> locals;  // Local variables by name
    Value return_value;        // Last computed value / return
    
    // ========== EXECUTION STATE ==========
    FiberState state;
    SuspensionReason suspended_reason;
    
    // ========== SUSPENSION CONTEXT ==========
    // What this fiber is waiting for (if suspended)
    void* suspension_context;  // Channel*, Thread*, Timer*, etc
    uint64_t suspension_timestamp;  // For timer-based waits
    
    // ========== METADATA ==========
    uint64_t created_time;     // Nanoseconds since epoch
    uint32_t parent_id;        // Parent fiber (if spawned by another)
    size_t max_stack_depth;    // Guard against stack overflow
    bool had_error;            // Did this fiber error?
    std::string error_message; // If had_error, why?
    
    // ========== STATIC CONSTANTS ==========
    static constexpr size_t DEFAULT_MAX_STACK = 16384;
    static constexpr size_t MAX_CALL_FRAMES = 1024;
    static constexpr size_t MAX_STACK_VALUES = 65536;
    
    // ===== CONSTRUCTOR =====
    
    explicit Fiber(uint32_t fiber_id, uint32_t start_function_id, 
                   uint32_t parent_fiber_id = 0, const std::string& fiber_name = "")
        : id(fiber_id), name(fiber_name.empty() ? ("fiber-" + std::to_string(fiber_id)) : fiber_name),
          current_function_id(start_function_id), current_chunk_index(0), ip(0),
          return_value(Value()),
          state(FiberState::CREATED), suspended_reason(SuspensionReason::NONE),
          suspension_context(nullptr), suspension_timestamp(0),
          created_time(std::chrono::system_clock::now().time_since_epoch().count()),
          parent_id(parent_fiber_id), max_stack_depth(DEFAULT_MAX_STACK),
          had_error(false)
    {
        // Pre-allocate stack space
        stack.reserve(1024);
        
        // Push initial call frame for the starting function
        pushCall(start_function_id, 0);  // 0 args for initial function
    }
    
    // ===== CALL FRAME MANAGEMENT =====
    
    /**
     * Add a function call to the call stack
     * @param function_id Function to call
     * @param arg_count Number of arguments passed
     */
    void pushCall(uint32_t function_id, uint32_t arg_count) {
        if (call_stack.size() >= MAX_CALL_FRAMES) {
            had_error = true;
            error_message = "Call stack exceeded " + std::to_string(MAX_CALL_FRAMES) + " frames";
            throw std::runtime_error(error_message);
        }
        
        CallFrame frame(function_id, current_chunk_index, locals.size());
        frame.arg_count = arg_count;
        call_stack.push_back(frame);
        
        // Update current position
        current_function_id = function_id;
        ip = 0;  // Start at function entry
    }
    
    /**
     * Remove the current function call from the call stack
     * (happens on return)
     */
    void popCall() {
        if (call_stack.empty()) {
            had_error = true;
            error_message = "Call stack underflow (popping from empty stack)";
            throw std::runtime_error(error_message);
        }
        
        // Save the frame we're leaving
        const CallFrame& leaving = call_stack.back();
        call_stack.pop_back();
        
        // Restore previous frame (if any)
        if (!call_stack.empty()) {
            const CallFrame& prev = call_stack.back();
            current_function_id = prev.function_id;
            current_chunk_index = prev.chunk_index;
            ip = prev.ip;  // Resume at where we called from
        }
    }
    
    /**
     * Get the current (topmost) call frame
     */
    CallFrame& currentFrame() {
        if (call_stack.empty()) {
            had_error = true;
            error_message = "No active call frame";
            throw std::runtime_error(error_message);
        }
        return call_stack.back();
    }
    
    const CallFrame& currentFrame() const {
        if (call_stack.empty()) {
            throw std::runtime_error("No active call frame");
        }
        return call_stack.back();
    }
    
    /**
     * Get frame at specific depth (0 = current, 1 = caller, etc)
     */
    CallFrame& getFrame(size_t depth) {
        if (depth >= call_stack.size()) {
            had_error = true;
            error_message = "Invalid frame depth " + std::to_string(depth);
            throw std::runtime_error(error_message);
        }
        return call_stack[call_stack.size() - 1 - depth];
    }
    
    // ===== EXECUTION STATE TRANSITIONS =====
    
    /**
     * Suspend this fiber (it will not run until unpark() is called)
     * @param reason Why is it suspending?
     * @param context Optional context pointer (channel*, thread*, etc)
     */
    void suspend(SuspensionReason reason, void* context = nullptr) {
        if (state != FiberState::RUNNING) {
            throw std::runtime_error("Cannot suspend non-RUNNING fiber");
        }
        
        state = FiberState::SUSPENDED;
        suspended_reason = reason;
        suspension_context = context;
        suspension_timestamp = std::chrono::system_clock::now().time_since_epoch().count();
        
        // ✅ NOTE: ip is preserved in the current CallFrame
        // When resumed, execution continues from this instruction
    }
    
    /**
     * Resume this fiber (make it runnable again)
     * Called by scheduler when the external event completes
     */
    void resume() {
        if (state != FiberState::SUSPENDED) {
            throw std::runtime_error("Cannot resume non-SUSPENDED fiber");
        }
        
        state = FiberState::RUNNABLE;
        suspended_reason = SuspensionReason::NONE;
        suspension_context = nullptr;
    }
    
    /**
     * Mark fiber as done
     */
    void markDone(const Value& ret_val) {
        state = FiberState::DONE;
        return_value = ret_val;
    }
    
    // ===== STATE PREDICATES =====
    
    bool isRunnable() const { return state == FiberState::RUNNABLE; }
    bool isRunning() const { return state == FiberState::RUNNING; }
    bool isSuspended() const { return state == FiberState::SUSPENDED; }
    bool isDone() const { return state == FiberState::DONE; }
    bool isCreated() const { return state == FiberState::CREATED; }
    
    // ===== GC SUPPORT =====
    
    /**
     * Get all Value references from this fiber for GC tracing
     * The GC must treat all these as roots to prevent collection
     * while the fiber is suspended
     */
    std::vector<Value> getGCRoots() const {
        std::vector<Value> roots;
        
        // Stack values
        for (const Value& v : stack.data()) {
            roots.push_back(v);
        }
        
        // Local variables
        for (const auto& [name, value] : locals) {
            roots.push_back(value);
        }
        
        // Return value
        roots.push_back(return_value);
        
        return roots;
    }
    
    // ===== DIAGNOSTICS =====
    
    /**
     * Estimate memory usage of this fiber
     */
    size_t estimateMemoryUsage() const {
        size_t total = 0;
        
        // Call stack frames
        total += call_stack.capacity() * sizeof(CallFrame);
        
        // Operand stack
        total += stack.data().capacity() * sizeof(Value);
        
        // Locals map
        total += locals.size() * sizeof(void*);
        for (const auto& [name, value] : locals) {
            total += name.capacity();  // String storage
        }
        
        return total;
    }
    
    /**
     * Get a human-readable state string
     */
    std::string stateString() const {
        switch (state) {
            case FiberState::CREATED: return "CREATED";
            case FiberState::RUNNABLE: return "RUNNABLE";
            case FiberState::RUNNING: return "RUNNING";
            case FiberState::SUSPENDED: return "SUSPENDED(" + suspensionReasonString() + ")";
            case FiberState::DONE: return "DONE";
            default: return "UNKNOWN";
        }
    }
    
    std::string suspensionReasonString() const {
        switch (suspended_reason) {
            case SuspensionReason::NONE: return "NONE";
            case SuspensionReason::YIELD: return "YIELD";
            case SuspensionReason::CHANNEL_RECV: return "CHANNEL_RECV";
            case SuspensionReason::CHANNEL_SEND: return "CHANNEL_SEND";
            case SuspensionReason::THREAD_JOIN: return "THREAD_JOIN";
            case SuspensionReason::TIMER: return "TIMER";
            case SuspensionReason::SLEEP: return "SLEEP";
            case SuspensionReason::EXTERNAL: return "EXTERNAL";
            default: return "UNKNOWN";
        }
    }
};

}  // namespace havel::compiler
