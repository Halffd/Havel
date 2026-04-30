#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IR/Module.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/TargetParser.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Support/CodeGen.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/MC/TargetRegistry.h>

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/Passes/PassBuilder.h>

#include "BytecodeOrcJIT.h"
#include "../../utils/Logger.hpp"
#include <cstring>
#include <iostream>

using namespace llvm::orc;

namespace havel::compiler {

// ============================================================================
// NaN-boxing constants (Phase 4 refined)
// ============================================================================
static constexpr uint64_t QNAN             = 0x7FF8000000000000ULL;
static constexpr uint64_t TAG_MASK         = 0x0007000000000000ULL;
static constexpr uint64_t PAYLOAD_MASK     = 0x0000FFFFFFFFFFFFULL;
static constexpr uint64_t EXT_PAYLOAD_MASK = 0x00000FFFFFFFFFFFULL;

static constexpr uint64_t INT_TAG          = 0x1;
static constexpr uint64_t EXT_TAG          = 0x7;

static constexpr uint64_t INT_TAG_BITS     = QNAN | (INT_TAG << 48); // 0x7FF9...
static constexpr uint64_t ARRAY_TAG_BITS   = QNAN | (EXT_TAG << 48) | (0x1ULL << 44);

// ============================================================================
// Native Bridge Helpers
// ============================================================================
extern "C" {

void havel_vm_throw_error(void* vm_ptr, const char* msg) {
    if (!vm_ptr) return;
    auto* vm = static_cast<VM*>(vm_ptr);
    vm->throwError(msg);
}

void havel_vm_throw_value(void* vm_ptr, uint64_t value_bits) {
    if (!vm_ptr) return;
    auto* vm = static_cast<VM*>(vm_ptr);
    Value v;
    std::memcpy(&v, &value_bits, sizeof(uint64_t));
    vm->setCurrentExceptionPublic(v);
    throw ScriptThrow{v};
}

// JIT throw bridge with explicit intent: this always raises into VM exception
// handling and never returns normally.
void havel_vm_throw_from_jit(void* vm_ptr, uint64_t value_bits) {
    havel_vm_throw_value(vm_ptr, value_bits);
}

void havel_vm_try_enter(JITStackFrame* frame, uint32_t catch_ip,
                        uint32_t finally_ip, uint32_t stack_depth) {
    if (!frame) return;
    if (frame->handler_count >= JITStackFrame::MAX_EXCEPTION_HANDLERS) return;
    const uint32_t idx = frame->handler_count++;
    frame->handler_catch_ip[idx] = catch_ip;
    frame->handler_finally_ip[idx] = finally_ip;
    frame->handler_stack_depth[idx] = stack_depth;
}

void havel_vm_try_exit(JITStackFrame* frame) {
    if (!frame || frame->handler_count == 0) return;
    --frame->handler_count;
}

uint32_t havel_vm_try_find_throw_target(JITStackFrame* frame,
                                        uint32_t* stack_depth_out,
                                        uint32_t* popped_count_out) {
    if (stack_depth_out) *stack_depth_out = 0;
    if (popped_count_out) *popped_count_out = 0;
    if (!frame || frame->handler_count == 0) return UINT32_MAX;

    uint32_t popped = 0;
    while (frame->handler_count > 0) {
        const uint32_t idx = frame->handler_count - 1;
        const uint32_t catch_ip = frame->handler_catch_ip[idx];
        const uint32_t finally_ip = frame->handler_finally_ip[idx];
        const uint32_t depth = frame->handler_stack_depth[idx];
        frame->handler_count = idx;
        ++popped;

        // Prefer catch target; fall back to finally target if catch is absent.
        // Bytecode currently patches catch_ip for try/catch and try/finally,
        // so this keeps compatibility while honoring finally metadata.
        uint32_t target_ip = catch_ip;
        if (target_ip == 0 && finally_ip != 0) {
            target_ip = finally_ip;
        }

        if (target_ip != UINT32_MAX && target_ip != 0) {
            if (stack_depth_out) *stack_depth_out = depth;
            if (popped_count_out) *popped_count_out = popped;
            return target_ip;
        }
    }

    if (popped_count_out) *popped_count_out = popped;
    return UINT32_MAX;
}

void havel_vm_set_exception(void* vm_ptr, uint64_t value_bits) {
    if (!vm_ptr) return;
    auto* vm = static_cast<VM*>(vm_ptr);
    Value v;
    std::memcpy(&v, &value_bits, sizeof(uint64_t));
    vm->setCurrentExceptionPublic(v);
}

uint64_t havel_vm_load_exception(void* vm_ptr) {
    if (!vm_ptr) return Value::makeNull().rawBits();
    auto* vm = static_cast<VM*>(vm_ptr);
    return vm->currentExceptionPublic().rawBits();
}

void havel_gc_write_barrier(void* vm_ptr, uint64_t new_value_bits) {
    if (!vm_ptr) return;
    auto* vm = static_cast<VM*>(vm_ptr);
    ::havel::core::Value val;
    std::memcpy(&val, &new_value_bits, sizeof(uint64_t));
    vm->pinExternalRoot(val);
}

void havel_gc_register_roots(void* vm_ptr, JITStackFrame* frame,
                              uint64_t* slot_bits, uint32_t count) {
    if (!vm_ptr || !frame) return;
    auto* vm = static_cast<VM*>(vm_ptr);
    frame->vm = vm_ptr;
    frame->root_count = 0;
    frame->handler_count = 0;
    for (uint32_t i = 0; i < count && i < JITStackFrame::MAX_GC_ROOTS; ++i) {
        ::havel::core::Value val;
        std::memcpy(&val, &slot_bits[i], sizeof(uint64_t));
        frame->root_ids[frame->root_count]     = vm->pinExternalRoot(val);
        frame->slot_values[frame->root_count]  = slot_bits[i];
        ++frame->root_count;
    }
}

void havel_gc_unregister_roots(JITStackFrame* frame) {
    if (!frame || !frame->vm) return;
    auto* vm = static_cast<VM*>(frame->vm);
    for (uint32_t i = 0; i < frame->root_count; ++i) {
        vm->unpinExternalRoot(frame->root_ids[i]);
    }
    frame->root_count = 0;
}

void havel_deoptimize(void* vm_ptr, uint64_t l, uint64_t r, const char* func) {
    ::havel::debug("[JIT] Deoptimizing in {} for types: 0x{:x} op 0x{:x}", func, l, r);
}

// JIT helper for function calls - delegates to VM
uint64_t havel_vm_call(void* vm_ptr, uint64_t* args, uint32_t count) {
    if (!vm_ptr) return 0x7FF8000000000003ULL; // null
    auto* vm = static_cast<VM*>(vm_ptr);
    
    // Convert args array to vector
    std::vector<Value> valArgs;
    for (uint32_t i = 0; i < count; ++i) {
        Value v;
        std::memcpy(&v, &args[i], sizeof(uint64_t));
        valArgs.push_back(v);
    }
    
    // The callee is the first argument (args[0])
    if (valArgs.empty()) {
        return 0x7FF8000000000003ULL; // null
    }
    
    Value callee = valArgs[0];
    valArgs.erase(valArgs.begin()); // Remove callee from args
    
    // Call the function through VM
    Value result = vm->callFunction(callee, valArgs);
    
    uint64_t bits;
    std::memcpy(&bits, &result, sizeof(uint64_t));
    return bits;
}

// JIT helper for tail calls - reuses current frame (proper TCO)
uint64_t havel_vm_tail_call(void* vm_ptr, uint64_t* args, uint32_t count) {
    if (!vm_ptr) return Value::makeNull().rawBits();
    auto* vm = static_cast<VM*>(vm_ptr);

    // Convert args array to vector
    std::vector<Value> valArgs;
    for (uint32_t i = 0; i < count; ++i) {
        Value v;
        std::memcpy(&v, &args[i], sizeof(uint64_t));
        valArgs.push_back(v);
    }

    if (valArgs.empty()) return Value::makeNull().rawBits();

    Value callee = valArgs[0];
    valArgs.erase(valArgs.begin());

    // Close open upvalues for current frame before reusing it
    uint32_t locals_base = static_cast<uint32_t>(vm->currentLocalsBasePublic());
    vm->closeFrameUpvaluesPublic(locals_base,
        static_cast<uint32_t>(vm->currentLocalsSizePublic()));

    // Reuse current frame (TCO)
    size_t saved_frame_count = vm->frameCountPublic();
    vm->doTailCallPublic(callee, std::move(valArgs));
    vm->runDispatchLoopPublic(saved_frame_count - 1);

    // Get result from stack
    Value result = vm->popStackPublic();
    return result.rawBits();
}

// Global variable access - use public API
uint64_t havel_vm_global_get(void* vm_ptr, uint32_t name_id) {
  if (!vm_ptr) return Value::makeNull().rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  auto* chunk = vm->getCurrentChunk();
  if (!chunk) return Value::makeNull().rawBits();
  
  const auto& name = chunk->getString(name_id);
  if (name.empty()) return Value::makeNull().rawBits();
  
  auto opt = vm->getGlobalThreadSafe(name);
  return opt.has_value() ? opt.value().rawBits() : Value::makeNull().rawBits();
}

void havel_vm_global_set(void* vm_ptr, uint32_t name_id, uint64_t value) {
  if (!vm_ptr) return;
  auto* vm = static_cast<VM*>(vm_ptr);
  auto* chunk = vm->getCurrentChunk();
  if (!chunk) return;
  
  const auto& name = chunk->getString(name_id);
  if (name.empty()) return;
  
  Value v;
  std::memcpy(&v, &value, sizeof(uint64_t));
  vm->setGlobalThreadSafe(name, v);
}

uint64_t havel_vm_upvalue_get(void* vm_ptr, uint32_t slot) {
    if (!vm_ptr) return Value::makeNull().rawBits();
    auto* vm = static_cast<VM*>(vm_ptr);
    auto* closure = vm->currentClosurePublic();
    if (!closure || slot >= closure->upvalues.size() || !closure->upvalues[slot])
        return Value::makeNull().rawBits();
    const auto& cell = closure->upvalues[slot];
    if (cell->is_open) {
        uint32_t abs_index = cell->locals_base + cell->open_index;
        return vm->readLocalPublic(abs_index).rawBits();
    }
    return cell->closed_value.rawBits();
}

void havel_vm_upvalue_set(void* vm_ptr, uint32_t slot, uint64_t value) {
    if (!vm_ptr) return;
    auto* vm = static_cast<VM*>(vm_ptr);
    auto* closure = vm->currentClosurePublic();
    if (!closure || slot >= closure->upvalues.size() || !closure->upvalues[slot])
        return;
    auto& cell = closure->upvalues[slot];
    Value v;
    std::memcpy(&v, &value, sizeof(uint64_t));
    if (cell->is_open) {
        uint32_t abs_index = cell->locals_base + cell->open_index;
        vm->writeLocalPublic(abs_index, v);
    } else {
        cell->closed_value = v;
    }
}

void havel_vm_close_upvalues(void* vm_ptr, uint32_t locals_base) {
    if (!vm_ptr) return;
    auto* vm = static_cast<VM*>(vm_ptr);
    vm->closeFrameUpvaluesPublic(locals_base,
        static_cast<uint32_t>(vm->currentLocalsSizePublic()));
}

uint32_t havel_vm_locals_base(void* vm_ptr) {
    if (!vm_ptr) return 0;
    auto* vm = static_cast<VM*>(vm_ptr);
    return static_cast<uint32_t>(vm->currentLocalsBasePublic());
}

// Semantic comparison bridges — handle NaN-boxed type dispatch correctly
// int 1 == double 1.0 must be true, but their bit representations differ

static double valueToDouble(uint64_t bits) {
  if ((bits & 0x7FF8000000000000ULL) != 0x7FF8000000000000ULL) {
    double d; std::memcpy(&d, &bits, sizeof(double)); return d;
  }
  uint64_t tag = (bits & 0x0007000000000000ULL) >> 48;
  if (tag == 0x1) { // INT48
    uint64_t payload = bits & 0x0000FFFFFFFFFFFFULL;
    int64_t val = (payload & 0x0000800000000000ULL)
      ? static_cast<int64_t>(payload | 0xFFFF000000000000ULL)
      : static_cast<int64_t>(payload);
    return static_cast<double>(val);
  }
  if (tag == 0x2) { // BOOL
    return static_cast<double>((bits & 0x0000FFFFFFFFFFFFULL) != 0 ? 1 : 0);
  }
  return 0.0/0.0; // NaN for null/ptr/refs — never equal to anything
}

static bool valueIsTruthy(uint64_t bits) {
  uint64_t nullBits = 0x7FF8000000000000ULL | (0x3ULL << 48);
  if (bits == nullBits) return false;
  uint64_t tag = (bits & 0x0007000000000000ULL) >> 48;
  if (tag == 0x1) { // INT48
    uint64_t payload = bits & 0x0000FFFFFFFFFFFFULL;
    int64_t val = (payload & 0x0000800000000000ULL)
      ? static_cast<int64_t>(payload | 0xFFFF000000000000ULL)
      : static_cast<int64_t>(payload);
    return val != 0;
  }
  if (tag == 0x2) return (bits & 0x0000FFFFFFFFFFFFULL) != 0; // BOOL
  if ((bits & 0x7FF8000000000000ULL) != 0x7FF8000000000000ULL) { // DOUBLE
    double d; std::memcpy(&d, &bits, sizeof(double)); return d != 0.0 && !std::isnan(d);
  }
  return true; // objects, arrays, etc. are truthy
}

// EQ: semantic equality — same bits OR both numeric and equal as doubles
uint64_t havel_vm_eq(uint64_t l, uint64_t r) {
  if (l == r) return Value::makeBool(true).rawBits();
  double ld = valueToDouble(l), rd = valueToDouble(r);
  if (!std::isnan(ld) && !std::isnan(rd)) return Value::makeBool(ld == rd).rawBits();
  return Value::makeBool(false).rawBits();
}

uint64_t havel_vm_neq(uint64_t l, uint64_t r) {
  if (l == r) return Value::makeBool(false).rawBits();
  double ld = valueToDouble(l), rd = valueToDouble(r);
  if (!std::isnan(ld) && !std::isnan(rd)) return Value::makeBool(ld != rd).rawBits();
  return Value::makeBool(true).rawBits();
}

uint64_t havel_vm_lt(uint64_t l, uint64_t r) {
  double ld = valueToDouble(l), rd = valueToDouble(r);
  if (std::isnan(ld) || std::isnan(rd)) return Value::makeBool(false).rawBits();
  return Value::makeBool(ld < rd).rawBits();
}

uint64_t havel_vm_lte(uint64_t l, uint64_t r) {
  double ld = valueToDouble(l), rd = valueToDouble(r);
  if (std::isnan(ld) || std::isnan(rd)) return Value::makeBool(false).rawBits();
  return Value::makeBool(ld <= rd).rawBits();
}

uint64_t havel_vm_gt(uint64_t l, uint64_t r) {
  double ld = valueToDouble(l), rd = valueToDouble(r);
  if (std::isnan(ld) || std::isnan(rd)) return Value::makeBool(false).rawBits();
  return Value::makeBool(ld > rd).rawBits();
}

uint64_t havel_vm_gte(uint64_t l, uint64_t r) {
  double ld = valueToDouble(l), rd = valueToDouble(r);
  if (std::isnan(ld) || std::isnan(rd)) return Value::makeBool(false).rawBits();
  return Value::makeBool(ld >= rd).rawBits();
}

uint64_t havel_vm_is(uint64_t l, uint64_t r) {
  return Value::makeBool(l == r).rawBits();
}

uint64_t havel_vm_not(uint64_t v) {
  return Value::makeBool(!valueIsTruthy(v)).rawBits();
}

int havel_vm_is_truthy(uint64_t v) {
  return valueIsTruthy(v) ? 1 : 0;
}

// Power function
uint64_t havel_vm_pow(uint64_t base_bits, uint64_t exp_bits) {
  Value base, exp;
  std::memcpy(&base, &base_bits, sizeof(uint64_t));
  std::memcpy(&exp, &exp_bits, sizeof(uint64_t));

  if (base.isInt() && exp.isInt()) {
    int64_t b = base.asInt();
    int64_t e = exp.asInt();
    if (e < 0) return Value(0.0).rawBits();
    int64_t result = 1;
    while (e > 0) {
      if (e & 1) result *= b;
      b *= b;
      e >>= 1;
    }
    return Value(result).rawBits();
  }
  return Value(std::pow(base.asDouble(), exp.asDouble())).rawBits();
}

// Array operations - use public heap API
uint64_t havel_vm_array_new(void* vm_ptr) {
  if (!vm_ptr) return Value::makeNull().rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  auto ref = vm->createHostArray();
  return Value::makeArrayId(ref.id).rawBits();
}

uint64_t havel_vm_array_get(void* vm_ptr, uint64_t arr_bits, uint64_t idx_bits) {
  if (!vm_ptr) return Value::makeNull().rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  Value arr, idx;
  std::memcpy(&arr, &arr_bits, sizeof(uint64_t));
  std::memcpy(&idx, &idx_bits, sizeof(uint64_t));
  if (!arr.isArrayId() || !idx.isInt()) return Value::makeNull().rawBits();
  return vm->getHostArrayValue(ArrayRef{arr.asArrayId()}, static_cast<size_t>(idx.asInt())).rawBits();
}

uint64_t havel_vm_array_set(void* vm_ptr, uint64_t arr_bits, uint64_t idx_bits, uint64_t val_bits) {
  if (!vm_ptr) return val_bits;
  auto* vm = static_cast<VM*>(vm_ptr);
  Value arr, idx, val;
  std::memcpy(&arr, &arr_bits, sizeof(uint64_t));
  std::memcpy(&idx, &idx_bits, sizeof(uint64_t));
  std::memcpy(&val, &val_bits, sizeof(uint64_t));
  if (!arr.isArrayId() || !idx.isInt()) return val_bits;
  vm->setHostArrayValue(ArrayRef{arr.asArrayId()}, static_cast<size_t>(idx.asInt()), val);
  return val_bits;
}

uint64_t havel_vm_array_len(void* vm_ptr, uint64_t arr_bits) {
  if (!vm_ptr) return Value(static_cast<int64_t>(0)).rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  Value arr;
  std::memcpy(&arr, &arr_bits, sizeof(uint64_t));
  if (!arr.isArrayId()) return Value(static_cast<int64_t>(0)).rawBits();
  return Value(static_cast<int64_t>(vm->getHostArrayLength(ArrayRef{arr.asArrayId()}))).rawBits();
}

void havel_vm_array_push(void* vm_ptr, uint64_t arr_bits, uint64_t val_bits) {
  if (!vm_ptr) return;
  auto* vm = static_cast<VM*>(vm_ptr);
  Value arr, val;
  std::memcpy(&arr, &arr_bits, sizeof(uint64_t));
  std::memcpy(&val, &val_bits, sizeof(uint64_t));
  if (!arr.isArrayId()) return;
  vm->pushHostArrayValue(ArrayRef{arr.asArrayId()}, val);
}

// Object operations - use public API
uint64_t havel_vm_object_new(void* vm_ptr) {
  if (!vm_ptr) return Value::makeNull().rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  auto ref = vm->createHostObject();
  return Value::makeObjectId(ref.id).rawBits();
}

uint64_t havel_vm_object_get(void* vm_ptr, uint64_t obj_bits, uint32_t key_id) {
  if (!vm_ptr) return Value::makeNull().rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  auto* chunk = vm->getCurrentChunk();
  if (!chunk) return Value::makeNull().rawBits();
  
  Value obj;
  std::memcpy(&obj, &obj_bits, sizeof(uint64_t));
  if (!obj.isObjectId()) return Value::makeNull().rawBits();
  
  const auto& key = chunk->getString(key_id);
  if (key.empty()) return Value::makeNull().rawBits();
  
  return vm->getHostObjectField(ObjectRef{obj.asObjectId()}, key).rawBits();
}

uint64_t havel_vm_object_set(void* vm_ptr, uint64_t obj_bits, uint32_t key_id, uint64_t val_bits) {
  if (!vm_ptr) return val_bits;
  auto* vm = static_cast<VM*>(vm_ptr);
  auto* chunk = vm->getCurrentChunk();
  if (!chunk) return val_bits;
  
  Value obj, val;
  std::memcpy(&obj, &obj_bits, sizeof(uint64_t));
  std::memcpy(&val, &val_bits, sizeof(uint64_t));
  if (!obj.isObjectId()) return val_bits;
  
  const auto& key = chunk->getString(key_id);
  if (key.empty()) return val_bits;
  
  vm->setHostObjectField(ObjectRef{obj.asObjectId()}, key, val);
  return val_bits;
}

uint64_t havel_vm_object_get_raw(void* vm_ptr, uint64_t obj_bits, uint64_t key_bits) {
    if (!vm_ptr) return Value::makeNull().rawBits();
    auto* vm = static_cast<VM*>(vm_ptr);
    Value obj, key_val;
    std::memcpy(&obj, &obj_bits, sizeof(uint64_t));
    std::memcpy(&key_val, &key_bits, sizeof(uint64_t));
    if (!obj.isObjectId()) return Value::makeNull().rawBits();

    auto key_str = vm->resolveKeyPublic(key_val);
    if (!key_str) return Value::makeNull().rawBits();

    if (obj.asObjectId() == vm->globalsMirrorObjectId()) {
        return vm->lookupGlobalByKey(*key_str).rawBits();
    }

    return vm->objectGetWithClassChain(obj.asObjectId(), *key_str).rawBits();
}

void havel_vm_backedge(void* vm_ptr, uint32_t ip) {
    if (!vm_ptr) return;
    auto* vm = static_cast<VM*>(vm_ptr);
    vm->recordBackedgePublic(ip);
}

// Range and iterator operations - use public heap API
uint64_t havel_vm_range_new(void* vm_ptr, uint64_t start_bits, uint64_t end_bits) {
  if (!vm_ptr) return Value::makeNull().rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  Value start, end;
  std::memcpy(&start, &start_bits, sizeof(uint64_t));
  std::memcpy(&end, &end_bits, sizeof(uint64_t));
  if (!start.isInt() || !end.isInt()) return Value::makeNull().rawBits();
  auto ref = vm->getHeap().allocateRange(start.asInt(), end.asInt(), 1);
  return Value::makeRangeId(ref.id).rawBits();
}

uint64_t havel_vm_iter_new(void* vm_ptr, uint64_t coll_bits) {
  if (!vm_ptr) return Value::makeNull().rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  Value coll;
  std::memcpy(&coll, &coll_bits, sizeof(uint64_t));
  auto ref = vm->createIterator(coll);
  return Value::makeIteratorId(ref.id).rawBits();
}

uint64_t havel_vm_iter_next(void* vm_ptr, uint64_t iter_bits) {
  if (!vm_ptr) return Value::makeNull().rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  Value iter;
  std::memcpy(&iter, &iter_bits, sizeof(uint64_t));
  if (!iter.isIteratorId()) return Value::makeNull().rawBits();
  return vm->iteratorNext(IteratorRef{iter.asIteratorId()}).rawBits();
}

// Concurrency primitives
uint64_t havel_vm_thread_new(void* vm_ptr, uint32_t func_id) {
  if (!vm_ptr) return Value::makeNull().rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  Value fn = Value::makeFunctionObjId(func_id);

  // Prefer explicit concurrency bridge naming, then language-level naming.
  Value result = vm->invokeHostFunctionDirect("thread_spawn", {fn});
  if (result.isNull()) {
    result = vm->invokeHostFunctionDirect("thread.spawn", {fn});
  }
  if (result.isNull()) {
    result = vm->invokeHostFunctionDirect("thread", {fn});
  }
  return result.rawBits();
}

uint64_t havel_vm_channel_new(void* vm_ptr, uint64_t cap_bits) {
  if (!vm_ptr) return Value::makeNull().rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  Value cap;
  std::memcpy(&cap, &cap_bits, sizeof(uint64_t));

  Value result = vm->invokeHostFunctionDirect("channel_new", {cap});
  if (result.isNull()) {
    result = vm->invokeHostFunctionDirect("channel.new", {cap});
  }
  if (result.isNull()) {
    result = vm->invokeHostFunctionDirect("channel_new", {});
  }
  if (result.isNull()) {
    result = vm->invokeHostFunctionDirect("channel.new", {});
  }

  // Fallback: preserve runtime progress even when host bridge isn't installed.
  if (result.isNull()) {
    auto ref = vm->getHeap().allocateChannel();
    result = Value::makeChannelId(ref.id);
  }
  return result.rawBits();
}

void havel_vm_channel_send(void* vm_ptr, uint64_t chan_bits, uint64_t val_bits) {
  if (!vm_ptr) return;
  auto* vm = static_cast<VM*>(vm_ptr);
  Value chan, val;
  std::memcpy(&chan, &chan_bits, sizeof(uint64_t));
  std::memcpy(&val, &val_bits, sizeof(uint64_t));

  Value result = vm->invokeHostFunctionDirect("channel_send", {chan, val});
  if (result.isNull()) {
    (void)vm->invokeHostFunctionDirect("channel.send", {chan, val});
  }
}

uint64_t havel_vm_channel_recv(void* vm_ptr, uint64_t chan_bits) {
  if (!vm_ptr) return Value::makeNull().rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  Value chan;
  std::memcpy(&chan, &chan_bits, sizeof(uint64_t));

  Value result = vm->invokeHostFunctionDirect("channel_receive", {chan});
  if (result.isNull()) {
    result = vm->invokeHostFunctionDirect("channel.receive", {chan});
  }
  return result.rawBits();
}

uint64_t havel_vm_thread_join(void* vm_ptr, uint64_t thread_bits) {
  if (!vm_ptr) return Value::makeNull().rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  Value thread;
  std::memcpy(&thread, &thread_bits, sizeof(uint64_t));
  Value result = vm->invokeHostFunctionDirect("thread_join", {thread});
  if (result.isNull()) {
    result = vm->invokeHostFunctionDirect("thread.join", {thread});
  }
  return result.rawBits();
}

void havel_vm_thread_send(void* vm_ptr, uint64_t thread_bits, uint64_t val_bits) {
  if (!vm_ptr) return;
  auto* vm = static_cast<VM*>(vm_ptr);
  Value thread, val;
  std::memcpy(&thread, &thread_bits, sizeof(uint64_t));
  std::memcpy(&val, &val_bits, sizeof(uint64_t));
  Value result = vm->invokeHostFunctionDirect("thread_send", {thread, val});
  if (result.isNull()) {
    (void)vm->invokeHostFunctionDirect("thread.send", {thread, val});
  }
}

uint64_t havel_vm_thread_recv(void* vm_ptr, uint64_t thread_bits) {
  if (!vm_ptr) return Value::makeNull().rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  Value thread;
  std::memcpy(&thread, &thread_bits, sizeof(uint64_t));
  Value result = vm->invokeHostFunctionDirect("thread_receive", {thread});
  if (result.isNull()) {
    result = vm->invokeHostFunctionDirect("thread.receive", {thread});
  }
  return result.rawBits();
}

uint64_t havel_vm_interval_start(void* vm_ptr, uint64_t duration_bits, uint64_t callback_bits) {
  if (!vm_ptr) return Value::makeNull().rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  Value duration, callback;
  std::memcpy(&duration, &duration_bits, sizeof(uint64_t));
  std::memcpy(&callback, &callback_bits, sizeof(uint64_t));
  Value result = vm->invokeHostFunctionDirect("interval_start", {duration, callback});
  if (result.isNull()) {
    result = vm->invokeHostFunctionDirect("interval.start", {duration, callback});
  }
  return result.rawBits();
}

uint64_t havel_vm_interval_stop(void* vm_ptr, uint64_t interval_bits) {
  if (!vm_ptr) return Value::makeNull().rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  Value interval;
  std::memcpy(&interval, &interval_bits, sizeof(uint64_t));
  Value result = vm->invokeHostFunctionDirect("interval_stop", {interval});
  if (result.isNull()) {
    result = vm->invokeHostFunctionDirect("interval.stop", {interval});
  }
  return result.rawBits();
}

uint64_t havel_vm_timeout_start(void* vm_ptr, uint64_t delay_bits, uint64_t callback_bits) {
  if (!vm_ptr) return Value::makeNull().rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  Value delay, callback;
  std::memcpy(&delay, &delay_bits, sizeof(uint64_t));
  std::memcpy(&callback, &callback_bits, sizeof(uint64_t));
  Value result = vm->invokeHostFunctionDirect("timeout_start", {delay, callback});
  if (result.isNull()) {
    result = vm->invokeHostFunctionDirect("timeout.start", {delay, callback});
  }
  return result.rawBits();
}

uint64_t havel_vm_timeout_cancel(void* vm_ptr, uint64_t timeout_bits) {
  if (!vm_ptr) return Value::makeNull().rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  Value timeout;
  std::memcpy(&timeout, &timeout_bits, sizeof(uint64_t));
  Value result = vm->invokeHostFunctionDirect("timeout_cancel", {timeout});
  if (result.isNull()) {
    result = vm->invokeHostFunctionDirect("timeout.cancel", {timeout});
  }
  return result.rawBits();
}

uint64_t havel_vm_channel_close(void* vm_ptr, uint64_t chan_bits) {
  if (!vm_ptr) return Value::makeNull().rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  Value chan;
  std::memcpy(&chan, &chan_bits, sizeof(uint64_t));
  Value result = vm->invokeHostFunctionDirect("channel_close", {chan});
  if (result.isNull()) {
    result = vm->invokeHostFunctionDirect("channel.close", {chan});
  }
  return result.rawBits();
}

uint64_t havel_vm_yield(void* vm_ptr, uint64_t val_bits) {
  // Yield in JIT context - just return the value
  return val_bits;
}

uint64_t havel_vm_await(void* vm_ptr, uint64_t val_bits) {
  // Await in JIT context - just return the value
  return val_bits;
}

// String operations
uint64_t havel_vm_string_len(void* vm_ptr, uint64_t str_bits) {
  if (!vm_ptr) return Value(static_cast<int64_t>(0)).rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  Value str;
  std::memcpy(&str, &str_bits, sizeof(uint64_t));
  if (!str.isStringId()) return Value(static_cast<int64_t>(0)).rawBits();
  return Value(static_cast<int64_t>(vm->getRuntimeStringLength(StringRef{str.asStringId()}))).rawBits();
}

uint64_t havel_vm_string_concat(void* vm_ptr, uint64_t l_bits, uint64_t r_bits) {
  if (!vm_ptr) return Value::makeNull().rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  auto* chunk = vm->getCurrentChunk();
  if (!chunk) return Value::makeNull().rawBits();
  
  Value l, r;
  std::memcpy(&l, &l_bits, sizeof(uint64_t));
  std::memcpy(&r, &r_bits, sizeof(uint64_t));
  
  if (!l.isStringId() || !r.isStringId()) {
    return Value::makeNull().rawBits();
  }
  
  const auto& lStr = chunk->getString(l.asStringId());
  const auto& rStr = chunk->getString(r.asStringId());
  std::string result = lStr + rStr;
  auto ref = vm->createRuntimeString(std::move(result));
  return Value::makeStringId(ref.id).rawBits();
}

// Host function call
uint64_t havel_vm_call_host(void* vm_ptr, uint32_t host_idx, uint64_t* args, uint32_t count) {
    if (!vm_ptr) return 0x7FF8000000000003ULL;
    auto* vm = static_cast<VM*>(vm_ptr);
    
    std::vector<Value> valArgs;
    for (uint32_t i = 0; i < count; ++i) {
        Value v;
        std::memcpy(&v, &args[i], sizeof(uint64_t));
        valArgs.push_back(v);
    }
    
    return vm->callHostFunction(host_idx, valArgs).rawBits();
}

uint64_t havel_vm_call_method(void* vm_ptr, uint64_t receiver_bits, uint32_t method_name_id,
                              uint64_t* args, uint32_t arg_count) {
    if (!vm_ptr) return Value::makeNull().rawBits();
    auto* vm = static_cast<VM*>(vm_ptr);
    auto* chunk = vm->getCurrentChunk();
    if (!chunk) return Value::makeNull().rawBits();

    Value receiver;
    std::memcpy(&receiver, &receiver_bits, sizeof(uint64_t));
    const std::string method_name = chunk->getString(method_name_id);
    if (method_name.empty()) return Value::makeNull().rawBits();

    std::vector<Value> callArgs;
    callArgs.reserve(static_cast<size_t>(arg_count) + 1);
    callArgs.push_back(receiver);
    for (uint32_t i = 0; i < arg_count; ++i) {
        Value v;
        std::memcpy(&v, &args[i], sizeof(uint64_t));
        callArgs.push_back(v);
    }

    if (auto methodIdx = vm->getPrototypeMethod(receiver, method_name)) {
        if (auto hostName = vm->getHostFunctionName(*methodIdx)) {
            Value result = vm->invokeHostFunctionDirect(*hostName, callArgs);
            if (!result.isNull()) return result.rawBits();
        }
    }

    if (receiver.isObjectId()) {
        Value methodValue = vm->getHostObjectField(ObjectRef{receiver.asObjectId(), true}, method_name);
        if (!methodValue.isNull()) {
            if (methodValue.isHostFuncId()) {
                if (auto hostName = vm->getHostFunctionName(methodValue.asHostFuncId())) {
                    return vm->invokeHostFunctionDirect(*hostName, callArgs).rawBits();
                }
            }
            return vm->callFunction(methodValue, callArgs).rawBits();
        }
    }

    std::string typeName;
    if (receiver.isStringValId() || receiver.isStringId()) typeName = "string";
    else if (receiver.isArrayId()) typeName = "array";
    else if (receiver.isObjectId()) typeName = "object";

    if (!typeName.empty()) {
        Value result = vm->invokeHostFunctionDirect(typeName + "." + method_name, callArgs);
        if (!result.isNull()) return result.rawBits();
    }

  return Value::makeNull().rawBits();
  }

uint64_t havel_vm_closure_new(void* vm_ptr, uint32_t func_index) {
    if (!vm_ptr) return Value::makeNull().rawBits();
    auto* vm = static_cast<VM*>(vm_ptr);
    auto* chunk = vm->getCurrentChunk();
    if (!chunk || !chunk->getFunction(func_index))
        return Value::makeNull().rawBits();
    const auto* target = chunk->getFunction(func_index);

    GCHeap::RuntimeClosure closure;
    closure.function_index = func_index;
    closure.upvalues.reserve(target->upvalues.size());

    for (const auto& descriptor : target->upvalues) {
        if (descriptor.captures_local) {
            uint32_t abs = vm->toAbsoluteLocalPublic(descriptor.index);
            auto& open_uv = vm->openUpvaluesPublic();
            auto open_it = open_uv.find(abs);
            if (open_it == open_uv.end()) {
                auto cell = std::make_shared<GCHeap::UpvalueCell>();
                cell->is_open = true;
                cell->open_index = descriptor.index;
                cell->locals_base = static_cast<uint32_t>(vm->currentLocalsBasePublic());
                open_uv.emplace(abs, cell);
                closure.upvalues.push_back(std::move(cell));
            } else {
                closure.upvalues.push_back(open_it->second);
            }
        } else {
            auto* parent_closure = vm->currentClosurePublic();
            if (!parent_closure || descriptor.index >= parent_closure->upvalues.size())
                return Value::makeNull().rawBits();
            closure.upvalues.push_back(parent_closure->upvalues[descriptor.index]);
        }
    }

    auto ref = vm->getHeap().allocateClosure(std::move(closure));
    return Value::makeClosureId(ref.id).rawBits();
}

uint64_t havel_vm_array_del(void* vm_ptr, uint64_t arr_bits, uint64_t idx_bits) {
  if (!vm_ptr) return Value::makeBool(false).rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  Value arr, idx;
  std::memcpy(&arr, &arr_bits, sizeof(uint64_t));
  std::memcpy(&idx, &idx_bits, sizeof(uint64_t));
  if (!arr.isArrayId()) return Value::makeBool(false).rawBits();
  auto* a = vm->getHeap().array(arr.asArrayId());
  if (!a) return Value::makeBool(false).rawBits();
  int64_t i = vm->toIntPublic(idx);
  if (i < 0) i += static_cast<int64_t>(a->size());
  if (i < 0 || static_cast<size_t>(i) >= a->size()) return Value::makeBool(false).rawBits();
  a->erase(a->begin() + static_cast<size_t>(i));
  return Value::makeBool(true).rawBits();
}

uint64_t havel_vm_array_freeze(void* vm_ptr, uint64_t arr_bits) {
  if (!vm_ptr) return arr_bits;
  auto* vm = static_cast<VM*>(vm_ptr);
  Value arr;
  std::memcpy(&arr, &arr_bits, sizeof(uint64_t));
  if (!arr.isArrayId()) return arr_bits;
  auto* a = vm->getHeap().array(arr.asArrayId());
  if (a) a->frozen = true;
  return arr_bits;
}

uint64_t havel_vm_array_pop(void* vm_ptr, uint64_t arr_bits) {
  if (!vm_ptr) return Value::makeNull().rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  Value arr;
  std::memcpy(&arr, &arr_bits, sizeof(uint64_t));
  if (!arr.isArrayId()) return Value::makeNull().rawBits();
  auto* a = vm->getHeap().array(arr.asArrayId());
  if (!a || a->frozen || a->empty()) return Value::makeNull().rawBits();
  Value back = a->back();
  a->pop_back();
  return back.rawBits();
}

uint64_t havel_vm_array_has(void* vm_ptr, uint64_t arr_bits, uint64_t val_bits) {
  if (!vm_ptr) return Value::makeBool(false).rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  Value arr, val;
  std::memcpy(&arr, &arr_bits, sizeof(uint64_t));
  std::memcpy(&val, &val_bits, sizeof(uint64_t));
  if (!arr.isArrayId()) return Value::makeBool(false).rawBits();
  auto* a = vm->getHeap().array(arr.asArrayId());
  if (!a) return Value::makeBool(false).rawBits();
  for (const auto& e : *a) {
    Value ev = e;
    if (ev.rawBits() == val.rawBits()) return Value::makeBool(true).rawBits();
  }
  return Value::makeBool(false).rawBits();
}

uint64_t havel_vm_array_find(void* vm_ptr, uint64_t arr_bits, uint64_t val_bits) {
  if (!vm_ptr) return Value::makeInt(-1).rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  Value arr, val;
  std::memcpy(&arr, &arr_bits, sizeof(uint64_t));
  std::memcpy(&val, &val_bits, sizeof(uint64_t));
  if (!arr.isArrayId()) return Value::makeInt(-1).rawBits();
  auto* a = vm->getHeap().array(arr.asArrayId());
  if (!a) return Value::makeInt(-1).rawBits();
  for (size_t i = 0; i < a->size(); i++) {
    if ((*a)[i].rawBits() == val.rawBits())
      return Value::makeInt(static_cast<int64_t>(i)).rawBits();
  }
  return Value::makeInt(-1).rawBits();
}

uint64_t havel_vm_array_map(void* vm_ptr, uint64_t arr_bits, uint64_t fn_bits) {
  if (!vm_ptr) return Value::makeNull().rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  Value arr, fn;
  std::memcpy(&arr, &arr_bits, sizeof(uint64_t));
  std::memcpy(&fn, &fn_bits, sizeof(uint64_t));
  if (!arr.isArrayId()) return Value::makeNull().rawBits();
  auto* a = vm->getHeap().array(arr.asArrayId());
  if (!a) return Value::makeNull().rawBits();
  auto resultRef = vm->getHeap().allocateArray();
  auto* result = vm->getHeap().array(resultRef.id);
  for (size_t i = 0; i < a->size(); i++) {
    Value mapped = vm->callFunctionSyncPublic(fn, {(*a)[i]});
    result->push_back(mapped);
  }
  return Value::makeArrayId(resultRef.id).rawBits();
}

uint64_t havel_vm_array_filter(void* vm_ptr, uint64_t arr_bits, uint64_t fn_bits) {
  if (!vm_ptr) return Value::makeNull().rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  Value arr, fn;
  std::memcpy(&arr, &arr_bits, sizeof(uint64_t));
  std::memcpy(&fn, &fn_bits, sizeof(uint64_t));
  if (!arr.isArrayId()) return Value::makeNull().rawBits();
  auto* a = vm->getHeap().array(arr.asArrayId());
  if (!a) return Value::makeNull().rawBits();
  auto resultRef = vm->getHeap().allocateArray();
  auto* result = vm->getHeap().array(resultRef.id);
  for (size_t i = 0; i < a->size(); i++) {
    Value predResult = vm->callFunctionSyncPublic(fn, {(*a)[i]});
    if (predResult.isBool() && predResult.asBool()) {
      result->push_back((*a)[i]);
    }
  }
  return Value::makeArrayId(resultRef.id).rawBits();
}

uint64_t havel_vm_array_reduce(void* vm_ptr, uint64_t arr_bits, uint64_t fn_bits, uint64_t init_bits) {
  if (!vm_ptr) return init_bits;
  auto* vm = static_cast<VM*>(vm_ptr);
  Value arr, fn, initial;
  std::memcpy(&arr, &arr_bits, sizeof(uint64_t));
  std::memcpy(&fn, &fn_bits, sizeof(uint64_t));
  std::memcpy(&initial, &init_bits, sizeof(uint64_t));
  if (!arr.isArrayId()) return init_bits;
  auto* a = vm->getHeap().array(arr.asArrayId());
  if (!a) return init_bits;
  Value acc = initial;
  for (size_t i = 0; i < a->size(); i++) {
    acc = vm->callFunctionSyncPublic(fn, {acc, (*a)[i]});
  }
  return acc.rawBits();
}

uint64_t havel_vm_array_foreach(void* vm_ptr, uint64_t arr_bits, uint64_t fn_bits) {
  if (!vm_ptr) return Value::makeNull().rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  Value arr, fn;
  std::memcpy(&arr, &arr_bits, sizeof(uint64_t));
  std::memcpy(&fn, &fn_bits, sizeof(uint64_t));
  if (!arr.isArrayId()) return Value::makeNull().rawBits();
  auto* a = vm->getHeap().array(arr.asArrayId());
  if (!a) return Value::makeNull().rawBits();
  for (size_t i = 0; i < a->size(); i++) {
    vm->callFunctionSyncPublic(fn, {(*a)[i]});
  }
  return Value::makeNull().rawBits();
}

uint64_t havel_vm_set_new(void* vm_ptr) {
  if (!vm_ptr) return Value::makeNull().rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  auto ref = vm->getHeap().allocateSet();
  return Value::makeSetId(ref.id).rawBits();
}

uint64_t havel_vm_set_set(void* vm_ptr, uint64_t set_bits, uint64_t val_bits, uint64_t key_bits) {
  if (!vm_ptr) return Value::makeNull().rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  Value setVal, val, key;
  std::memcpy(&setVal, &set_bits, sizeof(uint64_t));
  std::memcpy(&val, &val_bits, sizeof(uint64_t));
  std::memcpy(&key, &key_bits, sizeof(uint64_t));
  if (!setVal.isSetId()) return Value::makeNull().rawBits();
  auto* s = vm->getHeap().set(setVal.asSetId());
  if (!s) return Value::makeNull().rawBits();
        auto k = vm->resolveKeyPublic(key);
        if (!k) return Value::makeNull().rawBits();
        (*s)[*k] = val;
        return Value::makeNull().rawBits();
    }

uint64_t havel_vm_set_del(void* vm_ptr, uint64_t set_bits, uint64_t key_bits) {
    if (!vm_ptr) return Value::makeBool(false).rawBits();
    auto* vm = static_cast<VM*>(vm_ptr);
    Value setVal, key;
    std::memcpy(&setVal, &set_bits, sizeof(uint64_t));
    std::memcpy(&key, &key_bits, sizeof(uint64_t));
    if (!setVal.isSetId()) return Value::makeBool(false).rawBits();
    auto* s = vm->getHeap().set(setVal.asSetId());
    if (!s) return Value::makeBool(false).rawBits();
    auto k = vm->resolveKeyPublic(key);
  if (!k) return Value::makeBool(false).rawBits();
  return Value::makeBool(s->erase(*k) > 0).rawBits();
}

uint64_t havel_vm_range_step_new(void* vm_ptr, uint64_t start_bits, uint64_t end_bits, uint64_t step_bits) {
  if (!vm_ptr) return Value::makeNull().rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  Value start, end, step;
  std::memcpy(&start, &start_bits, sizeof(uint64_t));
  std::memcpy(&end, &end_bits, sizeof(uint64_t));
  std::memcpy(&step, &step_bits, sizeof(uint64_t));
    int64_t start_val = vm->toIntPublic(start);
    int64_t end_val = vm->toIntPublic(end);
    int64_t step_val = vm->toIntPublic(step);
    auto ref = vm->getHeap().allocateRange(start_val, end_val, step_val);
    return Value::makeRangeId(ref.id).rawBits();
}

uint64_t havel_vm_object_keys(void* vm_ptr, uint64_t obj_bits) {
  if (!vm_ptr) return Value::makeNull().rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  Value obj;
  std::memcpy(&obj, &obj_bits, sizeof(uint64_t));
  if (!obj.isObjectId()) return Value::makeNull().rawBits();
  auto* o = vm->getHeap().object(obj.asObjectId());
  if (!o) return Value::makeNull().rawBits();
  auto ref = vm->getHeap().allocateArray();
  auto* arr = vm->getHeap().array(ref.id);
  for (const auto& [k, v] : o->data) {
    auto strRef = vm->createRuntimeString(k);
    arr->push_back(Value::makeStringId(strRef.id));
  }
  return Value::makeArrayId(ref.id).rawBits();
}

uint64_t havel_vm_object_values(void* vm_ptr, uint64_t obj_bits) {
  if (!vm_ptr) return Value::makeNull().rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  Value obj;
  std::memcpy(&obj, &obj_bits, sizeof(uint64_t));
  if (!obj.isObjectId()) return Value::makeNull().rawBits();
  auto* o = vm->getHeap().object(obj.asObjectId());
  if (!o) return Value::makeNull().rawBits();
  auto ref = vm->getHeap().allocateArray();
  auto* arr = vm->getHeap().array(ref.id);
  for (const auto& [k, v] : o->data) {
    arr->push_back(v);
  }
  return Value::makeArrayId(ref.id).rawBits();
}

uint64_t havel_vm_object_entries(void* vm_ptr, uint64_t obj_bits) {
  if (!vm_ptr) return Value::makeNull().rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  Value obj;
  std::memcpy(&obj, &obj_bits, sizeof(uint64_t));
  if (!obj.isObjectId()) return Value::makeNull().rawBits();
  auto* o = vm->getHeap().object(obj.asObjectId());
  if (!o) return Value::makeNull().rawBits();
  auto ref = vm->getHeap().allocateArray();
  auto* arr = vm->getHeap().array(ref.id);
  for (const auto& [k, v] : o->data) {
    auto pairRef = vm->getHeap().allocateArray();
    auto* pair = vm->getHeap().array(pairRef.id);
    auto strRef = vm->createRuntimeString(k);
    pair->push_back(Value::makeStringId(strRef.id));
    pair->push_back(v);
    arr->push_back(Value::makeArrayId(pairRef.id));
  }
  return Value::makeArrayId(ref.id).rawBits();
}

uint64_t havel_vm_object_has(void* vm_ptr, uint64_t obj_bits, uint32_t key_id) {
  if (!vm_ptr) return Value::makeBool(false).rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  auto* chunk = vm->getCurrentChunk();
  if (!chunk) return Value::makeBool(false).rawBits();
  Value obj;
  std::memcpy(&obj, &obj_bits, sizeof(uint64_t));
  if (!obj.isObjectId()) return Value::makeBool(false).rawBits();
  auto* o = vm->getHeap().object(obj.asObjectId());
  if (!o) return Value::makeBool(false).rawBits();
  const std::string& key = chunk->getString(key_id);
  return Value::makeBool(o->data.count(key) > 0).rawBits();
}

uint64_t havel_vm_object_delete(void* vm_ptr, uint64_t obj_bits, uint32_t key_id) {
  if (!vm_ptr) return Value::makeBool(false).rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  auto* chunk = vm->getCurrentChunk();
  if (!chunk) return Value::makeBool(false).rawBits();
  Value obj;
  std::memcpy(&obj, &obj_bits, sizeof(uint64_t));
  if (!obj.isObjectId()) return Value::makeBool(false).rawBits();
  auto* o = vm->getHeap().object(obj.asObjectId());
  if (!o) return Value::makeBool(false).rawBits();
  const std::string& key = chunk->getString(key_id);
  return Value::makeBool(o->data.erase(key) > 0).rawBits();
}

uint64_t havel_vm_object_new_unsorted(void* vm_ptr) {
  if (!vm_ptr) return Value::makeNull().rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  auto ref = vm->getHeap().allocateObject(false);
  return Value::makeObjectId(ref.id).rawBits();
}

uint64_t havel_vm_string_upper(void* vm_ptr, uint64_t str_bits) {
  if (!vm_ptr) return Value::makeNull().rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  Value v;
  std::memcpy(&v, &str_bits, sizeof(uint64_t));
  std::string s = vm->toString(v);
  std::transform(s.begin(), s.end(), s.begin(), ::toupper);
  auto ref = vm->createRuntimeString(std::move(s));
  return Value::makeStringId(ref.id).rawBits();
}

uint64_t havel_vm_string_lower(void* vm_ptr, uint64_t str_bits) {
  if (!vm_ptr) return Value::makeNull().rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  Value v;
  std::memcpy(&v, &str_bits, sizeof(uint64_t));
  std::string s = vm->toString(v);
  std::transform(s.begin(), s.end(), s.begin(), ::tolower);
  auto ref = vm->createRuntimeString(std::move(s));
  return Value::makeStringId(ref.id).rawBits();
}

uint64_t havel_vm_string_trim(void* vm_ptr, uint64_t str_bits) {
  if (!vm_ptr) return Value::makeNull().rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  Value v;
  std::memcpy(&v, &str_bits, sizeof(uint64_t));
  std::string s = vm->toString(v);
  size_t start = s.find_first_not_of(" \t\n\r");
  if (start == std::string::npos) {
    auto ref = vm->createRuntimeString("");
    return Value::makeStringId(ref.id).rawBits();
  }
  size_t end = s.find_last_not_of(" \t\n\r");
  auto ref = vm->createRuntimeString(s.substr(start, end - start + 1));
  return Value::makeStringId(ref.id).rawBits();
}

uint64_t havel_vm_string_sub(void* vm_ptr, uint64_t str_bits, uint64_t start_bits, uint64_t len_bits) {
  if (!vm_ptr) return Value::makeNull().rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  Value v, sv, lv;
  std::memcpy(&v, &str_bits, sizeof(uint64_t));
  std::memcpy(&sv, &start_bits, sizeof(uint64_t));
  std::memcpy(&lv, &len_bits, sizeof(uint64_t));
  std::string s = vm->toString(v);
  int64_t start = vm->toIntPublic(sv);
  int64_t len = vm->toIntPublic(lv);
  if (start < 0) start = 0;
  if (start > static_cast<int64_t>(s.size())) start = static_cast<int64_t>(s.size());
  if (len < 0) len = 0;
  auto ref = vm->createRuntimeString(s.substr(static_cast<size_t>(start), static_cast<size_t>(len)));
  return Value::makeStringId(ref.id).rawBits();
}

uint64_t havel_vm_string_find(void* vm_ptr, uint64_t str_bits, uint64_t sub_bits) {
  if (!vm_ptr) return Value::makeInt(-1).rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  Value v, sub;
  std::memcpy(&v, &str_bits, sizeof(uint64_t));
  std::memcpy(&sub, &sub_bits, sizeof(uint64_t));
  std::string s = vm->toString(v);
  std::string subStr = vm->toString(sub);
  auto pos = s.find(subStr);
  if (pos == std::string::npos) return Value::makeInt(-1).rawBits();
  return Value::makeInt(static_cast<int64_t>(pos)).rawBits();
}

uint64_t havel_vm_string_has(void* vm_ptr, uint64_t str_bits, uint64_t sub_bits) {
  if (!vm_ptr) return Value::makeBool(false).rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  Value v, sub;
  std::memcpy(&v, &str_bits, sizeof(uint64_t));
  std::memcpy(&sub, &sub_bits, sizeof(uint64_t));
  std::string s = vm->toString(v);
  std::string subStr = vm->toString(sub);
  return Value::makeBool(s.find(subStr) != std::string::npos).rawBits();
}

uint64_t havel_vm_string_starts(void* vm_ptr, uint64_t str_bits, uint64_t pre_bits) {
  if (!vm_ptr) return Value::makeBool(false).rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  Value v, pre;
  std::memcpy(&v, &str_bits, sizeof(uint64_t));
  std::memcpy(&pre, &pre_bits, sizeof(uint64_t));
  std::string s = vm->toString(v);
  std::string p = vm->toString(pre);
  return Value::makeBool(s.size() >= p.size() && s.compare(0, p.size(), p) == 0).rawBits();
}

uint64_t havel_vm_string_ends(void* vm_ptr, uint64_t str_bits, uint64_t suf_bits) {
  if (!vm_ptr) return Value::makeBool(false).rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  Value v, suf;
  std::memcpy(&v, &str_bits, sizeof(uint64_t));
  std::memcpy(&suf, &suf_bits, sizeof(uint64_t));
  std::string s = vm->toString(v);
  std::string sf = vm->toString(suf);
  return Value::makeBool(s.size() >= sf.size() && s.compare(s.size() - sf.size(), sf.size(), sf) == 0).rawBits();
}

uint64_t havel_vm_string_split(void* vm_ptr, uint64_t str_bits, uint64_t delim_bits) {
  if (!vm_ptr) return Value::makeNull().rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  Value v, delim;
  std::memcpy(&v, &str_bits, sizeof(uint64_t));
  std::memcpy(&delim, &delim_bits, sizeof(uint64_t));
  std::string s = vm->toString(v);
  std::string d = vm->toString(delim);
  auto ref = vm->getHeap().allocateArray();
  auto* arr = vm->getHeap().array(ref.id);
  if (d.empty()) {
    for (char c : s) {
      auto sr = vm->createRuntimeString(std::string(1, c));
      arr->push_back(Value::makeStringId(sr.id));
    }
  } else {
    size_t pos = 0;
    while (pos <= s.size()) {
      size_t found = s.find(d, pos);
      if (found == std::string::npos) {
        auto sr = vm->createRuntimeString(s.substr(pos));
        arr->push_back(Value::makeStringId(sr.id));
        break;
      }
      auto sr = vm->createRuntimeString(s.substr(pos, found - pos));
      arr->push_back(Value::makeStringId(sr.id));
      pos = found + d.size();
    }
  }
  return Value::makeArrayId(ref.id).rawBits();
}

uint64_t havel_vm_string_replace(void* vm_ptr, uint64_t str_bits, uint64_t old_bits, uint64_t new_bits) {
  if (!vm_ptr) return Value::makeNull().rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  Value v, oldVal, newVal;
  std::memcpy(&v, &str_bits, sizeof(uint64_t));
  std::memcpy(&oldVal, &old_bits, sizeof(uint64_t));
  std::memcpy(&newVal, &new_bits, sizeof(uint64_t));
  std::string s = vm->toString(v);
  std::string oldStr = vm->toString(oldVal);
  std::string newStr = vm->toString(newVal);
  if (!oldStr.empty()) {
    size_t pos = 0;
    while ((pos = s.find(oldStr, pos)) != std::string::npos) {
      s.replace(pos, oldStr.size(), newStr);
      pos += newStr.size();
    }
  }
  auto ref = vm->createRuntimeString(std::move(s));
  return Value::makeStringId(ref.id).rawBits();
}

uint64_t havel_vm_string_promote(void* vm_ptr, uint64_t str_bits) {
  if (!vm_ptr) return str_bits;
  auto* vm = static_cast<VM*>(vm_ptr);
  Value v;
  std::memcpy(&v, &str_bits, sizeof(uint64_t));
  std::string s = vm->toString(v);
  auto ref = vm->createRuntimeString(std::move(s));
  return Value::makeStringId(ref.id).rawBits();
}

uint64_t havel_vm_to_int(void* vm_ptr, uint64_t val_bits) {
  if (!vm_ptr) return val_bits;
  auto* vm = static_cast<VM*>(vm_ptr);
  Value v;
  std::memcpy(&v, &val_bits, sizeof(uint64_t));
    return Value::makeInt(vm->toIntPublic(v)).rawBits();
}

uint64_t havel_vm_to_float(void* vm_ptr, uint64_t val_bits) {
  if (!vm_ptr) return val_bits;
  auto* vm = static_cast<VM*>(vm_ptr);
  Value v;
  std::memcpy(&v, &val_bits, sizeof(uint64_t));
    return Value::makeDouble(vm->toFloatPublic(v)).rawBits();
}

uint64_t havel_vm_to_string(void* vm_ptr, uint64_t val_bits) {
  if (!vm_ptr) return Value::makeNull().rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  Value v;
  std::memcpy(&v, &val_bits, sizeof(uint64_t));
  auto ref = vm->createRuntimeString(vm->toString(v));
  return Value::makeStringId(ref.id).rawBits();
}

uint64_t havel_vm_to_bool(void* vm_ptr, uint64_t val_bits) {
  if (!vm_ptr) return Value::makeBool(false).rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  Value v;
  std::memcpy(&v, &val_bits, sizeof(uint64_t));
    return Value::makeBool(vm->toBoolPublic(v)).rawBits();
}

uint64_t havel_vm_type_of(void* vm_ptr, uint64_t val_bits) {
  if (!vm_ptr) return Value::makeInt(0).rawBits();
  Value v;
  std::memcpy(&v, &val_bits, sizeof(uint64_t));
  if (v.isInt()) return Value::makeInt(1).rawBits();
  if (v.isDouble()) return Value::makeInt(2).rawBits();
  if (v.isBool()) return Value::makeInt(3).rawBits();
  if (v.isNull()) return Value::makeInt(4).rawBits();
  if (v.isStringId() || v.isStringValId()) return Value::makeInt(5).rawBits();
  if (v.isArrayId()) return Value::makeInt(6).rawBits();
  if (v.isObjectId()) return Value::makeInt(7).rawBits();
  if (v.isClosureId() || v.isFunctionObjId()) return Value::makeInt(8).rawBits();
  return Value::makeInt(0).rawBits();
}

uint64_t havel_vm_as_type(void* vm_ptr, uint64_t val_bits, uint32_t type_name_id) {
  if (!vm_ptr) return val_bits;
  auto* vm = static_cast<VM*>(vm_ptr);
  auto* chunk = vm->getCurrentChunk();
  if (!chunk) return val_bits;
  Value v;
  std::memcpy(&v, &val_bits, sizeof(uint64_t));
  const std::string& typeName = chunk->getString(type_name_id);
    if (typeName == "int" || typeName == "Int") return Value::makeInt(vm->toIntPublic(v)).rawBits();
  if (typeName == "float" || typeName == "Float" || typeName == "double" || typeName == "num" || typeName == "Num")
    return Value::makeDouble(vm->toFloatPublic(v)).rawBits();
  if (typeName == "string" || typeName == "String") {
    auto ref = vm->createRuntimeString(vm->toString(v));
    return Value::makeStringId(ref.id).rawBits();
  }
  if (typeName == "bool" || typeName == "Bool" || typeName == "boolean")
    return Value::makeBool(vm->toBoolPublic(v)).rawBits();
  if (typeName == "array" || typeName == "Array") {
    if (v.isArrayId()) return val_bits;
    auto ref = vm->getHeap().allocateArray();
    return Value::makeArrayId(ref.id).rawBits();
  }
  return val_bits;
}

uint64_t havel_vm_print(void* vm_ptr, uint64_t val_bits) {
  if (!vm_ptr) return Value::makeNull().rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  Value v;
  std::memcpy(&v, &val_bits, sizeof(uint64_t));
    ::havel::debug("{}", vm->toString(v));
  return Value::makeNull().rawBits();
}

uint64_t havel_vm_debug(void* vm_ptr) {
  if (!vm_ptr) return Value::makeNull().rawBits();
    ::havel::debug("JIT debug breakpoint");
  return Value::makeNull().rawBits();
}

uint64_t havel_vm_import(void* vm_ptr, uint64_t path_bits) {
  if (!vm_ptr) return Value::makeNull().rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  Value pathVal;
  std::memcpy(&pathVal, &path_bits, sizeof(uint64_t));
  std::string path = vm->toString(pathVal);
  if (path.empty()) return Value::makeNull().rawBits();
  return vm->loadModule(path).rawBits();
}

uint64_t havel_vm_yield_resume(void* vm_ptr, uint64_t co_bits) {
  if (!vm_ptr) return Value::makeNull().rawBits();
  return Value::makeNull().rawBits();
}

uint64_t havel_vm_go_async(void* vm_ptr, uint64_t fn_bits) {
  if (!vm_ptr) return Value::makeNull().rawBits();
  return Value::makeNull().rawBits();
}

uint64_t havel_vm_spread(void* vm_ptr, uint64_t val_bits) {
  if (!vm_ptr) return val_bits;
  auto* vm = static_cast<VM*>(vm_ptr);
  Value v;
  std::memcpy(&v, &val_bits, sizeof(uint64_t));
  if (v.isArrayId()) {
    auto* arr = vm->getHeap().array(v.asArrayId());
    if (!arr) return val_bits;
    for (const auto& elem : *arr) {
        vm->pushStackPublic(elem);
    }
  }
  return val_bits;
}

uint64_t havel_vm_class_new(void* vm_ptr, uint32_t type_id, uint32_t parent_type_id, uint32_t field_count) {
  if (!vm_ptr) return Value::makeNull().rawBits();
  (void)field_count;
  auto* vm = static_cast<VM*>(vm_ptr);
  Value typeName = Value::makeStringValId(type_id);
  Value result = vm->invokeHostFunctionDirect("class.new", {typeName});
  if (!result.isNull() && parent_type_id != 0) {
    Value parentName = Value::makeStringValId(parent_type_id);
    Value parentObj = vm->lookupGlobalByKey(vm->toString(parentName));
    if (!parentObj.isNull()) {
      (void)vm->invokeHostFunctionDirect("inherits", {result, parentObj});
    }
  }
  return result.rawBits();
}

uint64_t havel_vm_struct_new(void* vm_ptr, uint32_t type_id, uint64_t* args_bits,
                             uint32_t arg_count) {
  if (!vm_ptr) return Value::makeNull().rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  Value typeName = Value::makeStringValId(type_id);
  std::vector<Value> args;
  args.reserve(static_cast<size_t>(arg_count) + 1);
  args.push_back(typeName);
  for (uint32_t i = 0; i < arg_count; ++i) {
    Value v;
    std::memcpy(&v, &args_bits[i], sizeof(uint64_t));
    args.push_back(v);
  }
  return vm->invokeHostFunctionDirect("struct.new", args).rawBits();
}

uint64_t havel_vm_struct_get(void* vm_ptr, uint64_t obj_bits, uint32_t field_id) {
  if (!vm_ptr) return Value::makeNull().rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  Value obj;
  std::memcpy(&obj, &obj_bits, sizeof(uint64_t));
  Value field = Value::makeStringValId(field_id);
  return vm->invokeHostFunctionDirect("struct.get", {obj, field}).rawBits();
}

uint64_t havel_vm_struct_set(void* vm_ptr, uint64_t obj_bits, uint32_t field_id,
                             uint64_t val_bits) {
  if (!vm_ptr) return Value::makeNull().rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  Value obj, val;
  std::memcpy(&obj, &obj_bits, sizeof(uint64_t));
  std::memcpy(&val, &val_bits, sizeof(uint64_t));
  Value field = Value::makeStringValId(field_id);
  return vm->invokeHostFunctionDirect("struct.set", {obj, field, val}).rawBits();
}

uint64_t havel_vm_prot_check(void* vm_ptr, uint64_t value_bits, uint32_t proto_id) {
  if (!vm_ptr) return Value::makeBool(false).rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  auto* chunk = vm->getCurrentChunk();
  if (!chunk) return Value::makeBool(false).rawBits();
  Value value;
  std::memcpy(&value, &value_bits, sizeof(uint64_t));
  const std::string proto = chunk->getString(proto_id);
  if (proto == "Iterable") return vm->invokeHostFunctionDirect("isIterable", {value}).rawBits();
  if (proto == "Indexable") return vm->invokeHostFunctionDirect("isIndexable", {value}).rawBits();
  if (proto == "Callable") return vm->invokeHostFunctionDirect("callable", {value}).rawBits();
  return Value::makeBool(false).rawBits();
}

uint64_t havel_vm_prot_cast(void* vm_ptr, uint64_t value_bits, uint32_t proto_id) {
  if (!vm_ptr) return Value::makeNull().rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  Value value;
  std::memcpy(&value, &value_bits, sizeof(uint64_t));
  uint64_t check_bits = havel_vm_prot_check(vm_ptr, value_bits, proto_id);
  Value ok;
  std::memcpy(&ok, &check_bits, sizeof(uint64_t));
  return vm->toBoolPublic(ok) ? value.rawBits() : Value::makeNull().rawBits();
}

uint64_t havel_vm_class_get_field(void* vm_ptr, uint64_t obj_bits, uint32_t field_id) {
  if (!vm_ptr) return Value::makeNull().rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  Value obj;
  std::memcpy(&obj, &obj_bits, sizeof(uint64_t));
  if (!obj.isObjectId()) return Value::makeNull().rawBits();
  auto* o = vm->getHeap().object(obj.asObjectId());
  if (!o) return Value::makeNull().rawBits();
  auto* chunk = vm->getCurrentChunk();
  if (!chunk) return Value::makeNull().rawBits();
  const std::string& key = chunk->getString(field_id);
  auto it = o->data.find(key);
  if (it != o->data.end()) return it->second.rawBits();
  return Value::makeNull().rawBits();
}

uint64_t havel_vm_class_set_field(void* vm_ptr, uint64_t obj_bits, uint32_t field_id, uint64_t val_bits) {
  if (!vm_ptr) return val_bits;
  auto* vm = static_cast<VM*>(vm_ptr);
  Value obj, val;
  std::memcpy(&obj, &obj_bits, sizeof(uint64_t));
  std::memcpy(&val, &val_bits, sizeof(uint64_t));
  if (!obj.isObjectId()) return val_bits;
  auto* o = vm->getHeap().object(obj.asObjectId());
  if (!o) return val_bits;
  auto* chunk = vm->getCurrentChunk();
  if (!chunk) return val_bits;
  const std::string& key = chunk->getString(field_id);
  o->data[key] = val;
  return val_bits;
}

uint64_t havel_vm_load_class_proto(void* vm_ptr, uint32_t type_id) {
  if (!vm_ptr) return Value::makeNull().rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  auto* chunk = vm->getCurrentChunk();
  if (!chunk) return Value::makeNull().rawBits();
  const std::string& typeName = chunk->getString(type_id);
  auto global = vm->getGlobalThreadSafe(typeName);
  return global.has_value() ? global->rawBits() : Value::makeNull().rawBits();
}

uint64_t havel_vm_call_super(void* vm_ptr, uint64_t obj_bits, uint32_t method_id, uint64_t* args, uint32_t arg_count) {
  if (!vm_ptr) return Value::makeNull().rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  auto* chunk = vm->getCurrentChunk();
  if (!chunk) return Value::makeNull().rawBits();

  Value obj;
  std::memcpy(&obj, &obj_bits, sizeof(uint64_t));
  if (!obj.isObjectId()) return Value::makeNull().rawBits();

  // Resolve parent class/prototype.
  Value classObj = vm->getHostObjectField(ObjectRef{obj.asObjectId(), true}, "__class");
  if (classObj.isNull()) {
    classObj = vm->getHostObjectField(ObjectRef{obj.asObjectId(), true}, "__parent");
  }
  if (!classObj.isObjectId()) return Value::makeNull().rawBits();

  Value parentObj = vm->getHostObjectField(ObjectRef{classObj.asObjectId(), true}, "__parent");
  if (!parentObj.isObjectId()) return Value::makeNull().rawBits();

  const std::string methodName = chunk->getString(method_id);
  if (methodName.empty()) return Value::makeNull().rawBits();
  Value method = vm->objectGetWithClassChain(parentObj.asObjectId(), methodName);
  if (method.isNull()) return Value::makeNull().rawBits();

  std::vector<Value> callArgs;
  callArgs.reserve(static_cast<size_t>(arg_count) + 1);
  callArgs.push_back(obj); // self / receiver
  for (uint32_t i = 0; i < arg_count; ++i) {
    Value v;
    std::memcpy(&v, &args[i], sizeof(uint64_t));
    callArgs.push_back(v);
  }
  return vm->callFunction(method, callArgs).rawBits();
}

uint64_t havel_vm_enum_new(void* vm_ptr, uint32_t type_id, uint32_t tag, uint32_t payload_count) {
    if (!vm_ptr) return Value::makeNull().rawBits();
    auto* vm = static_cast<VM*>(vm_ptr);
    EnumRef ref = vm->createEnum(type_id, tag, payload_count);
    if (payload_count > 0) {
        auto* payloads = vm->getHeap().enumPayloadsMut(ref.id);
        if (payloads) {
            for (uint32_t i = 0; i < payload_count && i < payloads->size(); ++i) {
                Value p = vm->popStackPublic();
                (*payloads)[payloads->size() - 1 - i] = p;
            }
        }
    }
    return Value::makeEnumId(ref.id).rawBits();
}

uint64_t havel_vm_enum_tag(void* vm_ptr, uint64_t enum_bits) {
    if (!vm_ptr) return Value::makeInt(0).rawBits();
    auto* vm = static_cast<VM*>(vm_ptr);
    Value v;
    std::memcpy(&v, &enum_bits, sizeof(uint64_t));
    if (!v.isEnumId()) return Value::makeInt(0).rawBits();
    EnumRef ref{v.asEnumId(), 0, 0};
    return Value::makeInt(static_cast<int64_t>(vm->getEnumTag(ref))).rawBits();
}

uint64_t havel_vm_enum_payload(void* vm_ptr, uint64_t enum_bits, uint32_t idx) {
    if (!vm_ptr) return Value::makeNull().rawBits();
    auto* vm = static_cast<VM*>(vm_ptr);
    Value v;
    std::memcpy(&v, &enum_bits, sizeof(uint64_t));
    if (!v.isEnumId()) return Value::makeNull().rawBits();
    EnumRef ref{v.asEnumId(), 0, 0};
    return vm->getEnumPayload(ref, idx).rawBits();
}

uint64_t havel_vm_export_fn(void* vm_ptr, uint32_t name_id, uint64_t fn_bits) {
  if (!vm_ptr) return fn_bits;
  auto* vm = static_cast<VM*>(vm_ptr);
  auto* chunk = vm->getCurrentChunk();
  if (!chunk) return fn_bits;
  std::string name = chunk->getString(name_id);
  Value fn;
  std::memcpy(&fn, &fn_bits, sizeof(uint64_t));
  vm->setGlobal("__export_" + name, fn);
  return fn_bits;
}

uint64_t havel_vm_export_var(void* vm_ptr, uint32_t name_id, uint64_t val_bits) {
  if (!vm_ptr) return val_bits;
  auto* vm = static_cast<VM*>(vm_ptr);
  auto* chunk = vm->getCurrentChunk();
  if (!chunk) return val_bits;
  std::string name = chunk->getString(name_id);
  Value val;
  std::memcpy(&val, &val_bits, sizeof(uint64_t));
  vm->setGlobal("__export_" + name, val);
  return val_bits;
}

uint64_t havel_vm_begin_module(void* vm_ptr) {
  return Value::makeNull().rawBits();
}

uint64_t havel_vm_end_module(void* vm_ptr) {
  if (!vm_ptr) return Value::makeNull().rawBits();
  return Value::makeNull().rawBits();
}

} // extern "C"

// ============================================================================
// BytecodeOrcJIT – implementation
// ============================================================================

void BytecodeOrcJIT::InitializeLLVM() {
    static bool initialized = false;
    if (initialized) return;
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();
    initialized = true;
}

BytecodeOrcJIT::BytecodeOrcJIT() {
    InitializeLLVM();
    initTargetMachine();
    if (const char* optEnv = std::getenv("HAVEL_JIT_OPT_LEVEL")) {
        int parsed = std::atoi(optEnv);
        if (parsed < 0) parsed = 0;
        if (parsed > 3) parsed = 3;
        optimization_level_ = static_cast<uint8_t>(parsed);
    }

    auto jit_or_err = LLJITBuilder().create();
    if (!jit_or_err) {
        llvm::consumeError(jit_or_err.takeError());
        return;
    }
    lljit_ = std::move(*jit_or_err);

    auto &jd = lljit_->getMainJITDylib();
    auto &es = lljit_->getExecutionSession();

    SymbolMap syms;
    auto addSym = [&](const char* name, void* ptr) {
        syms[es.intern(name)] = {
            ExecutorAddr::fromPtr(ptr),
            llvm::JITSymbolFlags::Exported
        };
    };

addSym("havel_vm_throw_error", reinterpret_cast<void*>(&havel_vm_throw_error));
addSym("havel_vm_throw_value", reinterpret_cast<void*>(&havel_vm_throw_value));
    addSym("havel_vm_try_enter", reinterpret_cast<void*>(&havel_vm_try_enter));
    addSym("havel_vm_try_exit", reinterpret_cast<void*>(&havel_vm_try_exit));
    addSym("havel_vm_load_exception", reinterpret_cast<void*>(&havel_vm_load_exception));
addSym("havel_gc_write_barrier", reinterpret_cast<void*>(&havel_gc_write_barrier));
addSym("havel_gc_register_roots", reinterpret_cast<void*>(&havel_gc_register_roots));
addSym("havel_gc_unregister_roots",reinterpret_cast<void*>(&havel_gc_unregister_roots));
addSym("havel_deoptimize", reinterpret_cast<void*>(&havel_deoptimize));
addSym("havel_vm_call", reinterpret_cast<void*>(&havel_vm_call));
addSym("havel_vm_tail_call", reinterpret_cast<void*>(&havel_vm_tail_call));
addSym("havel_vm_global_get", reinterpret_cast<void*>(&havel_vm_global_get));
addSym("havel_vm_global_set", reinterpret_cast<void*>(&havel_vm_global_set));
addSym("havel_vm_upvalue_get", reinterpret_cast<void*>(&havel_vm_upvalue_get));
addSym("havel_vm_upvalue_set", reinterpret_cast<void*>(&havel_vm_upvalue_set));
addSym("havel_vm_close_upvalues", reinterpret_cast<void*>(&havel_vm_close_upvalues));
addSym("havel_vm_locals_base", reinterpret_cast<void*>(&havel_vm_locals_base));
addSym("havel_vm_pow", reinterpret_cast<void*>(&havel_vm_pow));
addSym("havel_vm_array_new", reinterpret_cast<void*>(&havel_vm_array_new));
addSym("havel_vm_array_get", reinterpret_cast<void*>(&havel_vm_array_get));
addSym("havel_vm_array_set", reinterpret_cast<void*>(&havel_vm_array_set));
addSym("havel_vm_array_len", reinterpret_cast<void*>(&havel_vm_array_len));
addSym("havel_vm_array_push", reinterpret_cast<void*>(&havel_vm_array_push));
addSym("havel_vm_object_new", reinterpret_cast<void*>(&havel_vm_object_new));
    addSym("havel_vm_object_get", reinterpret_cast<void*>(&havel_vm_object_get));
    addSym("havel_vm_object_get_raw", reinterpret_cast<void*>(&havel_vm_object_get_raw));
    addSym("havel_vm_object_set", reinterpret_cast<void*>(&havel_vm_object_set));
addSym("havel_vm_range_new", reinterpret_cast<void*>(&havel_vm_range_new));
addSym("havel_vm_iter_new", reinterpret_cast<void*>(&havel_vm_iter_new));
addSym("havel_vm_iter_next", reinterpret_cast<void*>(&havel_vm_iter_next));
addSym("havel_vm_thread_new", reinterpret_cast<void*>(&havel_vm_thread_new));
addSym("havel_vm_thread_join", reinterpret_cast<void*>(&havel_vm_thread_join));
addSym("havel_vm_thread_send", reinterpret_cast<void*>(&havel_vm_thread_send));
addSym("havel_vm_thread_recv", reinterpret_cast<void*>(&havel_vm_thread_recv));
addSym("havel_vm_channel_new", reinterpret_cast<void*>(&havel_vm_channel_new));
addSym("havel_vm_channel_send", reinterpret_cast<void*>(&havel_vm_channel_send));
addSym("havel_vm_channel_recv", reinterpret_cast<void*>(&havel_vm_channel_recv));
addSym("havel_vm_channel_close", reinterpret_cast<void*>(&havel_vm_channel_close));
addSym("havel_vm_interval_start", reinterpret_cast<void*>(&havel_vm_interval_start));
addSym("havel_vm_interval_stop", reinterpret_cast<void*>(&havel_vm_interval_stop));
addSym("havel_vm_timeout_start", reinterpret_cast<void*>(&havel_vm_timeout_start));
addSym("havel_vm_timeout_cancel", reinterpret_cast<void*>(&havel_vm_timeout_cancel));
addSym("havel_vm_yield", reinterpret_cast<void*>(&havel_vm_yield));
addSym("havel_vm_await", reinterpret_cast<void*>(&havel_vm_await));
addSym("havel_vm_string_len", reinterpret_cast<void*>(&havel_vm_string_len));
addSym("havel_vm_string_concat", reinterpret_cast<void*>(&havel_vm_string_concat));
        addSym("havel_vm_call_host", reinterpret_cast<void*>(&havel_vm_call_host));
        addSym("havel_vm_call_method", reinterpret_cast<void*>(&havel_vm_call_method));
        addSym("havel_vm_eq", reinterpret_cast<void*>(&havel_vm_eq));
        addSym("havel_vm_neq", reinterpret_cast<void*>(&havel_vm_neq));
        addSym("havel_vm_lt", reinterpret_cast<void*>(&havel_vm_lt));
        addSym("havel_vm_lte", reinterpret_cast<void*>(&havel_vm_lte));
        addSym("havel_vm_gt", reinterpret_cast<void*>(&havel_vm_gt));
        addSym("havel_vm_gte", reinterpret_cast<void*>(&havel_vm_gte));
        addSym("havel_vm_is", reinterpret_cast<void*>(&havel_vm_is));
        addSym("havel_vm_not", reinterpret_cast<void*>(&havel_vm_not));
        addSym("havel_vm_is_truthy", reinterpret_cast<void*>(&havel_vm_is_truthy));
        addSym("havel_vm_closure_new", reinterpret_cast<void*>(&havel_vm_closure_new));
        addSym("havel_vm_array_del", reinterpret_cast<void*>(&havel_vm_array_del));
        addSym("havel_vm_array_freeze", reinterpret_cast<void*>(&havel_vm_array_freeze));
        addSym("havel_vm_array_pop", reinterpret_cast<void*>(&havel_vm_array_pop));
        addSym("havel_vm_array_has", reinterpret_cast<void*>(&havel_vm_array_has));
        addSym("havel_vm_array_find", reinterpret_cast<void*>(&havel_vm_array_find));
        addSym("havel_vm_array_map", reinterpret_cast<void*>(&havel_vm_array_map));
        addSym("havel_vm_array_filter", reinterpret_cast<void*>(&havel_vm_array_filter));
        addSym("havel_vm_array_reduce", reinterpret_cast<void*>(&havel_vm_array_reduce));
        addSym("havel_vm_array_foreach", reinterpret_cast<void*>(&havel_vm_array_foreach));
        addSym("havel_vm_set_new", reinterpret_cast<void*>(&havel_vm_set_new));
        addSym("havel_vm_set_set", reinterpret_cast<void*>(&havel_vm_set_set));
        addSym("havel_vm_set_del", reinterpret_cast<void*>(&havel_vm_set_del));
        addSym("havel_vm_range_step_new", reinterpret_cast<void*>(&havel_vm_range_step_new));
        addSym("havel_vm_object_keys", reinterpret_cast<void*>(&havel_vm_object_keys));
        addSym("havel_vm_object_values", reinterpret_cast<void*>(&havel_vm_object_values));
        addSym("havel_vm_object_entries", reinterpret_cast<void*>(&havel_vm_object_entries));
        addSym("havel_vm_object_has", reinterpret_cast<void*>(&havel_vm_object_has));
    addSym("havel_vm_object_delete", reinterpret_cast<void*>(&havel_vm_object_delete));
    addSym("havel_vm_backedge", reinterpret_cast<void*>(&havel_vm_backedge));
    addSym("havel_vm_object_new_unsorted", reinterpret_cast<void*>(&havel_vm_object_new_unsorted));
        addSym("havel_vm_string_upper", reinterpret_cast<void*>(&havel_vm_string_upper));
        addSym("havel_vm_string_lower", reinterpret_cast<void*>(&havel_vm_string_lower));
        addSym("havel_vm_string_trim", reinterpret_cast<void*>(&havel_vm_string_trim));
        addSym("havel_vm_string_sub", reinterpret_cast<void*>(&havel_vm_string_sub));
        addSym("havel_vm_string_find", reinterpret_cast<void*>(&havel_vm_string_find));
        addSym("havel_vm_string_has", reinterpret_cast<void*>(&havel_vm_string_has));
        addSym("havel_vm_string_starts", reinterpret_cast<void*>(&havel_vm_string_starts));
        addSym("havel_vm_string_ends", reinterpret_cast<void*>(&havel_vm_string_ends));
        addSym("havel_vm_string_split", reinterpret_cast<void*>(&havel_vm_string_split));
        addSym("havel_vm_string_replace", reinterpret_cast<void*>(&havel_vm_string_replace));
        addSym("havel_vm_string_promote", reinterpret_cast<void*>(&havel_vm_string_promote));
        addSym("havel_vm_to_int", reinterpret_cast<void*>(&havel_vm_to_int));
        addSym("havel_vm_to_float", reinterpret_cast<void*>(&havel_vm_to_float));
        addSym("havel_vm_to_string", reinterpret_cast<void*>(&havel_vm_to_string));
        addSym("havel_vm_to_bool", reinterpret_cast<void*>(&havel_vm_to_bool));
        addSym("havel_vm_type_of", reinterpret_cast<void*>(&havel_vm_type_of));
        addSym("havel_vm_as_type", reinterpret_cast<void*>(&havel_vm_as_type));
        addSym("havel_vm_print", reinterpret_cast<void*>(&havel_vm_print));
        addSym("havel_vm_debug", reinterpret_cast<void*>(&havel_vm_debug));
        addSym("havel_vm_import", reinterpret_cast<void*>(&havel_vm_import));
        addSym("havel_vm_yield_resume", reinterpret_cast<void*>(&havel_vm_yield_resume));
        addSym("havel_vm_go_async", reinterpret_cast<void*>(&havel_vm_go_async));
        addSym("havel_vm_spread", reinterpret_cast<void*>(&havel_vm_spread));
        addSym("havel_vm_class_new", reinterpret_cast<void*>(&havel_vm_class_new));
        addSym("havel_vm_struct_new", reinterpret_cast<void*>(&havel_vm_struct_new));
        addSym("havel_vm_struct_get", reinterpret_cast<void*>(&havel_vm_struct_get));
        addSym("havel_vm_struct_set", reinterpret_cast<void*>(&havel_vm_struct_set));
        addSym("havel_vm_prot_check", reinterpret_cast<void*>(&havel_vm_prot_check));
        addSym("havel_vm_prot_cast", reinterpret_cast<void*>(&havel_vm_prot_cast));
        addSym("havel_vm_class_get_field", reinterpret_cast<void*>(&havel_vm_class_get_field));
        addSym("havel_vm_class_set_field", reinterpret_cast<void*>(&havel_vm_class_set_field));
        addSym("havel_vm_load_class_proto", reinterpret_cast<void*>(&havel_vm_load_class_proto));
        addSym("havel_vm_call_super", reinterpret_cast<void*>(&havel_vm_call_super));
        addSym("havel_vm_enum_new", reinterpret_cast<void*>(&havel_vm_enum_new));
        addSym("havel_vm_enum_tag", reinterpret_cast<void*>(&havel_vm_enum_tag));
        addSym("havel_vm_enum_payload", reinterpret_cast<void*>(&havel_vm_enum_payload));
        addSym("havel_vm_export_fn", reinterpret_cast<void*>(&havel_vm_export_fn));
        addSym("havel_vm_export_var", reinterpret_cast<void*>(&havel_vm_export_var));
        addSym("havel_vm_begin_module", reinterpret_cast<void*>(&havel_vm_begin_module));
        addSym("havel_vm_end_module", reinterpret_cast<void*>(&havel_vm_end_module));

        if (auto err = jd.define(absoluteSymbols(std::move(syms)))) {
        llvm::consumeError(std::move(err));
    }
}

BytecodeOrcJIT::~BytecodeOrcJIT() = default;

void BytecodeOrcJIT::initTargetMachine() {
    auto target_triple_str = llvm::sys::getDefaultTargetTriple();
    llvm::Triple target_triple(target_triple_str);
    llvm::errs() << "DEBUG: Target triple: " << target_triple_str << "\n";
    std::string error;
    auto target = llvm::TargetRegistry::lookupTarget(target_triple, error);
    if (!target) {
        llvm::errs() << "Failed to lookup target for triple '" << target_triple_str
                     << "': " << error << "\n";
        return;
    }
    llvm::errs() << "DEBUG: Target name: " << target->getName() << "\n";
    llvm::TargetOptions opt;
    target_machine_.reset(target->createTargetMachine(
        target_triple, "x86-64", "+64bit-mode", opt, llvm::Reloc::PIC_, std::nullopt, llvm::CodeGenOptLevel::Default));
    if (!target_machine_) {
        llvm::errs() << "Failed to create target machine for triple '" << target_triple_str << "'\n";
    } else {
        llvm::errs() << "DEBUG: Target machine created successfully\n";
    }
}

void BytecodeOrcJIT::compileFunction(const BytecodeFunction &func) {
    // Coroutines are currently interpreter-only: JIT frame suspension/resume
    // semantics are not preserved for YIELD/GO_ASYNC paths yet.
    for (const auto& instr : func.instructions) {
        switch (instr.opcode) {
            case OpCode::YIELD:
            case OpCode::YIELD_RESUME:
            case OpCode::GO_ASYNC:
                return;
            default:
                break;
        }
    }

    auto context = std::make_unique<llvm::LLVMContext>();
    auto module  = std::make_unique<llvm::Module>(func.name, *context);

    if (target_machine_) {
        module->setDataLayout(target_machine_->createDataLayout());
        module->setTargetTriple(target_machine_->getTargetTriple());
    }

    translate(func, *module);
    runOptimizations(*module);

    if (dump_ir_) {
        ::havel::debug("--- LLVM IR for {} ---", func.name);
        module->print(llvm::errs(), nullptr);
    }

    if ((debug_jit_ || dump_asm_to_file_) && target_machine_) {
        // Use raw_fd_ostream which is compatible with addPassesToEmitFile
        std::error_code ec;
        std::string asm_file = "/tmp/havel_asm_" + func.name + ".s";
        llvm::raw_fd_ostream ros(asm_file, ec, llvm::sys::fs::OF_None);
        
        if (!ec) {
            // Use a local pass manager for file emission (legacy but necessary for this)
            llvm::legacy::PassManager pm;
            if (!target_machine_->addPassesToEmitFile(pm, ros, nullptr, llvm::CodeGenFileType::AssemblyFile)) {
                pm.run(*module);
                ros.flush();
                ros.close();
                
                // Read the assembly into memory for debug output
                std::ifstream asm_input(asm_file);
                if (asm_input.is_open()) {
                    std::stringstream buffer;
                    buffer << asm_input.rdbuf();
                    last_asm_ = buffer.str();
                    asm_input.close();
                    
                    if (debug_jit_) {
        ::havel::debug("--- Assembly for {} ---", func.name);
        ::havel::debug("{}", last_asm_);
                    }
                }
            }
            
            if (dump_asm_to_file_) {
                dumpAssembly(func.name + ".s");
                std::error_code ec2;
                llvm::raw_fd_ostream ir_os(func.name + ".ll", ec2, llvm::sys::fs::OF_None);
                if (!ec2) module->print(ir_os, nullptr);
            }
        }
    }

    if (auto err = lljit_->addIRModule(ThreadSafeModule(std::move(module), std::move(context)))) {
        llvm::consumeError(std::move(err));
        return;
    }

    auto sym = lljit_->lookup(func.name);
    if (!sym) {
        llvm::consumeError(sym.takeError());
        return;
    }

    fptrs_[func.name] = reinterpret_cast<void*>((*sym).getValue());
    func.jit_compiled = true;
}

Value BytecodeOrcJIT::executeCompiled(VM* vm, const std::string &func_name,
                                      const std::vector<Value> &args) {
    auto it = fptrs_.find(func_name);
    if (it == fptrs_.end()) return Value::makeNull();

    typedef uint64_t (*NativeFunc)(void*, const Value*, uint32_t);
    auto func = reinterpret_cast<NativeFunc>(it->second);

    try {
        uint64_t res_bits =
            func(static_cast<void*>(vm), args.data(), static_cast<uint32_t>(args.size()));
        Value res;
        std::memcpy(&res, &res_bits, sizeof(uint64_t));
        return res;
    } catch (const ScriptThrow&) {
        // Preserve script exception semantics so VM dispatch can route to
        // TRY_ENTER handlers in active VM frames.
        throw;
    } catch (const std::exception& e) {
        vm->throwError(std::string("JIT exception in ") + func_name + ": " + e.what());
        return Value::makeNull();
    } catch (...) {
        vm->throwError(std::string("Unknown JIT exception in ") + func_name);
        return Value::makeNull();
    }
}

bool BytecodeOrcJIT::isCompiled(const std::string &func_name) const {
    return fptrs_.count(func_name) > 0;
}

void BytecodeOrcJIT::dumpAssembly(const std::string &filename) {
    std::error_code ec;
    llvm::raw_fd_ostream os(filename, ec, llvm::sys::fs::OF_None);
    if (!ec) os << last_asm_;
}

void BytecodeOrcJIT::runOptimizations(llvm::Module &module) {
    llvm::LoopAnalysisManager lam;
    llvm::FunctionAnalysisManager fam;
    llvm::CGSCCAnalysisManager cgam;
    llvm::ModuleAnalysisManager mam;
    
    llvm::PassBuilder pb;
    pb.registerModuleAnalyses(mam);
    pb.registerCGSCCAnalyses(cgam);
    pb.registerFunctionAnalyses(fam);
    pb.registerLoopAnalyses(lam);
    pb.crossRegisterProxies(lam, fam, cgam, mam);
    
    llvm::OptimizationLevel level = llvm::OptimizationLevel::O1;
    switch (optimization_level_) {
      case 0: level = llvm::OptimizationLevel::O0; break;
      case 1: level = llvm::OptimizationLevel::O1; break;
      case 2: level = llvm::OptimizationLevel::O2; break;
      case 3: level = llvm::OptimizationLevel::O3; break;
      default: level = llvm::OptimizationLevel::O1; break;
    }
    llvm::ModulePassManager mpm = pb.buildPerModuleDefaultPipeline(level);
    mpm.run(module, mam);
}

static bool opcodeProducesHeapRef(OpCode op) {
    switch (op) {
        case OpCode::ARRAY_NEW:
        case OpCode::OBJECT_NEW:
        case OpCode::LOAD_CONST:
        case OpCode::LOAD_VAR:
        case OpCode::LOAD_GLOBAL:
        case OpCode::CLOSURE:
            return true;
        default:
            return false;
    }
}

void BytecodeOrcJIT::translate(const BytecodeFunction &func, llvm::Module &module) {
    llvm::LLVMContext &ctx = module.getContext();
    llvm::IRBuilder<> B(ctx);

    llvm::Type *i1  = llvm::Type::getInt1Ty(ctx);
    llvm::Type *i32 = llvm::Type::getInt32Ty(ctx);
    llvm::Type *i64 = llvm::Type::getInt64Ty(ctx);
    llvm::Type *f64 = llvm::Type::getDoubleTy(ctx);
    llvm::Type *voidT = llvm::Type::getVoidTy(ctx);
    llvm::Type *i8p = llvm::PointerType::getUnqual(llvm::Type::getInt8Ty(ctx));
    llvm::Type *i64p = llvm::PointerType::getUnqual(i64);

    std::vector<llvm::Type*> paramTypes = {i8p, i64p, i32};
    llvm::FunctionType *funcType = llvm::FunctionType::get(i64, paramTypes, false);
    llvm::Function *f = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, func.name, &module);

