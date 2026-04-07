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
  Value makeNull() { return Value::makeNull(); }
  Value makeBool(bool v) { return Value::makeBool(v); }
  Value makeNumber(int64_t v) { return Value::makeInt(v); }
  Value makeNumber(double v) { return Value::makeDouble(v); }
  Value makeString(std::string v) {
    auto ref = vm.createRuntimeString(std::move(v));
    return Value::makeStringId(ref.id);
  }
  Value makeObject() { return Value::makeObjectId(vm.createHostObject().id); }
  Value makeArray() { return Value::makeArrayId(vm.createHostArray().id); }
  Value makeFunctionRef(const std::string &name) {
    return Value::makeHostFuncId(vm.getHostFunctionIndex(name));
  }

  // String conversion
  std::string toString(const Value &value) {
    return vm.toString(value);
  }

  // Global scope
  void setGlobal(const std::string &name, Value value) {
    vm.setGlobal(name, std::move(value));
  }

  // Object operations
  void setField(Value obj, const std::string &key,
                Value value) {
    if (!obj.isObjectId()) {
      throw std::runtime_error("VMApi::setField: expected object");
    }
    vm.setHostObjectField(ObjectRef{obj.asObjectId(), true}, key, std::move(value));
  }

  std::vector<std::string> getObjectKeys(Value obj) {
    if (!obj.isObjectId())
      return {};
    return vm.getHostObjectKeys(ObjectRef{obj.asObjectId(), true});
  }

  bool hasField(Value obj, const std::string &key) {
    if (!obj.isObjectId())
      return false;
    return vm.hasHostObjectField(ObjectRef{obj.asObjectId(), true}, key);
  }

  Value getField(Value obj, const std::string &key) {
    if (!obj.isObjectId())
      return Value::makeNull();
    return vm.getHostObjectField(ObjectRef{obj.asObjectId(), true}, key);
  }

  bool deleteField(Value obj, const std::string &key) {
    if (!obj.isObjectId())
      return false;
    return vm.deleteHostObjectField(ObjectRef{obj.asObjectId(), true}, key);
  }

  // Array operations
  void push(Value arr, Value value) {
    if (!arr.isArrayId()) {
      throw std::runtime_error("VMApi::push: expected array");
    }
    vm.pushHostArrayValue(ArrayRef{arr.asArrayId()}, std::move(value));
  }

  size_t getArrayLength(Value arr) {
    if (!arr.isArrayId())
      return 0;
    return vm.getHostArrayLength(ArrayRef{arr.asArrayId()});
  }

  Value getArrayValue(Value arr, size_t index) {
    if (!arr.isArrayId())
      return Value::makeNull();
    return vm.getHostArrayValue(ArrayRef{arr.asArrayId()}, index);
  }

  void setArrayValue(Value arr, size_t index, Value value) {
    if (!arr.isArrayId())
      return;
    vm.setHostArrayValue(ArrayRef{arr.asArrayId()}, index, std::move(value));
  }

  Value popArrayValue(Value arr) {
    if (!arr.isArrayId())
      return Value::makeNull();
    return vm.popHostArrayValue(ArrayRef{arr.asArrayId()});
  }

  void insertArrayValue(Value arr, size_t index, Value value) {
    if (!arr.isArrayId())
      return;
    vm.insertHostArrayValue(ArrayRef{arr.asArrayId()}, index, std::move(value));
  }

  Value removeArrayValue(Value arr, size_t index) {
    if (!arr.isArrayId())
      return Value::makeNull();
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

  Value callFunction(const Value &fn,
                             const std::vector<Value> &args = {}) {
    return vm.callFunction(fn, args);
  }

  // Prototype method registration
  void registerPrototypeMethod(const std::string &typeName,
                               const std::string &methodName,
                               uint32_t funcIndex) {
    vm.registerPrototypeMethod(typeName, methodName, funcIndex);
  }

  // Register prototype method by function name (looks up index)
  void registerPrototypeMethodByName(const std::string &typeName,
                                     const std::string &methodName,
                                     const std::string &funcName) {
    vm.registerPrototypeMethodByName(typeName, methodName, funcName);
  }

  // Callback system
  CallbackId registerCallback(const Value &closure) {
    return vm.registerCallback(closure);
  }

  Value invokeCallback(CallbackId id,
                               const std::vector<Value> &args = {}) {
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
