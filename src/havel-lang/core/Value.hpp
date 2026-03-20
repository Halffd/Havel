/*
 * Value.hpp
 *
 * Core value types for Havel language runtime.
 *
 * This is the foundation layer - no dependencies on Interpreter, Environment,
 * or host APIs.
 */
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace havel::core {

// Forward declare Value
struct Value;

// Basic value container types
using ArrayValue = std::shared_ptr<std::vector<Value>>;
using ObjectValue = std::shared_ptr<std::unordered_map<std::string, Value>>;

// Built-in function type
using BuiltinFunction = std::function<Value(const std::vector<Value> &)>;

/**
 * Runtime error wrapper
 */
struct RuntimeError {
  std::string message;
  size_t line = 0;
  size_t column = 0;
  bool hasLocation = false;

  RuntimeError() = default;
  explicit RuntimeError(const std::string &msg) : message(msg) {}
  RuntimeError(const std::string &msg, size_t l, size_t c)
      : message(msg), line(l), column(c), hasLocation(true) {}
};

/**
 * Control flow wrappers - must be defined BEFORE Result
 * Use shared_ptr to avoid incomplete type issues
 */
struct ReturnValue {
  std::shared_ptr<Value> value;
};

struct BreakValue {};
struct ContinueValue {};

/**
 * Result type - wraps all possible evaluation outcomes
 */
using Result =
    std::variant<Value, RuntimeError, ReturnValue, BreakValue, ContinueValue>;

/**
 * Core Value type - complete definition
 */
struct Value {
  using DataType =
      std::variant<std::nullptr_t, bool, int, double, std::string, ArrayValue,
                   ObjectValue, BuiltinFunction,
                   std::function<Result(const std::vector<Value> &)>>;

  DataType data;

  // Constructors
  Value() : data(nullptr) {}
  Value(std::nullptr_t) : data(nullptr) {}
  Value(bool b) : data(b) {}
  Value(int i) : data(i) {}
  Value(double d) : data(d) {}
  Value(const std::string &s) : data(s) {}
  Value(const char *s) : data(std::string(s)) {}
  Value(ArrayValue arr) : data(std::move(arr)) {}
  Value(ObjectValue obj) : data(std::move(obj)) {}
  Value(BuiltinFunction fn) : data(std::move(fn)) {}
  Value(std::function<Result(const std::vector<Value> &)> fn)
      : data(std::move(fn)) {}

  // Type checks
  bool isNull() const { return std::holds_alternative<std::nullptr_t>(data); }
  bool isBool() const { return std::holds_alternative<bool>(data); }
  bool isNumber() const {
    return std::holds_alternative<int>(data) ||
           std::holds_alternative<double>(data);
  }
  bool isInt() const { return std::holds_alternative<int>(data); }
  bool isDouble() const { return std::holds_alternative<double>(data); }
  bool isString() const { return std::holds_alternative<std::string>(data); }
  bool isArray() const { return std::holds_alternative<ArrayValue>(data); }
  bool isObject() const { return std::holds_alternative<ObjectValue>(data); }
  bool isFunction() const {
    return std::holds_alternative<BuiltinFunction>(data) ||
           std::holds_alternative<
               std::function<Result(const std::vector<Value> &)>>(data);
  }

  // Getters
  bool asBool() const { return std::get<bool>(data); }
  double asNumber() const {
    if (isDouble())
      return std::get<double>(data);
    if (isInt())
      return static_cast<double>(std::get<int>(data));
    return 0.0;
  }
  double asDouble() const { return asNumber(); }
  int asInt() const {
    if (isInt())
      return std::get<int>(data);
    if (isDouble())
      return static_cast<int>(std::get<double>(data));
    return 0;
  }
  const std::string &asString() const { return std::get<std::string>(data); }
  ArrayValue asArray() const { return std::get<ArrayValue>(data); }
  ObjectValue asObject() const { return std::get<ObjectValue>(data); }

  // Call function
  Result call(const std::vector<Value> &args) const {
    if (auto *fn = std::get_if<BuiltinFunction>(&data)) {
      return (*fn)(args);
    }
    if (auto *fn =
            std::get_if<std::function<Result(const std::vector<Value> &)>>(
                &data)) {
      return (*fn)(args);
    }
    return RuntimeError("Value is not callable");
  }

  // Convert to string
  std::string toString() const;
};

// ============================================================================
// Utility functions
// ============================================================================

inline Value unwrapResult(const Result &result) {
  if (auto *val = std::get_if<Value>(&result)) {
    return *val;
  }
  if (auto *ret = std::get_if<ReturnValue>(&result)) {
    return ret->value ? *ret->value : Value();
  }
  if (auto *brk = std::get_if<BreakValue>(&result)) {
    (void)brk;
    return Value();
  }
  if (auto *cont = std::get_if<ContinueValue>(&result)) {
    (void)cont;
    return Value();
  }
  if (auto *err = std::get_if<RuntimeError>(&result)) {
    throw *err;
  }
  return Value();
}

inline bool isError(const Result &result) {
  return std::holds_alternative<RuntimeError>(result);
}

} // namespace havel::core
