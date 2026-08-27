#pragma once

#include "VM.hpp"
#include <vector>
#include <optional>

namespace havel::compiler {

/// Canonical call frame representing a function activation
/// Used by VM, JIT, and VMExecutionContext for unified calling convention
struct CallFrame {
    // Function being called
    const BytecodeFunction* function = nullptr;
    const BytecodeChunk* chunk = nullptr;
    uint32_t function_index = 0;
    
    // Closure (if applicable)
    uint32_t closure_id = 0;
    
    // Instruction pointer (next instruction to execute)
    uint32_t ip = 0;
    
    // Stack depth at function entry
    uint32_t stack_depth = 0;
    
    // Locals base offset
    uint32_t locals_base = 0;
    
    // Number of local slots
    uint32_t local_count = 0;
    
    // Whether this is a tail call
    bool is_tail_call = false;
    
    // Upvalues captured by this frame
    std::vector<uint32_t> upvalue_slots;
    
    // Coroutine ID if this is a coroutine frame
    uint32_t coroutine_id = 0;

    CallFrame() = default;
    
    CallFrame(const BytecodeFunction* func, const BytecodeChunk* ch, 
              uint32_t func_idx, uint32_t cl_id, uint32_t locals_b,
              uint32_t local_cnt, uint32_t sp)
        : function(func), chunk(ch), function_index(func_idx), closure_id(cl_id),
          locals_base(locals_b), local_count(local_cnt), stack_depth(sp) {}
};

/// Call context - holds all state needed for a function call
/// Unified interface for VM, JIT, and VMExecutionContext
class CallContext {
public:
    CallContext(VM* vm, Value callee, std::vector<Value> args)
        : vm_(vm), callee_(std::move(callee)), args_(std::move(args)) {}
    
    ~CallContext() = default;
    
    // Execute the call
    Value execute();
    
    // Execute as tail call (reuses current frame)
    Value executeTail();
    
    // Get the call frame if created
    const CallFrame* frame() const { return frame_.has_value() ? &frame_.value() : nullptr; }
    
    // Error handling
    bool hasError() const { return !error_.empty(); }
    const std::string& error() const { return error_; }
    
    // Result
    Value result() const { return result_; }
    
private:
    VM* vm_;
    Value callee_;
    std::vector<Value> args_;
    std::optional<CallFrame> frame_;
    Value result_;
    std::string error_;
    
    // Setup call frame
    bool setupFrame();
    // Execute the function body
    Value executeFrame();
    // Handle host function call
    Value callHostFunction();
    // Handle closure call
    Value callClosure();
    // Handle coroutine resume
    Value callCoroutine();
    // Handle bound method call
    Value callBoundMethod();
    // Handle callable object (__call / op_call)
    Value callObject();
};

/// Canonical calling convention interface
/// All call paths (VM, JIT, VMExecutionContext) should use this
class CallConvention {
public:
    virtual ~CallConvention() = default;
    
    // Call a function with given arguments
    virtual Value call(VM* vm, Value callee, const std::vector<Value>& args) = 0;
    
    // Tail call - reuses current frame
    virtual Value tailCall(VM* vm, Value callee, const std::vector<Value>& args) = 0;
    
    // Call a host function
    virtual Value callHost(VM* vm, uint32_t host_idx, const std::vector<Value>& args) = 0;
    
    // Call a method on an object
    virtual Value callMethod(VM* vm, Value receiver, uint32_t method_name_id, 
                            const std::vector<Value>& args) = 0;
    
    // Call super method
    virtual Value callSuper(VM* vm, Value receiver, uint32_t method_id,
                           const std::vector<Value>& args) = 0;
};

/// Default implementation of CallConvention
class DefaultCallConvention : public CallConvention {
public:
    Value call(VM* vm, Value callee, const std::vector<Value>& args) override;
    Value tailCall(VM* vm, Value callee, const std::vector<Value>& args) override;
    Value callHost(VM* vm, uint32_t host_idx, const std::vector<Value>& args) override;
    Value callMethod(VM* vm, Value receiver, uint32_t method_name_id,
                     const std::vector<Value>& args) override;
    Value callSuper(VM* vm, Value receiver, uint32_t method_id,
                    const std::vector<Value>& args) override;
};

} // namespace havel::compiler