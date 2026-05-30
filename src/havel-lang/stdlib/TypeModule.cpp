#include "TypeModule.hpp"
#include "../compiler/vm/VMApi.hpp"
#include "../compiler/vm/VM.hpp"
#include <vector>
#include <string>
#include <stdexcept>
#include <algorithm>

using havel::compiler::Value;
using havel::compiler::VMApi;

namespace havel::stdlib {

static void registerTypeFallback(const VMApi &api) {
    api.registerFunction("isNumber", [](const std::vector<Value> &args) {
        if (args.empty()) throw std::runtime_error("isNumber() requires an argument");
        return Value(args[0].isInt() || args[0].isDouble());
    });
    api.registerFunction("isString", [](const std::vector<Value> &args) {
        if (args.empty()) throw std::runtime_error("isString() requires an argument");
        return Value(args[0].isStringValId());
    });
    api.registerFunction("isArray", [](const std::vector<Value> &args) {
        if (args.empty()) throw std::runtime_error("isArray() requires an argument");
        return Value(args[0].isArrayId());
    });
    api.registerFunction("isObject", [](const std::vector<Value> &args) {
        if (args.empty()) throw std::runtime_error("isObject() requires an argument");
        return Value(args[0].isObjectId());
    });
    api.registerFunction("isNull", [](const std::vector<Value> &args) {
        if (args.empty()) throw std::runtime_error("isNull() requires an argument");
        return Value(args[0].isNull());
    });
    api.registerFunction("isBoolean", [](const std::vector<Value> &args) {
        if (args.empty()) throw std::runtime_error("isBoolean() requires an argument");
        return Value::makeBool(args[0].isBool());
    });
    api.registerFunction("isEnum", [](const std::vector<Value> &args) {
        if (args.empty()) throw std::runtime_error("isEnum() requires an argument");
        return Value(args[0].isEnumId());
    });
    api.registerFunction("toString", [](const std::vector<Value> &args) {
        if (args.empty()) throw std::runtime_error("toString() requires an argument");
        const auto &arg = args[0];
        if (arg.isNull()) return Value::makeNull();
        if (arg.isBool()) return Value::makeBool(arg.asBool());
        if (arg.isInt()) return Value::makeInt(arg.asInt());
        if (arg.isDouble()) return Value::makeDouble(arg.asDouble());
        if (arg.isStringValId()) return arg;
        return Value::makeNull();
    });
    api.registerFunction("toNumber", [](const std::vector<Value> &args) {
        if (args.empty()) throw std::runtime_error("toNumber() requires an argument");
        const auto &arg = args[0];
        if (arg.isNull()) return Value::makeInt(0);
        if (arg.isBool()) return Value::makeInt(arg.asBool() ? 1 : 0);
        if (arg.isInt()) return arg;
        if (arg.isDouble()) return arg;
        return Value::makeInt(0);
    });
    api.registerFunction("newEnum", [api](const std::vector<Value> &args) {
        if (args.size() < 2) throw std::runtime_error("newEnum() requires at least type name and variant name");
        std::string typeName = api.toString(args[0]);
        std::string variantName = api.toString(args[1]);
        uint32_t typeId = api.registerEnumType(typeName, {variantName});
        std::vector<Value> payload;
        for (size_t i = 2; i < args.size(); ++i) payload.push_back(args[i]);
        return api.makeEnum(typeId, 0, payload);
    });
    api.registerFunction("getVariant", [api](const std::vector<Value> &args) {
        if (args.empty() || !args[0].isEnumId()) throw std::runtime_error("getVariant() requires an enum argument");
        Value val = args[0];
        return api.makeString(api.getEnumVariantName(val.asEnumTypeId(), api.getEnumTag(val)));
    });
    api.registerFunction("getVariantPayload", [api](const std::vector<Value> &args) {
        if (args.empty() || !args[0].isEnumId()) throw std::runtime_error("getVariantPayload() requires an enum argument");
        Value val = args[0];
        uint32_t count = api.getEnumPayloadCount(val);
        Value arr = api.makeArray();
        for (uint32_t i = 0; i < count; ++i) {
            api.push(arr, api.getEnumPayload(val, i));
        }
        return arr;
    });
    auto typeObj = api.makeObject();
    api.setField(typeObj, "isNumber", api.makeFunctionRef("isNumber"));
    api.setField(typeObj, "isString", api.makeFunctionRef("isString"));
    api.setField(typeObj, "isArray", api.makeFunctionRef("isArray"));
    api.setField(typeObj, "isObject", api.makeFunctionRef("isObject"));
    api.setField(typeObj, "isNull", api.makeFunctionRef("isNull"));
    api.setField(typeObj, "isBoolean", api.makeFunctionRef("isBoolean"));
    api.setField(typeObj, "isEnum", api.makeFunctionRef("isEnum"));
    api.setField(typeObj, "toString", api.makeFunctionRef("toString"));
    api.setField(typeObj, "toNumber", api.makeFunctionRef("toNumber"));
    api.setField(typeObj, "newEnum", api.makeFunctionRef("newEnum"));
    api.setField(typeObj, "getVariant", api.makeFunctionRef("getVariant"));
    api.setField(typeObj, "getVariantPayload", api.makeFunctionRef("getVariantPayload"));
    api.setGlobal("Type", typeObj);
    api.setGlobal("getVariant", api.makeFunctionRef("getVariant"));
    api.setGlobal("newEnum", api.makeFunctionRef("newEnum"));
}

void registerTypeModule(const VMApi &api) {
    auto &vm = api.vm();
    Value exports;
    try {
        exports = vm.loadModule("type");
    } catch (...) {
        registerTypeFallback(api);
        return;
    }

    if (!exports.isObjectId()) {
        registerTypeFallback(api);
        return;
    }

 api.setGlobal("Type", exports);

 auto *obj = vm.getHeap().object(exports.asObjectId());
 if (obj) {
 for (const auto& [name, value] : *obj) {
 if (name.empty() || name[0] == '_') continue;
 api.setGlobal(name, value);
 }
 }

 // Also register type-check functions as host functions so they
 // appear in host_function_globals_ (LOAD_GLOBAL fallback). This
 // ensures they're always available even when globals are swapped
 // to a module-local context (e.g., during executePersistent).
 api.registerFunction("isNumber", [&vm](const std::vector<Value> &args) {
 if (args.empty()) throw std::runtime_error("isNumber() requires an argument");
 return Value(args[0].isInt() || args[0].isDouble());
 });
 api.registerFunction("isString", [&vm](const std::vector<Value> &args) {
 if (args.empty()) throw std::runtime_error("isString() requires an argument");
 return Value(args[0].isStringValId());
 });
 api.registerFunction("isArray", [&vm](const std::vector<Value> &args) {
 if (args.empty()) throw std::runtime_error("isArray() requires an argument");
 return Value(args[0].isArrayId());
 });
 api.registerFunction("isObject", [&vm](const std::vector<Value> &args) {
 if (args.empty()) throw std::runtime_error("isObject() requires an argument");
 return Value(args[0].isObjectId());
 });
 api.registerFunction("isNull", [&vm](const std::vector<Value> &args) {
 if (args.empty()) throw std::runtime_error("isNull() requires an argument");
 return Value(args[0].isNull());
 });
 api.registerFunction("isBoolean", [&vm](const std::vector<Value> &args) {
 if (args.empty()) throw std::runtime_error("isBoolean() requires an argument");
 return Value::makeBool(args[0].isBool());
 });
 api.registerFunction("isEnum", [&vm](const std::vector<Value> &args) {
 if (args.empty()) throw std::runtime_error("isEnum() requires an argument");
 return Value(args[0].isEnumId());
 });
 api.registerFunction("toString", [&vm](const std::vector<Value> &args) {
 if (args.empty()) throw std::runtime_error("toString() requires an argument");
 const auto &arg = args[0];
 if (arg.isNull()) return Value::makeNull();
 if (arg.isBool()) return Value::makeBool(arg.asBool());
 if (arg.isInt()) return Value::makeInt(arg.asInt());
 if (arg.isDouble()) return Value::makeDouble(arg.asDouble());
 if (arg.isStringValId()) return arg;
 return Value::makeNull();
 });
 api.registerFunction("toNumber", [&vm](const std::vector<Value> &args) {
 if (args.empty()) throw std::runtime_error("toNumber() requires an argument");
 const auto &arg = args[0];
 if (arg.isNull()) return Value::makeInt(0);
 if (arg.isBool()) return Value::makeInt(arg.asBool() ? 1 : 0);
 if (arg.isInt()) return arg;
 if (arg.isDouble()) return arg;
 return Value::makeInt(0);
 });

    api.registerFunction("newEnum", [api](const std::vector<Value> &args) {
        if (args.size() < 2) throw std::runtime_error("newEnum() requires at least type name and variant name");
        std::string typeName = api.toString(args[0]);
        std::string variantName = api.toString(args[1]);
        uint32_t typeId = api.registerEnumType(typeName, {variantName});
        std::vector<Value> payload;
        for (size_t i = 2; i < args.size(); ++i) payload.push_back(args[i]);
        return api.makeEnum(typeId, 0, payload);
    });
    api.registerFunction("getVariant", [api](const std::vector<Value> &args) {
        if (args.empty() || !args[0].isEnumId()) throw std::runtime_error("getVariant() requires an enum argument");
        Value val = args[0];
        return api.makeString(api.getEnumVariantName(val.asEnumTypeId(), api.getEnumTag(val)));
    });
    api.registerFunction("getVariantPayload", [api](const std::vector<Value> &args) {
        if (args.empty() || !args[0].isEnumId()) throw std::runtime_error("getVariantPayload() requires an enum argument");
        Value val = args[0];
        uint32_t count = api.getEnumPayloadCount(val);
        Value arr = api.makeArray();
        for (uint32_t i = 0; i < count; ++i) {
            api.push(arr, api.getEnumPayload(val, i));
        }
        return arr;
    });

    if (obj) {
        (*obj)["newEnum"] = api.makeFunctionRef("newEnum");
        (*obj)["getVariant"] = api.makeFunctionRef("getVariant");
        (*obj)["getVariantPayload"] = api.makeFunctionRef("getVariantPayload");
    }

    api.setGlobal("newEnum", api.makeFunctionRef("newEnum"));
    api.setGlobal("getVariant", api.makeFunctionRef("getVariant"));
}

} // namespace havel::stdlib
