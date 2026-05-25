#pragma once

#include <string>
#include <vector>
#include <functional>
#include <optional>
#include <memory>
#include <variant>

namespace havel {

struct VM;
struct Value;

template<typename T>
struct Result {
    bool ok;
    T value;
    std::string error;

    static Result ok_result(T val) {
        return Result{true, std::move(val), ""};
    }

    static Result err_result(std::string err) {
        return Result{false, T{}, std::move(err)};
    }

    operator bool() const { return ok; }
    T& operator*() { return value; }
    T* operator->() { return &value; }
};

struct Value {
    enum class Type {
        Nil,
        Bool,
        Int,
        Float,
        String,
        Array,
        Object,
        Function,
        Error
    };

    Type type = Type::Nil;

    struct Impl;
    std::shared_ptr<Impl> impl;

    Value();
    Value(std::nullptr_t);
    Value(bool b);
    Value(int n);
    Value(int64_t n);
    Value(double n);
    Value(const std::string& s);
    Value(const char* s);

    bool isNil() const { return type == Type::Nil; }
    bool isBool() const { return type == Type::Bool; }
    bool isInt() const { return type == Type::Int; }
    bool isFloat() const { return type == Type::Float; }
    bool isNumber() const { return type == Type::Int || type == Type::Float; }
    bool isString() const { return type == Type::String; }
    bool isArray() const { return type == Type::Array; }
    bool isObject() const { return type == Type::Object; }
    bool isFunction() const { return type == Type::Function; }
    bool isError() const { return type == Type::Error; }

    bool asBool() const;
    int64_t asInt() const;
    double asFloat() const;
    double asNumber() const;
    std::string asString() const;

    bool isTruthy() const;
    std::string toString() const;
};

using NativeFunction = std::function<Value(VM&, const std::vector<Value>&)>;

struct VM {
    struct Impl;
    std::unique_ptr<Impl> impl;

    VM();
    explicit VM(bool leanStartup);
    ~VM();

    VM(const VM&) = delete;
    VM& operator=(const VM&) = delete;
    VM(VM&&) noexcept;
    VM& operator=(VM&&) noexcept;

    Result<Value> load(const std::string& code, const std::string& sourceName = "script");

    Result<Value> loadFile(const std::string& path);

    Result<Value> call(const std::string& funcName, const std::vector<Value>& args = {});

    Result<Value> call(const Value& func, const std::vector<Value>& args = {});

    Value getGlobal(const std::string& name);

    void setGlobal(const std::string& name, const Value& value);

    void registerFn(const std::string& name, NativeFunction fn);

    void registerModule(const std::string& name, const std::vector<std::pair<std::string, NativeFunction>>& fns);

    std::string getError() const;
    void clearError();
};

struct Array {
    Value value;

    Array();
    explicit Array(const Value& v);

    size_t size() const;
    Value get(size_t index) const;
    void set(size_t index, const Value& val);
    void push(const Value& val);
    Value pop();
};

struct Object {
    Value value;

    Object();
    explicit Object(const Value& v);

    Value get(const std::string& key) const;
    void set(const std::string& key, const Value& val);
    bool has(const std::string& key) const;
    std::vector<std::string> keys() const;
};

} // namespace havel
