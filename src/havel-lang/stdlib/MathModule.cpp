/* MathModule.cpp - VM-native stdlib module */
#include "MathModule.hpp"

using havel::compiler::Value;
using havel::compiler::VMApi;

namespace havel::stdlib {

// Register math module with VMApi (stable API layer)
void registerMathModule(VMApi &api) {
  // Helper: convert Value to double
  auto toNum = [](const Value &v) -> double {
    if (v.isInt())
      return static_cast<double>(v.asInt());
    if (v.isDouble())
      return v.asDouble();
    return 0.0;
  };

  // Register math functions via VMApi
  api.registerFunction(
      "math.abs", [toNum](const std::vector<Value> &args) {
        if (args.size() != 1)
          throw std::runtime_error("math.abs() requires 1 argument");
        return Value(std::abs(toNum(args[0])));
      });

  api.registerFunction(
      "math.ceil", [toNum](const std::vector<Value> &args) {
        if (args.size() != 1)
          throw std::runtime_error("math.ceil() requires 1 argument");
        return Value(std::ceil(toNum(args[0])));
      });

  api.registerFunction(
      "math.floor", [toNum](const std::vector<Value> &args) {
        if (args.size() != 1)
          throw std::runtime_error("math.floor() requires 1 argument");
        return Value(std::floor(toNum(args[0])));
      });

  api.registerFunction(
      "math.round", [toNum](const std::vector<Value> &args) {
        if (args.size() != 1)
          throw std::runtime_error("math.round() requires 1 argument");
        return Value(std::round(toNum(args[0])));
      });

  api.registerFunction(
      "math.sin", [toNum](const std::vector<Value> &args) {
        if (args.size() != 1)
          throw std::runtime_error("math.sin() requires 1 argument");
        return Value(std::sin(toNum(args[0])));
      });

  api.registerFunction(
      "math.cos", [toNum](const std::vector<Value> &args) {
        if (args.size() != 1)
          throw std::runtime_error("math.cos() requires 1 argument");
        return Value(std::cos(toNum(args[0])));
      });

  api.registerFunction(
      "math.tan", [toNum](const std::vector<Value> &args) {
        if (args.size() != 1)
          throw std::runtime_error("math.tan() requires 1 argument");
        return Value(std::tan(toNum(args[0])));
      });

  api.registerFunction(
      "math.sqrt", [toNum](const std::vector<Value> &args) {
        if (args.size() != 1)
          throw std::runtime_error("math.sqrt() requires 1 argument");
        double val = toNum(args[0]);
        if (val < 0)
          throw std::runtime_error("math.sqrt() argument must be non-negative");
        return Value(std::sqrt(val));
      });

  api.registerFunction(
      "math.log", [toNum](const std::vector<Value> &args) {
        if (args.size() != 1)
          throw std::runtime_error("math.log() requires 1 argument");
        double val = toNum(args[0]);
        if (val <= 0)
          throw std::runtime_error("math.log() argument must be positive");
        return Value(std::log(val));
      });

  api.registerFunction(
      "math.exp", [toNum](const std::vector<Value> &args) {
        if (args.size() != 1)
          throw std::runtime_error("math.exp() requires 1 argument");
        return Value(std::exp(toNum(args[0])));
      });

  api.registerFunction(
      "math.pow", [toNum](const std::vector<Value> &args) {
        if (args.size() != 2)
          throw std::runtime_error("math.pow() requires 2 arguments");
        return Value(std::pow(toNum(args[0]), toNum(args[1])));
      });

  api.registerFunction(
      "math.min", [toNum](const std::vector<Value> &args) {
        if (args.empty())
          throw std::runtime_error("math.min() requires at least 1 argument");
        double min = toNum(args[0]);
        for (size_t i = 1; i < args.size(); ++i) {
          double val = toNum(args[i]);
          if (val < min)
            min = val;
        }
        return Value(min);
      });

  api.registerFunction(
      "math.max", [toNum](const std::vector<Value> &args) {
        if (args.empty())
          throw std::runtime_error("math.max() requires at least 1 argument");
        double max = toNum(args[0]);
        for (size_t i = 1; i < args.size(); ++i) {
          double val = toNum(args[i]);
          if (val > max)
            max = val;
        }
        return Value(max);
      });

  api.registerFunction("math.random", [](const std::vector<Value> &) {
    return Value(static_cast<double>(std::rand()) / RAND_MAX);
  });

  // Constants
  api.setGlobal("PI", Value(3.14159265358979323846));
  api.setGlobal("E", Value(2.71828182845904523536));

  // Register math object
  auto mathObj = api.makeObject();
  api.setField(mathObj, "abs", api.makeFunctionRef("math.abs"));
  api.setField(mathObj, "ceil", api.makeFunctionRef("math.ceil"));
  api.setField(mathObj, "floor", api.makeFunctionRef("math.floor"));
  api.setField(mathObj, "round", api.makeFunctionRef("math.round"));
  api.setField(mathObj, "sin", api.makeFunctionRef("math.sin"));
  api.setField(mathObj, "cos", api.makeFunctionRef("math.cos"));
  api.setField(mathObj, "tan", api.makeFunctionRef("math.tan"));
  api.setField(mathObj, "sqrt", api.makeFunctionRef("math.sqrt"));
  api.setField(mathObj, "log", api.makeFunctionRef("math.log"));
  api.setField(mathObj, "exp", api.makeFunctionRef("math.exp"));
  api.setField(mathObj, "pow", api.makeFunctionRef("math.pow"));
  api.setField(mathObj, "min", api.makeFunctionRef("math.min"));
  api.setField(mathObj, "max", api.makeFunctionRef("math.max"));
  api.setField(mathObj, "random", api.makeFunctionRef("math.random"));
  api.setGlobal("math", mathObj);
}

} // namespace havel::stdlib
