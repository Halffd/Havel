#include "Havel.hpp"
#include "havel-lang/core/Value.hpp"
#include "havel-lang/compiler/vm/VM.hpp"
#include "havel-lang/compiler/core/Pipeline.hpp"
#include "havel-lang/compiler/gc/GC.hpp"
#include "havel-lang/runtime/HavelEngine.hpp"

#include <sstream>
#include <fstream>

namespace havel {

using CoreValue = ::havel::core::Value;
using CompilerVM = ::havel::compiler::VM;

struct Value::Impl {
    CoreValue internal;
    CompilerVM* vm = nullptr;
    std::string stringContent;
    bool needsIntern = false;

    Impl() = default;
    explicit Impl(CoreValue v, CompilerVM* vmPtr = nullptr)
        : internal(std::move(v)), vm(vmPtr) {}
    explicit Impl(std::string s, CompilerVM* vmPtr = nullptr)
        : vm(vmPtr), stringContent(std::move(s)), needsIntern(true) {}
};

namespace internal {

static Value::Type classifyType(const CoreValue& v) {
    if (v.isNull()) return Value::Type::Nil;
    if (v.isBool()) return Value::Type::Bool;
    if (v.isInt()) return Value::Type::Int;
    if (v.isDouble()) return Value::Type::Float;
    if (v.isStringId() || v.isStringValId()) return Value::Type::String;
    if (v.isArrayId()) return Value::Type::Array;
    if (v.isObjectId()) return Value::Type::Object;
    if (v.isClosureId() || v.isHostFuncId()) return Value::Type::Function;
    return Value::Type::Error;
}

static Value toPublicValue(CoreValue internal, CompilerVM* vm = nullptr) {
    Value pub;
    pub.type = classifyType(internal);
    pub.impl = std::make_shared<Value::Impl>(std::move(internal), vm);
    if (pub.type == Value::Type::String && vm) {
        auto* ptr = vm->getStringPtr(pub.impl->internal);
        if (ptr) pub.impl->stringContent = *ptr;
    }
    return pub;
}

static CoreValue toCoreValue(const Value& pub) {
    if (!pub.impl) return CoreValue::makeNull();
    if (pub.impl->needsIntern && pub.impl->vm) {
        auto* vm = pub.impl->vm;
        auto ref = vm->heap_.allocateString(pub.impl->stringContent);
        pub.impl->internal = CoreValue::makeStringId(ref.id);
        pub.impl->needsIntern = false;
    }
    return pub.impl->internal;
}

static CompilerVM* vmFrom(const Value& pub) {
    if (!pub.impl) return nullptr;
    return pub.impl->vm;
}

}

Value::Value() : type(Type::Nil), impl(std::make_shared<Impl>()) {}
Value::Value(std::nullptr_t) : type(Type::Nil), impl(std::make_shared<Impl>()) {}

Value::Value(bool b) : type(Type::Bool), impl(std::make_shared<Impl>(CoreValue::makeBool(b))) {}

Value::Value(int n) : type(Type::Int), impl(std::make_shared<Impl>(CoreValue::makeInt(n))) {}

Value::Value(int64_t n) : type(Type::Int), impl(std::make_shared<Impl>(CoreValue::makeInt(static_cast<int64_t>(n)))) {}

Value::Value(double n) : type(Type::Float), impl(std::make_shared<Impl>(CoreValue::makeDouble(n))) {}

Value::Value(const std::string& s) : type(Type::String), impl(std::make_shared<Impl>(s)) {}

Value::Value(const char* s) : type(Type::String), impl(std::make_shared<Impl>(std::string(s))) {}

bool Value::asBool() const {
    if (!impl) return false;
    if (impl->internal.isBool()) return impl->internal.asBool();
    if (impl->internal.isInt()) return impl->internal.asInt64() != 0;
    if (impl->internal.isDouble()) return impl->internal.asDouble() != 0.0;
    return false;
}

int64_t Value::asInt() const {
    if (!impl) return 0;
    if (impl->internal.isInt()) return impl->internal.asInt64();
    if (impl->internal.isDouble()) return static_cast<int64_t>(impl->internal.asDouble());
    if (impl->internal.isBool()) return impl->internal.asBool() ? 1 : 0;
    return 0;
}

double Value::asFloat() const {
    if (!impl) return 0.0;
    if (impl->internal.isDouble()) return impl->internal.asDouble();
    if (impl->internal.isInt()) return static_cast<double>(impl->internal.asInt64());
    return 0.0;
}

double Value::asNumber() const {
    if (impl && impl->internal.isDouble()) return impl->internal.asDouble();
    if (impl && impl->internal.isInt()) return static_cast<double>(impl->internal.asInt64());
    return 0.0;
}

std::string Value::asString() const {
    if (!impl) return "";
    if (impl->needsIntern) return impl->stringContent;
    if (impl->internal.isStringId() || impl->internal.isStringValId()) {
        auto* vmPtr = impl->vm;
        if (vmPtr) {
            auto* ptr = vmPtr->getStringPtr(impl->internal);
            if (ptr) return *ptr;
        }
        return impl->internal.toString();
    }
    return toString();
}

bool Value::isTruthy() const {
    if (type == Type::Nil) return false;
    if (type == Type::Bool) return asBool();
    if (type == Type::Int) return asInt() != 0;
    if (type == Type::Float) return asFloat() != 0.0;
    if (type == Type::String) return !asString().empty();
    return true;
}

std::string Value::toString() const {
    if (!impl) return "nil";
    switch (type) {
    case Type::Nil: return "nil";
    case Type::Bool: return asBool() ? "true" : "false";
    case Type::Int: return std::to_string(asInt());
    case Type::Float: return std::to_string(asFloat());
    case Type::String: return asString();
    case Type::Array: return "<array>";
    case Type::Object: return "<object>";
    case Type::Function: return "<function>";
    case Type::Error: return "<error>";
    }
    return "<unknown>";
}

struct VM::Impl {
    std::unique_ptr<HavelEngine> engine;
    std::string lastError;
    bool initialized = false;

