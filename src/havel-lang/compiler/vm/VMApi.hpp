#pragma once

#include "havel-lang/compiler/core/BytecodeIR.hpp"
#include "VM.hpp"

#include <stdexcept>
#include <string>
#include <vector>

namespace havel::compiler {

/**
 * VMApi - Stable API layer for stdlib modules
 *
 * PURPOSE:
 * - Decouple stdlib modules from VM internals
 * - Provide stable interface that won't break when VM changes
 * - Allow VM rewrites without touching all modules
 *
 * USAGE:
 *   VMApi api{ *ctx.vm };
 *   registerMath(api);
 *
 * ARCHITECTURE:
 * - Modules depend on VMApi, NOT VM
 * - VMApi wraps VM operations
 * - One place (HostBridge) adapts VM → API
 */
struct VMApi {
  VM &vm;

  // Value creation
  BytecodeValue makeNull() { return BytecodeValue::makeNull(); }
  BytecodeValue makeBool(bool v) { return BytecodeValue::makeBool(v); }
  BytecodeValue makeNumber(int64_t v) { return BytecodeValue::makeInt(v); }
  BytecodeValue makeNumber(double v) { return BytecodeValue::makeDouble(v); }
  BytecodeValue makeString(std::string v) {
    // Strings are stored as string IDs - register in pool and return ID
    // For now, use a placeholder - the VM's string pool handles this
    (void)v;
    return BytecodeValue::makeNull(); // TODO: integrate with string pool
  }
  BytecodeValue makeObject() { return BytecodeValue::makeObjectId(vm.createHostObject().id); }
  BytecodeValue makeArray() { return BytecodeValue::makeArrayId(vm.createHostArray().id); }
  BytecodeValue makeFunctionRef(const std::string &name) {
    return BytecodeValue::makeHostFuncId(0); // TODO: register and get function ID
    (void)name;
  }

  // Global scope
  void setGlobal(const std::string &name, BytecodeValue value) {
    vm.setGlobal(name, std::move(value));
  }

  // Object operations
  void setField(BytecodeValue obj, const std::string &key,
                BytecodeValue value) {
    if (!obj.isObjectId()) {
      throw std::runtime_error("VMApi::setField: expected object");
    }
    vm.setHostObjectField(ObjectRef{obj.asObjectId(), true}, key, std::move(value));
  }

  std::vector<std::string> getObjectKeys(BytecodeValue obj) {
    if (!obj.isObjectId())
      return {};
    return vm.getHostObjectKeys(ObjectRef{obj.asObjectId(), true});
  }

  bool hasField(BytecodeValue obj, const std::string &key) {
    if (!obj.isObjectId())
      return false;
    return vm.hasHostObjectField(ObjectRef{obj.asObjectId(), true}, key);
  }

  BytecodeValue getField(BytecodeValue obj, const std::string &key) {
    if (!obj.isObjectId())
      return BytecodeValue::makeNull();
    return vm.getHostObjectField(ObjectRef{obj.asObjectId(), true}, key);
  }

  bool deleteField(BytecodeValue obj, const std::string &key) {
    if (!obj.isObjectId())
      return false;
    return vm.deleteHostObjectField(ObjectRef{obj.asObjectId(), true}, key);
  }

  // Array operations
  void push(BytecodeValue arr, BytecodeValue value) {
    if (!arr.isArrayId()) {
      throw std::runtime_error("VMApi::push: expected array");
    }
    vm.pushHostArrayValue(ArrayRef{arr.asArrayId()}, std::move(value));
  }

  size_t getArrayLength(BytecodeValue arr) {
    if (!arr.isArrayId())
      return 0;
    return vm.getHostArrayLength(ArrayRef{arr.asArrayId()});
  }

  BytecodeValue getArrayValue(BytecodeValue arr, size_t index) {
    if (!arr.isArrayId())
      return BytecodeValue::makeNull();
    return vm.getHostArrayValue(ArrayRef{arr.asArrayId()}, index);
  }

  void setArrayValue(BytecodeValue arr, size_t index, BytecodeValue value) {
    if (!arr.isArrayId())
      return;
    vm.setHostArrayValue(ArrayRef{arr.asArrayId()}, index, std::move(value));
  }

  BytecodeValue popArrayValue(BytecodeValue arr) {
    if (!arr.isArrayId())
      return BytecodeValue::makeNull();
    return vm.popHostArrayValue(ArrayRef{arr.asArrayId()});
  }

  void insertArrayValue(BytecodeValue arr, size_t index, BytecodeValue value) {
    if (!arr.isArrayId())
      return;
    vm.insertHostArrayValue(ArrayRef{arr.asArrayId()}, index, std::move(value));
  }

  BytecodeValue removeArrayValue(BytecodeValue arr, size_t index) {
    if (!arr.isArrayId())
      return BytecodeValue::makeNull();
    return vm.removeHostArrayValue(ArrayRef{arr.asArrayId()}, index);
  }

  // Function registration
  void registerFunction(const std::string &name, BytecodeHostFunction fn) {
    vm.registerHostFunction(name, std::move(fn));
  }

  void registerFunction(const std::string &name, size_t arity,
                        BytecodeHostFunction fn) {
    vm.registerHostFunction(name, arity, std::move(fn));
  }

  BytecodeValue callFunction(const BytecodeValue &fn,
                             const std::vector<BytecodeValue> &args = {}) {
    return vm.callFunction(fn, args);
  }

  // Prototype method registration
  void registerPrototypeMethod(const std::string &typeName,
                               const std::string &methodName,
                               const std::string &functionName) {
    vm.registerPrototypeMethod(typeName, methodName,
                               HostFunctionRef{.name = functionName});
  }

  // Callback system
  CallbackId registerCallback(const BytecodeValue &closure) {
    return vm.registerCallback(closure);
  }

  BytecodeValue invokeCallback(CallbackId id,
                               const std::vector<BytecodeValue> &args = {}) {
    return vm.invokeCallback(id, args);
  }

  void releaseCallback(CallbackId id) { vm.releaseCallback(id); }

  bool isValidCallback(CallbackId id) const { return vm.isValidCallback(id); }

  // Image helpers
  VMImage createImage(int width, int height, int stride, PixelFormat format,
                      const uint8_t *data) {
    return vm.createImage(width, height, stride, format, data);
  }

  VMImage createImageFromRGBA(int width, int height,
                              const std::vector<uint8_t> &rgbaData) {
    return vm.createImageFromRGBA(width, height, rgbaData);
  }
};

} // namespace havel::compiler
