#pragma once

#include "../core/BytecodeIR.hpp"
#include "VM.hpp"
#include "../../runtime/concurrency/Scheduler.hpp"

#include <stdexcept>
#include <string>
#include <vector>
#include <utility>
#include <functional>

namespace havel::compiler {

using Value = ::havel::core::Value;

struct VMApi {
    VM *vm_;

    VMApi(VM &vm) : vm_(&vm) {}
    VM &vm() const { return *vm_; }

    Value makeNull() const { return Value::makeNull(); }
    Value makeBool(bool v) const { return Value::makeBool(v); }
    Value makeNumber(int64_t v) const { return Value::makeInt(v); }
    Value makeNumber(double v) const { return Value::makeDouble(v); }
    Value makeString(std::string v) const {
        auto ref = vm().createRuntimeString(std::move(v));
        return Value::makeStringId(ref.id);
    }
    Value makeObject() const { return Value::makeObjectId(vm().createHostObject().id); }
    Value makeArray() const { return Value::makeArrayId(vm().createHostArray().id); }
    Value makeFunctionRef(const std::string &name) const {
        return Value::makeHostFuncId(vm().getHostFunctionIndex(name));
    }
    Value makeEnum(uint32_t typeId, uint32_t tag, const std::vector<Value> &payload = {}) const {
        auto ref = vm().createEnum(typeId, tag, payload.size());
        for (size_t i = 0; i < payload.size(); ++i) {
            vm().setEnumPayload(ref, i, payload[i]);
        }
        return Value::makeEnumId(ref.id, typeId);
    }

    std::string toString(const Value &value) const {
        return vm().toString(value);
    }

    const std::string* getStringPtr(const Value &value) const {
        return vm().getStringPtr(value);
    }

    std::string resolveString(const Value &value) const {
        return vm().resolveStringKey(value);
    }

    void setGlobal(const std::string &name, Value value) const {
        vm().setGlobal(name, std::move(value));
    }

    void setField(Value obj, const std::string &key,
                  Value value) const {
        if (!obj.isObjectId()) {
            throw std::runtime_error("VMApi::setField: expected object");
        }
        vm().setHostObjectField(ObjectRef{obj.asObjectId(), true}, key, std::move(value));
    }

    std::vector<std::string> getObjectKeys(Value obj) const {
        if (!obj.isObjectId())
            return {};
        return vm().getHostObjectKeys(ObjectRef{obj.asObjectId(), true});
    }

    bool hasField(Value obj, const std::string &key) const {
        if (!obj.isObjectId())
            return false;
        return vm().hasHostObjectField(ObjectRef{obj.asObjectId(), true}, key);
    }

    Value getField(Value obj, const std::string &key) const {
        if (!obj.isObjectId())
            return Value::makeNull();
        return vm().getHostObjectField(ObjectRef{obj.asObjectId(), true}, key);
    }

    bool deleteField(Value obj, const std::string &key) const {
        if (!obj.isObjectId())
            return false;
        return vm().deleteHostObjectField(ObjectRef{obj.asObjectId(), true}, key);
    }

    void push(Value arr, Value value) const {
        if (!arr.isArrayId()) {
            throw std::runtime_error("VMApi::push: expected array");
        }
        vm().pushHostArrayValue(ArrayRef{arr.asArrayId()}, std::move(value));
    }

    Value pop(Value arr) const {
        if (!arr.isArrayId()) {
            throw std::runtime_error("VMApi::pop: expected array");
        }
        return vm().popHostArrayValue(ArrayRef{arr.asArrayId()});
    }

    uint32_t length(Value arr) const {
        if (arr.isArrayId()) {
            return (uint32_t)vm().getHostArrayLength(ArrayRef{arr.asArrayId()});
        } else if (arr.isStringId()) {
            return (uint32_t)vm().getRuntimeStringLength(StringRef{arr.asStringId()});
        }
        return 0;
    }

    Value getAt(Value arr, uint32_t index) const {
        if (!arr.isArrayId())
            return Value::makeNull();
        return vm().getHostArrayValue(ArrayRef{arr.asArrayId()}, index);
    }

    void setAt(Value arr, uint32_t index, Value value) const {
        if (!arr.isArrayId())
            return;
        vm().setHostArrayValue(ArrayRef{arr.asArrayId()}, index, std::move(value));
    }