    Impl() = default;
};

VM::VM() : impl(std::make_unique<Impl>()) {
    impl->engine = std::make_unique<HavelEngine>();
    impl->engine->initializeMinimal();
    impl->initialized = true;
}

VM::VM(bool leanStartup) : impl(std::make_unique<Impl>()) {
    EngineConfig config;
    config.leanMinimalStartup = leanStartup;
    impl->engine = std::make_unique<HavelEngine>(config);
    if (leanStartup) {
        impl->engine->initializeMinimal();
    } else {
        auto io = std::make_shared<IO>();
        auto hostAPI = std::make_shared<HostAPI>(io.get(), nullptr, Configs::Get());
        impl->engine->initializeFull(hostAPI, leanStartup);
    }
    impl->initialized = true;
}

VM::~VM() = default;

VM::VM(VM&&) noexcept = default;
VM& VM::operator=(VM&&) noexcept = default;

Result<Value> VM::load(const std::string& code, const std::string& sourceName) {
    if (!impl->initialized || !impl->engine) {
        return Result<Value>::err_result("VM not initialized");
    }

    try {
        CoreValue result = impl->engine->execute(code, "__main__", sourceName);
        auto* vm = impl->engine->vm();
        return Result<Value>::ok_result(internal::toPublicValue(std::move(result), vm));
    } catch (const std::exception& e) {
        impl->lastError = e.what();
        return Result<Value>::err_result(e.what());
    }
}

Result<Value> VM::loadFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        return Result<Value>::err_result("Cannot open file: " + path);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return load(buffer.str(), path);
}

