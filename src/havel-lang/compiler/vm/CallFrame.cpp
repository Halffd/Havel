#include "CallFrame.hpp"
#include "VM.hpp"

namespace havel::compiler {

Value CallContext::execute() {
    // Delegate to VM's callFunction which handles all callee types
    return vm_->callFunction(callee_, args_);
}

Value CallContext::executeTail() {
    // For tail calls, delegate to VM's tail call if available
    return vm_->callFunction(callee_, args_);
}

// DefaultCallConvention implementation
Value DefaultCallConvention::call(VM* vm, Value callee, const std::vector<Value>& args) {
    return vm->callFunction(callee, args);
}

Value DefaultCallConvention::tailCall(VM* vm, Value callee, const std::vector<Value>& args) {
    // TODO: Implement proper tail call optimization
    return call(vm, callee, args);
}

Value DefaultCallConvention::callHost(VM* vm, uint32_t host_idx, const std::vector<Value>& args) {
    // Create a host function value and call it
    Value host_fn = Value::makeHostFuncId(host_idx);
    return vm->callFunction(host_fn, args);
}

Value DefaultCallConvention::callMethod(VM* vm, Value receiver, uint32_t method_name_id,
                                       const std::vector<Value>& args) {
    return vm->callMethod(receiver, method_name_id, args);
}

Value DefaultCallConvention::callSuper(VM* vm, Value receiver, uint32_t method_id,
                                      const std::vector<Value>& args) {
    return vm->callSuper(receiver, method_id, args);
}

} // namespace havel::compiler