    template <typename F>
    void registerFunction(const std::string &name, F func) const {
        vm().registerHostFunction(name, std::function<Value(const std::vector<Value> &)>(std::move(func)));
    }

    template <typename F>
    void registerFunction(const std::string &name, uint32_t arity, F func) const {
        vm().registerHostFunction(name, arity, std::function<Value(const std::vector<Value> &)>(std::move(func)));
    }

    Value invoke(Value callee, const std::vector<Value> &args) const {
        return vm().callFunction(callee, args);
    }

    bool toBool(Value v) const { return vm().toBoolPublic(v); }

    Value callFunction(const Value &fn,
                       const std::vector<Value> &args = {}) const {
        return vm().callFunction(fn, args);
    }

    void registerPrototypeMethod(const std::string &typeName,
                                 const std::string &methodName,
                                 uint32_t funcIndex) const {
        vm().registerPrototypeMethod(typeName, methodName, funcIndex);
    }

    template <typename F>
    void registerPrototypeMethod(const std::string &typeName,
                                 const std::string &methodName,
                                 uint32_t arity, F func) const {
        std::string fullName = typeName + "." + methodName;
        vm().registerHostFunction(fullName, arity, std::move(func));
        vm().registerPrototypeMethodByName(typeName, methodName, fullName);
    }

    void registerPrototypeMethodByName(const std::string &typeName,
                                       const std::string &methodName,
                                       const std::string &funcName) const {
        vm().registerPrototypeMethodByName(typeName, methodName, funcName);
    }

    CallbackId registerCallback(const Value &closure) const {
        return vm().registerCallback(closure);
    }

    Value invokeCallback(CallbackId id,
                         const std::vector<Value> &args = {}) const {
        return vm().invokeCallback(id, args);
    }

    void releaseCallback(CallbackId id) const { vm().releaseCallback(id); }

    bool isValidCallback(CallbackId id) const { return vm().isValidCallback(id); }

    uint32_t spawnGoroutine(const Value &callee, const std::vector<Value> &args = {}) const {
        return vm().spawnGoroutine(callee, args);
    }

    void requestSuspension(uint8_t reason, void *context = nullptr) const {
        vm().requestSuspension(reason, context);
    }

    bool hasScheduler() const {
        return vm().getScheduler() != nullptr;
    }

  bool isInGoroutine() const {
    auto *sched = vm().getScheduler();
    return sched && sched->current() != nullptr;
  }

  template<typename F>
  void deferToVM(F&& fn) const {
    auto *sched = vm().getScheduler();
    if (!sched) {
      fn();
      return;
    }
    sched->deferToVM(std::forward<F>(fn));
  }

    VMImage createImage(int width, int height, int stride, PixelFormat format,
                        const uint8_t *data) const {
        return vm().createImage(width, height, stride, format, data);
    }

    VMImage createImageFromRGBA(int width, int height,
                                const std::vector<uint8_t> &rgbaData) const {
        return vm().createImageFromRGBA(width, height, rgbaData);
    }

    uint32_t registerEnumType(const std::string &name, const std::vector<std::string> &variants) const {
        return vm().registerEnumType(name, variants);
    }

    uint32_t getEnumTag(Value val) const {
        if (!val.isEnumId()) return 0;
        return vm().getEnumTag(EnumRef{val.asEnumId()});
    }

    Value getEnumPayload(Value val, size_t index) const {
        if (!val.isEnumId()) return Value::makeNull();
        return vm().getEnumPayload(EnumRef{val.asEnumId()}, index);
    }

    uint32_t getEnumValueCount(Value val) const {
        if (!val.isEnumId()) return 0;
        return vm().getEnumPayloadCount(EnumRef{val.asEnumId()});
    }

    uint32_t getEnumPayloadCount(Value val) const {
        if (!val.isEnumId()) return 0;
        return vm().getEnumPayloadCount(EnumRef{val.asEnumId()});
    }

    std::string getEnumTypeName(uint32_t typeId) const {
        return vm().getEnumTypeName(typeId);
    }

    std::string getEnumVariantName(uint32_t typeId, uint32_t tag) const {
        return vm().getEnumVariantName(typeId, tag);
    }

    uint32_t getEnumTypeVariantCount(uint32_t typeId) const {
        return vm().getEnumTypeVariantCount(typeId);
    }
};

} // namespace havel::compiler
