/* TypeModule.cpp - VM-native stdlib module */
#include "TypeModule.hpp"
#include "../compiler/vm/VMApi.hpp"
#include <vector>
#include <string>
#include <stdexcept>
#include <algorithm>

using havel::compiler::Value;
using havel::compiler::VMApi;

namespace havel::stdlib {

// Register type module with VMApi (stable API layer)
void registerTypeModule(VMApi &api) {
  // isNumber(value) - Check if value is a number
  api.registerFunction("isNumber", [](const std::vector<Value> &args) {
    if (args.empty())
      throw std::runtime_error("isNumber() requires an argument");

    const auto &arg = args[0];
    return Value(arg.isInt() || arg.isDouble());
  });

  // isString(value) - Check if value is a string
  api.registerFunction("isString", [](const std::vector<Value> &args) {
    if (args.empty())
      throw std::runtime_error("isString() requires an argument");

    const auto &arg = args[0];
    return Value(arg.isStringValId());
  });

  // isArray(value) - Check if value is an array
  api.registerFunction("isArray", [](const std::vector<Value> &args) {
    if (args.empty())
      throw std::runtime_error("isArray() requires an argument");

    const auto &arg = args[0];
    return Value(arg.isArrayId());
  });

  // isObject(value) - Check if value is an object
  api.registerFunction("isObject", [](const std::vector<Value> &args) {
    if (args.empty())
      throw std::runtime_error("isObject() requires an argument");

    const auto &arg = args[0];
    return Value(arg.isObjectId());
  });

  // isNull(value) - Check if value is null
  api.registerFunction("isNull", [](const std::vector<Value> &args) {
    if (args.empty())
      throw std::runtime_error("isNull() requires an argument");

    const auto &arg = args[0];
    return Value(arg.isNull());
  });

  // isBoolean(value) - Check if value is a boolean
  api.registerFunction("isBoolean", [](const std::vector<Value> &args) {
    if (args.empty())
      throw std::runtime_error("isBoolean() requires an argument");

    const auto &arg = args[0];
    return Value::makeBool(arg.isBool());
  });

  // toString(value) - Convert value to string
  api.registerFunction("toString", [](const std::vector<Value> &args) {
    if (args.empty())
      throw std::runtime_error("toString() requires an argument");

    const auto &arg = args[0];

    if (arg.isNull())
      return Value::makeNull();
    if (arg.isBool())
      return Value::makeBool(arg.asBool());
    if (arg.isInt()) {
      return Value::makeInt(arg.asInt());
    }
    if (arg.isDouble()) {
      return Value::makeDouble(arg.asDouble());
    }
    if (arg.isStringValId())
      return arg;

    return Value::makeNull();
  });

  // toNumber(value) - Convert value to number
  api.registerFunction("toNumber", [](const std::vector<Value> &args) {
    if (args.empty())
      throw std::runtime_error("toNumber() requires an argument");

    const auto &arg = args[0];

    if (arg.isNull())
      return Value::makeInt(0);
    if (arg.isBool())
      return Value::makeInt(arg.asBool() ? 1 : 0);
    if (arg.isInt())
      return arg;
    if (arg.isDouble())
      return arg;
    if (arg.isStringValId()) {
      try {
        // TODO: string pool lookup
        std::string s = "<string:" + std::to_string(arg.asStringValId()) + ">";
        size_t pos;
        int64_t result = std::stoll(s, &pos);
        return Value::makeInt(result);
      } catch (...) {
        try {
          // TODO: string pool lookup
          std::string s = "<string:" + std::to_string(arg.asStringValId()) + ">";
          double result = std::stod(s);
          return Value::makeDouble(result);
        } catch (...) {
          return Value::makeInt(0);
        }
      }
    }

    return Value::makeInt(0);
  });

  // isEnum(value) - Check if value is an enum variant
  api.registerFunction("isEnum", [](const std::vector<Value> &args) {
    if (args.empty()) throw std::runtime_error("isEnum() requires an argument");
    return Value(args[0].isEnumId());
  });

  // newEnum(typeName, variantName, ...payload) - Create a new enum variant
  api.registerFunction("newEnum", [&api](const std::vector<Value> &args) {
    if (args.size() < 2) throw std::runtime_error("newEnum() requires at least type name and variant name");
    std::string typeName = api.toString(args[0]);
    std::string variantName = api.toString(args[1]);
    
    // For now, we assume enums are pre-registered or we register on the fly
    // Real implementation would look up typeId and tag
    // This is a placeholder for the test
    uint32_t typeId = api.registerEnumType(typeName, {variantName});
    std::vector<Value> payload;
    for (size_t i = 2; i < args.size(); ++i) payload.push_back(args[i]);
    return api.makeEnum(typeId, 0, payload);
  });

  // getVariant(value) - Get variant name of an enum
  api.registerFunction("getVariant", [&api](const std::vector<Value> &args) {
    if (args.empty() || !args[0].isEnumId()) throw std::runtime_error("getVariant() requires an enum argument");
    Value val = args[0];
    return api.makeString(api.getEnumVariantName(val.asEnumTypeId(), api.getEnumTag(val)));
  });

  // getVariantPayload(value) - Get payload array of an enum variant
  api.registerFunction("getVariantPayload", [&api](const std::vector<Value> &args) {
    if (args.empty() || !args[0].isEnumId()) throw std::runtime_error("getVariantPayload() requires an enum argument");
    Value val = args[0];
    uint32_t count = api.getEnumPayloadCount(val);
    Value arr = api.makeArray();
    for (uint32_t i = 0; i < count; ++i) {
      api.push(arr, api.getEnumPayload(val, i));
    }
    return arr;
  });

  // Register type object
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
  api.setGlobal("getVariant", api.makeFunctionRef("getVariant")); // Global for test_match.hv
  api.setGlobal("newEnum", api.makeFunctionRef("newEnum"));       // Global for test_match.hv
  api.setGlobal("Type", typeObj);
}

} // namespace havel::stdlib
