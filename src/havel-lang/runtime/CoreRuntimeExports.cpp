#include "havel-lang/compiler/vm/VM.hpp"
#include "havel-lang/compiler/BytecodeOrcJIT.h"
#include "havel-lang/ffi/FFICall.hpp"
#include "havel-lang/ffi/FFIMemory.hpp"
#include "havel-lang/ffi/FFITypes.hpp"

#include <cstdint>
#include <vector>
#include <memory>

using havel::compiler::VM;
using havel::compiler::Value;
using havel::ffi::FFICall;
using havel::ffi::FFIMemory;
using havel::ffi::FFIType;
using havel::ffi::FFITypeKind;
using havel::ffi::FFITypeRegistry;

extern "C" void havel_gc_register_roots(void *, havel::compiler::JITStackFrame *,
                                         uint64_t *, uint32_t) {}

extern "C" void havel_gc_unregister_roots(havel::compiler::JITStackFrame *) {}

extern "C" void havel_deoptimize(void *, uint64_t, uint64_t, const char *) {}

extern "C" uint64_t havel_vm_call(void *vm_ptr, uint64_t *args, uint32_t count) {
  auto *vm = static_cast<VM *>(vm_ptr);
  if (!vm || !args || count == 0) {
    return Value::makeNull().rawBits();
  }
  Value callee = Value::fromRawBits(args[0]);
  std::vector<Value> call_args;
  for (uint32_t i = 1; i < count; ++i) {
    call_args.push_back(Value::fromRawBits(args[i]));
  }
  return vm->callFunction(callee, call_args).rawBits();
}

extern "C" uint64_t havel_vm_tail_call(void *vm_ptr, uint64_t *args, uint32_t count) {
  return havel_vm_call(vm_ptr, args, count);
}

extern "C" uint64_t havel_vm_call_dyn(void *vm_ptr, uint32_t arg_count) {
  auto *vm = static_cast<VM *>(vm_ptr);
  if (!vm) return Value::makeNull().rawBits();
  
  if (vm->getStackSizePublic() < static_cast<size_t>(arg_count) + 1) {
    return Value::makeNull().rawBits();
  }
  
  std::vector<Value> args;
  args.reserve(arg_count);
  for (uint32_t i = 0; i < arg_count; ++i) {
    args.insert(args.begin(), vm->popStackPublic());
  }
  Value callee = vm->popStackPublic();
  return vm->callFunction(callee, args).rawBits();
}

extern "C" uint64_t havel_vm_call_spread(void *vm_ptr, uint64_t callee_raw,
                                         uint32_t lit_before, uint32_t lit_after,
                                         uint64_t array_raw) {
  auto *vm = static_cast<VM *>(vm_ptr);
  if (!vm) return Value::makeNull().rawBits();
  
  Value callee_val = Value::fromRawBits(callee_raw);
  Value array_val = Value::fromRawBits(array_raw);
  
  std::vector<Value> spread_elements;
  if (array_val.isArrayId()) {
    auto *arr = vm->getHeap().array(array_val.asArrayId());
    if (arr) {
      for (auto &elem : *arr) {
        spread_elements.push_back(elem);
      }
    }
  }
  
  std::vector<Value> after_args(lit_after);
  for (uint32_t i = 0; i < lit_after; ++i) {
    after_args[lit_after - 1 - i] = vm->popStackPublic();
  }
  
  vm->popStackPublic();
  
  std::vector<Value> before_args(lit_before);
  for (uint32_t i = 0; i < lit_before; ++i) {
    before_args[lit_before - 1 - i] = vm->popStackPublic();
  }
  
  Value callee = vm->popStackPublic();
  
  std::vector<Value> all_args;
  all_args.reserve(lit_before + spread_elements.size() + lit_after);
  for (auto &a : before_args) all_args.push_back(a);
  for (auto &a : spread_elements) all_args.push_back(a);
  for (auto &a : after_args) all_args.push_back(a);
  
  return vm->callFunction(callee, all_args).rawBits();
}

