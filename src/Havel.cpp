// Havel.cpp
// Implementation of embeddable C++ API

#include "Havel.hpp"
#include "havel-lang/runtime/Interpreter.hpp"
#include "havel-lang/runtime/Environment.hpp"
#include "havel-lang/types/HavelType.hpp"
#include <sstream>
#include <fstream>

namespace havel {

// ============================================================================
// Internal helpers
// ============================================================================

namespace internal {

// Convert internal HavelValue to public Value
Value toPublicValue(const ::havel::HavelValue& internal) {
    Value publicVal;
    
    // Check type and set accordingly
    if (internal.is<std::nullptr_t>()) {
        publicVal.type = Value::Type::Nil;
    } else if (internal.is<bool>()) {
        publicVal.type = Value::Type::Bool;
    } else if (internal.is<int>() || internal.is<double>()) {
        publicVal.type = Value::Type::Number;
    } else if (internal.is<std::string>()) {
        publicVal.type = Value::Type::String;
    } else if (internal.is<::havel::HavelArray>()) {
        publicVal.type = Value::Type::Array;
    } else if (internal.is<::havel::HavelObject>()) {
        publicVal.type = Value::Type::Object;
    } else if (internal.is<::havel::HavelStructInstance>()) {
        publicVal.type = Value::Type::Struct;
    } else if (internal.is<::havel::BuiltinFunction>() || 
               internal.is<std::shared_ptr<::havel::HavelFunction>>()) {
        publicVal.type = Value::Type::Function;
    } else if (internal.is<::havel::HavelRuntimeError>()) {
        publicVal.type = Value::Type::Error;
    }
    
    // Store internal value
    publicVal.impl = std::make_shared<Value::Impl>();
    publicVal.impl->internal = std::make_shared<::havel::HavelValue>(internal);
    
    return publicVal;
}

// Convert public Value to internal HavelValue
::havel::HavelValue toInternalValue(const Value& publicVal) {
    if (!publicVal.impl || !publicVal.impl->internal) {
        return ::havel::HavelValue(nullptr);
    }
    return *publicVal.impl->internal;
}

} // namespace internal

// ============================================================================
// Value implementation
// ============================================================================

struct Value::Impl {
    std::shared_ptr<::havel::HavelValue> internal;
};

Value::Value(bool b) : type(Type::Bool), impl(std::make_shared<Impl>()) {
    impl->internal = std::make_shared<::havel::HavelValue>(b);
}

Value::Value(double n) : type(Type::Number), impl(std::make_shared<Impl>()) {
    impl->internal = std::make_shared<::havel::HavelValue>(n);
}

Value::Value(int n) : type(Type::Number), impl(std::make_shared<Impl>()) {
    impl->internal = std::make_shared<::havel::HavelValue>(static_cast<double>(n));
}

Value::Value(const std::string& s) : type(Type::String), impl(std::make_shared<Impl>()) {
    impl->internal = std::make_shared<::havel::HavelValue>(s);
}

Value::Value(const char* s) : type(Type::String), impl(std::make_shared<Impl>()) {
    impl->internal = std::make_shared<::havel::HavelValue>(std::string(s));
}

bool Value::asBool() const {
    if (!impl || !impl->internal) return false;
    
    if (impl->internal->is<bool>()) {
        return impl->internal->get<bool>();
    }
    if (impl->internal->isNumber()) {
        return impl->internal->asNumber() != 0.0;
    }
    if (impl->internal->is<std::string>()) {
        return !impl->internal->get<std::string>().empty();
    }
    return false;
}

double Value::asNumber() const {
    if (!impl || !impl->internal) return 0.0;
    return impl->internal->asNumber();
}

std::string Value::asString() const {
    if (!impl || !impl->internal) return "";
    
    if (impl->internal->is<std::string>()) {
        return impl->internal->get<std::string>();
    }
    
    // Convert other types to string
    std::ostringstream oss;
    if (impl->internal->is<bool>()) {
        oss << (impl->internal->get<bool>() ? "true" : "false");
    } else if (impl->internal->isNumber()) {
        oss << impl->internal->asNumber();
    }
    return oss.str();
}

bool Value::isTruthy() const {
    if (type == Type::Nil) return false;
    if (type == Type::Bool) return asBool();
    if (type == Type::Number) return asNumber() != 0.0;
    if (type == Type::String) return !asString().empty();
    return true;
}

std::string Value::toString() const {
    if (!impl || !impl->internal) return "nil";
    
    switch (type) {
        case Type::Nil: return "nil";
        case Type::Bool: return asBool() ? "true" : "false";
        case Type::Number: return std::to_string(asNumber());
        case Type::String: return asString();
        case Type::Array: return "<array>";
        case Type::Object: return "<object>";
        case Type::Function: return "<function>";
        case Type::Struct: return "<struct>";
        case Type::Error: return "<error>";
    }
    return "<unknown>";
}

// ============================================================================
// Array implementation
// ============================================================================

size_t Array::size() const {
    if (!value.impl || !value.impl->internal) return 0;
    
    if (auto* arrPtr = value.impl->internal->get_if<::havel::HavelArray>()) {
        if (*arrPtr) {
            return (*arrPtr)->size();
        }
    }
    return 0;
}

Value Array::get(size_t index) const {
    if (!value.impl || !value.impl->internal) return Value();
    
    if (auto* arrPtr = value.impl->internal->get_if<::havel::HavelArray>()) {
        if (*arrPtr && index < (*arrPtr)->size()) {
            return internal::toPublicValue((*arrPtr)->at(index));
        }
    }
    return Value();
}

void Array::set(size_t index, const Value& val) {
    if (!value.impl || !value.impl->internal) return;
    
    if (auto* arrPtr = value.impl->internal->get_if<::havel::HavelArray>()) {
        if (*arrPtr && index < (*arrPtr)->size()) {
            (*arrPtr)->at(index) = internal::toInternalValue(val);
        }
    }
}

void Array::push(const Value& val) {
    if (!value.impl || !value.impl->internal) return;
    
    if (auto* arrPtr = value.impl->internal->get_if<::havel::HavelArray>()) {
        if (*arrPtr) {
            (*arrPtr)->push_back(internal::toInternalValue(val));
        }
    }
}

Value Array::pop() {
    if (!value.impl || !value.impl->internal) return Value();
    
    if (auto* arrPtr = value.impl->internal->get_if<::havel::HavelArray>()) {
        if (*arrPtr && !(*arrPtr)->empty()) {
            auto val = (*arrPtr)->back();
            (*arrPtr)->pop_back();
            return internal::toPublicValue(val);
        }
    }
    return Value();
}

// ============================================================================
// Object implementation
// ============================================================================

Value Object::get(const std::string& key) const {
    if (!value.impl || !value.impl->internal) return Value();
    
    if (auto* objPtr = value.impl->internal->get_if<::havel::HavelObject>()) {
        if (*objPtr) {
            auto it = (*objPtr)->find(key);
            if (it != (*objPtr)->end()) {
                return internal::toPublicValue(it->second);
            }
        }
    }
    return Value();
}

void Object::set(const std::string& key, const Value& val) {
    if (!value.impl || !value.impl->internal) return;
    
    if (auto* objPtr = value.impl->internal->get_if<::havel::HavelObject>()) {
        if (*objPtr) {
            (*objPtr)->insert_or_assign(key, internal::toInternalValue(val));
        }
    }
}

bool Object::has(const std::string& key) const {
    if (!value.impl || !value.impl->internal) return false;
    
    if (auto* objPtr = value.impl->internal->get_if<::havel::HavelObject>()) {
        if (*objPtr) {
            return (*objPtr)->find(key) != (*objPtr)->end();
        }
    }
    return false;
}

std::vector<std::string> Object::keys() const {
    std::vector<std::string> result;
    
    if (!value.impl || !value.impl->internal) return result;
    
    if (auto* objPtr = value.impl->internal->get_if<::havel::HavelObject>()) {
        if (*objPtr) {
            for (const auto& [key, val] : **objPtr) {
                result.push_back(key);
            }
        }
    }
    return result;
}

// ============================================================================
// Struct implementation
// ============================================================================

std::string Struct::getType() const {
    if (!value.impl || !value.impl->internal) return "";
    
    if (auto* structPtr = value.impl->internal->get_if<::havel::HavelStructInstance>()) {
        return structPtr->typeName;
    }
    return "";
}

Value Struct::getField(const std::string& name) const {
    if (!value.impl || !value.impl->internal) return Value();
    
    if (auto* structPtr = value.impl->internal->get_if<::havel::HavelStructInstance>()) {
        if (structPtr->fields) {
            auto it = structPtr->fields->find(name);
            if (it != structPtr->fields->end()) {
                return internal::toPublicValue(it->second);
            }
        }
    }
    return Value();
}

void Struct::setField(const std::string& name, const Value& val) {
    if (!value.impl || !value.impl->internal) return;
    
    if (auto* structPtr = value.impl->internal->get_if<::havel::HavelStructInstance>()) {
        if (structPtr->fields) {
            structPtr->fields->insert_or_assign(name, internal::toInternalValue(val));
        }
    }
}

Value Struct::callMethod(const std::string& name, const std::vector<Value>& args) {
    if (!value.impl || !value.impl->internal) return Value();
    
    auto* structPtr = value.impl->internal->get_if<::havel::HavelStructInstance>();
    if (!structPtr || !structPtr->structType) return Value();
    
    // Get method from struct type
    auto method = structPtr->structType->getMethod(name);
    if (!method || !method->body) return Value();
    
    // TODO: Execute method through VM
    // For now, return nil
    return Value();
}

// ============================================================================
// VM implementation
// ============================================================================

struct VM::Impl {
    std::shared_ptr<::havel::Interpreter> interpreter;
    std::string lastError;
    void* hostContext = nullptr;
    
