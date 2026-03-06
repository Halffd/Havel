/*
 * MathModule.cpp
 * 
 * Mathematical functions for Havel standard library.
 * Extracted from Interpreter.cpp as part of runtime refactoring.
 */
#include "MathModule.hpp"
#include <cmath>

namespace havel::stdlib {

void registerMathModule(Environment* env) {
  auto mathObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
  auto& math = *mathObj;
  
  // Helper lambda for number conversion
  auto toNum = [](const HavelValue& v) {
    if (v.is<double>()) return v.get<double>();
    if (v.is<int>()) return static_cast<double>(v.get<int>());
    return 0.0;
  };

  // === Basic arithmetic functions ===
  
  math["abs"] = BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() != 1) return HavelRuntimeError("abs() requires 1 argument");
    return HavelValue(std::abs(toNum(args[0])));
  });

  math["ceil"] = BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() != 1) return HavelRuntimeError("ceil() requires 1 argument");
    return HavelValue(std::ceil(toNum(args[0])));
  });

  math["floor"] = BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() != 1) return HavelRuntimeError("floor() requires 1 argument");
    return HavelValue(std::floor(toNum(args[0])));
  });

  math["round"] = BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() != 1) return HavelRuntimeError("round() requires 1 argument");
    return HavelValue(std::round(toNum(args[0])));
  });

  // === Trigonometric functions ===
  
  math["sin"] = BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() != 1) return HavelRuntimeError("sin() requires 1 argument");
    return HavelValue(std::sin(toNum(args[0])));
  });

  math["cos"] = BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() != 1) return HavelRuntimeError("cos() requires 1 argument");
    return HavelValue(std::cos(toNum(args[0])));
  });

  math["tan"] = BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() != 1) return HavelRuntimeError("tan() requires 1 argument");
    return HavelValue(std::tan(toNum(args[0])));
  });

  math["asin"] = BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() != 1) return HavelRuntimeError("asin() requires 1 argument");
    double value = toNum(args[0]);
    if (value < -1.0 || value > 1.0) 
      return HavelRuntimeError("asin() argument must be between -1 and 1");
    return HavelValue(std::asin(value));
  });

  math["acos"] = BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() != 1) return HavelRuntimeError("acos() requires 1 argument");
    double value = toNum(args[0]);
    if (value < -1.0 || value > 1.0) 
      return HavelRuntimeError("acos() argument must be between -1 and 1");
    return HavelValue(std::acos(value));
  });

  math["atan"] = BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() != 1) return HavelRuntimeError("atan() requires 1 argument");
    return HavelValue(std::atan(toNum(args[0])));
  });

  math["atan2"] = BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() != 2) return HavelRuntimeError("atan2() requires 2 arguments (y, x)");
    return HavelValue(std::atan2(toNum(args[0]), toNum(args[1])));
  });

  // === Hyperbolic functions ===
  
  math["sinh"] = BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() != 1) return HavelRuntimeError("sinh() requires 1 argument");
    return HavelValue(std::sinh(toNum(args[0])));
  });

  math["cosh"] = BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() != 1) return HavelRuntimeError("cosh() requires 1 argument");
    return HavelValue(std::cosh(toNum(args[0])));
  });

  math["tanh"] = BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() != 1) return HavelRuntimeError("tanh() requires 1 argument");
    return HavelValue(std::tanh(toNum(args[0])));
  });

  // === Exponential and logarithmic functions ===
  
  math["exp"] = BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() != 1) return HavelRuntimeError("exp() requires 1 argument");
    return HavelValue(std::exp(toNum(args[0])));
  });

  math["log"] = BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() != 1) return HavelRuntimeError("log() requires 1 argument");
    double value = toNum(args[0]);
    if (value <= 0) return HavelRuntimeError("log() argument must be positive");
    return HavelValue(std::log(value));
  });

  math["log10"] = BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() != 1) return HavelRuntimeError("log10() requires 1 argument");
    double value = toNum(args[0]);
    if (value <= 0) return HavelRuntimeError("log10() argument must be positive");
    return HavelValue(std::log10(value));
  });

  math["log2"] = BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() != 1) return HavelRuntimeError("log2() requires 1 argument");
    double value = toNum(args[0]);
    if (value <= 0) return HavelRuntimeError("log2() argument must be positive");
    return HavelValue(std::log2(value));
  });

  // === Power and root functions ===
  
  math["sqrt"] = BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() != 1) return HavelRuntimeError("sqrt() requires 1 argument");
    double value = toNum(args[0]);
    if (value < 0) return HavelRuntimeError("sqrt() argument must be non-negative");
    return HavelValue(std::sqrt(value));
  });

  math["cbrt"] = BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() != 1) return HavelRuntimeError("cbrt() requires 1 argument");
    return HavelValue(std::cbrt(toNum(args[0])));
  });

  math["pow"] = BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() != 2) return HavelRuntimeError("pow() requires 2 arguments (base, exponent)");
    return HavelValue(std::pow(toNum(args[0]), toNum(args[1])));
  });

  math["hypot"] = BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() != 2) return HavelRuntimeError("hypot() requires 2 arguments");
    return HavelValue(std::hypot(toNum(args[0]), toNum(args[1])));
  });

  // === Constants ===
  
  math["PI"] = HavelValue(3.14159265358979323846);
  math["E"] = HavelValue(2.71828182845904523536);
  math["PHI"] = HavelValue(1.61803398874989484820);  // Golden ratio
  math["SQRT2"] = HavelValue(1.41421356237309504880);
  math["SQRT1_2"] = HavelValue(0.70710678118654752440);
  math["LN2"] = HavelValue(0.69314718055994530942);
  math["LN10"] = HavelValue(2.30258509299404568402);

  // Register math module in environment
  env->Define("math", HavelValue(mathObj));
}

} // namespace havel::stdlib