    llvm::BasicBlock *entryBB = llvm::BasicBlock::Create(ctx, "entry", f);
    B.SetInsertPoint(entryBB);

    llvm::Value *vmArg = f->getArg(0);
    llvm::Value *argsArg = f->getArg(1);

    llvm::Type *frameType = llvm::StructType::create(
        ctx,
        {i8p,
         llvm::ArrayType::get(i64, 32),
         llvm::ArrayType::get(i64, 32),
         i32,
         llvm::ArrayType::get(i32, 32),
         llvm::ArrayType::get(i32, 32),
         llvm::ArrayType::get(i32, 32),
         i32},
        "JITStackFrame");
    llvm::Value *frame = B.CreateAlloca(frameType, nullptr, "gc_frame");

    llvm::Function *fn_reg = module.getFunction("havel_gc_register_roots");
    if (!fn_reg) fn_reg = llvm::Function::Create(llvm::FunctionType::get(voidT, {i8p, llvm::PointerType::getUnqual(frameType), i64p, i32}, false), llvm::Function::ExternalLinkage, "havel_gc_register_roots", &module);
    B.CreateCall(fn_reg, {vmArg, frame, B.CreateInBoundsGEP(i64, argsArg, llvm::ConstantInt::get(i32, 0)), llvm::ConstantInt::get(i32, func.local_count)});