Result<Value> VM::call(const std::string& funcName, const std::vector<Value>& args) {
    if (!impl->initialized || !impl->engine) {
        return Result<Value>::err_result("VM not initialized");
    }

    auto* vm = impl->engine->vm();
    if (!vm) {
        return Result<Value>::err_result("VM not available");
    }

    try {
        auto it = vm->globals.find(funcName);
        if (it == vm->globals.end()) {
            auto hostIt = vm->host_function_globals_.find(funcName);
            if (hostIt == vm->host_function_globals_.end()) {
                return Result<Value>::err_result("Function not found: " + funcName);
            }
            CoreValue callee = hostIt->second;
            std::vector<CoreValue> coreArgs;
            for (const auto& arg : args) {
                coreArgs.push_back(internal::toCoreValue(arg));
            }
            CoreValue result = vm->callFunctionSync(callee, coreArgs);
            return Result<Value>::ok_result(internal::toPublicValue(std::move(result), vm));
        }

        CoreValue callee = it->second;
        std::vector<CoreValue> coreArgs;
        for (const auto& arg : args) {
            coreArgs.push_back(internal::toCoreValue(arg));
        }

        CoreValue result = vm->callFunctionSync(callee, coreArgs);
        return Result<Value>::ok_result(internal::toPublicValue(std::move(result), vm));
    } catch (const std::exception& e) {
        impl->lastError = e.what();
        return Result<Value>::err_result(e.what());
    }
}

Result<Value> VM::call(const Value& func, const std::vector<Value>& args) {
    if (!impl->initialized || !impl->engine) {
        return Result<Value>::err_result("VM not initialized");
    }

    auto* vm = impl->engine->vm();
    if (!vm) {
        return Result<Value>::err_result("VM not available");
    }

    try {
        CoreValue callee = internal::toCoreValue(func);
        std::vector<CoreValue> coreArgs;
        for (const auto& arg : args) {
            coreArgs.push_back(internal::toCoreValue(arg));
        }

        CoreValue result = vm->callFunctionSync(callee, coreArgs);
        return Result<Value>::ok_result(internal::toPublicValue(std::move(result), vm));
    } catch (const std::exception& e) {
        impl->lastError = e.what();
        return Result<Value>::err_result(e.what());
    }
}

Value VM::getGlobal(const std::string& name) {
    if (!impl->initialized || !impl->engine) return Value();

    auto* vm = impl->engine->vm();
    if (!vm) return Value();

    auto it = vm->globals.find(name);
    if (it != vm->globals.end()) {
        return internal::toPublicValue(it->second, vm);
    }
    auto hostIt = vm->host_function_globals_.find(name);
    if (hostIt != vm->host_function_globals_.end()) {
        return internal::toPublicValue(hostIt->second, vm);
    }
    return Value();
}

void VM::setGlobal(const std::string& name, const Value& value) {
    if (!impl->initialized || !impl->engine) return;

    auto* vm = impl->engine->vm();
    if (!vm) return;

    vm->globals[name] = internal::toCoreValue(value);
}

void VM::registerFn(const std::string& name, NativeFunction fn) {
    if (!impl->initialized || !impl->engine) return;

    auto* vm = impl->engine->vm();
    if (!vm) return;

    vm->registerHostFunction(name, [fn, this, vm](const std::vector<CoreValue>& coreArgs) -> CoreValue {
        std::vector<Value> pubArgs;
        pubArgs.reserve(coreArgs.size());
        for (const auto& arg : coreArgs) {
            pubArgs.push_back(internal::toPublicValue(arg, vm));
        }

        Value result = fn(*this, pubArgs);
        return internal::toCoreValue(result);
    });
}

void VM::registerModule(const std::string& name, const std::vector<std::pair<std::string, NativeFunction>>& fns) {
    if (!impl->initialized || !impl->engine) return;

    auto* vm = impl->engine->vm();
    if (!vm) return;

    uint32_t objId = vm->heap_.allocateObject().id;
    auto* obj = vm->heap_.object(objId);

    for (const auto& [fnName, fn] : fns) {
        std::string qualName = name + "." + fnName;

        vm->registerHostFunction(qualName, [fn, this, vm](const std::vector<CoreValue>& coreArgs) -> CoreValue {
            std::vector<Value> pubArgs;
            pubArgs.reserve(coreArgs.size());
            for (const auto& arg : coreArgs) {
                pubArgs.push_back(internal::toPublicValue(arg, vm));
            }
            Value result = fn(*this, pubArgs);
            return internal::toCoreValue(result);
        });

        auto it = vm->host_function_globals_.find(qualName);
        if (it != vm->host_function_globals_.end()) {
            obj->set(fnName, it->second);
        }
    }

    vm->globals[name] = CoreValue::makeObjectId(objId);
}

