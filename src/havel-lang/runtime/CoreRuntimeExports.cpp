#include "havel-lang/compiler/vm/VM.hpp"
#include "havel-lang/compiler/BytecodeOrcJIT.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
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

static uint64_t havelVmBinaryOp(void *vm_ptr, havel::compiler::OpCode op,
                                uint64_t l, uint64_t r);

// Arithmetic bridges for backends that lower speculative int paths and
// need generic semantics for everything else (the Cranelift prototype).
// The VM's execBinaryOp owns the language semantics; the bridge saves the
// operand words onto the VM stack, runs it with a synthetic instruction,
// and returns the resulting word. Part of the Runtime ABI (TODO #27).
extern "C" uint64_t havel_vm_add(void *vm_ptr, uint64_t l, uint64_t r) {
  return havelVmBinaryOp(vm_ptr, havel::compiler::OpCode::ADD, l, r);
}
extern "C" uint64_t havel_vm_sub(void *vm_ptr, uint64_t l, uint64_t r) {
  return havelVmBinaryOp(vm_ptr, havel::compiler::OpCode::SUB, l, r);
}
extern "C" uint64_t havel_vm_mul(void *vm_ptr, uint64_t l, uint64_t r) {
  return havelVmBinaryOp(vm_ptr, havel::compiler::OpCode::MUL, l, r);
}

// Comparison bridges (Runtime ABI): pure word semantics, no VM state.
// Numeric comparisons coerce both sides as doubles (int48/bool coerce,
// null/refs yield NaN so comparisons are false); EQ/NEQ first check raw
// bit equality so identical words (including non-numerics) compare equal.
// Moved from BytecodeOrcJIT.cpp so every backend resolves them from one
// home; havel_vm_is_truthy stays in BytecodeOrcJIT.cpp for now.
static double havelValueToDouble(uint64_t bits) {
  if ((bits & 0x7FF8000000000000ULL) != 0x7FF8000000000000ULL) {
    double d;
    std::memcpy(&d, &bits, sizeof(double));
    return d;
  }
  uint64_t tag = (bits & 0x0007000000000000ULL) >> 48;
  if (tag == 0x1) {  // INT48
    uint64_t payload = bits & 0x0000FFFFFFFFFFFFULL;
    int64_t val = (payload & 0x0000800000000000ULL)
                      ? static_cast<int64_t>(payload | 0xFFFF000000000000ULL)
                      : static_cast<int64_t>(payload);
    return static_cast<double>(val);
  }
  if (tag == 0x2) {  // BOOL
    return static_cast<double>((bits & 0x0000FFFFFFFFFFFFULL) != 0 ? 1 : 0);
  }
  return 0.0 / 0.0;  // NaN for null/refs: never equal to anything
}

extern "C" uint64_t havel_vm_eq(uint64_t l, uint64_t r) {
  if (l == r) return Value::makeBool(true).rawBits();
  double ld = havelValueToDouble(l), rd = havelValueToDouble(r);
  if (!std::isnan(ld) && !std::isnan(rd)) {
    return Value::makeBool(ld == rd).rawBits();
  }
  return Value::makeBool(false).rawBits();
}

extern "C" uint64_t havel_vm_neq(uint64_t l, uint64_t r) {
  if (l == r) return Value::makeBool(false).rawBits();
  double ld = havelValueToDouble(l), rd = havelValueToDouble(r);
  if (!std::isnan(ld) && !std::isnan(rd)) {
    return Value::makeBool(ld != rd).rawBits();
  }
  return Value::makeBool(true).rawBits();
}

extern "C" uint64_t havel_vm_lt(uint64_t l, uint64_t r) {
  double ld = havelValueToDouble(l), rd = havelValueToDouble(r);
  if (std::isnan(ld) || std::isnan(rd)) return Value::makeBool(false).rawBits();
  return Value::makeBool(ld < rd).rawBits();
}

extern "C" uint64_t havel_vm_lte(uint64_t l, uint64_t r) {
  double ld = havelValueToDouble(l), rd = havelValueToDouble(r);
  if (std::isnan(ld) || std::isnan(rd)) return Value::makeBool(false).rawBits();
  return Value::makeBool(ld <= rd).rawBits();
}

extern "C" uint64_t havel_vm_gt(uint64_t l, uint64_t r) {
  double ld = havelValueToDouble(l), rd = havelValueToDouble(r);
  if (std::isnan(ld) || std::isnan(rd)) return Value::makeBool(false).rawBits();
  return Value::makeBool(ld > rd).rawBits();
}