extern "C" uint64_t havel_vm_call_method_spread(void *vm_ptr, uint64_t receiver_raw,
                                                 uint32_t method_name_id,
                                                 uint32_t lit_before, uint32_t lit_after,
                                                 uint64_t array_raw) {
  auto *vm = static_cast<VM *>(vm_ptr);
  if (!vm) return Value::makeNull().rawBits();
  
  Value array_val = Value::fromRawBits(array_raw);
  std::vector<Value> spread_elements;
  if (array_val.isArrayId()) {
    auto *arr = vm->getHeap().array(array_val.asArrayId());
    if (arr) {
      for (auto &elem : *arr) {
        spread_elements.push_back(elem);
      }
    }
  }
  
  std::vector<Value> after_args(lit_after);
  for (uint32_t i = 0; i < lit_after; ++i) {
    after_args[lit_after - 1 - i] = vm->popStackPublic();
  }
  
  vm->popStackPublic();
  
  std::vector<Value> before_args(lit_before);
  for (uint32_t i = 0; i < lit_before; ++i) {
    before_args[lit_before - 1 - i] = vm->popStackPublic();
  }
  
  Value receiver = vm->popStackPublic();
  
  std::vector<Value> all_args;
  all_args.reserve(lit_before + spread_elements.size() + lit_after);
  for (auto &a : before_args) all_args.push_back(a);
  for (auto &a : spread_elements) all_args.push_back(a);
  for (auto &a : after_args) all_args.push_back(a);
  
  const auto *chunk = vm->getCurrentChunk();
  if (!chunk) return Value::makeNull().rawBits();
  std::string method_name = chunk->getString(method_name_id);
  
  return vm->callMethod(receiver, method_name_id, all_args).rawBits();
}

extern "C" uint64_t havel_vm_call_if_function(void *vm_ptr, uint64_t val_raw) {
  auto *vm = static_cast<VM *>(vm_ptr);
  if (!vm) return Value::makeNull().rawBits();
  
  Value val = Value::fromRawBits(val_raw);
  if (val.isHostFuncId() || val.isFunctionObjId() || 
      val.isClosureId() || val.isBoundMethodId()) {
    return vm->callFunction(val, {}).rawBits();
  }
  return val.rawBits();
}

extern "C" uint64_t havel_vm_global_get(void *vm_ptr, uint32_t name_id) {
  auto *vm = static_cast<VM *>(vm_ptr);
  if (!vm) {
    return Value::makeNull().rawBits();
  }
  const auto *chunk = vm->getCurrentChunk();
  if (!chunk || name_id >= chunk->getAllStrings().size()) {
    return Value::makeNull().rawBits();
  }
  const std::string& name = chunk->getString(name_id);
  auto it = vm->getAllGlobals().find(name);
  return it != vm->getAllGlobals().end() ? it->second.rawBits() : Value::makeNull().rawBits();
}

extern "C" void havel_vm_global_set(void *vm_ptr, uint32_t name_id, uint64_t value) {
  auto *vm = static_cast<VM *>(vm_ptr);
  if (!vm) {
    return;
  }
  const auto *chunk = vm->getCurrentChunk();
  if (!chunk || name_id >= chunk->getAllStrings().size()) {
    return;
  }
  const std::string& name = chunk->getString(name_id);
  Value val = Value::fromRawBits(value);
  vm->setGlobal(name, val);
}

// ============================================================================
// FFI/JIT Helpers - Direct libffi calls from JIT-compiled code
// ============================================================================

// Stub: Direct libffi call from JIT-compiled code
// This is a placeholder - full implementation requires complex type marshaling
extern "C" uint64_t havel_vm_ffi_call(void *vm_ptr, uint64_t fn_ptr_raw,
                                      uint64_t ret_type_raw,
                                      uint64_t param_types_raw,
                                      uint64_t args_array_raw,
                                      uint32_t arg_count) {
  // Stub implementation - returns null for now
  // Full implementation would:
  // 1. Decode fn_ptr, ret_type, param_types from raw encodings
  // 2. Extract args from args_array
  // 3. Marshal arguments using libffi
  // 4. Call native function via libffi
  // 5. Return result as Value
  (void)vm_ptr;
  (void)fn_ptr_raw;
  (void)ret_type_raw;
  (void)param_types_raw;
  (void)args_array_raw;
  (void)arg_count;
  return Value::makeNull().rawBits();
}

// Stub: Create and cache CIF for function signature
extern "C" void* havel_vm_ffi_prepare_cif(void* fn_ptr, 
                                           uint64_t ret_type_raw,
                                           uint64_t param_types_raw,
                                           uint32_t param_count) {
  (void)fn_ptr;
  (void)ret_type_raw;
  (void)param_types_raw;
  (void)param_count;
  return nullptr;
}

// Stub: Create libffi closure for callback into Havel VM
extern "C" uint64_t havel_vm_ffi_callback_create(void* vm_ptr, uint64_t closure_raw,
                                                  uint64_t ret_type_raw,
                                                  uint64_t param_types_raw) {
  (void)vm_ptr;
  (void)closure_raw;
  (void)ret_type_raw;
  (void)param_types_raw;
  return Value::makeNull().rawBits();
}