    std::vector<llvm::Value*> vlocals;
    for (uint32_t i = 0; i < func.local_count; ++i) {
        vlocals.push_back(B.CreateAlloca(i64, nullptr, "l" + std::to_string(i)));
        if (i < func.param_count) {
             B.CreateStore(B.CreateLoad(i64, B.CreateInBoundsGEP(i64, argsArg, llvm::ConstantInt::get(i32, i))), vlocals[i]);
        } else {
             B.CreateStore(llvm::ConstantInt::get(i64, QNAN | (3ULL << 48)), vlocals[i]);
        }
    }

    std::vector<llvm::Value*> vstack;
    std::vector<size_t> jit_try_stack_depths;

    auto makeNull = [&]() { return llvm::ConstantInt::get(i64, QNAN | (3ULL << 48)); };

    auto emitWriteBarrier = [&](llvm::Value* val) {
        llvm::Function *fn_wb = module.getFunction("havel_gc_write_barrier");
        if (!fn_wb) fn_wb = llvm::Function::Create(llvm::FunctionType::get(voidT, {i8p, i64}, false), llvm::Function::ExternalLinkage, "havel_gc_write_barrier", &module);
        B.CreateCall(fn_wb, {vmArg, val});
    };

    auto unboxInt = [&](llvm::Value* boxed) {
        llvm::Value* payload = B.CreateAnd(boxed, llvm::ConstantInt::get(i64, PAYLOAD_MASK));
        llvm::Value* shl = B.CreateShl(payload, llvm::ConstantInt::get(i64, 16));
        return B.CreateAShr(shl, llvm::ConstantInt::get(i64, 16));
    };
    
