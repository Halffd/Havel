/* MathModule.cpp - VM-native math host bridges
 * Only registers <cmath> functions that need native code.
 * Pure-Havel additions (clamp, lerp, sign, etc.) come from
 * the sidecar module modules/lang/math/math.hv
 * Physics constants/functions come from
 * the sidecar module modules/lang/math/physics.hv
 */
#include "MathModule.hpp"
#include "../compiler/vm/VM.hpp"
#include <limits>

using havel::compiler::Value;
using havel::compiler::VMApi;

namespace havel::stdlib {

static double toNum(const Value &v) {
  if (v.isInt()) return static_cast<double>(v.asInt());
  if (v.isDouble()) return v.asDouble();
  return 0.0;
}

static void mergeExports(const VMApi &api, Value targetObj, Value exports) {
  auto &vm = api.vm();
  if (!exports.isObjectId()) return;
  auto *obj = vm.getHeap().object(exports.asObjectId());
  if (!obj) return;
  for (const auto& [name, value] : *obj) {
    if (name.empty() || name[0] == '_') continue;
    api.setField(targetObj, name, value);
    api.setGlobal(name, value);
  }
}

void registerMathModule(const VMApi &api) {
  auto &vm = api.vm();

  // --- cmath host bridges (require native <cmath>) ---

  api.registerFunction("math.ceil", [](const std::vector<Value> &args) {
    if (args.size() != 1) throw std::runtime_error("math.ceil() requires 1 argument");
    return Value(std::ceil(toNum(args[0])));
  });
  api.registerFunction("math.floor", [](const std::vector<Value> &args) {
    if (args.size() != 1) throw std::runtime_error("math.floor() requires 1 argument");
    return Value(std::floor(toNum(args[0])));
  });
  api.registerFunction("math.round", [](const std::vector<Value> &args) {
    if (args.size() != 1) throw std::runtime_error("math.round() requires 1 argument");
    return Value(std::round(toNum(args[0])));
  });
  api.registerFunction("math.sin", [](const std::vector<Value> &args) {
    if (args.size() != 1) throw std::runtime_error("math.sin() requires 1 argument");
    return Value(std::sin(toNum(args[0])));
  });
  api.registerFunction("math.cos", [](const std::vector<Value> &args) {
    if (args.size() != 1) throw std::runtime_error("math.cos() requires 1 argument");
    return Value(std::cos(toNum(args[0])));
  });
  api.registerFunction("math.tan", [](const std::vector<Value> &args) {
    if (args.size() != 1) throw std::runtime_error("math.tan() requires 1 argument");
    return Value(std::tan(toNum(args[0])));
  });
  api.registerFunction("math.sqrt", [](const std::vector<Value> &args) {
    if (args.size() != 1) throw std::runtime_error("math.sqrt() requires 1 argument");
    double val = toNum(args[0]);
    if (val < 0) throw std::runtime_error("math.sqrt() argument must be non-negative");
    return Value(std::sqrt(val));
  });
  api.registerFunction("math.log", [](const std::vector<Value> &args) {
    if (args.size() != 1) throw std::runtime_error("math.log() requires 1 argument");
    double val = toNum(args[0]);
    if (val <= 0) throw std::runtime_error("math.log() argument must be positive");
    return Value(std::log(val));
  });
  api.registerFunction("math.exp", [](const std::vector<Value> &args) {
    if (args.size() != 1) throw std::runtime_error("math.exp() requires 1 argument");
    return Value(std::exp(toNum(args[0])));
  });
  api.registerFunction("math.pow", [](const std::vector<Value> &args) {
    if (args.size() != 2) throw std::runtime_error("math.pow() requires 2 arguments");
    return Value(std::pow(toNum(args[0]), toNum(args[1])));
  });
  api.registerFunction("math.random", [](const std::vector<Value> &) {
    return Value(static_cast<double>(std::rand()) / RAND_MAX);
  });
  api.registerFunction("math.abs", [](const std::vector<Value> &args) {
    if (args.size() != 1) throw std::runtime_error("math.abs() requires 1 argument");
    return Value(std::abs(toNum(args[0])));
  });
  api.registerFunction("math.min", [](const std::vector<Value> &args) {
    if (args.empty()) throw std::runtime_error("math.min() requires at least 1 argument");
    double min = toNum(args[0]);
    for (size_t i = 1; i < args.size(); ++i) { double val = toNum(args[i]); if (val < min) min = val; }
    return Value(min);
  });
  api.registerFunction("math.max", [](const std::vector<Value> &args) {
    if (args.empty()) throw std::runtime_error("math.max() requires at least 1 argument");
    double max = toNum(args[0]);
    for (size_t i = 1; i < args.size(); ++i) { double val = toNum(args[i]); if (val > max) max = val; }
    return Value(max);
  });

  // Bare constants (backward compat)
  api.setGlobal("PI", Value(3.14159265358979323846));
  api.setGlobal("E", Value(2.71828182845904523536));
  api.setGlobal("INF", Value(std::numeric_limits<double>::infinity()));
  api.setGlobal("NAN", Value(std::numeric_limits<double>::quiet_NaN()));

  // Build "math" namespace object from host bridges
  auto mathObj = api.makeObject();
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
  api.setField(mathObj, "random", api.makeFunctionRef("math.random"));
  api.setField(mathObj, "abs", api.makeFunctionRef("math.abs"));
  api.setField(mathObj, "min", api.makeFunctionRef("math.min"));
  api.setField(mathObj, "max", api.makeFunctionRef("math.max"));
  api.setField(mathObj, "PI", Value(3.14159265358979323846));
  api.setField(mathObj, "E", Value(2.71828182845904523536));
  api.setField(mathObj, "TAU", Value(6.28318530717958647692));
  api.setField(mathObj, "SQRT2", Value(1.41421356237309504880));
  api.setField(mathObj, "INF", Value(std::numeric_limits<double>::infinity()));
  api.setField(mathObj, "NAN", Value(std::numeric_limits<double>::quiet_NaN()));

  // Set "math" global BEFORE loading sidecars so they can use math.PI etc
  api.setGlobal("math", mathObj);

  // Load pure-Havel math sidecar (adds TAU, SQRT2, clamp, lerp, etc.)
  // Load "math/math" directly — that file defines top-level globals
  // which become module exports. The wrapper "math" only uses `use`
  // which creates locals (not globals), so they don't get exported.
  Value mathExports;
  try {
    mathExports = vm.loadModule("math/math");
  } catch (const std::exception& e) {
  } catch (...) {
  }
  mergeExports(api, mathObj, mathExports);

  // Load pure-Havel physics sidecar (adds G, C, force, momentum, etc.)
  Value physicsExports;
  try {
    physicsExports = vm.loadModule("math/physics");
  } catch (...) {
  }

  // Build "physics"/"Physics" namespace object from physics sidecar
  auto physicsObj = api.makeObject();
  if (physicsExports.isObjectId()) {
    auto *pobj = vm.getHeap().object(physicsExports.asObjectId());
    if (pobj) {
      for (const auto& [name, value] : *pobj) {
        if (name.empty() || name[0] == '_') continue;
        api.setField(physicsObj, name, value);
        api.setGlobal(name, value);
      }
    }
  }

  api.setGlobal("physics", physicsObj);
  api.setGlobal("Physics", physicsObj);
}

} // namespace havel::stdlib

#ifdef HAVEL_MODULE_PLUGIN
#include "c/ModulePlugin.h"

HAVEL_MODULE_PLUGIN_IMPL(math, "1.0.0", "Math operations stdlib module",
    havel::stdlib::registerMathModule(*api);
)
#endif
