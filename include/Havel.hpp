// Havel.hpp
// Clean, embeddable C++ API for Havel language
// Direct API design (QuickJS/Wren style) - no stack manipulation

#pragma once
#include <string>
#include <vector>
#include <functional>
#include <optional>
#include <variant>
#include <memory>

namespace havel {

// Forward declarations
struct VM;
struct Value;

/**
 * Result type for operations that can fail
 */
template<typename T>
struct Result {
    bool ok;
    T value;
    std::string error;
    
    static Result ok_result(T val) {
        return Result{true, val, ""};
    }
    
    static Result err_result(std::string err) {
        return Result{false, T{}, err};
    }
    
    operator bool() const { return ok; }
    T& operator*() { return value; }
    T* operator->() { return &value; }
};

/**
 * Havel Value - universal runtime object
 * Everything becomes a Value: numbers, strings, arrays, objects, functions
 */
struct Value {
    enum class Type {
        Nil,
        Bool,
        Number,
        String,
        Array,
        Object,
        Function,
        Struct,
        Error
    };
    
    Type type;
    
    // Internal storage (implementation details hidden)
    struct Impl;
    std::shared_ptr<Impl> impl;
    
    // Constructors
    Value() : type(Type::Nil) {}
    Value(std::nullptr_t) : type(Type::Nil) {}
    Value(bool b);
    Value(double n);
    Value(int n);
    Value(const std::string& s);
    Value(const char* s);
    
    // Type checks
    bool isNil() const { return type == Type::Nil; }
    bool isBool() const { return type == Type::Bool; }
    bool isNumber() const { return type == Type::Number; }
    bool isString() const { return type == Type::String; }
    bool isArray() const { return type == Type::Array; }
    bool isObject() const { return type == Type::Object; }
    bool isFunction() const { return type == Type::Function; }
    bool isStruct() const { return type == Type::Struct; }
    bool isError() const { return type == Type::Error; }
    
    // Type conversions
    bool asBool() const;
    double asNumber() const;
    std::string asString() const;
    
    // Convenience
    bool isTruthy() const;
    std::string toString() const;
};

/**
 * Native function signature
 * Functions receive VM reference and argument span
 */
using NativeFunction = std::function<Value(VM&, const std::vector<Value>&)>;

/**
 * Havel VM - main interpreter interface
 * 
 * Usage:
 *   havel::VM vm;
 *   vm.load(R"(
 *     fn add(a, b) { return a + b }
 *   )");
 *   
 *   auto result = vm.call("add", {5, 3});
 *   print(result->asNumber());  // 8
 */
struct VM {
    struct Impl;
    std::unique_ptr<Impl> impl;
    
    VM();
    ~VM();
    
    // Non-copyable, movable
    VM(const VM&) = delete;
    VM& operator=(const VM&) = delete;
    VM(VM&&) noexcept;
    VM& operator=(VM&&) noexcept;
    
    /**
     * Load and execute Havel code
     * Returns error if compilation or runtime error occurs
     */
    Result<Value> load(const std::string& code, const std::string& sourceName = "script");
    
    /**
     * Load and execute Havel code from file
     */
    Result<Value> loadFile(const std::string& path);
    
    /**
     * Call a global function by name
     * Example: vm.call("add", {5, 3})
     */
    Result<Value> call(const std::string& funcName, const std::vector<Value>& args = {});
    
    /**
     * Call a function value directly
     */
    Result<Value> call(const Value& func, const std::vector<Value>& args = {});
    
    /**
     * Get a global variable
     */
    Value getGlobal(const std::string& name);
    
    /**
     * Set a global variable
     */
    void setGlobal(const std::string& name, const Value& value);
    
    /**
     * Register a native C++ function
     * Example: vm.registerFn("print", [](VM& vm, auto& args) { ... })
     */
    void registerFn(const std::string& name, NativeFunction fn);
    
    /**
     * Register a module (object with multiple functions)
     * Example: 
     *   vm.registerModule("io", {
     *     {"print", printFn},
     *     {"read", readFn}
     *   });
     */
    void registerModule(const std::string& name, const std::vector<std::pair<std::string, NativeFunction>>& fns);
    
    /**
     * Get last error message
     */
    std::string getError() const;
    
    /**
     * Clear last error
     */
    void clearError();
    
    /**
     * Set host context (IO, WindowManager, etc.)
     * This is how you inject host-specific functionality
     */
    void setHostContext(void* ctx);
    
    /**
     * Get host context
     */
    void* getHostContext() const;
};

/**
 * Array helper - makes working with Havel arrays easier
 */
struct Array {
    Value value;
    
    Array() : value() {}
    explicit Array(const Value& v);
    
    size_t size() const;
    Value get(size_t index) const;
    void set(size_t index, const Value& val);
    void push(const Value& val);
    Value pop();
};

/**
 * Object helper - makes working with Havel objects easier
 */
struct Object {
    Value value;
    
    Object() : value() {}
    explicit Object(const Value& v);
    
    Value get(const std::string& key) const;
    void set(const std::string& key, const Value& val);
    bool has(const std::string& key) const;
    std::vector<std::string> keys() const;
};

/**
 * Struct helper - makes working with Havel structs easier
 */
struct Struct {
    Value value;
    
    Struct() : value() {}
    explicit Struct(const Value& v);
    
    std::string getType() const;
    Value getField(const std::string& name) const;
    void setField(const std::string& name, const Value& val);
    Value callMethod(const std::string& name, const std::vector<Value>& args = {});
};

// ============================================================================
// Inline implementations
// ============================================================================

inline Value::Value(bool b) : type(Type::Bool) {}
inline Value::Value(double n) : type(Type::Number) {}
inline Value::Value(int n) : type(Type::Number) {}
inline Value::Value(const std::string& s) : type(Type::String) {}
inline Value::Value(const char* s) : type(Type::String) {}

inline Array::Array(const Value& v) : value(v) {}
inline Object::Object(const Value& v) : value(v) {}
inline Struct::Struct(const Value& v) : value(v) {}

} // namespace havel