extern "C" uint64_t havel_vm_gte(uint64_t l, uint64_t r) {
  double ld = havelValueToDouble(l), rd = havelValueToDouble(r);
  if (std::isnan(ld) || std::isnan(rd)) return Value::makeBool(false).rawBits();
  return Value::makeBool(ld >= rd).rawBits();
}

// Truthiness of a raw Value word (Runtime ABI): null falsy, bool by
// payload, int48 non-zero, raw double non-zero/non-NaN, refs truthy.
// Mirrors VM::isTruthy for the scalar shapes; moved from
// BytecodeOrcJIT.cpp so every backend resolves it from one home.
extern "C" int havel_vm_is_truthy(uint64_t v) {
  const uint64_t nullBits = 0x7FF8000000000000ULL | (0x3ULL << 48);
  if (v == nullBits) return 0;
  uint64_t tag = (v & 0x0007000000000000ULL) >> 48;
  if (tag == 0x1) {  // INT48
    uint64_t payload = v & 0x0000FFFFFFFFFFFFULL;
    int64_t val = (payload & 0x0000800000000000ULL)
                      ? static_cast<int64_t>(payload | 0xFFFF000000000000ULL)
                      : static_cast<int64_t>(payload);
    return val != 0 ? 1 : 0;
  }
  if (tag == 0x2) {  // BOOL
    return (v & 0x0000FFFFFFFFFFFFULL) != 0 ? 1 : 0;
  }
  if ((v & 0x7FF8000000000000ULL) != 0x7FF8000000000000ULL) {  // DOUBLE
    double d;
    std::memcpy(&d, &v, sizeof(double));
    return (d != 0.0 && !std::isnan(d)) ? 1 : 0;
  }
  return 1;  // objects, arrays, etc. are truthy
}

static uint64_t havelVmBinaryOp(void *vm_ptr, havel::compiler::OpCode op,
                                uint64_t l, uint64_t r) {
  auto *vm = static_cast<VM *>(vm_ptr);
  if (!vm) {
    return Value::makeNull().rawBits();
  }
  // The VM stack is mid-dispatch while JIT code runs: save the operand
  // words, run the generic op in isolation, take the result, restore.
  // execBinaryOp pops two and pushes one on the shared stack, so the
  // sequence is balanced as long as no exception escapes; the catch
  // restores depth and yields null for a failed bridge op.
  const size_t depth_before = vm->stackDepthPublic();
  vm->pushStackPublic(Value::fromRawBits(l));
  vm->pushStackPublic(Value::fromRawBits(r));
  havel::compiler::Instruction instr;
  instr.opcode = op;
  try {
    vm->execBinaryOpPublic(instr);
    Value result = vm->popStackPublic();
    vm->truncateStackPublic(depth_before);
    return result.rawBits();
  } catch (...) {
    vm->truncateStackPublic(depth_before);
    return Value::makeNull().rawBits();
  }
}

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
  if (std::getenv("HCLB_TRACE_GLOBALS")) {
    fprintf(stderr, "[GGET] name_id=%u\n", name_id);
  }
  if (!vm) {
    return Value::makeNull().rawBits();
  }
  const auto *chunk = vm->getCurrentChunk();
  if (!chunk || name_id >= chunk->getAllStrings().size()) {
    return Value::makeNull().rawBits();
  }
  const std::string& name = chunk->getString(name_id);
  // Full LOAD_GLOBAL chain (VMDispatch.cpp): ambient globals, host
  // functions, then the closure's module_globals sidecar - the
  // authoritative store for module-level state. Reading only the ambient
  // map made JIT-compiled module functions miss sidecar-only keys.
  Value out;
  if (vm->resolveGlobalPublic(name, &out)) {
    return out.rawBits();
  }
  return Value::makeNull().rawBits();
}

extern "C" void havel_vm_global_set(void *vm_ptr, uint32_t name_id, uint64_t value) {
  auto *vm = static_cast<VM *>(vm_ptr);
  if (std::getenv("HCLB_TRACE_GLOBALS")) {
    fprintf(stderr, "[GSET] name_id=%u val_bits=%llx\n", name_id,
            static_cast<unsigned long long>(value));
  }
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
  // Module-globals persistence, matching the interpreter's STORE_GLOBAL
  // (VMDispatch.cpp): a write from a module function frame must also land
  // in the closure's shared module_globals map and be recorded in
  // written_globals so returning refreshes the caller's snapshot. Without
  // this, JIT-compiled module functions lose every global write for all
  // other frames (self-hosted parser corruption when getBPTABLE tiered).
  vm->persistModuleGlobalPublic(name, val);
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