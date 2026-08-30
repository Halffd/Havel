/* MathNativeModule.cpp - Native math module with vectorized operations
 * Provides batch operations on arrays for performance-critical math
 */
#include "MathModule.hpp"
#include "compiler/vm/VM.hpp"
#include <cmath>
#include <vector>
#include <algorithm>

using havel::compiler::Value;
using havel::compiler::VMApi;

namespace havel::stdlib {

static inline double toNum(const Value &v) {
    if (v.isInt()) return static_cast<double>(v.asInt());
    if (v.isDouble()) return v.asDouble();
    return 0.0;
}

static inline int64_t toInt(const Value &v) {
    if (v.isInt()) return v.asInt();
    if (v.isDouble()) return static_cast<int64_t>(v.asDouble());
    return 0;
}

static havel::compiler::GCHeap::ArrayEntry* getArray(const Value &v, const VMApi &api) {
    if (!v.isArrayId()) return nullptr;
    return api.vm().getHeap().array(v.asArrayId());
}

static Value vecAdd(const VMApi &api, const std::vector<Value> &args) {
    if (args.size() != 2) throw std::runtime_error("vec.add expects 2 arrays");
    auto *a = getArray(args[0], api);
    auto *b = getArray(args[1], api);
    if (!a || !b) throw std::runtime_error("vec.add expects arrays");
    if (a->data.size() != b->data.size()) throw std::runtime_error("arrays must have same length");
    
    auto resultRef = api.vm().getHeap().allocateArray();
    auto *result = api.vm().getHeap().array(resultRef.id);
    result->data.reserve(a->data.size());
    for (size_t i = 0; i < a->data.size(); ++i) {
        double av = toNum(a->data[i]);
        double bv = toNum(b->data[i]);
        result->data.push_back(Value(av + bv));
    }
    return Value::makeArrayId(resultRef.id);
}

static Value vecSub(const VMApi &api, const std::vector<Value> &args) {
    if (args.size() != 2) throw std::runtime_error("vec.sub expects 2 arrays");
    auto *a = getArray(args[0], api);
    auto *b = getArray(args[1], api);
    if (!a || !b) throw std::runtime_error("vec.sub expects arrays");
    if (a->data.size() != b->data.size()) throw std::runtime_error("arrays must have same length");
    
auto resultRef = api.vm().getHeap().allocateArray();
    auto *result = api.vm().getHeap().array(resultRef.id);
    result->data.reserve(a->data.size());
    for (size_t i = 0; i < a->data.size(); ++i) {
        double av = toNum(a->data[i]);
        double bv = toNum(b->data[i]);
        result->data.push_back(Value(av + bv));
    }
    return Value::makeArrayId(resultRef.id);
}

static Value vecMul(const VMApi &api, const std::vector<Value> &args) {
    if (args.size() != 2) throw std::runtime_error("vec.mul expects 2 arrays");
    auto *a = getArray(args[0], api);
    auto *b = getArray(args[1], api);
    if (!a || !b) throw std::runtime_error("vec.mul expects arrays");
    if (a->data.size() != b->data.size()) throw std::runtime_error("arrays must have same length");
    
    auto resultRef = api.vm().getHeap().allocateArray();
    auto *result = api.vm().getHeap().array(resultRef.id);
    result->data.reserve(a->data.size());
    for (size_t i = 0; i < a->data.size(); ++i) {
        double av = toNum(a->data[i]);
        double bv = toNum(b->data[i]);
        result->data.push_back(Value(av * bv));
    }
    return Value::makeArrayId(resultRef.id);
}

static Value vecDiv(const VMApi &api, const std::vector<Value> &args) {
    if (args.size() != 2) throw std::runtime_error("vec.div expects 2 arrays");
    auto *a = getArray(args[0], api);
    auto *b = getArray(args[1], api);
    if (!a || !b) throw std::runtime_error("vec.div expects arrays");
    if (a->data.size() != b->data.size()) throw std::runtime_error("arrays must have same length");
    
    auto resultRef = api.vm().getHeap().allocateArray();
    auto *result = api.vm().getHeap().array(resultRef.id);
    result->data.reserve(a->data.size());
    for (size_t i = 0; i < a->data.size(); ++i) {
        double av = toNum(a->data[i]);
        double bv = toNum(b->data[i]);
        if (toNum(b->data[i]) == 0) throw std::runtime_error("division by zero");
        result->data.push_back(Value(av / bv));
    }
    return Value::makeArrayId(resultRef.id);
}

// Scalar operations on arrays
static Value vecScale(const VMApi &api, const std::vector<Value> &args) {
    if (args.size() != 2) throw std::runtime_error("vec.scale expects array and scalar");
    auto *a = getArray(args[0], api);
    if (!a) throw std::runtime_error("vec.scale expects array");
    double scale = toNum(args[1]);
    
    auto resultRef = api.vm().getHeap().allocateArray();
    auto *result = api.vm().getHeap().array(resultRef.id);
    result->data.reserve(a->data.size());
    for (size_t i = 0; i < a->data.size(); ++i) {
        double av = toNum(a->data[i]);
        result->data.push_back(Value(av * scale));
    }
    return Value::makeArrayId(resultRef.id);
}

static Value vecAddScalar(const VMApi &api, const std::vector<Value> &args) {
    if (args.size() != 2) throw std::runtime_error("vec.addScalar expects array and scalar");
    auto *a = getArray(args[0], api);
    if (!a) throw std::runtime_error("vec.addScalar expects array");
    double s = toNum(args[1]);
    
    auto resultRef = api.vm().getHeap().allocateArray();
    auto *result = api.vm().getHeap().array(resultRef.id);
    result->data.reserve(a->data.size());
    for (size_t i = 0; i < a->data.size(); ++i) {
        double av = toNum(a->data[i]);
        result->data.push_back(Value(av + s));
    }
    return Value::makeArrayId(resultRef.id);
}

// Reduction operations
static Value vecSum(const VMApi &api, const std::vector<Value> &args) {
    if (args.size() != 1) throw std::runtime_error("vec.sum expects 1 array");
    auto *a = getArray(args[0], api);
    if (!a) throw std::runtime_error("vec.sum expects array");
    
    double sum = 0;
    for (size_t i = 0; i < a->data.size(); ++i) {
        sum += toNum(a->data[i]);
    }
    return Value(sum);
}

static Value vecMean(const VMApi &api, const std::vector<Value> &args) {
    if (args.size() != 1) throw std::runtime_error("vec.mean expects 1 array");
    auto *a = getArray(args[0], api);
    if (!a || a->data.empty()) return Value::makeNull();
    
    double sum = 0;
    for (size_t i = 0; i < a->data.size(); ++i) {
        sum += toNum(a->data[i]);
    }
    return Value(sum / a->data.size());
}

static Value vecMin(const VMApi &api, const std::vector<Value> &args) {
    if (args.size() != 1) throw std::runtime_error("vec.min expects 1 array");
    auto *a = getArray(args[0], api);
    if (!a || a->data.empty()) return Value::makeNull();
    
    double min = toNum(a->data[0]);
    for (size_t i = 1; i < a->data.size(); ++i) {
        double v = toNum(a->data[i]);
        if (v < min) min = v;
    }
    return Value(min);
}

static Value vecMax(const VMApi &api, const std::vector<Value> &args) {
    if (args.size() != 1) throw std::runtime_error("vec.max expects 1 array");
    auto *a = getArray(args[0], api);
    if (!a || a->data.empty()) return Value::makeNull();
    
    double max = toNum(a->data[0]);
    for (size_t i = 1; i < a->data.size(); ++i) {
        double v = toNum(a->data[i]);
        if (v > max) max = v;
    }
    return Value(max);
}

// Dot product
static Value vecDot(const VMApi &api, const std::vector<Value> &args) {
    if (args.size() != 2) throw std::runtime_error("vec.dot expects 2 arrays");
    auto *a = getArray(args[0], api);
    auto *b = getArray(args[1], api);
    if (!a || !b) throw std::runtime_error("vec.dot expects arrays");
    if (a->data.size() != b->data.size()) throw std::runtime_error("arrays must have same length");
    
    double sum = 0;
    for (size_t i = 0; i < a->data.size(); ++i) {
        sum += toNum(a->data[i]) * toNum(b->data[i]);
    }
    return Value(sum);
}

// Element-wise map with a function (stub - requires VM callback)
static Value vecMap(const VMApi &, const std::vector<Value> &args) {
    if (args.size() != 2) throw std::runtime_error("vec.map expects array and function");
    // TODO: implement with VM callback
    return Value::makeNull();
}

// Element-wise filter
static Value vecFilter(const VMApi &, const std::vector<Value> &args) {
    if (args.size() != 2) throw std::runtime_error("vec.filter expects array and predicate");
    // TODO: implement with VM callback
    return Value::makeNull();
}

// Register native math module
void registerMathNativeModule(const VMApi &api) {
    // Vector operations
    api.registerFunction("vec.add", [&](const std::vector<Value> &args) { return vecAdd(api, args); });
    api.registerFunction("vec.sub", [&](const std::vector<Value> &args) { return vecSub(api, args); });
    api.registerFunction("vec.mul", [&](const std::vector<Value> &args) { return vecMul(api, args); });
    api.registerFunction("vec.div", [&](const std::vector<Value> &args) { return vecDiv(api, args); });
    
    // Scalar operations
    api.registerFunction("vec.scale", [&](const std::vector<Value> &args) { return vecScale(api, args); });
    api.registerFunction("vec.addScalar", [&](const std::vector<Value> &args) { return vecAddScalar(api, args); });
    
    // Reduction operations
    api.registerFunction("vec.sum", [&](const std::vector<Value> &args) { return vecSum(api, args); });
    api.registerFunction("vec.mean", [&](const std::vector<Value> &args) { return vecMean(api, args); });
    api.registerFunction("vec.min", [&](const std::vector<Value> &args) { return vecMin(api, args); });
    api.registerFunction("vec.max", [&](const std::vector<Value> &args) { return vecMax(api, args); });
    
    // Dot product
    api.registerFunction("vec.dot", [&](const std::vector<Value> &args) { return vecDot(api, args); });
    
    // Vector operations namespace
    auto vecObj = api.makeObject();
    api.setField(vecObj, "add", api.makeFunctionRef("vec.add"));
    api.setField(vecObj, "sub", api.makeFunctionRef("vec.sub"));
    api.setField(vecObj, "mul", api.makeFunctionRef("vec.mul"));
    api.setField(vecObj, "div", api.makeFunctionRef("vec.div"));
    api.setField(vecObj, "scale", api.makeFunctionRef("vec.scale"));
    api.setField(vecObj, "addScalar", api.makeFunctionRef("vec.addScalar"));
    api.setField(vecObj, "sum", api.makeFunctionRef("vec.sum"));
    api.setField(vecObj, "mean", api.makeFunctionRef("vec.mean"));
    api.setField(vecObj, "min", api.makeFunctionRef("vec.min"));
    api.setField(vecObj, "max", api.makeFunctionRef("vec.max"));
    api.setField(vecObj, "dot", api.makeFunctionRef("vec.dot"));
    
    api.setGlobal("vec", vecObj);
}

} // namespace havel::stdlib

#ifdef HAVEL_MODULE_PLUGIN
#include "c/ModulePlugin.h"

HAVEL_MODULE_PLUGIN_EAGER(math_native, "1.0.0", "Native vectorized math operations",
    havel::stdlib::registerMathNativeModule(*api);
)
#endif