std::string VM::getError() const {
    return impl->lastError;
}

void VM::clearError() {
    impl->lastError.clear();
}

Array::Array() : value() {}

Array::Array(const Value& v) : value(v) {}

size_t Array::size() const {
    if (!value.impl || !value.impl->internal.isArrayId()) return 0;
    auto* vm = internal::vmFrom(value);
    if (!vm) return 0;
    auto* arr = vm->heap_.array(value.impl->internal.asArrayId());
    return arr ? arr->size() : 0;
}

Value Array::get(size_t index) const {
    if (!value.impl || !value.impl->internal.isArrayId()) return Value();
    auto* vm = internal::vmFrom(value);
    if (!vm) return Value();
    auto* arr = vm->heap_.array(value.impl->internal.asArrayId());
    if (!arr || index >= arr->size()) return Value();
    return internal::toPublicValue((*arr)[index], vm);
}

void Array::set(size_t index, const Value& val) {
    if (!value.impl || !value.impl->internal.isArrayId()) return;
    auto* vm = internal::vmFrom(value);
    if (!vm) return;
    auto* arr = vm->heap_.array(value.impl->internal.asArrayId());
    if (!arr || index >= arr->size()) return;
    (*arr)[index] = internal::toCoreValue(val);
}

void Array::push(const Value& val) {
    if (!value.impl || !value.impl->internal.isArrayId()) return;
    auto* vm = internal::vmFrom(value);
    if (!vm) return;
    auto* arr = vm->heap_.array(value.impl->internal.asArrayId());
    if (!arr) return;
    arr->push_back(internal::toCoreValue(val));
}

Value Array::pop() {
    if (!value.impl || !value.impl->internal.isArrayId()) return Value();
    auto* vm = internal::vmFrom(value);
    if (!vm) return Value();
    auto* arr = vm->heap_.array(value.impl->internal.asArrayId());
    if (!arr || arr->empty()) return Value();
    CoreValue back = arr->back();
    arr->pop_back();
    return internal::toPublicValue(std::move(back), vm);
}

Object::Object() : value() {}

Object::Object(const Value& v) : value(v) {}

Value Object::get(const std::string& key) const {
    if (!value.impl || !value.impl->internal.isObjectId()) return Value();
    auto* vm = internal::vmFrom(value);
    if (!vm) return Value();
    auto* obj = vm->heap_.object(value.impl->internal.asObjectId());
    if (!obj) return Value();
    auto* val = obj->get(key);
    return val ? internal::toPublicValue(*val, vm) : Value();
}

void Object::set(const std::string& key, const Value& val) {
    if (!value.impl || !value.impl->internal.isObjectId()) return;
    auto* vm = internal::vmFrom(value);
    if (!vm) return;
    auto* obj = vm->heap_.object(value.impl->internal.asObjectId());
    if (!obj) return;
    obj->set(key, internal::toCoreValue(val));
}

bool Object::has(const std::string& key) const {
    if (!value.impl || !value.impl->internal.isObjectId()) return false;
    auto* vm = internal::vmFrom(value);
    if (!vm) return false;
    auto* obj = vm->heap_.object(value.impl->internal.asObjectId());
    return obj && obj->get(key) != nullptr;
}

std::vector<std::string> Object::keys() const {
    if (!value.impl || !value.impl->internal.isObjectId()) return {};
    auto* vm = internal::vmFrom(value);
    if (!vm) return {};
    auto* obj = vm->heap_.object(value.impl->internal.asObjectId());
    return obj ? obj->getKeys() : std::vector<std::string>{};
}

} // namespace havel