    Impl() {
        // Create interpreter instance
        try {
            interpreter = std::make_shared<::havel::Interpreter>();
        } catch (const std::exception& e) {
            lastError = e.what();
        }
    }
};

VM::VM() : impl(std::make_unique<Impl>()) {}

VM::~VM() = default;

VM::VM(VM&&) noexcept = default;
VM& VM::operator=(VM&&) noexcept = default;

Result<Value> VM::load(const std::string& code, const std::string& sourceName) {
    if (!impl->interpreter) {
        return Result<Value>::err_result("VM not initialized: " + impl->lastError);
    }
    
    try {
        auto result = impl->interpreter->Execute(code);
        
        // Check for errors
        if (std::holds_alternative<::havel::HavelRuntimeError>(result)) {
            auto& err = std::get<::havel::HavelRuntimeError>(result);
            return Result<Value>::err_result(err.what());
        }
        
        // Return result value
        if (auto* valPtr = std::get_if<::havel::HavelValue>(&result)) {
            return Result<Value>::ok_result(internal::toPublicValue(*valPtr));
        }
        
        return Result<Value>::ok_result(Value());
        
    } catch (const std::exception& e) {
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
    // Get function from globals
    auto funcVal = getGlobal(funcName);
    if (funcVal.isNil()) {
        return Result<Value>::err_result("Function not found: " + funcName);
    }
    return call(funcVal, args);
}

Result<Value> VM::call(const Value& func, const std::vector<Value>& args) {
    if (!impl->interpreter) {
        return Result<Value>::err_result("VM not initialized");
    }
    
    try {
        // Convert args to internal values
        std::vector<::havel::HavelValue> internalArgs;
        for (const auto& arg : args) {
            internalArgs.push_back(internal::toInternalValue(arg));
        }
        
        // Get function value
        auto funcVal = getGlobal(funcName);
        if (funcVal.isNil()) {
            return Result<Value>::err_result("Function not found: " + funcName);
        }
        
        // Call the function through interpreter
        try {
            auto internalFunc = internal::toInternalValue(funcVal);
            auto result = impl->interpreter->CallFunction(internalFunc, internalArgs);
            
            // Check for errors
            if (std::holds_alternative<::havel::HavelRuntimeError>(result)) {
                auto& err = std::get<::havel::HavelRuntimeError>(result);
                return Result<Value>::err_result(err.what());
            }
            
            // Return result value
            if (auto* valPtr = std::get_if<::havel::HavelValue>(&result)) {
                return Result<Value>::ok_result(internal::toPublicValue(*valPtr));
            }
            
            return Result<Value>::ok_result(Value());
            
        } catch (const std::exception& e) {
            return Result<Value>::err_result(e.what());
        }
        
    } catch (const std::exception& e) {
        return Result<Value>::err_result(e.what());
    }
}

Value VM::getGlobal(const std::string& name) {
    if (!impl->interpreter) return Value();
    
    try {
        auto& env = impl->interpreter->getEnvironment();
        if (auto val = env.Get(name)) {
            return internal::toPublicValue(*val);
        }
    } catch (...) {
        // Return nil on error
    }
    
    return Value();
}

void VM::setGlobal(const std::string& name, const Value& value) {
    if (!impl->interpreter) return;
    
    try {
        auto& env = impl->interpreter->getEnvironment();
        env.Define(name, internal::toInternalValue(value));
    } catch (...) {
        // Ignore errors
    }
}

void VM::registerFn(const std::string& name, NativeFunction fn) {
    if (!impl->interpreter) return;
    
    try {
        auto& env = impl->interpreter->getEnvironment();
        
        // Wrap native function in Havel BuiltinFunction
        ::havel::BuiltinFunction wrapper([fn, this](const std::vector<::havel::HavelValue>& internalArgs) -> ::havel::HavelResult {
            // Convert internal args to public values
            std::vector<Value> publicArgs;
            for (const auto& arg : internalArgs) {
                publicArgs.push_back(internal::toPublicValue(arg));
            }
            
            // Call native function
            Value result = fn(*this, publicArgs);
            
            // Convert result back to internal value
            return internal::toInternalValue(result);
        });
        
        env.Define(name, wrapper);
        
    } catch (...) {
        // Ignore errors
    }
}

void VM::registerModule(const std::string& name, const std::vector<std::pair<std::string, NativeFunction>>& fns) {
    if (!impl->interpreter) return;
    
    try {
        auto& env = impl->interpreter->getEnvironment();
        
        // Create module object
        ::havel::HavelObject moduleObj = std::make_shared<std::unordered_map<std::string, ::havel::HavelValue>>();
        
        // Add each function to module
        for (const auto& [fnName, fn] : fns) {
            ::havel::BuiltinFunction wrapper([fn, this](const std::vector<::havel::HavelValue>& internalArgs) -> ::havel::HavelResult {
                std::vector<Value> publicArgs;
                for (const auto& arg : internalArgs) {
                    publicArgs.push_back(internal::toPublicValue(arg));
                }
                
                Value result = fn(*this, publicArgs);
                return internal::toInternalValue(result);
            });
            
            (*moduleObj)[fnName] = wrapper;
        }
        
        // Register module
        env.Define(name, ::havel::HavelValue(moduleObj));
        
    } catch (...) {
        // Ignore errors
    }
}

std::string VM::getError() const {
    return impl->lastError;
}

void VM::clearError() {
    impl->lastError.clear();
}

void VM::setHostContext(void* ctx) {
    impl->hostContext = ctx;
    
    // Also set on interpreter if available
    if (impl->interpreter) {
        // TODO: Pass host context to interpreter
    }
}

void* VM::getHostContext() const {
    return impl->hostContext;
}

} // namespace havel