    auto boxInt = [&](llvm::Value* raw) {
        llvm::Value* masked = B.CreateAnd(raw, llvm::ConstantInt::get(i64, PAYLOAD_MASK));
        return B.CreateOr(masked, llvm::ConstantInt::get(i64, INT_TAG_BITS));
    };

    auto isInt48Loc = [&](llvm::Value* v) -> llvm::Value* {
        llvm::Value* tag = B.CreateAnd(v, llvm::ConstantInt::get(i64, TAG_MASK));
        return B.CreateICmpEQ(tag, llvm::ConstantInt::get(i64, 0x0001000000000000ULL));
    };

    auto isDblLoc = [&](llvm::Value* v) -> llvm::Value* {
        return B.CreateICmpNE(B.CreateAnd(v, llvm::ConstantInt::get(i64, QNAN)), llvm::ConstantInt::get(i64, QNAN));
    };

auto emitSpecializedBinop = [&](OpCode op, const TypeFeedback* fb, size_t ip, llvm::Value* left, llvm::Value* right) -> llvm::Value* {
    // Check for AOT type hint - if we know the type at compile time, use direct path
    uint64_t type_hint = (fb && fb->has_aot_hint) ? fb->aot_type_hint : 0;
    
    // If AOT hint says both operands are int, use direct integer path
    if ((type_hint & TYPE_HINT_INT) && !(type_hint & TYPE_HINT_NUMBER)) {
        // Pure integer operation - no runtime check needed
        llvm::Value *lIv = unboxInt(left);
        llvm::Value *rIv = unboxInt(right);
        llvm::Value *iRes = nullptr;
        if (op == OpCode::ADD) iRes = B.CreateAdd(lIv, rIv);
        else if (op == OpCode::SUB) iRes = B.CreateSub(lIv, rIv);
        else if (op == OpCode::MUL) iRes = B.CreateMul(lIv, rIv);
else if (op == OpCode::INT_DIV) iRes = B.CreateSDiv(lIv, rIv);
  else if (op == OpCode::REMAINDER) iRes = B.CreateSRem(lIv, rIv);
  else if (op == OpCode::DIV) {
    llvm::Value* lD = B.CreateSIToFP(lIv, f64);
    llvm::Value* rD = B.CreateSIToFP(rIv, f64);
    llvm::Value* dRes = B.CreateFDiv(lD, rD);
    return B.CreateBitCast(dRes, i64);
  }
  else if (op == OpCode::MOD) {
    // Python-style: sign follows divisor
    // result = ((l % r) + r) % r when signs differ
    llvm::Value* cRem = B.CreateSRem(lIv, rIv);
    llvm::Value* signsDiffer = B.CreateICmpSLT(B.CreateXor(cRem, rIv), llvm::ConstantInt::get(i64, 0));
    llvm::Value* adjusted = B.CreateAdd(cRem, rIv);
    iRes = B.CreateSelect(signsDiffer, adjusted, cRem);
  }
        else iRes = B.CreateAdd(lIv, rIv);
        return boxInt(iRes);
    }
    
    // If AOT hint says number (float), use direct float path
    if (type_hint & TYPE_HINT_NUMBER) {
        llvm::Value *lDv = B.CreateBitCast(left, f64);
        llvm::Value *rDv = B.CreateBitCast(right, f64);
        llvm::Value *dRes = nullptr;
        if (op == OpCode::ADD) dRes = B.CreateFAdd(lDv, rDv);
        else if (op == OpCode::SUB) dRes = B.CreateFSub(lDv, rDv);
        else if (op == OpCode::MUL) dRes = B.CreateFMul(lDv, rDv);
        else if (op == OpCode::DIV) dRes = B.CreateFDiv(lDv, rDv);
else if (op == OpCode::INT_DIV) {
    llvm::Value* lI = B.CreateFPToSI(lDv, i64);
    llvm::Value* rI = B.CreateFPToSI(rDv, i64);
    llvm::Value* iRes = B.CreateSDiv(lI, rI);
    return boxInt(iRes);
  }
  else if (op == OpCode::REMAINDER) {
    llvm::Value* lI = B.CreateFPToSI(lDv, i64);
    llvm::Value* rI = B.CreateFPToSI(rDv, i64);
    llvm::Value* iRes = B.CreateSRem(lI, rI);
    return boxInt(iRes);
  }
  else if (op == OpCode::MOD) dRes = B.CreateFRem(lDv, rDv);
        else dRes = B.CreateFAdd(lDv, rDv);
        return B.CreateBitCast(dRes, i64);
    }

    // No AOT hint - use speculative optimization with runtime type checks
    std::string pfx = "op" + std::to_string(ip) + "_";
    llvm::BasicBlock *intBB = llvm::BasicBlock::Create(ctx, pfx+"int", f);
    llvm::BasicBlock *chkDblBB = llvm::BasicBlock::Create(ctx, pfx+"chk_dbl", f);
    llvm::BasicBlock *dblBB = llvm::BasicBlock::Create(ctx, pfx+"dbl", f);
    llvm::BasicBlock *deoptBB = llvm::BasicBlock::Create(ctx, pfx+"deopt", f);
    llvm::BasicBlock *mergeBB = llvm::BasicBlock::Create(ctx, pfx+"merge", f);

    llvm::Value* bothInt = B.CreateAnd(isInt48Loc(left), isInt48Loc(right));
    B.CreateCondBr(bothInt, intBB, chkDblBB);

    B.SetInsertPoint(intBB);
    llvm::Value *lIv = unboxInt(left);
    llvm::Value *rIv = unboxInt(right);
    llvm::Value *iRes = nullptr;
    llvm::Value *iBoxed = nullptr;
    if (op == OpCode::ADD) iRes = B.CreateAdd(lIv, rIv);
    else if (op == OpCode::SUB) iRes = B.CreateSub(lIv, rIv);
    else if (op == OpCode::MUL) iRes = B.CreateMul(lIv, rIv);
else if (op == OpCode::INT_DIV) iRes = B.CreateSDiv(lIv, rIv);
  else if (op == OpCode::REMAINDER) iRes = B.CreateSRem(lIv, rIv);
  else if (op == OpCode::DIV) {
    llvm::Value* lD = B.CreateSIToFP(lIv, f64);
    llvm::Value* rD = B.CreateSIToFP(rIv, f64);
    llvm::Value* dRes = B.CreateFDiv(lD, rD);
    iBoxed = B.CreateBitCast(dRes, i64);
  }
  else if (op == OpCode::MOD) {
    llvm::Value* cRem = B.CreateSRem(lIv, rIv);
    llvm::Value* signsDiffer = B.CreateICmpSLT(B.CreateXor(cRem, rIv), llvm::ConstantInt::get(i64, 0));
    llvm::Value* adjusted = B.CreateAdd(cRem, rIv);
    iRes = B.CreateSelect(signsDiffer, adjusted, cRem);
  }
    else iRes = B.CreateAdd(lIv, rIv); // Fallback
    if (!iBoxed) iBoxed = boxInt(iRes);
    auto* iExitBB = B.GetInsertBlock();
    B.CreateBr(mergeBB);

    B.SetInsertPoint(chkDblBB);
    llvm::Value* bothDbl = B.CreateAnd(isDblLoc(left), isDblLoc(right));
    B.CreateCondBr(bothDbl, dblBB, deoptBB);

    B.SetInsertPoint(dblBB);
    llvm::Value *lDv = B.CreateBitCast(left, f64);
    llvm::Value *rDv = B.CreateBitCast(right, f64);
    llvm::Value *dBoxed = nullptr;
if (op == OpCode::INT_DIV) {
    llvm::Value* lI = B.CreateFPToSI(lDv, i64);
    llvm::Value* rI = B.CreateFPToSI(rDv, i64);
    llvm::Value* iRes2 = B.CreateSDiv(lI, rI);
    dBoxed = boxInt(iRes2);
  } else if (op == OpCode::REMAINDER) {
    llvm::Value* lI = B.CreateFPToSI(lDv, i64);
    llvm::Value* rI = B.CreateFPToSI(rDv, i64);
    llvm::Value* iRes2 = B.CreateSRem(lI, rI);
    dBoxed = boxInt(iRes2);
  } else {
        llvm::Value *dRes = nullptr;
        if (op == OpCode::ADD) dRes = B.CreateFAdd(lDv, rDv);
        else if (op == OpCode::SUB) dRes = B.CreateFSub(lDv, rDv);
        else if (op == OpCode::MUL) dRes = B.CreateFMul(lDv, rDv);
        else if (op == OpCode::MOD) dRes = B.CreateFRem(lDv, rDv);
        else dRes = B.CreateFDiv(lDv, rDv);
        dBoxed = B.CreateBitCast(dRes, i64);
    }
    auto* dExitBB = B.GetInsertBlock();
    B.CreateBr(mergeBB);

    B.SetInsertPoint(deoptBB);
    llvm::Function *fn_deopt = module.getFunction("havel_deoptimize");
    if (!fn_deopt) fn_deopt = llvm::Function::Create(llvm::FunctionType::get(voidT, {i8p, i64, i64, i8p}, false), llvm::Function::ExternalLinkage, "havel_deoptimize", &module);

    // Create global string constant for function name
    llvm::Constant *funcNameStr = llvm::ConstantDataArray::getString(module.getContext(), func.name);
    llvm::GlobalVariable *gv = new llvm::GlobalVariable(
        module, funcNameStr->getType(), true,
        llvm::GlobalValue::PrivateLinkage, funcNameStr);
    llvm::Value* funcNameConst = B.CreatePointerCast(gv, i8p);

    B.CreateCall(fn_deopt, {vmArg, left, right, funcNameConst});
    B.CreateBr(mergeBB);

    B.SetInsertPoint(mergeBB);
    llvm::PHINode *phi = B.CreatePHI(i64, 3);
    phi->addIncoming(iBoxed, iExitBB);
    phi->addIncoming(dBoxed, dExitBB);
    phi->addIncoming(makeNull(), deoptBB);
    return phi;
};

