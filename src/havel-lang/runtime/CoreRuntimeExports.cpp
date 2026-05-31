#include "havel-lang/compiler/vm/VM.hpp"
#include "havel-lang/compiler/BytecodeOrcJIT.h"

#include <cstdint>
#include <vector>

using havel::compiler::VM;
using havel::compiler::Value;

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

extern "C" uint64_t havel_vm_global_get(void *vm_ptr, uint32_t name_id) {
  auto *vm = static_cast<VM *>(vm_ptr);
  if (!vm) {
    return Value::makeNull().rawBits();
  }
  const auto *chunk = vm->getCurrentChunk();
  if (!chunk || name_id >= chunk->getAllStrings().size()) {
    return Value::makeNull().rawBits();
  }
  auto it = vm->getAllGlobals().find(chunk->getString(name_id));
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
  vm->setGlobal(chunk->getString(name_id), Value::fromRawBits(value));
}
