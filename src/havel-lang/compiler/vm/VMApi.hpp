#pragma once

#include "../core/BytecodeIR.hpp"
#include "VM.hpp"

#include <stdexcept>
#include <string>
#include <vector>
#include <utility>
#include <functional>

namespace havel::compiler {

using Value = ::havel::core::Value;

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
  Value makeEnum(uint32_t typeId, uint32_t tag, const std::vector<Value> &payload = {}) {
    auto ref = vm.createEnum(typeId, tag, payload.size());
    for (size_t i = 0; i < payload.size(); ++i) {
      vm.setEnumPayload(ref, i, payload[i]);
    }
    return Value::makeEnumId(ref.id, typeId);
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

  Value pop(Value arr) {
    if (!arr.isArrayId()) {
      throw std::runtime_error("VMApi::pop: expected array");
    }
    return vm.popHostArrayValue(ArrayRef{arr.asArrayId()});
  }

  uint32_t length(Value arr) {
    if (arr.isArrayId()) {
      return (uint32_t)vm.getHostArrayLength(ArrayRef{arr.asArrayId()});
    } else if (arr.isStringId()) {
      return (uint32_t)vm.getRuntimeStringLength(StringRef{arr.asStringId()});
    }
    return 0;
  }

  Value getAt(Value arr, uint32_t index) {
    if (!arr.isArrayId())
      return Value::makeNull();
    return vm.getHostArrayValue(ArrayRef{arr.asArrayId()}, index);
  }

  void setAt(Value arr, uint32_t index, Value value) {
    if (!arr.isArrayId())
      return;
    vm.setHostArrayValue(ArrayRef{arr.asArrayId()}, index, std::move(value));
  }

  // Function registration
  void registerFunction(const std::string &name,
                        std::function<Value(const std::vector<Value> &)> func) {
    vm.registerHostFunction(name, std::move(func));
  }

  template <typename F>
  void registerFunction(const std::string &name, uint32_t arity, F func) {
    vm.registerHostFunction(name, arity, std::move(func));
  }

  Value invoke(Value callee, const std::vector<Value> &args) {
    return vm.callFunction(callee, args);
  }

  bool toBool(Value v) { return vm.toBoolPublic(v); }

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

  template <typename F>
  void registerPrototypeMethod(const std::string &typeName,
                               const std::string &methodName,
                               uint32_t arity, F func) {
    std::string fullName = typeName + "." + methodName;
    vm.registerHostFunction(fullName, arity, std::move(func));
    vm.registerPrototypeMethodByName(typeName, methodName, fullName);
  }

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

  uint32_t spawnGoroutine(const Value &callee, const std::vector<Value> &args = {}) {
    return vm.spawnGoroutine(callee, args);
  }

  // Image helpers
  VMImage createImage(int width, int height, int stride, PixelFormat format,
                      const uint8_t *data) {
    return vm.createImage(width, height, stride, format, data);
  }

  VMImage createImageFromRGBA(int width, int height,
                              const std::vector<uint8_t> &rgbaData) {
    return vm.createImageFromRGBA(width, height, rgbaData);
  }

  // Enum operations
  uint32_t registerEnumType(const std::string &name, const std::vector<std::string> &variants) {
    return vm.registerEnumType(name, variants);
  }

  uint32_t getEnumTag(Value val) {
    if (!val.isEnumId()) return 0;
    return vm.getEnumTag(EnumRef{val.asEnumId()});
  }

  Value getEnumPayload(Value val, size_t index) {
    if (!val.isEnumId()) return Value::makeNull();
    return vm.getEnumPayload(EnumRef{val.asEnumId()}, index);
  }

  uint32_t getEnumValueCount(Value val) {
    if (!val.isEnumId()) return 0;
    return vm.getEnumPayloadCount(EnumRef{val.asEnumId()});
  }

  uint32_t getEnumPayloadCount(Value val) {
    if (!val.isEnumId()) return 0;
    return vm.getEnumPayloadCount(EnumRef{val.asEnumId()});
  }

  std::string getEnumTypeName(uint32_t typeId) {
    return vm.getEnumTypeName(typeId);
  }

  std::string getEnumVariantName(uint32_t typeId, uint32_t tag) {
    return vm.getEnumVariantName(typeId, tag);
  }

  uint32_t getEnumTypeVariantCount(uint32_t typeId) {
    return vm.getEnumTypeVariantCount(typeId);
  }
};

} // namespace havel::compiler
