#pragma once

#include "BytecodeIR.hpp"
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
  BytecodeValue makeNull() { return BytecodeValue(nullptr); }
  BytecodeValue makeBool(bool v) { return BytecodeValue(v); }
  BytecodeValue makeNumber(int64_t v) { return BytecodeValue(v); }
  BytecodeValue makeNumber(double v) { return BytecodeValue(v); }
  BytecodeValue makeString(std::string v) {
    return BytecodeValue(std::move(v));
  }
  BytecodeValue makeObject() { return BytecodeValue(vm.createHostObject()); }
  BytecodeValue makeArray() { return BytecodeValue(vm.createHostArray()); }
  BytecodeValue makeFunctionRef(const std::string &name) {
    return BytecodeValue(HostFunctionRef{.name = name});
  }

  // Global scope
  void setGlobal(const std::string &name, BytecodeValue value) {
    vm.setGlobal(name, std::move(value));
  }

  // Object operations
  void setField(BytecodeValue obj, const std::string &key,
                BytecodeValue value) {
    if (!std::holds_alternative<ObjectRef>(obj)) {
      throw std::runtime_error("VMApi::setField: expected object");
    }
    vm.setHostObjectField(std::get<ObjectRef>(obj), key, std::move(value));
  }

  std::vector<std::string> getObjectKeys(BytecodeValue obj) {
    if (!std::holds_alternative<ObjectRef>(obj))
      return {};
    return vm.getHostObjectKeys(std::get<ObjectRef>(obj));
  }

  bool hasField(BytecodeValue obj, const std::string &key) {
    if (!std::holds_alternative<ObjectRef>(obj))
      return false;
    return vm.hasHostObjectField(std::get<ObjectRef>(obj), key);
  }

  bool deleteField(BytecodeValue obj, const std::string &key) {
    if (!std::holds_alternative<ObjectRef>(obj))
      return false;
    return vm.deleteHostObjectField(std::get<ObjectRef>(obj), key);
  }

  // Array operations
  void push(BytecodeValue arr, BytecodeValue value) {
    if (!std::holds_alternative<ArrayRef>(arr)) {
      throw std::runtime_error("VMApi::push: expected array");
    }
    vm.pushHostArrayValue(std::get<ArrayRef>(arr), std::move(value));
  }

  size_t getArrayLength(BytecodeValue arr) {
    if (!std::holds_alternative<ArrayRef>(arr))
      return 0;
    return vm.getHostArrayLength(std::get<ArrayRef>(arr));
  }

  BytecodeValue getArrayValue(BytecodeValue arr, size_t index) {
    if (!std::holds_alternative<ArrayRef>(arr))
      return BytecodeValue(nullptr);
    return vm.getHostArrayValue(std::get<ArrayRef>(arr), index);
  }

  void setArrayValue(BytecodeValue arr, size_t index, BytecodeValue value) {
    if (!std::holds_alternative<ArrayRef>(arr))
      return;
    vm.setHostArrayValue(std::get<ArrayRef>(arr), index, std::move(value));
  }

  BytecodeValue popArrayValue(BytecodeValue arr) {
    if (!std::holds_alternative<ArrayRef>(arr))
      return BytecodeValue(nullptr);
    return vm.popHostArrayValue(std::get<ArrayRef>(arr));
  }

  void insertArrayValue(BytecodeValue arr, size_t index, BytecodeValue value) {
    if (!std::holds_alternative<ArrayRef>(arr))
      return;
    vm.insertHostArrayValue(std::get<ArrayRef>(arr), index, std::move(value));
  }

  BytecodeValue removeArrayValue(BytecodeValue arr, size_t index) {
    if (!std::holds_alternative<ArrayRef>(arr))
      return BytecodeValue(nullptr);
    return vm.removeHostArrayValue(std::get<ArrayRef>(arr), index);
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
