#include "TypeModule.hpp"
#include "../compiler/vm/VM.hpp"
#include <vector>
#include <string>
#include <stdexcept>

using havel::compiler::Value;
using havel::compiler::VMApi;

namespace havel::stdlib {

void registerTypeModule(const VMApi &api) {
    api.registerFunction("type._isNumber", [](const std::vector<Value> &args) {
        if (args.empty()) throw std::runtime_error("type._isNumber() requires an argument");
        return Value(args[0].isInt() || args[0].isDouble());
    });
    api.registerFunction("type._isString", [](const std::vector<Value> &args) {
        if (args.empty()) throw std::runtime_error("type._isString() requires an argument");
        return Value(args[0].isStringValId());
    });
    api.registerFunction("type._isArray", [](const std::vector<Value> &args) {
        if (args.empty()) throw std::runtime_error("type._isArray() requires an argument");
        return Value(args[0].isArrayId());
    });
    api.registerFunction("type._isObject", [](const std::vector<Value> &args) {
        if (args.empty()) throw std::runtime_error("type._isObject() requires an argument");
        return Value(args[0].isObjectId());
    });
    api.registerFunction("type._isNull", [](const std::vector<Value> &args) {
        if (args.empty()) throw std::runtime_error("type._isNull() requires an argument");
        return Value(args[0].isNull());
    });
    api.registerFunction("type._isBoolean", [](const std::vector<Value> &args) {
        if (args.empty()) throw std::runtime_error("type._isBoolean() requires an argument");
        return Value::makeBool(args[0].isBool());
    });
    api.registerFunction("type._isEnum", [](const std::vector<Value> &args) {
        if (args.empty()) throw std::runtime_error("type._isEnum() requires an argument");
        return Value(args[0].isEnumId());
    });
    api.registerFunction("type._newEnum", [api](const std::vector<Value> &args) {
        if (args.size() < 2) throw std::runtime_error("type._newEnum() requires at least type name and variant name");
        std::string typeName = api.toString(args[0]);
        std::string variantName = api.toString(args[1]);
        uint32_t typeId = api.registerEnumType(typeName, {variantName});
        std::vector<Value> payload;
        for (size_t i = 2; i < args.size(); ++i) payload.push_back(args[i]);
        return api.makeEnum(typeId, 0, payload);
    });
    api.registerFunction("type._getVariant", [api](const std::vector<Value> &args) {
        if (args.empty() || !args[0].isEnumId()) throw std::runtime_error("type._getVariant() requires an enum argument");
        Value val = args[0];
        return api.makeString(api.getEnumVariantName(val.asEnumTypeId(), api.getEnumTag(val)));
    });
    api.registerFunction("type._getVariantPayload", [api](const std::vector<Value> &args) {
        if (args.empty() || !args[0].isEnumId()) throw std::runtime_error("type._getVariantPayload() requires an enum argument");
        Value val = args[0];
        uint32_t count = api.getEnumPayloadCount(val);
        Value arr = api.makeArray();
        for (uint32_t i = 0; i < count; ++i) {
            api.push(arr, api.getEnumPayload(val, i));
        }
        return arr;
    });

    auto typeObj = api.makeObject();
    api.setField(typeObj, "_isNumber", api.makeFunctionRef("type._isNumber"));
    api.setField(typeObj, "_isString", api.makeFunctionRef("type._isString"));
    api.setField(typeObj, "_isArray", api.makeFunctionRef("type._isArray"));
    api.setField(typeObj, "_isObject", api.makeFunctionRef("type._isObject"));
    api.setField(typeObj, "_isNull", api.makeFunctionRef("type._isNull"));
    api.setField(typeObj, "_isBoolean", api.makeFunctionRef("type._isBoolean"));
    api.setField(typeObj, "_isEnum", api.makeFunctionRef("type._isEnum"));
    api.setField(typeObj, "_newEnum", api.makeFunctionRef("type._newEnum"));
    api.setField(typeObj, "_getVariant", api.makeFunctionRef("type._getVariant"));
    api.setField(typeObj, "_getVariantPayload", api.makeFunctionRef("type._getVariantPayload"));
    api.setGlobal("Type", typeObj);

    auto &vm = api.vm();
    Value exports;
    try {
        exports = vm.loadModule("type/type");
    } catch (...) {
    }

    if (exports.isObjectId()) {
        auto *obj = vm.getHeap().object(exports.asObjectId());
        if (obj) {
            for (const auto& [name, value] : *obj) {
                if (name.empty() || name[0] == '_') continue;
                api.setField(typeObj, name, value);
            }
        }
    }

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
}

} // namespace havel::stdlib

#ifdef HAVEL_MODULE_PLUGIN
#include "c/ModulePlugin.h"
HAVEL_MODULE_PLUGIN_EAGER(type, "1.0.0", "Type introspection stdlib module",
    havel::stdlib::registerTypeModule(*api);
)
#endif