    // Build one block per bytecode instruction plus one exit/fallthrough block.
    // This gives correct branch fallthrough targets for conditional jumps.
    std::vector<llvm::BasicBlock*> basicBlocks(func.instructions.size() + 1, nullptr);
    for (size_t ip = 0; ip <= func.instructions.size(); ++ip) {
        basicBlocks[ip] = llvm::BasicBlock::Create(ctx, "ip" + std::to_string(ip), f);
    }
    B.CreateBr(basicBlocks[0]);

    // Emit instructions with control flow.
    for (size_t ip = 0; ip < func.instructions.size(); ++ip) {
        B.SetInsertPoint(basicBlocks[ip]);
        llvm::BasicBlock* instrBlock = basicBlocks[ip]; // Save the instruction block

        // Skip if current block already has instructions (and thus may have terminator)
        // Note: use empty() instead of getTerminator() due to LLVM internal state issues
        if (!instrBlock->empty()) {
            continue;
        }

        const auto &instr = func.instructions[ip];
        const TypeFeedback* fb = (ip < func.type_feedback.size()) ? &func.type_feedback[ip] : nullptr;

        switch (instr.opcode) {
        case OpCode::LOAD_CONST: {
            uint64_t bits; std::memcpy(&bits, &func.constants[instr.operands[0].asInt()], 8);
            vstack.push_back(llvm::ConstantInt::get(i64, bits));
            break;
        }
        case OpCode::LOAD_VAR: vstack.push_back(B.CreateLoad(i64, vlocals[instr.operands[0].asInt()])); break;
        case OpCode::STORE_VAR: {
            llvm::Value* v = vstack.back(); vstack.pop_back();
            B.CreateStore(v, vlocals[instr.operands[0].asInt()]);
            if (opcodeProducesHeapRef(instr.opcode)) emitWriteBarrier(v);
            break;
        }
        case OpCode::POP: vstack.pop_back(); break;
        case OpCode::DUP: vstack.push_back(vstack.back()); break;

        case OpCode::ADD:
        case OpCode::SUB:
        case OpCode::MUL:
    case OpCode::DIV:
case OpCode::INT_DIV:
    case OpCode::DIVMOD:
    case OpCode::REMAINDER: {
      llvm::Value* r = vstack.back(); vstack.pop_back();
      llvm::Value* l = vstack.back(); vstack.pop_back();
      vstack.push_back(emitSpecializedBinop(instr.opcode, fb, ip, l, r));
      break;
    }
        case OpCode::NEGATE: {
            llvm::Value* v = vstack.back(); vstack.pop_back();
            // Negate: for int48, unbox, negate, rebox; for double, just fneg
            llvm::Value* isInt = isInt48Loc(v);
            llvm::BasicBlock *intBB = llvm::BasicBlock::Create(ctx, "neg_int", f);
            llvm::BasicBlock *dblBB = llvm::BasicBlock::Create(ctx, "neg_dbl", f);
            llvm::BasicBlock *mergeBB = llvm::BasicBlock::Create(ctx, "neg_merge", f);
            B.CreateCondBr(isInt, intBB, dblBB);
            B.SetInsertPoint(intBB);
            llvm::Value* intNeg = boxInt(B.CreateNeg(unboxInt(v)));
            B.CreateBr(mergeBB);
            B.SetInsertPoint(dblBB);
            llvm::Value* dblNeg = B.CreateBitCast(B.CreateFNeg(B.CreateBitCast(v, f64)), i64);
            B.CreateBr(mergeBB);
            B.SetInsertPoint(mergeBB);
            llvm::PHINode* phi = B.CreatePHI(i64, 2);
            phi->addIncoming(intNeg, intBB);
            phi->addIncoming(dblNeg, dblBB);
            vstack.push_back(phi);
            break;
        }
        case OpCode::INCLOCAL:
        case OpCode::DECLOCAL:
        case OpCode::INCLOCAL_POST:
        case OpCode::DECLOCAL_POST: {
            uint32_t slot = instr.operands[0].asInt();
            llvm::Value* oldBoxed = B.CreateLoad(i64, vlocals[slot]);
            llvm::Value* oldRaw = unboxInt(oldBoxed);
            llvm::Value* delta = llvm::ConstantInt::get(i64,
                (instr.opcode == OpCode::INCLOCAL || instr.opcode == OpCode::INCLOCAL_POST) ? 1 : -1);
            llvm::Value* newRaw = B.CreateAdd(oldRaw, delta);
            llvm::Value* newBoxed = boxInt(newRaw);
            B.CreateStore(newBoxed, vlocals[slot]);

            // Postfix returns old value; prefix returns new value.
            if (instr.opcode == OpCode::INCLOCAL_POST || instr.opcode == OpCode::DECLOCAL_POST) {
                vstack.push_back(oldBoxed);
            } else {
                vstack.push_back(newBoxed);
            }
            break;
        }

      // Comparisons — use semantic bridge functions for correct NaN-boxed dispatch
      case OpCode::EQ:
      case OpCode::NEQ:
      case OpCode::LT:
      case OpCode::LTE:
      case OpCode::GT:
      case OpCode::GTE: {
        llvm::Value* r = vstack.back(); vstack.pop_back();
        llvm::Value* l = vstack.back(); vstack.pop_back();
        const char* fname = nullptr;
        switch (instr.opcode) {
          case OpCode::EQ:  fname = "havel_vm_eq";  break;
          case OpCode::NEQ: fname = "havel_vm_neq"; break;
          case OpCode::LT:  fname = "havel_vm_lt";  break;
          case OpCode::LTE: fname = "havel_vm_lte"; break;
          case OpCode::GT:  fname = "havel_vm_gt";  break;
          case OpCode::GTE: fname = "havel_vm_gte"; break;
        }
        llvm::Function* fnComp = module.getFunction(fname);
        if (!fnComp) {
            fnComp = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i64}, false),
                llvm::Function::ExternalLinkage, fname, &module);
        }
        vstack.push_back(B.CreateCall(fnComp, {vmArg, l, r}));
        break;
      }
    // CLOSURE - operand: func_index
    case OpCode::CLOSURE: {
        uint32_t funcIndex = instr.operands[0].asInt();
        llvm::Function* fnClosure = module.getFunction("havel_vm_closure_new");
        if (!fnClosure) {
            fnClosure = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i32}, false),
                llvm::Function::ExternalLinkage, "havel_vm_closure_new", &module);
        }
        vstack.push_back(B.CreateCall(fnClosure, {vmArg, llvm::ConstantInt::get(i32, funcIndex)}));
        break;
    }
    case OpCode::DEFINE_FUNC:
        break; // No-op in JIT, function already compiled

    // Array additional operations
    case OpCode::ARRAY_DEL: {
        llvm::Value* idx = vstack.back(); vstack.pop_back();
        llvm::Value* arr = vstack.back(); vstack.pop_back();
        llvm::Function* fnDel = module.getFunction("havel_vm_array_del");
        if (!fnDel) {
            fnDel = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_array_del", &module);
        }
        vstack.push_back(B.CreateCall(fnDel, {vmArg, arr, idx}));
        break;
    }
    case OpCode::ARRAY_FREEZE: {
        llvm::Value* arr = vstack.back(); vstack.pop_back();
        llvm::Function* fnFreeze = module.getFunction("havel_vm_array_freeze");
        if (!fnFreeze) {
            fnFreeze = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_array_freeze", &module);
        }
        vstack.push_back(B.CreateCall(fnFreeze, {vmArg, arr}));
        break;
    }
    case OpCode::ARRAY_POP: {
        llvm::Value* arr = vstack.back(); vstack.pop_back();
        llvm::Function* fnPop = module.getFunction("havel_vm_array_pop");
        if (!fnPop) {
            fnPop = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_array_pop", &module);
        }
        vstack.push_back(B.CreateCall(fnPop, {vmArg, arr}));
        break;
    }
    case OpCode::ARRAY_HAS: {
        llvm::Value* val = vstack.back(); vstack.pop_back();
        llvm::Value* arr = vstack.back(); vstack.pop_back();
        llvm::Function* fnHas = module.getFunction("havel_vm_array_has");
        if (!fnHas) {
            fnHas = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_array_has", &module);
        }
        vstack.push_back(B.CreateCall(fnHas, {vmArg, arr, val}));
        break;
    }
    case OpCode::ARRAY_FIND: {
        llvm::Value* val = vstack.back(); vstack.pop_back();
        llvm::Value* arr = vstack.back(); vstack.pop_back();
        llvm::Function* fnFind = module.getFunction("havel_vm_array_find");
        if (!fnFind) {
            fnFind = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_array_find", &module);
        }
        vstack.push_back(B.CreateCall(fnFind, {vmArg, arr, val}));
        break;
    }
    case OpCode::ARRAY_MAP: {
        llvm::Value* fn = vstack.back(); vstack.pop_back();
        llvm::Value* arr = vstack.back(); vstack.pop_back();
        llvm::Function* fnMap = module.getFunction("havel_vm_array_map");
        if (!fnMap) {
            fnMap = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_array_map", &module);
        }
        vstack.push_back(B.CreateCall(fnMap, {vmArg, arr, fn}));
        break;
    }
    case OpCode::ARRAY_FILTER: {
        llvm::Value* fn = vstack.back(); vstack.pop_back();
        llvm::Value* arr = vstack.back(); vstack.pop_back();
        llvm::Function* fnFilter = module.getFunction("havel_vm_array_filter");
        if (!fnFilter) {
            fnFilter = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_array_filter", &module);
        }
        vstack.push_back(B.CreateCall(fnFilter, {vmArg, arr, fn}));
        break;
    }
    case OpCode::ARRAY_REDUCE: {
        llvm::Value* init = vstack.back(); vstack.pop_back();
        llvm::Value* fn = vstack.back(); vstack.pop_back();
        llvm::Value* arr = vstack.back(); vstack.pop_back();
        llvm::Function* fnReduce = module.getFunction("havel_vm_array_reduce");
        if (!fnReduce) {
            fnReduce = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i64, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_array_reduce", &module);
        }
        vstack.push_back(B.CreateCall(fnReduce, {vmArg, arr, fn, init}));
        break;
    }
    case OpCode::ARRAY_FOREACH: {
        llvm::Value* fn = vstack.back(); vstack.pop_back();
        llvm::Value* arr = vstack.back(); vstack.pop_back();
        llvm::Function* fnForeach = module.getFunction("havel_vm_array_foreach");
        if (!fnForeach) {
            fnForeach = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_array_foreach", &module);
        }
        vstack.push_back(B.CreateCall(fnForeach, {vmArg, arr, fn}));
        break;
    }

    // Set operations
    case OpCode::SET_NEW: {
        llvm::Function* fnNew = module.getFunction("havel_vm_set_new");
        if (!fnNew) {
            fnNew = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p}, false),
                llvm::Function::ExternalLinkage, "havel_vm_set_new", &module);
        }
        vstack.push_back(B.CreateCall(fnNew, {vmArg}));
        break;
    }
    case OpCode::SET_SET: {
        // Stack: [..., set, value, key] - pops key, value, set
        llvm::Value* key = vstack.back(); vstack.pop_back();
        llvm::Value* val = vstack.back(); vstack.pop_back();
        llvm::Value* set = vstack.back(); vstack.pop_back();
        llvm::Function* fnSet = module.getFunction("havel_vm_set_set");
        if (!fnSet) {
            fnSet = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i64, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_set_set", &module);
        }
        B.CreateCall(fnSet, {vmArg, set, val, key});
        // SET_SET does not push set back - caller manages it
        break;
    }
    case OpCode::SET_DEL: {
        // Stack: [..., set, key]
        llvm::Value* key = vstack.back(); vstack.pop_back();
        llvm::Value* set = vstack.back(); vstack.pop_back();
        llvm::Function* fnDel = module.getFunction("havel_vm_set_del");
        if (!fnDel) {
            fnDel = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_set_del", &module);
        }
        vstack.push_back(B.CreateCall(fnDel, {vmArg, set, key}));
        break;
    }

    // Range with step
    case OpCode::RANGE_STEP_NEW: {
        // Stack: [..., start, end, step]
        llvm::Value* step = vstack.back(); vstack.pop_back();
        llvm::Value* end = vstack.back(); vstack.pop_back();
        llvm::Value* start = vstack.back(); vstack.pop_back();
        llvm::Function* fnRange = module.getFunction("havel_vm_range_step_new");
        if (!fnRange) {
            fnRange = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i64, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_range_step_new", &module);
        }
        vstack.push_back(B.CreateCall(fnRange, {vmArg, start, end, step}));
        break;
    }

    // Enum operations
    case OpCode::ENUM_NEW: {
        // Operands: typeId, tag, payloadCount
    uint32_t typeId = instr.operands[0].asInt();
    uint32_t tag = instr.operands[1].asInt();
    uint32_t payloadCount = instr.operands[2].asInt();
    llvm::Function* fnEnum = module.getFunction("havel_vm_enum_new");
    if (!fnEnum) {
        fnEnum = llvm::Function::Create(
            llvm::FunctionType::get(i64, {i8p, i32, i32, i32}, false),
            llvm::Function::ExternalLinkage, "havel_vm_enum_new", &module);
    }
    vstack.push_back(B.CreateCall(fnEnum, {vmArg,
        llvm::ConstantInt::get(i32, typeId),
        llvm::ConstantInt::get(i32, tag),
        llvm::ConstantInt::get(i32, payloadCount)}));
        break;
    }
    case OpCode::ENUM_TAG: {
        llvm::Value* enumVal = vstack.back(); vstack.pop_back();
        llvm::Function* fnTag = module.getFunction("havel_vm_enum_tag");
        if (!fnTag) {
            fnTag = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_enum_tag", &module);
        }
        vstack.push_back(B.CreateCall(fnTag, {vmArg, enumVal}));
        break;
    }
    case OpCode::ENUM_PAYLOAD: {
        // Stack: [..., enum, index]
        llvm::Value* idx = vstack.back(); vstack.pop_back();
        llvm::Value* enumVal = vstack.back(); vstack.pop_back();
        llvm::Function* fnPayload = module.getFunction("havel_vm_enum_payload");
        if (!fnPayload) {
            fnPayload = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i32}, false),
                llvm::Function::ExternalLinkage, "havel_vm_enum_payload", &module);
        }
        // Bridge takes (vm, enum_bits, uint32_t idx) but idx is on stack as i64
        // Need to truncate i64 to i32
        vstack.push_back(B.CreateCall(fnPayload, {vmArg, enumVal, B.CreateTrunc(idx, i32, "enum_idx")}));
        break;
    }
    case OpCode::ENUM_MATCH: {
        // Stack: [..., enum, expectedTag] - compare enum tag with expected
        // ENUM_MATCH pops both and pushes bool
        llvm::Value* tag = vstack.back(); vstack.pop_back();
        llvm::Value* enumVal = vstack.back(); vstack.pop_back();
        // Use enum_tag bridge then compare
        llvm::Function* fnTag = module.getFunction("havel_vm_enum_tag");
        if (!fnTag) {
            fnTag = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_enum_tag", &module);
        }
        llvm::Value* actualTag = B.CreateCall(fnTag, {vmArg, enumVal});
        // Both are i64, compare with EQ semantics (both are NaN-boxed ints)
        // For simplicity, use the eq bridge
        llvm::Function* fnEq = module.getFunction("havel_vm_eq");
        if (!fnEq) {
            fnEq = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i64, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_eq", &module);
        }
        vstack.push_back(B.CreateCall(fnEq, {actualTag, tag}));
        break;
    }

    // Object additional operations
    case OpCode::OBJECT_NEW_UNSORTED: {
        llvm::Function* fnNew = module.getFunction("havel_vm_object_new_unsorted");
        if (!fnNew) {
            fnNew = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p}, false),
                llvm::Function::ExternalLinkage, "havel_vm_object_new_unsorted", &module);
        }
        vstack.push_back(B.CreateCall(fnNew, {vmArg}));
        break;
    }
    case OpCode::OBJECT_KEYS: {
        llvm::Value* obj = vstack.back(); vstack.pop_back();
        llvm::Function* fnKeys = module.getFunction("havel_vm_object_keys");
        if (!fnKeys) {
            fnKeys = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_object_keys", &module);
        }
        vstack.push_back(B.CreateCall(fnKeys, {vmArg, obj}));
        break;
    }
    case OpCode::OBJECT_VALUES: {
        llvm::Value* obj = vstack.back(); vstack.pop_back();
        llvm::Function* fnVals = module.getFunction("havel_vm_object_values");
        if (!fnVals) {
            fnVals = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_object_values", &module);
        }
        vstack.push_back(B.CreateCall(fnVals, {vmArg, obj}));
        break;
    }
    case OpCode::OBJECT_ENTRIES: {
        llvm::Value* obj = vstack.back(); vstack.pop_back();
        llvm::Function* fnEntries = module.getFunction("havel_vm_object_entries");
        if (!fnEntries) {
            fnEntries = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_object_entries", &module);
        }
        vstack.push_back(B.CreateCall(fnEntries, {vmArg, obj}));
        break;
    }
    case OpCode::OBJECT_HAS: {
        // Uses operand for key_id (same as OBJECT_GET)
        uint32_t keyId = instr.operands[0].asInt();
        llvm::Value* obj = vstack.back(); vstack.pop_back();
        llvm::Function* fnHas = module.getFunction("havel_vm_object_has");
        if (!fnHas) {
            fnHas = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i32}, false),
                llvm::Function::ExternalLinkage, "havel_vm_object_has", &module);
        }
        vstack.push_back(B.CreateCall(fnHas, {vmArg, obj, llvm::ConstantInt::get(i32, keyId)}));
        break;
    }
    case OpCode::OBJECT_DELETE: {
        uint32_t keyId = instr.operands[0].asInt();
        llvm::Value* obj = vstack.back(); vstack.pop_back();
        llvm::Function* fnDel = module.getFunction("havel_vm_object_delete");
        if (!fnDel) {
            fnDel = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i32}, false),
                llvm::Function::ExternalLinkage, "havel_vm_object_delete", &module);
        }
        vstack.push_back(B.CreateCall(fnDel, {vmArg, obj, llvm::ConstantInt::get(i32, keyId)}));
        break;
    }
        case OpCode::OBJECT_GET_RAW: {
            llvm::Value* key = vstack.back(); vstack.pop_back();
            llvm::Value* obj = vstack.back(); vstack.pop_back();
            llvm::Function* fnGetRaw = module.getFunction("havel_vm_object_get_raw");
            if (!fnGetRaw) {
                fnGetRaw = llvm::Function::Create(
                    llvm::FunctionType::get(i64, {i8p, i64, i64}, false),
                    llvm::Function::ExternalLinkage, "havel_vm_object_get_raw", &module);
            }
            vstack.push_back(B.CreateCall(fnGetRaw, {vmArg, obj, key}));
            break;
        }

    // String additional operations
    case OpCode::STRING_UPPER: {
        llvm::Value* str = vstack.back(); vstack.pop_back();
        llvm::Function* fnUpper = module.getFunction("havel_vm_string_upper");
        if (!fnUpper) {
            fnUpper = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_string_upper", &module);
        }
        vstack.push_back(B.CreateCall(fnUpper, {vmArg, str}));
        break;
    }
    case OpCode::STRING_LOWER: {
        llvm::Value* str = vstack.back(); vstack.pop_back();
        llvm::Function* fnLower = module.getFunction("havel_vm_string_lower");
        if (!fnLower) {
            fnLower = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_string_lower", &module);
        }
        vstack.push_back(B.CreateCall(fnLower, {vmArg, str}));
        break;
    }
    case OpCode::STRING_TRIM: {
        llvm::Value* str = vstack.back(); vstack.pop_back();
        llvm::Function* fnTrim = module.getFunction("havel_vm_string_trim");
        if (!fnTrim) {
            fnTrim = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_string_trim", &module);
        }
        vstack.push_back(B.CreateCall(fnTrim, {vmArg, str}));
        break;
    }
    case OpCode::STRING_SUB: {
        // Stack: [..., str, start, len]
        llvm::Value* len = vstack.back(); vstack.pop_back();
        llvm::Value* start = vstack.back(); vstack.pop_back();
        llvm::Value* str = vstack.back(); vstack.pop_back();
        llvm::Function* fnSub = module.getFunction("havel_vm_string_sub");
        if (!fnSub) {
            fnSub = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i64, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_string_sub", &module);
        }
        vstack.push_back(B.CreateCall(fnSub, {vmArg, str, start, len}));
        break;
    }
    case OpCode::STRING_FIND: {
        // Stack: [..., str, substr]
        llvm::Value* sub = vstack.back(); vstack.pop_back();
        llvm::Value* str = vstack.back(); vstack.pop_back();
        llvm::Function* fnFind = module.getFunction("havel_vm_string_find");
        if (!fnFind) {
            fnFind = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_string_find", &module);
        }
        vstack.push_back(B.CreateCall(fnFind, {vmArg, str, sub}));
        break;
    }
    case OpCode::STRING_HAS: {
        llvm::Value* sub = vstack.back(); vstack.pop_back();
        llvm::Value* str = vstack.back(); vstack.pop_back();
        llvm::Function* fnHas = module.getFunction("havel_vm_string_has");
        if (!fnHas) {
            fnHas = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_string_has", &module);
        }
        vstack.push_back(B.CreateCall(fnHas, {vmArg, str, sub}));
        break;
    }
    case OpCode::STRING_STARTS: {
        llvm::Value* prefix = vstack.back(); vstack.pop_back();
        llvm::Value* str = vstack.back(); vstack.pop_back();
        llvm::Function* fnStarts = module.getFunction("havel_vm_string_starts");
        if (!fnStarts) {
            fnStarts = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_string_starts", &module);
        }
        vstack.push_back(B.CreateCall(fnStarts, {vmArg, str, prefix}));
        break;
    }
    case OpCode::STRING_ENDS: {
        llvm::Value* suffix = vstack.back(); vstack.pop_back();
        llvm::Value* str = vstack.back(); vstack.pop_back();
        llvm::Function* fnEnds = module.getFunction("havel_vm_string_ends");
        if (!fnEnds) {
            fnEnds = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_string_ends", &module);
        }
        vstack.push_back(B.CreateCall(fnEnds, {vmArg, str, suffix}));
        break;
    }
    case OpCode::STRING_SPLIT: {
        // Stack: [..., str, delimiter]
        llvm::Value* delim = vstack.back(); vstack.pop_back();
        llvm::Value* str = vstack.back(); vstack.pop_back();
        llvm::Function* fnSplit = module.getFunction("havel_vm_string_split");
        if (!fnSplit) {
            fnSplit = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_string_split", &module);
        }
        vstack.push_back(B.CreateCall(fnSplit, {vmArg, str, delim}));
        break;
    }
    case OpCode::STRING_REPLACE: {
        // Stack: [..., str, old, new]
        llvm::Value* newVal = vstack.back(); vstack.pop_back();
        llvm::Value* oldVal = vstack.back(); vstack.pop_back();
        llvm::Value* str = vstack.back(); vstack.pop_back();
        llvm::Function* fnReplace = module.getFunction("havel_vm_string_replace");
        if (!fnReplace) {
            fnReplace = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i64, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_string_replace", &module);
        }
        vstack.push_back(B.CreateCall(fnReplace, {vmArg, str, oldVal, newVal}));
        break;
    }
    case OpCode::STRING_PROMOTE: {
        llvm::Value* str = vstack.back(); vstack.pop_back();
        llvm::Function* fnPromote = module.getFunction("havel_vm_string_promote");
        if (!fnPromote) {
            fnPromote = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_string_promote", &module);
        }
        vstack.push_back(B.CreateCall(fnPromote, {vmArg, str}));
        break;
    }

    // Spread - pushes multiple values onto VM stack, returns count
    case OpCode::SPREAD: {
        llvm::Value* val = vstack.back(); vstack.pop_back();
        llvm::Function* fnSpread = module.getFunction("havel_vm_spread");
        if (!fnSpread) {
            fnSpread = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_spread", &module);
        }
        // spread pushes elements onto VM stack directly, doesn't return a value to vstack
        B.CreateCall(fnSpread, {vmArg, val});
        break;
    }
    case OpCode::SPREAD_CALL: {
        // Same as SPREAD
        llvm::Value* val = vstack.back(); vstack.pop_back();
        llvm::Function* fnSpread = module.getFunction("havel_vm_spread");
        if (!fnSpread) {
            fnSpread = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_spread", &module);
        }
        B.CreateCall(fnSpread, {vmArg, val});
        break;
    }

    // Type conversions
    case OpCode::TO_INT: {
        llvm::Value* val = vstack.back(); vstack.pop_back();
        llvm::Function* fnToInt = module.getFunction("havel_vm_to_int");
        if (!fnToInt) {
            fnToInt = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_to_int", &module);
        }
        vstack.push_back(B.CreateCall(fnToInt, {vmArg, val}));
        break;
    }
    case OpCode::TO_FLOAT: {
        llvm::Value* val = vstack.back(); vstack.pop_back();
        llvm::Function* fnToFloat = module.getFunction("havel_vm_to_float");
        if (!fnToFloat) {
            fnToFloat = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_to_float", &module);
        }
        vstack.push_back(B.CreateCall(fnToFloat, {vmArg, val}));
        break;
    }
    case OpCode::TO_STRING: {
        llvm::Value* val = vstack.back(); vstack.pop_back();
        llvm::Function* fnToStr = module.getFunction("havel_vm_to_string");
        if (!fnToStr) {
            fnToStr = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_to_string", &module);
        }
        vstack.push_back(B.CreateCall(fnToStr, {vmArg, val}));
        break;
    }
    case OpCode::TO_BOOL: {
        llvm::Value* val = vstack.back(); vstack.pop_back();
        llvm::Function* fnToBool = module.getFunction("havel_vm_to_bool");
        if (!fnToBool) {
            fnToBool = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_to_bool", &module);
        }
        vstack.push_back(B.CreateCall(fnToBool, {vmArg, val}));
        break;
    }
    case OpCode::TYPE_OF: {
        llvm::Value* val = vstack.back(); vstack.pop_back();
        llvm::Function* fnTypeOf = module.getFunction("havel_vm_type_of");
        if (!fnTypeOf) {
            fnTypeOf = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_type_of", &module);
        }
        vstack.push_back(B.CreateCall(fnTypeOf, {vmArg, val}));
        break;
    }
    case OpCode::AS_TYPE: {
        // Operand: type_name_id (string val id)
        uint32_t typeNameId = instr.operands[0].asStringValId();
        llvm::Value* val = vstack.back(); vstack.pop_back();
        llvm::Function* fnAsType = module.getFunction("havel_vm_as_type");
        if (!fnAsType) {
            fnAsType = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i32}, false),
                llvm::Function::ExternalLinkage, "havel_vm_as_type", &module);
        }
        vstack.push_back(B.CreateCall(fnAsType, {vmArg, val, llvm::ConstantInt::get(i32, typeNameId)}));
        break;
    }

    // Special operations
    case OpCode::PRINT: {
        llvm::Value* val = vstack.back(); vstack.pop_back();
        llvm::Function* fnPrint = module.getFunction("havel_vm_print");
        if (!fnPrint) {
            fnPrint = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_print", &module);
        }
        B.CreateCall(fnPrint, {vmArg, val});
        break;
    }
    case OpCode::DEBUG: {
        llvm::Function* fnDebug = module.getFunction("havel_vm_debug");
        if (!fnDebug) {
            fnDebug = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p}, false),
                llvm::Function::ExternalLinkage, "havel_vm_debug", &module);
        }
        B.CreateCall(fnDebug, {vmArg});
        break;
    }
    case OpCode::IMPORT: {
        llvm::Value* path = vstack.back(); vstack.pop_back();
        llvm::Function* fnImport = module.getFunction("havel_vm_import");
        if (!fnImport) {
            fnImport = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_import", &module);
        }
        vstack.push_back(B.CreateCall(fnImport, {vmArg, path}));
        break;
    }

    // Class operations
    case OpCode::STRUCT_NEW: {
        // Operands: type_name_id, arg_count
        uint32_t typeId = instr.operands[0].asStringValId();
        uint32_t argCount = instr.operands[1].asInt();
        llvm::Value* argsArray = B.CreateAlloca(llvm::ArrayType::get(i64, argCount), nullptr, "struct_args");
        for (uint32_t i = 0; i < argCount; ++i) {
            llvm::Value* arg = vstack.back(); vstack.pop_back();
            B.CreateStore(arg, B.CreateInBoundsGEP(llvm::ArrayType::get(i64, argCount), argsArray,
                {llvm::ConstantInt::get(i32, 0), llvm::ConstantInt::get(i32, argCount - 1 - i)}));
        }
        llvm::Function* fnStructNew = module.getFunction("havel_vm_struct_new");
        if (!fnStructNew) {
            fnStructNew = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i32, i64p, i32}, false),
                llvm::Function::ExternalLinkage, "havel_vm_struct_new", &module);
        }
        vstack.push_back(B.CreateCall(fnStructNew, {vmArg,
            llvm::ConstantInt::get(i32, typeId),
            B.CreateInBoundsGEP(llvm::ArrayType::get(i64, argCount), argsArray,
                {llvm::ConstantInt::get(i32, 0), llvm::ConstantInt::get(i32, 0)}),
            llvm::ConstantInt::get(i32, argCount)}));
        break;
    }
    case OpCode::STRUCT_GET: {
        uint32_t fieldId = instr.operands[0].asStringValId();
        llvm::Value* obj = vstack.back(); vstack.pop_back();
        llvm::Function* fnStructGet = module.getFunction("havel_vm_struct_get");
        if (!fnStructGet) {
            fnStructGet = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i32}, false),
                llvm::Function::ExternalLinkage, "havel_vm_struct_get", &module);
        }
        vstack.push_back(B.CreateCall(fnStructGet, {vmArg, obj, llvm::ConstantInt::get(i32, fieldId)}));
        break;
    }
    case OpCode::STRUCT_SET: {
        uint32_t fieldId = instr.operands[0].asStringValId();
        llvm::Value* val = vstack.back(); vstack.pop_back();
        llvm::Value* obj = vstack.back(); vstack.pop_back();
        llvm::Function* fnStructSet = module.getFunction("havel_vm_struct_set");
        if (!fnStructSet) {
            fnStructSet = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i32, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_struct_set", &module);
        }
        vstack.push_back(B.CreateCall(fnStructSet, {vmArg, obj, llvm::ConstantInt::get(i32, fieldId), val}));
        break;
    }
    case OpCode::PROT_CHECK: {
        uint32_t protoId = instr.operands[0].asStringValId();
        llvm::Value* val = vstack.back(); vstack.pop_back();
        llvm::Function* fnProtCheck = module.getFunction("havel_vm_prot_check");
        if (!fnProtCheck) {
            fnProtCheck = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i32}, false),
                llvm::Function::ExternalLinkage, "havel_vm_prot_check", &module);
        }
        vstack.push_back(B.CreateCall(fnProtCheck, {vmArg, val, llvm::ConstantInt::get(i32, protoId)}));
        break;
    }
    case OpCode::PROT_CAST: {
        uint32_t protoId = instr.operands[0].asStringValId();
        llvm::Value* val = vstack.back(); vstack.pop_back();
        llvm::Function* fnProtCast = module.getFunction("havel_vm_prot_cast");
        if (!fnProtCast) {
            fnProtCast = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i32}, false),
                llvm::Function::ExternalLinkage, "havel_vm_prot_cast", &module);
        }
        vstack.push_back(B.CreateCall(fnProtCast, {vmArg, val, llvm::ConstantInt::get(i32, protoId)}));
        break;
    }
    case OpCode::CLASS_NEW: {
        // Operands: typeId, parentTypeId, fieldCount
        uint32_t typeId = instr.operands[0].asInt();
        uint32_t parentTypeId = instr.operands[1].asInt();
        uint32_t fieldCount = instr.operands[2].asInt();
        llvm::Function* fnClassNew = module.getFunction("havel_vm_class_new");
        if (!fnClassNew) {
            fnClassNew = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i32, i32, i32}, false),
                llvm::Function::ExternalLinkage, "havel_vm_class_new", &module);
        }
        vstack.push_back(B.CreateCall(fnClassNew, {vmArg,
            llvm::ConstantInt::get(i32, typeId),
            llvm::ConstantInt::get(i32, parentTypeId),
            llvm::ConstantInt::get(i32, fieldCount)}));
        break;
    }
    case OpCode::CLASS_GET_FIELD: {
        // Operand: field_id; Stack: [object]
        uint32_t fieldId = instr.operands[0].asInt();
        llvm::Value* obj = vstack.back(); vstack.pop_back();
        llvm::Function* fnGetField = module.getFunction("havel_vm_class_get_field");
        if (!fnGetField) {
            fnGetField = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i32}, false),
                llvm::Function::ExternalLinkage, "havel_vm_class_get_field", &module);
        }
        vstack.push_back(B.CreateCall(fnGetField, {vmArg, obj, llvm::ConstantInt::get(i32, fieldId)}));
        break;
    }
    case OpCode::CLASS_SET_FIELD: {
        // Operand: field_id; Stack: [object, value]
        uint32_t fieldId = instr.operands[0].asInt();
        llvm::Value* val = vstack.back(); vstack.pop_back();
        llvm::Value* obj = vstack.back(); vstack.pop_back();
        llvm::Function* fnSetField = module.getFunction("havel_vm_class_set_field");
        if (!fnSetField) {
            fnSetField = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i32, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_class_set_field", &module);
        }
        vstack.push_back(B.CreateCall(fnSetField, {vmArg, obj, llvm::ConstantInt::get(i32, fieldId), val}));
        break;
    }
    case OpCode::LOAD_CLASS_PROTO: {
        // Operand: type_id
        uint32_t typeId = instr.operands[0].asInt();
        llvm::Function* fnLoadProto = module.getFunction("havel_vm_load_class_proto");
        if (!fnLoadProto) {
            fnLoadProto = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i32}, false),
                llvm::Function::ExternalLinkage, "havel_vm_load_class_proto", &module);
        }
        vstack.push_back(B.CreateCall(fnLoadProto, {vmArg, llvm::ConstantInt::get(i32, typeId)}));
        break;
    }
    case OpCode::CALL_SUPER: {
        // Operands: method_name_id, arg_count
        uint32_t methodNameId = instr.operands[0].asStringValId();
        uint32_t argCount = instr.operands[1].asInt();
        llvm::Value* argsArray = B.CreateAlloca(llvm::ArrayType::get(i64, argCount), nullptr, "super_args");
        for (uint32_t i = 0; i < argCount; ++i) {
            llvm::Value* arg = vstack.back(); vstack.pop_back();
            B.CreateStore(arg, B.CreateInBoundsGEP(llvm::ArrayType::get(i64, argCount), argsArray,
                {llvm::ConstantInt::get(i32, 0), llvm::ConstantInt::get(i32, argCount - 1 - i)}));
        }
        llvm::Value* obj = vstack.back(); vstack.pop_back();
        llvm::Function* fnSuper = module.getFunction("havel_vm_call_super");
        if (!fnSuper) {
            fnSuper = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i32, i64p, i32}, false),
                llvm::Function::ExternalLinkage, "havel_vm_call_super", &module);
        }
        vstack.push_back(B.CreateCall(fnSuper, {vmArg, obj,
            llvm::ConstantInt::get(i32, methodNameId),
            B.CreateInBoundsGEP(llvm::ArrayType::get(i64, argCount), argsArray,
                {llvm::ConstantInt::get(i32, 0), llvm::ConstantInt::get(i32, 0)}),
            llvm::ConstantInt::get(i32, argCount)}));
        break;
    }

    // Coroutine operations
    case OpCode::YIELD_RESUME: {
        llvm::Value* co = vstack.back(); vstack.pop_back();
        llvm::Function* fnResume = module.getFunction("havel_vm_yield_resume");
        if (!fnResume) {
            fnResume = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_yield_resume", &module);
        }
        vstack.push_back(B.CreateCall(fnResume, {vmArg, co}));
        break;
    }
    case OpCode::GO_ASYNC: {
        llvm::Value* fn = vstack.back(); vstack.pop_back();
        llvm::Function* fnGoAsync = module.getFunction("havel_vm_go_async");
        if (!fnGoAsync) {
            fnGoAsync = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_go_async", &module);
        }
        vstack.push_back(B.CreateCall(fnGoAsync, {vmArg, fn}));
        break;
    }

  case OpCode::BEGIN_MODULE: {
        llvm::Function* fnBegin = module.getFunction("havel_vm_begin_module");
        if (!fnBegin) {
            fnBegin = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p}, false),
                llvm::Function::ExternalLinkage, "havel_vm_begin_module", &module);
        }
        B.CreateCall(fnBegin, {vmArg});
        break;
    }
    case OpCode::END_MODULE: {
        llvm::Function* fnEnd = module.getFunction("havel_vm_end_module");
        if (!fnEnd) {
            fnEnd = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p}, false),
                llvm::Function::ExternalLinkage, "havel_vm_end_module", &module);
        }
        vstack.push_back(B.CreateCall(fnEnd, {vmArg}));
        break;
    }

    case OpCode::IS: {
        llvm::Value* r = vstack.back(); vstack.pop_back();
        llvm::Value* l = vstack.back(); vstack.pop_back();
        llvm::Function* fnIs = module.getFunction("havel_vm_is");
        if (!fnIs) {
          fnIs = llvm::Function::Create(
            llvm::FunctionType::get(i64, {i64, i64}, false),
            llvm::Function::ExternalLinkage, "havel_vm_is", &module);
        }
        vstack.push_back(B.CreateCall(fnIs, {l, r}));
        break;
    }
    case OpCode::IS_NULL: {
        llvm::Value* v = vstack.back(); vstack.pop_back();
        llvm::Value* isNull = B.CreateICmpEQ(v, makeNull());
        vstack.push_back(boxInt(B.CreateZExt(isNull, i64)));
        break;
    }
    case OpCode::NOT: {
        llvm::Value* v = vstack.back(); vstack.pop_back();
        llvm::Function* fnNot = module.getFunction("havel_vm_not");
        if (!fnNot) {
          fnNot = llvm::Function::Create(
            llvm::FunctionType::get(i64, {i64}, false),
            llvm::Function::ExternalLinkage, "havel_vm_not", &module);
        }
        vstack.push_back(B.CreateCall(fnNot, {v}));
        break;
    }

    // Control flow
    case OpCode::JUMP: {
        size_t target = instr.operands[0].asInt();
        if (target < ip) {
            llvm::Function* fnBe = module.getFunction("havel_vm_backedge");
            if (!fnBe) {
                fnBe = llvm::Function::Create(
                    llvm::FunctionType::get(llvm::Type::getVoidTy(ctx), {i8p, i32}, false),
                    llvm::Function::ExternalLinkage, "havel_vm_backedge", &module);
            }
            B.CreateCall(fnBe, {vmArg, llvm::ConstantInt::get(i32, static_cast<uint32_t>(ip))});
        }
        if (target < basicBlocks.size()) {
            B.CreateBr(basicBlocks[target]);
        } else {
            B.CreateBr(basicBlocks[ip + 1]);
        }
        break;
    }
    case OpCode::JUMP_IF_FALSE: {
        size_t target = instr.operands[1].asInt();
        llvm::Value* cond = vstack.back(); vstack.pop_back();
        llvm::Function* fnTruthy = module.getFunction("havel_vm_is_truthy");
        if (!fnTruthy) {
            fnTruthy = llvm::Function::Create(
                llvm::FunctionType::get(i32, {i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_is_truthy", &module);
        }
        llvm::Value* truthyResult = B.CreateCall(fnTruthy, {cond});
        llvm::Value* isFalsy = B.CreateICmpEQ(truthyResult, llvm::ConstantInt::get(i32, 0));
        if (target < ip) {
            llvm::Function* fnBe = module.getFunction("havel_vm_backedge");
            if (!fnBe) {
                fnBe = llvm::Function::Create(
                    llvm::FunctionType::get(llvm::Type::getVoidTy(ctx), {i8p, i32}, false),
                    llvm::Function::ExternalLinkage, "havel_vm_backedge", &module);
            }
            B.CreateCall(fnBe, {vmArg, llvm::ConstantInt::get(i32, static_cast<uint32_t>(ip))});
        }
        if (target < basicBlocks.size()) {
            B.CreateCondBr(isFalsy, basicBlocks[target], basicBlocks[ip + 1]);
        } else {
            B.CreateBr(basicBlocks[ip + 1]);
        }
        break;
    }
    case OpCode::JUMP_IF_TRUE: {
        size_t target = instr.operands[1].asInt();
        llvm::Value* cond = vstack.back(); vstack.pop_back();
        llvm::Function* fnTruthy = module.getFunction("havel_vm_is_truthy");
        if (!fnTruthy) {
            fnTruthy = llvm::Function::Create(
                llvm::FunctionType::get(i32, {i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_is_truthy", &module);
        }
        llvm::Value* truthyResult = B.CreateCall(fnTruthy, {cond});
        llvm::Value* isTruthy = B.CreateICmpNE(truthyResult, llvm::ConstantInt::get(i32, 0));
        if (target < ip) {
            llvm::Function* fnBe = module.getFunction("havel_vm_backedge");
            if (!fnBe) {
                fnBe = llvm::Function::Create(
                    llvm::FunctionType::get(llvm::Type::getVoidTy(ctx), {i8p, i32}, false),
                    llvm::Function::ExternalLinkage, "havel_vm_backedge", &module);
            }
            B.CreateCall(fnBe, {vmArg, llvm::ConstantInt::get(i32, static_cast<uint32_t>(ip))});
        }
        if (target < basicBlocks.size()) {
            B.CreateCondBr(isTruthy, basicBlocks[target], basicBlocks[ip + 1]);
        } else {
            B.CreateBr(basicBlocks[ip + 1]);
        }
        break;
    }
    case OpCode::JUMP_IF_NULL: {
        size_t target = instr.operands[1].asInt();
        llvm::Value* v = vstack.back();
        llvm::Value* isNull = B.CreateICmpEQ(v, makeNull());
        vstack.pop_back(); // Coalesce consumes the value
        if (target < basicBlocks.size()) {
            B.CreateCondBr(isNull, basicBlocks[target], basicBlocks[ip + 1]);
        } else {
            B.CreateBr(basicBlocks[ip + 1]);
        }
        break;
    }

    // Function calls
    case OpCode::CALL: {
        uint32_t argCount = instr.operands[0].asInt();
        // Collect args from stack (in reverse order for calling convention)
        // Stack layout: [callee, arg0, arg1, ..., argN]
        // havel_vm_call expects: args[0] = callee, args[1..N] = actual args
        std::vector<llvm::Value*> args;
        args.push_back(vmArg);
        // Create args array on stack (argCount + 1 for callee)
        llvm::Value* argsArray = B.CreateAlloca(llvm::ArrayType::get(i64, argCount + 1), nullptr, "call_args");
        // Pop args in reverse order (argN, argN-1, ..., arg0)
        for (uint32_t i = 0; i < argCount; ++i) {
            llvm::Value* arg = vstack.back(); vstack.pop_back();
            B.CreateStore(arg, B.CreateInBoundsGEP(llvm::ArrayType::get(i64, argCount + 1), argsArray,
                {llvm::ConstantInt::get(i32, 0), llvm::ConstantInt::get(i32, argCount - i)}));
        }
        // Pop callee and store at index 0
        llvm::Value* callee = vstack.back(); vstack.pop_back();
        B.CreateStore(callee, B.CreateInBoundsGEP(llvm::ArrayType::get(i64, argCount + 1), argsArray,
            {llvm::ConstantInt::get(i32, 0), llvm::ConstantInt::get(i32, 0)}));
        args.push_back(B.CreateInBoundsGEP(llvm::ArrayType::get(i64, argCount + 1), argsArray,
            {llvm::ConstantInt::get(i32, 0), llvm::ConstantInt::get(i32, 0)}));
        args.push_back(llvm::ConstantInt::get(i32, argCount + 1)); // +1 for callee

        // Call havel_vm_call(vm, args, count)
        llvm::Function* fnCall = module.getFunction("havel_vm_call");
        if (!fnCall) {
            fnCall = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64p, i32}, false),
                llvm::Function::ExternalLinkage, "havel_vm_call", &module);
        }
        vstack.push_back(B.CreateCall(fnCall, args));
        break;
    }
    case OpCode::TAIL_CALL: {
        uint32_t argCount = instr.operands[0].asInt();
        // Collect args from stack
        std::vector<llvm::Value*> args;
        args.push_back(vmArg);
        llvm::Value* argsArray = B.CreateAlloca(llvm::ArrayType::get(i64, argCount), nullptr, "tail_args");
        for (uint32_t i = 0; i < argCount; ++i) {
            llvm::Value* arg = vstack.back(); vstack.pop_back();
            B.CreateStore(arg, B.CreateInBoundsGEP(llvm::ArrayType::get(i64, argCount), argsArray,
                {llvm::ConstantInt::get(i32, 0), llvm::ConstantInt::get(i32, argCount - 1 - i)}));
        }
        args.push_back(B.CreateInBoundsGEP(llvm::ArrayType::get(i64, argCount), argsArray,
            {llvm::ConstantInt::get(i32, 0), llvm::ConstantInt::get(i32, 0)}));
        args.push_back(llvm::ConstantInt::get(i32, argCount));

        // Unregister GC roots before tail call
        llvm::Function *fn_unreg = module.getFunction("havel_gc_unregister_roots");
        if (!fn_unreg) fn_unreg = llvm::Function::Create(llvm::FunctionType::get(voidT, {llvm::PointerType::getUnqual(frameType)}, false), llvm::Function::ExternalLinkage, "havel_gc_unregister_roots", &module);
        B.CreateCall(fn_unreg, {frame});

        // Call havel_vm_tail_call which handles frame reuse
        llvm::Function* fnTailCall = module.getFunction("havel_vm_tail_call");
        if (!fnTailCall) {
            fnTailCall = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64p, i32}, false),
                llvm::Function::ExternalLinkage, "havel_vm_tail_call", &module);
        }
        // Musttail call for proper tail call optimization
        llvm::CallInst* call = B.CreateCall(fnTailCall, args);
        call->setTailCallKind(llvm::CallInst::TCK_MustTail);
        B.CreateRet(call);
        break;
    }

    case OpCode::RETURN: {
        // Close open upvalues for this frame before returning
        llvm::Function* fnClose = module.getFunction("havel_vm_close_upvalues");
        if (!fnClose) {
            fnClose = llvm::Function::Create(
                llvm::FunctionType::get(voidT, {i8p, i32}, false),
                llvm::Function::ExternalLinkage, "havel_vm_close_upvalues", &module);
        }
        llvm::Function* fnLocalsBase = module.getFunction("havel_vm_locals_base");
        if (!fnLocalsBase) {
            fnLocalsBase = llvm::Function::Create(
                llvm::FunctionType::get(i32, {i8p}, false),
                llvm::Function::ExternalLinkage, "havel_vm_locals_base", &module);
        }
        llvm::Value* lb = B.CreateCall(fnLocalsBase, {vmArg});
        B.CreateCall(fnClose, {vmArg, lb});
        llvm::Function *fn_unreg = module.getFunction("havel_gc_unregister_roots");
        if (!fn_unreg) fn_unreg = llvm::Function::Create(llvm::FunctionType::get(voidT, {llvm::PointerType::getUnqual(frameType)}, false), llvm::Function::ExternalLinkage, "havel_gc_unregister_roots", &module);
        B.CreateCall(fn_unreg, {frame});
        B.CreateRet(vstack.empty() ? makeNull() : vstack.back());
        break;
    }
    case OpCode::TRY_ENTER: {
        uint32_t catchIp = instr.operands[0].asInt();
        uint32_t finallyIp = (instr.operands.size() >= 2 && instr.operands[1].isInt()) ? instr.operands[1].asInt() : 0;
        llvm::Function* fnTryEnter = module.getFunction("havel_vm_try_enter");
        if (!fnTryEnter) {
            fnTryEnter = llvm::Function::Create(
                llvm::FunctionType::get(voidT, {llvm::PointerType::getUnqual(frameType), i32, i32, i32}, false),
                llvm::Function::ExternalLinkage, "havel_vm_try_enter", &module);
        }
        B.CreateCall(fnTryEnter, {frame,
                                  llvm::ConstantInt::get(i32, catchIp),
                                  llvm::ConstantInt::get(i32, finallyIp),
                                  llvm::ConstantInt::get(i32, static_cast<uint32_t>(vstack.size()))});
        jit_try_stack_depths.push_back(vstack.size());
        break;
    }
    case OpCode::TRY_EXIT: {
        llvm::Function* fnTryExit = module.getFunction("havel_vm_try_exit");
        if (!fnTryExit) {
            fnTryExit = llvm::Function::Create(
                llvm::FunctionType::get(voidT, {llvm::PointerType::getUnqual(frameType)}, false),
                llvm::Function::ExternalLinkage, "havel_vm_try_exit", &module);
        }
        B.CreateCall(fnTryExit, {frame});
        if (!jit_try_stack_depths.empty()) {
            jit_try_stack_depths.pop_back();
        }
        break;
    }
    case OpCode::LOAD_EXCEPTION: {
        llvm::Function* fnLoadExc = module.getFunction("havel_vm_load_exception");
        if (!fnLoadExc) {
            fnLoadExc = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p}, false),
                llvm::Function::ExternalLinkage, "havel_vm_load_exception", &module);
        }
        vstack.push_back(B.CreateCall(fnLoadExc, {vmArg}));
        break;
    }
    case OpCode::THROW: {
        llvm::Value* thrown = vstack.empty() ? makeNull() : vstack.back();
        if (!vstack.empty()) {
            vstack.pop_back();
        }
        llvm::Function* fnSetExc = module.getFunction("havel_vm_set_exception");
        if (!fnSetExc) {
            fnSetExc = llvm::Function::Create(
                llvm::FunctionType::get(voidT, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_set_exception", &module);
        }
        B.CreateCall(fnSetExc, {vmArg, thrown});

        llvm::Value* catchDepthAlloca = B.CreateAlloca(i32, nullptr, "catch_depth");
        llvm::Value* poppedCountAlloca = B.CreateAlloca(i32, nullptr, "popped_count");
        B.CreateStore(llvm::ConstantInt::get(i32, 0), catchDepthAlloca);
        B.CreateStore(llvm::ConstantInt::get(i32, 0), poppedCountAlloca);

        llvm::Function* fnFindHandler = module.getFunction("havel_vm_try_find_throw_target");
        if (!fnFindHandler) {
            fnFindHandler = llvm::Function::Create(
                llvm::FunctionType::get(
                    i32,
                    {llvm::PointerType::getUnqual(frameType),
                     llvm::PointerType::getUnqual(i32),
                     llvm::PointerType::getUnqual(i32)},
                    false),
                llvm::Function::ExternalLinkage, "havel_vm_try_find_throw_target", &module);
        }
        llvm::Value* catchIp =
            B.CreateCall(fnFindHandler, {frame, catchDepthAlloca, poppedCountAlloca});
        llvm::Value* hasHandler =
            B.CreateICmpNE(catchIp, llvm::ConstantInt::get(i32, UINT32_MAX));

        llvm::BasicBlock* throwDispatchBB = llvm::BasicBlock::Create(ctx, "throw_dispatch", f);
        llvm::BasicBlock* throwUnwindBB = llvm::BasicBlock::Create(ctx, "throw_unwind", f);
        B.CreateCondBr(hasHandler, throwDispatchBB, throwUnwindBB);

        B.SetInsertPoint(throwDispatchBB);
        // Conservative stack-state reset for catch entry. This avoids
        // reusing stale SSA values after handler-walk pops.
        vstack.clear();
        jit_try_stack_depths.clear();
        llvm::SwitchInst* sw = B.CreateSwitch(catchIp, throwUnwindBB, basicBlocks.size());
        for (size_t target = 0; target < basicBlocks.size(); ++target) {
            auto* caseVal = llvm::cast<llvm::ConstantInt>(
                llvm::ConstantInt::get(i32, static_cast<uint32_t>(target)));
            sw->addCase(caseVal, basicBlocks[target]);
        }

        B.SetInsertPoint(throwUnwindBB);
        llvm::Function* fnThrow = module.getFunction("havel_vm_throw_from_jit");
        if (!fnThrow) {
            fnThrow = llvm::Function::Create(
                llvm::FunctionType::get(voidT, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_throw_from_jit", &module);
        }
        B.CreateCall(fnThrow, {vmArg, thrown});
        B.CreateUnreachable();
        break;
    }

    // Global and upvalue access - critical for closures
    case OpCode::LOAD_GLOBAL: {
        uint32_t nameId = instr.operands[0].asInt();
        llvm::Function* fnGet = module.getFunction("havel_vm_global_get");
        if (!fnGet) {
            fnGet = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i32}, false),
                llvm::Function::ExternalLinkage, "havel_vm_global_get", &module);
        }
        vstack.push_back(B.CreateCall(fnGet, {vmArg, llvm::ConstantInt::get(i32, nameId)}));
        break;
    }
    case OpCode::STORE_GLOBAL: {
        uint32_t nameId = instr.operands[0].asInt();
        llvm::Value* v = vstack.back(); vstack.pop_back();
        llvm::Function* fnSet = module.getFunction("havel_vm_global_set");
        if (!fnSet) {
            fnSet = llvm::Function::Create(
                llvm::FunctionType::get(voidT, {i8p, i32, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_global_set", &module);
        }
        B.CreateCall(fnSet, {vmArg, llvm::ConstantInt::get(i32, nameId), v});
        vstack.push_back(v); // Store returns the value
        break;
    }
    case OpCode::LOAD_UPVALUE: {
        uint32_t slot = instr.operands[0].asInt();
        llvm::Function* fnUp = module.getFunction("havel_vm_upvalue_get");
        if (!fnUp) {
            fnUp = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i32}, false),
                llvm::Function::ExternalLinkage, "havel_vm_upvalue_get", &module);
        }
        vstack.push_back(B.CreateCall(fnUp, {vmArg, llvm::ConstantInt::get(i32, slot)}));
        break;
    }
    case OpCode::STORE_UPVALUE: {
        uint32_t slot = instr.operands[0].asInt();
        llvm::Value* v = vstack.back(); vstack.pop_back();
        llvm::Function* fnUp = module.getFunction("havel_vm_upvalue_set");
        if (!fnUp) {
            fnUp = llvm::Function::Create(
                llvm::FunctionType::get(voidT, {i8p, i32, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_upvalue_set", &module);
        }
        B.CreateCall(fnUp, {vmArg, llvm::ConstantInt::get(i32, slot), v});
        vstack.push_back(v);
        break;
    }

    // Arithmetic - MOD and POW for loops
    case OpCode::MOD: {
        llvm::Value* r = vstack.back(); vstack.pop_back();
        llvm::Value* l = vstack.back(); vstack.pop_back();
        // Use specialized path for MOD
        vstack.push_back(emitSpecializedBinop(OpCode::MOD, fb, ip, l, r));
        break;
    }
    case OpCode::POW: {
        // Power requires runtime call (no LLVM pow intrinsic for integers)
        llvm::Value* exp = vstack.back(); vstack.pop_back();
        llvm::Value* base = vstack.back(); vstack.pop_back();
        llvm::Function* fnPow = module.getFunction("havel_vm_pow");
        if (!fnPow) {
            fnPow = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i64, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_pow", &module);
        }
        vstack.push_back(B.CreateCall(fnPow, {base, exp}));
        break;
    }

    // Logical operations
    case OpCode::AND: {
        llvm::Value* r = vstack.back(); vstack.pop_back();
        llvm::Value* l = vstack.back(); vstack.pop_back();
        llvm::Function* fnTruthy = module.getFunction("havel_vm_is_truthy");
        if (!fnTruthy) {
            fnTruthy = llvm::Function::Create(
                llvm::FunctionType::get(i32, {i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_is_truthy", &module);
        }
        llvm::Value* lTruthy = B.CreateCall(fnTruthy, {l});
        llvm::Value* lIsFalsy = B.CreateICmpEQ(lTruthy, llvm::ConstantInt::get(i32, 0));
        vstack.push_back(B.CreateSelect(lIsFalsy, l, r));
        break;
    }
    case OpCode::OR: {
        llvm::Value* r = vstack.back(); vstack.pop_back();
        llvm::Value* l = vstack.back(); vstack.pop_back();
        llvm::Function* fnTruthy = module.getFunction("havel_vm_is_truthy");
        if (!fnTruthy) {
            fnTruthy = llvm::Function::Create(
                llvm::FunctionType::get(i32, {i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_is_truthy", &module);
        }
        llvm::Value* lTruthy = B.CreateCall(fnTruthy, {l});
        llvm::Value* lIsTruthy = B.CreateICmpNE(lTruthy, llvm::ConstantInt::get(i32, 0));
        vstack.push_back(B.CreateSelect(lIsTruthy, l, r));
        break;
    }

    // Array operations - critical for loops
    case OpCode::ARRAY_NEW: {
        llvm::Function* fnNew = module.getFunction("havel_vm_array_new");
        if (!fnNew) {
            fnNew = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p}, false),
                llvm::Function::ExternalLinkage, "havel_vm_array_new", &module);
        }
        vstack.push_back(B.CreateCall(fnNew, {vmArg}));
        break;
    }
    case OpCode::ARRAY_GET: {
        llvm::Value* idx = vstack.back(); vstack.pop_back();
        llvm::Value* arr = vstack.back(); vstack.pop_back();
        llvm::Function* fnGet = module.getFunction("havel_vm_array_get");
        if (!fnGet) {
            fnGet = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_array_get", &module);
        }
        vstack.push_back(B.CreateCall(fnGet, {vmArg, arr, idx}));
        break;
    }
    case OpCode::ARRAY_SET: {
        llvm::Value* val = vstack.back(); vstack.pop_back();
        llvm::Value* idx = vstack.back(); vstack.pop_back();
        llvm::Value* arr = vstack.back(); vstack.pop_back();
        llvm::Function* fnSet = module.getFunction("havel_vm_array_set");
        if (!fnSet) {
            fnSet = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i64, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_array_set", &module);
        }
        vstack.push_back(B.CreateCall(fnSet, {vmArg, arr, idx, val}));
        break;
    }
    case OpCode::ARRAY_LEN: {
        llvm::Value* arr = vstack.back(); vstack.pop_back();
        llvm::Function* fnLen = module.getFunction("havel_vm_array_len");
        if (!fnLen) {
            fnLen = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_array_len", &module);
        }
        vstack.push_back(B.CreateCall(fnLen, {vmArg, arr}));
        break;
    }
    case OpCode::ARRAY_PUSH: {
        llvm::Value* val = vstack.back(); vstack.pop_back();
        llvm::Value* arr = vstack.back(); vstack.pop_back();
        llvm::Function* fnPush = module.getFunction("havel_vm_array_push");
        if (!fnPush) {
            fnPush = llvm::Function::Create(
                llvm::FunctionType::get(voidT, {i8p, i64, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_array_push", &module);
        }
        B.CreateCall(fnPush, {vmArg, arr, val});
        vstack.push_back(arr); // Push array back
        break;
    }

    // Object operations
    case OpCode::OBJECT_NEW: {
        llvm::Function* fnNew = module.getFunction("havel_vm_object_new");
        if (!fnNew) {
            fnNew = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p}, false),
                llvm::Function::ExternalLinkage, "havel_vm_object_new", &module);
        }
        vstack.push_back(B.CreateCall(fnNew, {vmArg}));
        break;
    }
    case OpCode::OBJECT_GET: {
        uint32_t keyId = instr.operands[0].asInt();
        llvm::Value* obj = vstack.back(); vstack.pop_back();
        llvm::Function* fnGet = module.getFunction("havel_vm_object_get");
        if (!fnGet) {
            fnGet = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i32}, false),
                llvm::Function::ExternalLinkage, "havel_vm_object_get", &module);
        }
        vstack.push_back(B.CreateCall(fnGet, {vmArg, obj, llvm::ConstantInt::get(i32, keyId)}));
        break;
    }
    case OpCode::OBJECT_SET: {
        uint32_t keyId = instr.operands[0].asInt();
        llvm::Value* val = vstack.back(); vstack.pop_back();
        llvm::Value* obj = vstack.back(); vstack.pop_back();
        llvm::Function* fnSet = module.getFunction("havel_vm_object_set");
        if (!fnSet) {
            fnSet = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i32, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_object_set", &module);
        }
        vstack.push_back(B.CreateCall(fnSet, {vmArg, obj, llvm::ConstantInt::get(i32, keyId), val}));
        break;
    }

    // Range and iterators - critical for for loops
    case OpCode::RANGE_NEW: {
        llvm::Value* end = vstack.back(); vstack.pop_back();
        llvm::Value* start = vstack.back(); vstack.pop_back();
        llvm::Function* fnRange = module.getFunction("havel_vm_range_new");
        if (!fnRange) {
            fnRange = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_range_new", &module);
        }
        vstack.push_back(B.CreateCall(fnRange, {vmArg, start, end}));
        break;
    }
    case OpCode::ITER_NEW: {
        llvm::Value* coll = vstack.back(); vstack.pop_back();
        llvm::Function* fnIter = module.getFunction("havel_vm_iter_new");
        if (!fnIter) {
            fnIter = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_iter_new", &module);
        }
        vstack.push_back(B.CreateCall(fnIter, {vmArg, coll}));
        break;
    }
    case OpCode::ITER_NEXT: {
        llvm::Value* iter = vstack.back(); vstack.pop_back();
        llvm::Function* fnNext = module.getFunction("havel_vm_iter_next");
        if (!fnNext) {
            fnNext = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_iter_next", &module);
        }
        // Interpreter semantics: consume iterator operand and push next result.
        llvm::Value* result = B.CreateCall(fnNext, {vmArg, iter});
        vstack.push_back(result);
        break;
    }

    // Concurrency primitives - threads, coroutines, channels
    case OpCode::THREAD_SPAWN: {
        uint32_t funcId = instr.operands[0].asInt();
        llvm::Function* fnThread = module.getFunction("havel_vm_thread_new");
        if (!fnThread) {
            fnThread = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i32}, false),
                llvm::Function::ExternalLinkage, "havel_vm_thread_new", &module);
        }
        vstack.push_back(B.CreateCall(fnThread, {vmArg, llvm::ConstantInt::get(i32, funcId)}));
        break;
    }
    case OpCode::THREAD_JOIN: {
        llvm::Value* thread = vstack.back(); vstack.pop_back();
        llvm::Function* fnJoin = module.getFunction("havel_vm_thread_join");
        if (!fnJoin) {
            fnJoin = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_thread_join", &module);
        }
        vstack.push_back(B.CreateCall(fnJoin, {vmArg, thread}));
        break;
    }
    case OpCode::THREAD_SEND: {
        llvm::Value* msg = vstack.back(); vstack.pop_back();
        llvm::Value* thread = vstack.back(); vstack.pop_back();
        llvm::Function* fnSend = module.getFunction("havel_vm_thread_send");
        if (!fnSend) {
            fnSend = llvm::Function::Create(
                llvm::FunctionType::get(voidT, {i8p, i64, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_thread_send", &module);
        }
        B.CreateCall(fnSend, {vmArg, thread, msg});
        vstack.push_back(makeNull());
        break;
    }
    case OpCode::THREAD_RECEIVE: {
        llvm::Value* thread = vstack.back(); vstack.pop_back();
        llvm::Function* fnRecv = module.getFunction("havel_vm_thread_recv");
        if (!fnRecv) {
            fnRecv = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_thread_recv", &module);
        }
        vstack.push_back(B.CreateCall(fnRecv, {vmArg, thread}));
        break;
    }
    case OpCode::INTERVAL_START: {
        llvm::Value* callback = vstack.back(); vstack.pop_back();
        llvm::Value* duration = vstack.back(); vstack.pop_back();
        llvm::Function* fnStart = module.getFunction("havel_vm_interval_start");
        if (!fnStart) {
            fnStart = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_interval_start", &module);
        }
        vstack.push_back(B.CreateCall(fnStart, {vmArg, duration, callback}));
        break;
    }
    case OpCode::INTERVAL_STOP: {
        llvm::Value* interval = vstack.back(); vstack.pop_back();
        llvm::Function* fnStop = module.getFunction("havel_vm_interval_stop");
        if (!fnStop) {
            fnStop = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_interval_stop", &module);
        }
        vstack.push_back(B.CreateCall(fnStop, {vmArg, interval}));
        break;
    }
    case OpCode::TIMEOUT_START: {
        llvm::Value* callback = vstack.back(); vstack.pop_back();
        llvm::Value* delay = vstack.back(); vstack.pop_back();
        llvm::Function* fnStart = module.getFunction("havel_vm_timeout_start");
        if (!fnStart) {
            fnStart = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_timeout_start", &module);
        }
        vstack.push_back(B.CreateCall(fnStart, {vmArg, delay, callback}));
        break;
    }
    case OpCode::TIMEOUT_CANCEL: {
        llvm::Value* timeout = vstack.back(); vstack.pop_back();
        llvm::Function* fnCancel = module.getFunction("havel_vm_timeout_cancel");
        if (!fnCancel) {
            fnCancel = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_timeout_cancel", &module);
        }
        vstack.push_back(B.CreateCall(fnCancel, {vmArg, timeout}));
        break;
    }
    case OpCode::CHANNEL_NEW: {
        llvm::Value* cap = vstack.empty() ? llvm::ConstantInt::get(i64, 0) : vstack.back();
        if (!vstack.empty()) vstack.pop_back();
        llvm::Function* fnChan = module.getFunction("havel_vm_channel_new");
        if (!fnChan) {
            fnChan = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_channel_new", &module);
        }
        vstack.push_back(B.CreateCall(fnChan, {vmArg, cap}));
        break;
    }
    case OpCode::CHANNEL_SEND: {
        llvm::Value* val = vstack.back(); vstack.pop_back();
        llvm::Value* chan = vstack.back(); vstack.pop_back();
        llvm::Function* fnSend = module.getFunction("havel_vm_channel_send");
        if (!fnSend) {
            fnSend = llvm::Function::Create(
                llvm::FunctionType::get(voidT, {i8p, i64, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_channel_send", &module);
        }
        B.CreateCall(fnSend, {vmArg, chan, val});
        vstack.push_back(makeNull());
        break;
    }
    case OpCode::CHANNEL_RECEIVE: {
        llvm::Value* chan = vstack.back(); vstack.pop_back();
        llvm::Function* fnRecv = module.getFunction("havel_vm_channel_recv");
        if (!fnRecv) {
            fnRecv = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_channel_recv", &module);
        }
        vstack.push_back(B.CreateCall(fnRecv, {vmArg, chan}));
        break;
    }
    case OpCode::CHANNEL_CLOSE: {
        llvm::Value* chan = vstack.back(); vstack.pop_back();
        llvm::Function* fnClose = module.getFunction("havel_vm_channel_close");
        if (!fnClose) {
            fnClose = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_channel_close", &module);
        }
        vstack.push_back(B.CreateCall(fnClose, {vmArg, chan}));
        break;
    }
    case OpCode::YIELD: {
        llvm::Value* val = vstack.empty() ? makeNull() : vstack.back();
        if (!vstack.empty()) vstack.pop_back();
        llvm::Function* fnYield = module.getFunction("havel_vm_yield");
        if (!fnYield) {
            fnYield = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_yield", &module);
        }
        vstack.push_back(B.CreateCall(fnYield, {vmArg, val}));
        break;
    }
    // AWAIT opcode doesn't exist - awaiting is handled by interpreter

    // String operations
    case OpCode::STRING_LEN: {
        llvm::Value* str = vstack.back(); vstack.pop_back();
        llvm::Function* fnLen = module.getFunction("havel_vm_string_len");
        if (!fnLen) {
            fnLen = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_string_len", &module);
        }
        vstack.push_back(B.CreateCall(fnLen, {vmArg, str}));
        break;
    }
    case OpCode::STRING_CONCAT: {
        llvm::Value* r = vstack.back(); vstack.pop_back();
        llvm::Value* l = vstack.back(); vstack.pop_back();
        llvm::Function* fnCat = module.getFunction("havel_vm_string_concat");
        if (!fnCat) {
            fnCat = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_string_concat", &module);
        }
        vstack.push_back(B.CreateCall(fnCat, {vmArg, l, r}));
        break;
    }

    // Stack manipulation
    case OpCode::PUSH_NULL:
        vstack.push_back(makeNull());
        break;
    case OpCode::SWAP: {
        llvm::Value* b = vstack.back(); vstack.pop_back();
        llvm::Value* a = vstack.back(); vstack.pop_back();
        vstack.push_back(b);
        vstack.push_back(a);
        break;
    }
    case OpCode::NOP:
        break;

    case OpCode::CALL_METHOD: {
        if (instr.operands.size() != 2 || !instr.operands[0].isStringValId() || !instr.operands[1].isInt()) {
            vstack.push_back(makeNull());
            break;
        }

        uint32_t methodNameId = instr.operands[0].asStringValId();
        uint32_t argCount = static_cast<uint32_t>(instr.operands[1].asInt());

        llvm::Value* argsArray = B.CreateAlloca(llvm::ArrayType::get(i64, argCount), nullptr, "method_args");
        for (uint32_t i = 0; i < argCount; ++i) {
            llvm::Value* arg = vstack.back(); vstack.pop_back();
            B.CreateStore(arg, B.CreateInBoundsGEP(llvm::ArrayType::get(i64, argCount), argsArray,
                {llvm::ConstantInt::get(i32, 0), llvm::ConstantInt::get(i32, argCount - 1 - i)}));
        }
        llvm::Value* receiver = vstack.back(); vstack.pop_back();

        llvm::Function* fnMethod = module.getFunction("havel_vm_call_method");
        if (!fnMethod) {
            fnMethod = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i32, i64p, i32}, false),
                llvm::Function::ExternalLinkage, "havel_vm_call_method", &module);
        }

        vstack.push_back(B.CreateCall(fnMethod, {
            vmArg,
            receiver,
            llvm::ConstantInt::get(i32, methodNameId),
            B.CreateInBoundsGEP(llvm::ArrayType::get(i64, argCount), argsArray,
                {llvm::ConstantInt::get(i32, 0), llvm::ConstantInt::get(i32, 0)}),
            llvm::ConstantInt::get(i32, argCount)
        }));
        break;
    }

    default: break;
    }

    // Default fallthrough to next instruction block if the opcode didn't terminate.
    // Use instrBlock (saved at loop start) because some opcodes (emitSpecializedBinop)
    // may change the insert point to a different block.
    // Note: check getTerminator() on non-empty blocks; empty() is used as a fast path
    B.SetInsertPoint(instrBlock);
    if (instrBlock->empty() || instrBlock->getTerminator() == nullptr) {
        B.CreateBr(basicBlocks[ip + 1]);
    }
}

// Function epilogue for paths that reach the synthetic exit block.
B.SetInsertPoint(basicBlocks[func.instructions.size()]);
if (B.GetInsertBlock()->getTerminator() == nullptr) {
    llvm::Function *fn_unreg = module.getFunction("havel_gc_unregister_roots");
    if (!fn_unreg) {
        fn_unreg = llvm::Function::Create(
            llvm::FunctionType::get(voidT, {llvm::PointerType::getUnqual(frameType)}, false),
            llvm::Function::ExternalLinkage, "havel_gc_unregister_roots", &module);
    }
    B.CreateCall(fn_unreg, {frame});
    B.CreateRet(makeNull());
}
}

} // namespace havel::compiler
