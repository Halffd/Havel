// ===== JIT runtime bridges (LLVMRuntimeABI, TODO.md #21) =====
//
// The havel_vm_* extern "C" helpers the JIT lowers calls to: the runtime
// side of the Runtime ABI (compiler/runtime/RuntimeABI.hpp). Historically
// these lived inside BytecodeOrcJIT.cpp alongside the LLVM lowering; they
// are runtime semantics, not JIT concerns, so they live in their own
// translation unit - the LLVMRuntimeABI slice of the decomposition. The
// JIT keeps: LLVM IR lowering, ORC session management, symbol resolution,
// object loading, compiled-code lookup (TODO #20).

#include "BytecodeOrcJIT.h"
#include "JitBridgesCommon.hpp"
#include "compiler/runtime/RuntimeABI.hpp"
#include "compiler/vm/VM.hpp"
#include "runtime/HavelEngine.hpp"

#include <cmath>
#include <cstring>

// The bridges lived inside namespace havel::compiler in their original
// TU (BytecodeOrcJIT.cpp), which is where unqualified Value/VM/JITStackFrame
// resolved from; keep the same scope so the bodies move verbatim.
namespace havel::compiler {

using havel::core::Value;


// Value-boxing constants: JitBridgesCommon.hpp (shared with the lowering).

// ============================================================================
// Native Bridge Helpers
// ============================================================================
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif
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


extern "C" void havel_gc_register_roots(void* vm_ptr, JITStackFrame* frame,
                              uint64_t* slot_bits, uint32_t count);
extern "C" void havel_gc_unregister_roots(JITStackFrame* frame);
extern "C" void havel_deoptimize(void* vm_ptr, uint64_t l, uint64_t r, const char* func);
extern "C" uint64_t havel_vm_call(void* vm_ptr, uint64_t* args, uint32_t count);
extern "C" uint64_t havel_vm_tail_call(void* vm_ptr, uint64_t* args, uint32_t count);
extern "C" uint64_t havel_vm_global_get(void* vm_ptr, uint32_t name_id);
extern "C" void havel_vm_global_set(void* vm_ptr, uint32_t name_id, uint64_t value);

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

// All bridges in this TU compile with -ffast-math, where std::isnan folds
// to constant false and floating comparisons against NaN fold arbitrarily
// (this exact combination made havel_vm_neq(60, null) return false, which
// corrupted every mixed-type comparison from JIT-compiled code - the
// self-hosted parser read BP_NONE for every operator once getBindingPower
// tiered). Comparisons must therefore reason about NaN through the raw
// bit pattern, never through isnan or float equality against NaN.
// Fast-math guard: these functions must NOT be compiled with -ffast-math
// (release default). Under fast-math the compiler both folds isnan to false
// AND emits vucomisd-based branches whose NaN handling is undefined; the
// disassembly of the release build showed the neq NaN path branching into
// an unrelated trace block with clobbered registers. Per-function optnone
// keeps the bit-pattern NaN logic honest (it is already integer-based, but
// the surrounding double compare in the non-NaN path must also stay IEEE).
#if defined(__clang__)
#define HAVEL_NAN_SAFE __attribute__((optnone))
#else
#define HAVEL_NAN_SAFE __attribute__((optimize("no-fast-math")))
#endif

static HAVEL_NAN_SAFE bool rawBitsAreNaN(double d) {
  uint64_t bits;
  std::memcpy(&bits, &d, sizeof(bits));
  return (bits & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL &&
         (bits & 0x000FFFFFFFFFFFFFULL) != 0;
}

static HAVEL_NAN_SAFE double valueToDouble(uint64_t bits) {
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
  // Null/ptr/refs coerce to a NaN payload value; comparisons treat them as
  // never-equal. Build the NaN through the bit pattern (0x7FF8...) so the
  // value remains distinguishable under -ffast-math via rawBitsAreNaN.
  uint64_t nan_bits = 0x7FF8000000000001ULL;
  double d;
  std::memcpy(&d, &nan_bits, sizeof(d));
  return d;
}

static HAVEL_NAN_SAFE bool valueIsTruthy(uint64_t bits) {
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
    // Fast-math-safe: NaN doubles are falsy; test by raw bits, not isnan.
    if ((bits & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL &&
        (bits & 0x000FFFFFFFFFFFFFULL) != 0) {
      return false;  // NaN: falsy
    }
    double d; std::memcpy(&d, &bits, sizeof(double));
    return d != 0.0;  // note: -0.0 == 0.0 compares equal under IEEE;
                      // the interpreter treats -0.0 as falsy via the same
                      // comparison, so this matches.
  }
  return true; // objects, arrays, etc. are truthy
}

// EQ/comparison bridges moved to CoreRuntimeExports.cpp (Runtime ABI
// single home); declared in runtime/RuntimeABI.hpp.

uint64_t havel_vm_is(uint64_t l, uint64_t r) {
  return Value::makeBool(l == r).rawBits();
}

uint64_t havel_vm_not(uint64_t v) {
    return Value::makeBool(!valueIsTruthy(v)).rawBits();
}

uint64_t havel_vm_length(void* vm_ptr, uint64_t val_bits) {
    if (!vm_ptr) return Value(static_cast<int64_t>(0)).rawBits();
    auto* vm = static_cast<VM*>(vm_ptr);
    Value v;
    std::memcpy(&v, &val_bits, sizeof(uint64_t));
    return vm->execLengthOp(v).rawBits();
}

// Comparison bridges (Runtime ABI): pure word semantics, no VM state.
// Numeric comparisons coerce both sides as doubles; null/refs coerce to
// NaN so comparisons against them are false; EQ/NEQ first check raw bit
// equality so identical words compare equal. The single Runtime ABI home
// for these (an accidental second copy in CoreRuntimeExports.cpp broke
// the linker's one-definition rule once - do not duplicate).
extern "C" HAVEL_NAN_SAFE uint64_t havel_vm_eq(uint64_t l, uint64_t r) {
  if (l == r) return Value::makeBool(true).rawBits();
  const double ld = valueToDouble(l), rd = valueToDouble(r);
  if (rawBitsAreNaN(ld) || rawBitsAreNaN(rd)) return Value::makeBool(false).rawBits();
  return Value::makeBool(ld == rd).rawBits();
}

// VM-aware equality: EQ/NEQ may compare strings, and string content lives
// in the heap (or chunk string tables), so the pure (l, r) bridges can never
// implement the interpreter's valuesEqualDeep string rule. JIT-compiled
// `node.kind == "NumberLiteral"` compares a heap StringId against a
// chunk-local StringValId - with only bit/NaN semantics both sides coerce to
// NaN, the comparison reads "not equal", and dispatch code (the self-hosted
// emitter's AST walker) falls through every branch. These variants take the
// vm pointer so strings compare by CONTENT like the interpreter.
extern "C" uint64_t havel_vm_eq_vm(void* vm_ptr, uint64_t l, uint64_t r) {
  if (l == r) return Value::makeBool(true).rawBits();
  if (vm_ptr) {
    auto* vm = static_cast<VM*>(vm_ptr);
    Value lv = Value::fromRawBits(l);
    Value rv = Value::fromRawBits(r);
    const bool l_is_str = lv.isStringId() || lv.isStringValId() || lv.isRegexValId();
    const bool r_is_str = rv.isStringId() || rv.isStringValId() || rv.isRegexValId();
    if (l_is_str || r_is_str) {
      if (l_is_str && r_is_str) {
        return Value::makeBool(vm->stringsEqualPublic(l, r)).rawBits();
      }
      return Value::makeBool(false).rawBits();
    }
  }
  const double ld = valueToDouble(l), rd = valueToDouble(r);
  if (rawBitsAreNaN(ld) || rawBitsAreNaN(rd)) return Value::makeBool(false).rawBits();
  return Value::makeBool(ld == rd).rawBits();
}

extern "C" uint64_t havel_vm_neq_vm(void* vm_ptr, uint64_t l, uint64_t r) {
  if (l == r) return Value::makeBool(false).rawBits();
  if (vm_ptr) {
    auto* vm = static_cast<VM*>(vm_ptr);
    Value lv = Value::fromRawBits(l);
    Value rv = Value::fromRawBits(r);
    const bool l_is_str = lv.isStringId() || lv.isStringValId() || lv.isRegexValId();
    const bool r_is_str = rv.isStringId() || rv.isStringValId() || rv.isRegexValId();
    if (l_is_str || r_is_str) {
      if (l_is_str && r_is_str) {
        return Value::makeBool(!vm->stringsEqualPublic(l, r)).rawBits();
      }
      return Value::makeBool(true).rawBits();
    }
  }
  const double ld = valueToDouble(l), rd = valueToDouble(r);
  if (rawBitsAreNaN(ld) || rawBitsAreNaN(rd)) return Value::makeBool(true).rawBits();
  return Value::makeBool(ld != rd).rawBits();
}

extern "C" HAVEL_NAN_SAFE uint64_t havel_vm_neq(uint64_t l, uint64_t r) {
  if (l == r) return Value::makeBool(false).rawBits();
  const double ld = valueToDouble(l), rd = valueToDouble(r);
  if (rawBitsAreNaN(ld) || rawBitsAreNaN(rd)) return Value::makeBool(true).rawBits();
  return Value::makeBool(ld != rd).rawBits();
}

extern "C" HAVEL_NAN_SAFE uint64_t havel_vm_lt(uint64_t l, uint64_t r) {
  const double ld = valueToDouble(l), rd = valueToDouble(r);
  if (rawBitsAreNaN(ld) || rawBitsAreNaN(rd)) return Value::makeBool(false).rawBits();
  return Value::makeBool(ld < rd).rawBits();
}

extern "C" HAVEL_NAN_SAFE uint64_t havel_vm_lte(uint64_t l, uint64_t r) {
  const double ld = valueToDouble(l), rd = valueToDouble(r);
  if (rawBitsAreNaN(ld) || rawBitsAreNaN(rd)) return Value::makeBool(false).rawBits();
  return Value::makeBool(ld <= rd).rawBits();
}

extern "C" HAVEL_NAN_SAFE uint64_t havel_vm_gt(uint64_t l, uint64_t r) {
  const double ld = valueToDouble(l), rd = valueToDouble(r);
  if (rawBitsAreNaN(ld) || rawBitsAreNaN(rd)) return Value::makeBool(false).rawBits();
  return Value::makeBool(ld > rd).rawBits();
}

extern "C" HAVEL_NAN_SAFE uint64_t havel_vm_gte(uint64_t l, uint64_t r) {
  const double ld = valueToDouble(l), rd = valueToDouble(r);
  if (rawBitsAreNaN(ld) || rawBitsAreNaN(rd)) return Value::makeBool(false).rawBits();
  return Value::makeBool(ld >= rd).rawBits();
}

// Truthiness of a raw Value word (Runtime ABI): null falsy, bool by
// payload, int48 non-zero, raw double non-zero/non-NaN, refs truthy.
extern "C" HAVEL_NAN_SAFE int havel_vm_is_truthy(uint64_t v) {
  return valueIsTruthy(v) ? 1 : 0;
}

// Arithmetic bridges for backends that lower speculative int paths and
// need generic semantics for everything else (the Cranelift prototype):
// the VM's execBinaryOp owns the language semantics, so stage the operand
// words on the VM stack and run it in isolation.
static uint64_t runVmBinaryOp(void* vm_ptr, havel::compiler::OpCode op,
                              uint64_t l, uint64_t r);

extern "C" uint64_t havel_vm_add(void* vm_ptr, uint64_t l, uint64_t r) {
  return runVmBinaryOp(vm_ptr, havel::compiler::OpCode::ADD, l, r);
}
extern "C" uint64_t havel_vm_sub(void* vm_ptr, uint64_t l, uint64_t r) {
  return runVmBinaryOp(vm_ptr, havel::compiler::OpCode::SUB, l, r);
}
extern "C" uint64_t havel_vm_mul(void* vm_ptr, uint64_t l, uint64_t r) {
  return runVmBinaryOp(vm_ptr, havel::compiler::OpCode::MUL, l, r);
}

static uint64_t runVmBinaryOp(void* vm_ptr, havel::compiler::OpCode op,
                              uint64_t l, uint64_t r) {
  auto* vm = static_cast<VM*>(vm_ptr);
  if (!vm) return Value::makeNull().rawBits();
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

uint64_t havel_vm_collection_get_raw(void* vm_ptr, uint64_t container_bits, uint64_t key_bits) {
  if (!vm_ptr) return Value::makeNull().rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  Value container, key_val;
  std::memcpy(&container, &container_bits, sizeof(uint64_t));
  std::memcpy(&key_val, &key_bits, sizeof(uint64_t));
  auto indexFromRaw = [](const Value &v) -> std::optional<int64_t> {
    if (v.isInt()) return v.asInt();
    return std::nullopt;
  };

  if (container.isArrayId()) {
    auto index = indexFromRaw(key_val);
    if (!index) return Value::makeNull().rawBits();
    auto* array = vm->getHeap().array(container.asArrayId());
    if (!array) return Value::makeNull().rawBits();
    int64_t idx = *index;
    if (idx < 0) idx = static_cast<int64_t>(array->size()) + idx;
    if (idx < 0 || static_cast<size_t>(idx) >= array->size()) return Value::makeNull().rawBits();
    return (*array)[static_cast<size_t>(idx)].rawBits();
  }

  if (container.isSetId()) {
    auto key = vm->resolveKeyPublic(key_val);
    if (!key) return Value::makeBool(false).rawBits();
    auto* set = vm->getHeap().set(container.asSetId());
    if (!set) return Value::makeBool(false).rawBits();
    return Value::makeBool(set->find(*key) != set->end()).rawBits();
  }

  if (container.isStringId() || container.isStringValId()) {
    auto index = indexFromRaw(key_val);
    if (!index) return Value::makeNull().rawBits();
    std::string s;
    if (container.isStringId()) {
      auto *sp = vm->getHeap().string(container.asStringId());
      if (sp) s = *sp;
    } else if (container.isStringValId() && vm->getCurrentChunk()) {
      s = vm->getCurrentChunk()->getString(container.asStringValId());
    }
    int64_t numCodepoints = 0;
    size_t bytePos = 0;
    while (bytePos < s.size()) {
      unsigned char c = static_cast<unsigned char>(s[bytePos]);
      size_t cpLen = 1;
      if (c < 0x80) cpLen = 1;
      else if ((c & 0xE0) == 0xC0) cpLen = 2;
      else if ((c & 0xF0) == 0xE0) cpLen = 3;
      else if ((c & 0xF8) == 0xF0) cpLen = 4;
      if (bytePos + cpLen > s.size()) cpLen = 1;
      numCodepoints++;
      bytePos += cpLen;
    }
    int64_t idx = *index;
    if (idx < 0) idx = numCodepoints + idx;
    if (idx < 0 || idx >= numCodepoints) return Value::makeNull().rawBits();
    size_t targetByte = 0;
    int64_t cpIdx = 0;
    while (cpIdx < idx && targetByte < s.size()) {
      unsigned char c = static_cast<unsigned char>(s[targetByte]);
      size_t cpLen = 1;
      if (c < 0x80) cpLen = 1;
      else if ((c & 0xE0) == 0xC0) cpLen = 2;
      else if ((c & 0xF0) == 0xE0) cpLen = 3;
      else if ((c & 0xF8) == 0xF0) cpLen = 4;
      if (targetByte + cpLen > s.size()) cpLen = 1;
      targetByte += cpLen;
      cpIdx++;
    }
    size_t cpLen = 1;
    if (targetByte < s.size()) {
      unsigned char c = static_cast<unsigned char>(s[targetByte]);
      if (c < 0x80) cpLen = 1;
      else if ((c & 0xE0) == 0xC0) cpLen = 2;
      else if ((c & 0xF0) == 0xE0) cpLen = 3;
      else if ((c & 0xF8) == 0xF0) cpLen = 4;
      if (targetByte + cpLen > s.size()) cpLen = 1;
    }
    auto ref = vm->getHeap().allocateString(s.substr(targetByte, cpLen));
    return Value::makeStringId(ref.id).rawBits();
  }

  if (container.isObjectId()) {
    auto key = vm->resolveKeyPublic(key_val);
    if (!key) return Value::makeNull().rawBits();
    if (container.asObjectId() == vm->globalsMirrorObjectId()) {
      return vm->lookupGlobalByKey(*key).rawBits();
    }
    return vm->objectGetWithClassChain(container.asObjectId(), *key).rawBits();
  }

  return Value::makeNull().rawBits();
}

uint64_t havel_vm_collection_get_raw_ic(void* vm_ptr, uint64_t container_bits, uint64_t key_bits) {
    struct CacheEntry {
        uint64_t container_bits = 0;
        uint64_t version = 0;
        uint64_t key_bits = 0;
        uint64_t value_bits = 0;
        uint64_t gc_epoch = 0;
        bool valid = false;
    };

    // Same GC-epoch discipline as object_get_raw_ic: container ids are
    // recycled after a sweep, so the epoch must ride along in the key or
    // a dead container's entry can be served to its id's new owner.
    thread_local uint64_t last_epoch = 0;
    thread_local std::array<CacheEntry, 8> cache{};

    if (!vm_ptr) return Value::makeNull().rawBits();
    auto* vm = static_cast<VM*>(vm_ptr);
    Value container, key_val;
    std::memcpy(&container, &container_bits, sizeof(uint64_t));
    std::memcpy(&key_val, &key_bits, sizeof(uint64_t));
    const uint64_t epoch = vm->getHeap().gcEpoch();
    if (epoch != last_epoch) {
        last_epoch = epoch;
        for (auto& e : cache) e.valid = false;
    }

    uint64_t version = 0;
    if (container.isArrayId()) version = vm->arrayVersion(container.asArrayId());
    else if (container.isSetId()) version = vm->setVersion(container.asSetId());
    else if (container.isObjectId()) version = vm->objectLookupVersion(container.asObjectId());
    else if (container.isStringId() || container.isStringValId()) version = container_bits;

    const size_t primary = static_cast<size_t>((container_bits ^ key_bits ^ (version >> 1)) & (cache.size() - 1));
    for (size_t probe = 0; probe < cache.size(); ++probe) {
        const auto &entry = cache[(primary + probe) & (cache.size() - 1)];
        if (entry.valid && entry.container_bits == container_bits && entry.version == version && entry.key_bits == key_bits &&
            entry.gc_epoch == epoch) {
            return entry.value_bits;
        }
    }

    auto result_bits = havel_vm_collection_get_raw(vm_ptr, container_bits, key_bits);
    cache[primary] = CacheEntry{container_bits, version, key_bits, result_bits, epoch, true};
    return result_bits;
}

uint64_t havel_vm_array_set(void* vm_ptr, uint64_t arr_bits, uint64_t idx_bits, uint64_t val_bits) {
    if (!vm_ptr) return val_bits;
    auto* vm = static_cast<VM*>(vm_ptr);
    // Full interpreter parity: ARRAY_SET falls through array/set/object
    // semantics (VM::indexAssignPublic mirrors VMCollections.cpp,
    // including the object GC write barrier and op_index_set dispatch).
    // Previously this bailed on non-array containers, so every
    // obj[key] = value in JIT-compiled code silently no-opped - the
    // self-hosted parser's binding-power table built empty and every
    // operator lookup read BP_NONE, corrupting parses once getBPTABLE
    // tiered.
    return vm->indexAssignPublic(arr_bits, idx_bits, val_bits);
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
    // Member access on non-objects (array len/index, fn properties, string
    // prototypes, ...) previously bailed to null here - the interpreter's
    // OBJECT_GET handles all of them, so route through the parity seam.
    // tokens.len on an array reading null hung the self-hosted parser.
    Value out;
    if (vm->memberGetPublic(obj_bits, key_bits, &out)) {
        return out.rawBits();
    }
    return Value::makeNull().rawBits();
}

uint64_t havel_vm_object_get_raw_ic(void* vm_ptr, uint64_t obj_bits, uint64_t key_bits) {
    struct CacheEntry {
        uint32_t obj_id = 0;
        uint64_t shape_version = 0;
        uint64_t key_bits = 0;
        uint64_t value_bits = 0;
        uint64_t gc_epoch = 0;
        bool valid = false;
    };

    // GC epoch: heap ids are RECYCLED, so an (obj_id, shape_version, key)
    // key alone can hit a dead object's stale entry when a new object
    // reuses its id (the self-hosted emitter's AST nodes - one per
    // expression, churned fast - read each other's `kind` through exactly
    // this collision once a collection cycle ran mid-emission, and every
    // node dispatched into the wrong emit branch). Folding the epoch in
    // makes every cache die with the collection that invalidated it.
    thread_local uint64_t last_epoch = 0;
    thread_local std::array<CacheEntry, 4> cache{};

    if (!vm_ptr) return Value::makeNull().rawBits();
    auto* vm = static_cast<VM*>(vm_ptr);
    Value obj, key_val;
    std::memcpy(&obj, &obj_bits, sizeof(uint64_t));
    std::memcpy(&key_val, &key_bits, sizeof(uint64_t));
    const uint64_t epoch = vm->getHeap().gcEpoch();
    if (epoch != last_epoch) {
        last_epoch = epoch;
        for (auto& e : cache) e.valid = false;
    }
    if (!obj.isObjectId()) {
        // Non-object receivers take the un-cached parity path
        // (memberGetPublic); arrays are mutable so the IC's shape-version
        // scheme does not apply to them anyway.
        Value out;
        if (vm->memberGetPublic(obj_bits, key_bits, &out)) {
            return out.rawBits();
        }
        return Value::makeNull().rawBits();
    }

    const uint32_t obj_id = obj.asObjectId();
    if (obj_id == vm->globalsMirrorObjectId()) {
        auto key_str = vm->resolveKeyPublic(key_val);
        if (!key_str) return Value::makeNull().rawBits();
        return vm->lookupGlobalByKey(*key_str).rawBits();
    }

    const uint64_t version = vm->objectLookupVersion(obj_id);
    const size_t primary = static_cast<size_t>((obj_id ^ key_bits ^ (key_bits >> 32)) & (cache.size() - 1));
    for (size_t probe = 0; probe < cache.size(); ++probe) {
        const auto &entry = cache[(primary + probe) & (cache.size() - 1)];
        if (entry.valid && entry.obj_id == obj_id && entry.shape_version == version && entry.key_bits == key_bits &&
            entry.gc_epoch == epoch) {
            return entry.value_bits;
        }
    }

    auto result_bits = havel_vm_object_get_raw(vm_ptr, obj_bits, key_bits);
    cache[primary] = CacheEntry{obj_id, version, key_bits, result_bits, epoch, true};
    return result_bits;
}

uint64_t havel_vm_object_set_raw(void* vm_ptr, uint64_t obj_bits, uint64_t key_bits, uint64_t val_bits) {
    if (!vm_ptr) return val_bits;
    auto* vm = static_cast<VM*>(vm_ptr);
    Value obj, key_val, val;
    std::memcpy(&obj, &obj_bits, sizeof(uint64_t));
    std::memcpy(&key_val, &key_bits, sizeof(uint64_t));
    std::memcpy(&val, &val_bits, sizeof(uint64_t));
    if (!obj.isObjectId()) return val_bits;

    auto key_str = vm->resolveKeyPublic(key_val);
    if (!key_str) return val_bits;

    vm->setHostObjectField(ObjectRef{obj.asObjectId()}, *key_str, val);
    // Return the object reference (not the value) so chained sets
    // (obj.a, obj.b, obj.c pushed through repeated SET/SET/SET) keep
    // operating on the same object. Previously this returned val_bits,
    // which made later chained sets pass a non-ObjectId as obj and
    // silently no-op — turning every multi-field object initializer
    // into a one-field object followed by globals pollution. Matches
    // interpreter OBJECT_SET which pops key/value/obj and pushes obj.
    return obj_bits;
}

uint64_t havel_vm_object_has_raw(void* vm_ptr, uint64_t obj_bits, uint64_t key_bits) {
    if (!vm_ptr) return Value::makeBool(false).rawBits();
    auto* vm = static_cast<VM*>(vm_ptr);
    Value obj, key_val;
    std::memcpy(&obj, &obj_bits, sizeof(uint64_t));
    std::memcpy(&key_val, &key_bits, sizeof(uint64_t));
    if (!obj.isObjectId()) return Value::makeBool(false).rawBits();

    auto key_str = vm->resolveKeyPublic(key_val);
    if (!key_str) return Value::makeBool(false).rawBits();

    return Value::makeBool(vm->hasHostObjectField(ObjectRef{obj.asObjectId()}, *key_str)).rawBits();
}

void havel_vm_object_delete_raw(void* vm_ptr, uint64_t obj_bits, uint64_t key_bits) {
    if (!vm_ptr) return;
    auto* vm = static_cast<VM*>(vm_ptr);
    Value obj, key_val;
    std::memcpy(&obj, &obj_bits, sizeof(uint64_t));
    std::memcpy(&key_val, &key_bits, sizeof(uint64_t));
    if (!obj.isObjectId()) return;

    auto key_str = vm->resolveKeyPublic(key_val);
    if (!key_str) return;

    vm->deleteHostObjectField(ObjectRef{obj.asObjectId()}, *key_str);
}

void havel_vm_backedge(void* vm_ptr, uint32_t ip) {
  if (!vm_ptr) return;
  auto* vm = static_cast<VM*>(vm_ptr);
  vm->recordBackedgePublic(ip);
  if (vm->consumeJitYieldRequest()) {
    throw JitCoroutineSignal{JitCoroutineSignal::Op::YIELD, Value::makeNull()};
  }
}

#include "runtime/HavelEngine.hpp"
extern "C" void* havel_vm_init_standalone(const char** strings, uint32_t count) {
    static ::havel::HavelEngine engine;
    if (!engine.isInitialized()) {
        engine.initializeMinimal();
        
        // If we have strings, create a dummy chunk so that name lookups (LOAD_GLOBAL, CALL_METHOD) work in AOT
        if (strings && count > 0) {
            auto chunk = std::make_shared<BytecodeChunk>();
            for (uint32_t i = 0; i < count; ++i) {
                chunk->addString(strings[i]);
            }
            // Set as current chunk for the VM (this is a hack for AOT, but it works)
            engine.vm()->setCurrentChunkPublic(chunk.get());
            // Push the initial frame for the main script (even if function count is 0)
            // This ensures that LOAD_VAR/STORE_VAR and other frame-dependent operations
            // don't crash when executed in the top-level script.
            engine.vm()->pushFramePublic(nullptr, 0, 0, 0);
            
            // Keep it alive
            static std::shared_ptr<BytecodeChunk> keepAlive = chunk;
        }
    }
    return engine.vm();
}

// Extended init for AOT with closures: also creates function entries in the chunk
// so that CLOSURE opcode can look up functions by index.
extern "C" void* havel_vm_init_standalone_with_functions(
    const char** strings, uint32_t string_count,
    const char** func_names, uint32_t func_count,
    const uint32_t* func_param_counts, const uint32_t* func_local_counts,
    const uint32_t* func_upvalue_counts, const uint32_t* func_is_generator,
    const uint32_t* upvalue_indices, const uint32_t* upvalue_captures_local,
    uint32_t total_upvalues,
    const uint32_t* func_const_counts, const uint64_t* func_const_data,
    const uint32_t* func_instr_counts, const uint64_t* func_instr_data,
    uint32_t num_functions,
    const char* build_dir
) {
    static ::havel::HavelEngine engine;
    if (!engine.isInitialized()) {
        engine.initializeMinimal();
        
        // Initialize module loader for AOT so that IMPORT works
        auto* vm = engine.vm();
        if (vm) {
            // Use build_dir (passed from stub) for module paths
            // build_dir is the build directory (e.g., /path/to/build-debug)
            // Source modules are at build_dir/../modules
            std::string stdlibPath;
            const char* envStdlib = std::getenv("HAVEL_STDLIB");
            if (envStdlib && envStdlib[0] != '\0') {
                stdlibPath = envStdlib;
            } else if (build_dir && build_dir[0] != '\0') {
                stdlibPath = (std::filesystem::path(build_dir) / ".." / "modules" / "std").string();
            } else {
                auto exePath = Env::executable();
                if (!exePath.empty()) {
                    stdlibPath = (std::filesystem::path(exePath).parent_path() / ".." / "modules" / "std").string();
                } else {
                    stdlibPath = "./modules/std";
                }
            }
            vm->moduleLoader().setStdlibPath(stdlibPath);

            // Add module search paths - use build_dir/.. for source
            std::string modulesRoot;
            if (build_dir && build_dir[0] != '\0') {
                modulesRoot = (std::filesystem::path(build_dir) / ".." / "modules").string();
            } else {
                auto exePath = Env::executable();
                if (!exePath.empty()) {
                    modulesRoot = (std::filesystem::path(exePath).parent_path() / ".." / "modules").string();
                } else {
                    modulesRoot = "./modules";
                }
            }
            auto canonicalRoot = std::filesystem::exists(modulesRoot)
                ? std::filesystem::canonical(modulesRoot).string() : modulesRoot;
            vm->moduleLoader().addSearchPath(canonicalRoot + "/lang");
            vm->moduleLoader().addSearchPath(canonicalRoot + "/std");
            vm->moduleLoader().addSearchPath(canonicalRoot + "/app");
            vm->moduleLoader().addSearchPath(canonicalRoot);

            // Also add build directory to C loader's search paths for native modules
            if (build_dir && build_dir[0] != '\0') {
                auto extLoader = vm->pluginLoader();
                if (extLoader) {
                    std::string buildModulesPath = (std::filesystem::path(build_dir) / "modules").string();
                    extLoader->addSearchPath(buildModulesPath);
                    extLoader->addModulePaths();
                }
            }
        }
        
        if (strings && string_count > 0) {
            auto chunk = std::make_shared<BytecodeChunk>();
            for (uint32_t i = 0; i < string_count; ++i) {
                chunk->addString(strings[i]);
            }
            
            // Parse upvalue data
            uint32_t upvalue_offset = 0;
            uint64_t const_data_offset = 0;
            uint64_t instr_data_offset = 0;
            for (uint32_t fi = 0; fi < func_count; ++fi) {
                std::string name(func_names[fi]);
                uint32_t param_count = func_param_counts[fi];
                uint32_t local_count = func_local_counts[fi];
                uint32_t upvalue_count = func_upvalue_counts[fi];
                bool is_gen = func_is_generator[fi] != 0;
                
                BytecodeFunction func(name, param_count, local_count);
                func.is_generator = is_gen;
                
                // Add upvalue descriptors
                for (uint32_t ui = 0; ui < upvalue_count; ++ui) {
                    UpvalueDescriptor desc;
                    desc.index = upvalue_indices[upvalue_offset + ui];
                    desc.captures_local = upvalue_captures_local[upvalue_offset + ui] != 0;
                    func.upvalues.push_back(desc);
                }
                upvalue_offset += upvalue_count;
                
                // Deserialize constants
                if (func_const_counts && func_const_data && fi < num_functions) {
                    uint32_t const_count = func_const_counts[fi];
                    func.constants.reserve(const_count);
                    for (uint32_t ci = 0; ci < const_count; ++ci) {
                        func.constants.push_back(Value::fromRawBits(func_const_data[const_data_offset++]));
                    }
                }
                
                // Deserialize instructions
                if (func_instr_counts && func_instr_data && fi < num_functions) {
                    uint32_t instr_count = func_instr_counts[fi];
                    for (uint32_t ii = 0; ii < instr_count; ++ii) {
                        uint32_t opcode = static_cast<uint32_t>(func_instr_data[instr_data_offset++]);
                        uint32_t num_operands = static_cast<uint32_t>(func_instr_data[instr_data_offset++]);
                        std::vector<Value> operands;
                        operands.reserve(num_operands);
                        for (uint32_t oi = 0; oi < num_operands; ++oi) {
                            operands.push_back(Value::fromRawBits(func_instr_data[instr_data_offset++]));
                        }
                        func.instructions.emplace_back(static_cast<OpCode>(opcode), std::move(operands));
                    }
                }
                
                chunk->addFunction(std::move(func));
            }
            
            engine.vm()->setCurrentChunkPublic(chunk.get());
            engine.vm()->pushFramePublic(nullptr, 0, 0, 0);
            
            static std::shared_ptr<BytecodeChunk> keepAlive = chunk;
        }
    }
    return engine.vm();
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
  
  // DEBUG
  uint32_t iter_id = iter.asIteratorId();
  auto* iter_ref = vm->getHeap().iterator(iter_id);
  if (iter_ref && iter_ref->iterable.isRangeId()) {
    auto* r = vm->getHeap().range(iter_ref->iterable.asRangeId());
  }
  
  return vm->iteratorNext(IteratorRef{iter.asIteratorId()}).rawBits();
}

uint64_t havel_vm_time_now(void* vm_ptr) {
  (void)vm_ptr;
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
  return Value(static_cast<int64_t>(ms)).rawBits();
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
  Value v;
  std::memcpy(&v, &val_bits, sizeof(uint64_t));
  throw JitCoroutineSignal{JitCoroutineSignal::Op::YIELD, v};
}

uint64_t havel_vm_await(void* vm_ptr, uint64_t val_bits) {
  Value v;
  std::memcpy(&v, &val_bits, sizeof(uint64_t));
  throw JitCoroutineSignal{JitCoroutineSignal::Op::AWAIT, v};
}

// Yield-point check: called by JIT-compiled code at loop backedges.
// If the scheduler has requested preemption, throws JitCoroutineSignal
// to exit the JIT frame and return control to the scheduler.
void havel_vm_check_yield(void* vm_ptr) {
  if (!vm_ptr) return;
  auto* vm = static_cast<VM*>(vm_ptr);
  if (vm->consumeJitYieldRequest()) {
    throw JitCoroutineSignal{JitCoroutineSignal::Op::YIELD, Value::makeNull()};
  }
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
  
  if (!(l.isStringId() || l.isStringValId()) || !(r.isStringId() || r.isStringValId())) {
    return Value::makeNull().rawBits();
  }
  
  std::string lStr, rStr;
  if (l.isStringId()) {
    auto* strPtr = vm->getHeap().string(l.asStringId());
    lStr = strPtr ? *strPtr : "";
  } else {
    lStr = chunk->getString(l.asStringValId());
  }
  if (r.isStringId()) {
    auto* strPtr = vm->getHeap().string(r.asStringId());
    rStr = strPtr ? *strPtr : "";
  } else {
    rStr = chunk->getString(r.asStringValId());
  }
  
  std::string result = lStr + rStr;
  auto ref = vm->createRuntimeString(std::move(result));
  return Value::makeStringId(ref.id).rawBits();
}

// Bitwise operations (only valid for int48 values)
uint64_t havel_vm_bit_and(uint64_t a_bits, uint64_t b_bits) {
  Value a, b;
  std::memcpy(&a, &a_bits, sizeof(uint64_t));
  std::memcpy(&b, &b_bits, sizeof(uint64_t));
  if (a.isInt() && b.isInt()) return Value(a.asInt() & b.asInt()).rawBits();
  return Value::makeNull().rawBits();
}
uint64_t havel_vm_bit_or(uint64_t a_bits, uint64_t b_bits) {
  Value a, b;
  std::memcpy(&a, &a_bits, sizeof(uint64_t));
  std::memcpy(&b, &b_bits, sizeof(uint64_t));
  if (a.isInt() && b.isInt()) return Value(a.asInt() | b.asInt()).rawBits();
  return Value::makeNull().rawBits();
}
uint64_t havel_vm_bit_xor(uint64_t a_bits, uint64_t b_bits) {
  Value a, b;
  std::memcpy(&a, &a_bits, sizeof(uint64_t));
  std::memcpy(&b, &b_bits, sizeof(uint64_t));
  if (a.isInt() && b.isInt()) return Value(a.asInt() ^ b.asInt()).rawBits();
  return Value::makeNull().rawBits();
}
uint64_t havel_vm_bit_not(uint64_t val_bits) {
  Value v;
  std::memcpy(&v, &val_bits, sizeof(uint64_t));
  if (v.isInt()) return Value(~v.asInt()).rawBits();
  return Value::makeNull().rawBits();
}
uint64_t havel_vm_bit_lsh(uint64_t a_bits, uint64_t b_bits) {
  Value a, b;
  std::memcpy(&a, &a_bits, sizeof(uint64_t));
  std::memcpy(&b, &b_bits, sizeof(uint64_t));
  if (a.isInt() && b.isInt()) return Value(a.asInt() << b.asInt()).rawBits();
  return Value::makeNull().rawBits();
}
uint64_t havel_vm_bit_rsh(uint64_t a_bits, uint64_t b_bits) {
  Value a, b;
  std::memcpy(&a, &a_bits, sizeof(uint64_t));
  std::memcpy(&b, &b_bits, sizeof(uint64_t));
  if (a.isInt() && b.isInt()) return Value(a.asInt() >> b.asInt()).rawBits();
  return Value::makeNull().rawBits();
}

uint64_t havel_vm_fiber_sleep(void* vm_ptr, uint64_t ms_bits) {
  Value v;
  std::memcpy(&v, &ms_bits, sizeof(uint64_t));
  throw JitCoroutineSignal{JitCoroutineSignal::Op::SLEEP, v};
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
    for (uint32_t i = 0; i < arg_count; ++i) {
        Value v;
        std::memcpy(&v, &args[i], sizeof(uint64_t));
        callArgs.push_back(v);
    }

    bool passReceiverAsSelf = true;

    if (receiver.isObjectId()) {
        ObjectRef recvRef{receiver.asObjectId(), true};
        auto* obj = vm->getHeap().object(recvRef.id);
        if (obj) {
            bool foundViaModule = false;
            for (const auto& [name, val] : vm->getGlobals()) {
                if (val.isObjectId() && val.asObjectId() == receiver.asObjectId()) {
                    foundViaModule = true;
                    break;
                }
            }
            if (foundViaModule) {
                passReceiverAsSelf = false;
                Value methodValue = vm->getHostObjectField(recvRef, method_name);
                if (!methodValue.isNull()) {
                    if (methodValue.isHostFuncId()) {
                        if (auto hostName = vm->getHostFunctionName(methodValue.asHostFuncId())) {
                            Value result = vm->invokeHostFunctionDirect(*hostName, callArgs);
                            return result.rawBits();
                        }
                    }
                    return vm->callFunction(methodValue, callArgs).rawBits();
                }
            } else {
                auto* classVal = obj->get("__class");
                if (!classVal) classVal = obj->get("__struct");
                if (classVal && classVal->isObjectId()) {
                    passReceiverAsSelf = true;
                } else if (obj->get("__is_class") || obj->get("__is_struct")) {
                    passReceiverAsSelf = true;
                } else {
                    Value methodValue = vm->getHostObjectField(recvRef, method_name);
                    if (methodValue.isHostFuncId()) {
                        uint32_t hostIdx = methodValue.asHostFuncId();
                        if (vm->host_function_wants_self_.count(hostIdx) > 0) {
                            passReceiverAsSelf = true;
                        } else {
                            passReceiverAsSelf = false;
                        }
                    } else {
                        passReceiverAsSelf = false;
                    }
                }
            }
        }
    }

    if (passReceiverAsSelf) {
        callArgs.insert(callArgs.begin(), receiver);
    }

    if (receiver.isObjectId() && !passReceiverAsSelf) {
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

    if (auto methodIdx = vm->getPrototypeMethod(receiver, method_name)) {
        if (auto hostName = vm->getHostFunctionName(*methodIdx)) {
            // Prototype methods are receiver-bound by definition: the
            // interpreter's OBJECT_GET materializes them as
            // allocateBoundMethod(hostfn, receiver), so the host function
            // sees the receiver as its first argument. callArgs does not
            // carry it yet when passReceiverAsSelf was false - insert it,
            // or receiver-dependent builtins blow up (node.keys() reached
            // object.keys with no self and threw "Object.keys() requires
            // object" from JIT-compiled containsYield).
            std::vector<Value> boundArgs;
            boundArgs.reserve(callArgs.size() + 1);
            boundArgs.push_back(receiver);
            boundArgs.insert(boundArgs.end(), callArgs.begin(), callArgs.end());
            Value result = vm->invokeHostFunctionDirect(*hostName, boundArgs);
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
 closure.chunk = chunk;
 closure.chunk_ref = vm->findOwningChunk(chunk);
 closure.upvalues.reserve(target->upvalues.size());

 auto& mainChunk = vm->getMainChunk();
 if (mainChunk && chunk != mainChunk.get()) {
     closure.module_globals = std::make_shared<std::unordered_map<std::string, Value>>(vm->getGlobals());
 }

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
  vm->getHeap().bumpArrayVersion(arr.asArrayId());
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
  if (s->find(*k) != s->end()) {
    (*s)[*k] = val;
  } else {
    s->erase(*k);
  }
  vm->getHeap().bumpSetVersion(setVal.asSetId());
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
  s->erase(*k);
  vm->getHeap().bumpSetVersion(setVal.asSetId());
  return Value::makeNull().rawBits();
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
  if (!v.isStringValId()) return str_bits;
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
  Value result = vm->loadModule(path);
  return result.rawBits();
}

void havel_vm_import_wildcard(void* vm_ptr, uint64_t exports_bits) {
  if (!vm_ptr) return;
  auto* vm = static_cast<VM*>(vm_ptr);
  Value exports;
  std::memcpy(&exports, &exports_bits, sizeof(uint64_t));
  if (!exports.isObjectId()) return;
  auto* obj = vm->getHeap().object(exports.asObjectId());
  if (!obj) return;
  for (const auto& [name, value] : *obj) {
    if (name.empty() || name[0] == '_') continue;
    vm->getGlobals()[name] = value;
  }
}

uint64_t havel_vm_yield_resume(void* vm_ptr, uint64_t co_bits) {
  Value v;
  std::memcpy(&v, &co_bits, sizeof(uint64_t));
  throw JitCoroutineSignal{JitCoroutineSignal::Op::YIELD_RESUME, v};
}

uint64_t havel_vm_go_async(void* vm_ptr, uint64_t fn_bits) {
  if (!vm_ptr) return Value::makeNull().rawBits();
  auto* vm = static_cast<VM*>(vm_ptr);
  Value fn;
  std::memcpy(&fn, &fn_bits, sizeof(uint64_t));
  uint32_t gid = vm->spawnGoroutine(fn, {});
  return Value::makeThreadId(gid).rawBits();
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
  // Return the object reference (not the value) so chained sets on the
  // same instance keep operating on the same instance. See comment in
  // havel_vm_object_set_raw for the rationale.
  return obj_bits;
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
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif

}  // namespace havel::compiler
