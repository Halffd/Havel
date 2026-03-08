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
  math["TAU"] = HavelValue(6.28318530717958647692);  // 2*PI

  // === Random functions ===

  math["random"] = BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.empty()) {
      // random() - float [0, 1)
      return HavelValue(static_cast<double>(rand()) / RAND_MAX);
    } else if (args.size() == 1) {
      // random(max) - float [0, max)
      double max = toNum(args[0]);
      return HavelValue(static_cast<double>(rand()) / RAND_MAX * max);
    } else {
      // random(min, max) - float [min, max)
      double min = toNum(args[0]);
      double max = toNum(args[1]);
      return HavelValue(min + static_cast<double>(rand()) / RAND_MAX * (max - min));
    }
  });

  math["randint"] = BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.empty()) {
      return HavelRuntimeError("randint() requires at least 1 argument");
    } else if (args.size() == 1) {
      // randint(max) - int [0, max]
      int max = static_cast<int>(toNum(args[0]));
      return HavelValue(rand() % (max + 1));
    } else {
      // randint(min, max) - int [min, max]
      int min = static_cast<int>(toNum(args[0]));
      int max = static_cast<int>(toNum(args[1]));
      return HavelValue(min + rand() % (max - min + 1));
    }
  });

  // === Utility functions ===

  math["min"] = BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.empty()) return HavelRuntimeError("min() requires at least 1 argument");
    double minVal = toNum(args[0]);
    for (size_t i = 1; i < args.size(); ++i) {
      double val = toNum(args[i]);
      if (val < minVal) minVal = val;
    }
    return HavelValue(minVal);
  });

  math["max"] = BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.empty()) return HavelRuntimeError("max() requires at least 1 argument");
    double maxVal = toNum(args[0]);
    for (size_t i = 1; i < args.size(); ++i) {
      double val = toNum(args[i]);
      if (val > maxVal) maxVal = val;
    }
    return HavelValue(maxVal);
  });

  math["clamp"] = BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() < 3) return HavelRuntimeError("clamp() requires (value, min, max)");
    double val = toNum(args[0]);
    double minVal = toNum(args[1]);
    double maxVal = toNum(args[2]);
    return HavelValue(std::max(minVal, std::min(val, maxVal)));
  });

  math["lerp"] = BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() < 3) return HavelRuntimeError("lerp() requires (a, b, t)");
    double a = toNum(args[0]);
    double b = toNum(args[1]);
    double t = toNum(args[2]);
    return HavelValue(a + t * (b - a));
  });

  math["sign"] = BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.empty()) return HavelRuntimeError("sign() requires 1 argument");
    double val = toNum(args[0]);
    return HavelValue(val > 0 ? 1.0 : (val < 0 ? -1.0 : 0.0));
  });

  math["fract"] = BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.empty()) return HavelRuntimeError("fract() requires 1 argument");
    double val = toNum(args[0]);
    return HavelValue(val - std::floor(val));
  });

  math["mod"] = BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() < 2) return HavelRuntimeError("mod() requires 2 arguments");
    double a = toNum(args[0]);
    double b = toNum(args[1]);
    return HavelValue(std::fmod(a, b));
  });

  math["distance"] = BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() < 4) return HavelRuntimeError("distance() requires (x1, y1, x2, y2)");
    double x1 = toNum(args[0]);
    double y1 = toNum(args[1]);
    double x2 = toNum(args[2]);
    double y2 = toNum(args[3]);
    return HavelValue(std::sqrt(std::pow(x2 - x1, 2) + std::pow(y2 - y1, 2)));
  });

  math["deg2rad"] = BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.empty()) return HavelRuntimeError("deg2rad() requires 1 argument");
    constexpr double PI = 3.14159265358979323846;
    return HavelValue(toNum(args[0]) * PI / 180.0);
  });

  math["rad2deg"] = BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.empty()) return HavelRuntimeError("rad2deg() requires 1 argument");
    constexpr double PI = 3.14159265358979323846;
    return HavelValue(toNum(args[0]) * 180.0 / PI);
  });

  // Register math module in environment
  env->Define("math", HavelValue(mathObj));
}

} // namespace havel::stdlib
