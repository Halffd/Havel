/* TypeModule.cpp - VM-native stdlib module */
#include "TypeModule.hpp"

using havel::compiler::BytecodeValue;
using havel::compiler::VMApi;

namespace havel::stdlib {

// Register type module with VMApi (stable API layer)
void registerTypeModule(VMApi &api) {
  // isNumber(value) - Check if value is a number
  api.registerFunction("isNumber", [](const std::vector<BytecodeValue> &args) {
    if (args.empty())
      throw std::runtime_error("isNumber() requires an argument");

    const auto &arg = args[0];
    return BytecodeValue(arg.isInt() || arg.isDouble());
  });

  // isString(value) - Check if value is a string
  api.registerFunction("isString", [](const std::vector<BytecodeValue> &args) {
    if (args.empty())
      throw std::runtime_error("isString() requires an argument");

    const auto &arg = args[0];
    return BytecodeValue(arg.isStringValId());
  });

  // isArray(value) - Check if value is an array
  api.registerFunction("isArray", [](const std::vector<BytecodeValue> &args) {
    if (args.empty())
      throw std::runtime_error("isArray() requires an argument");

    const auto &arg = args[0];
    return BytecodeValue(arg.isArrayId());
  });

  // isObject(value) - Check if value is an object
  api.registerFunction("isObject", [](const std::vector<BytecodeValue> &args) {
    if (args.empty())
      throw std::runtime_error("isObject() requires an argument");

    const auto &arg = args[0];
    return BytecodeValue(arg.isObjectId());
  });

  // isNull(value) - Check if value is null
  api.registerFunction("isNull", [](const std::vector<BytecodeValue> &args) {
    if (args.empty())
      throw std::runtime_error("isNull() requires an argument");

    const auto &arg = args[0];
    return BytecodeValue(arg.isNull());
  });

  // isBoolean(value) - Check if value is a boolean
  api.registerFunction("isBoolean", [](const std::vector<BytecodeValue> &args) {
    if (args.empty())
      throw std::runtime_error("isBoolean() requires an argument");

    const auto &arg = args[0];
    return BytecodeValue(arg.isBool());
  });

  // toString(value) - Convert value to string
  api.registerFunction("toString", [](const std::vector<BytecodeValue> &args) {
    if (args.empty())
      throw std::runtime_error("toString() requires an argument");

    const auto &arg = args[0];

    if (arg.isNull())
      return BytecodeValue(std::string("null"));
    if (arg.isBool())
      return BytecodeValue(arg.asBool() ? "true" : "false");
    if (arg.isInt()) {
      std::ostringstream oss;
      oss << arg.asInt();
      return BytecodeValue(oss.str());
    }
    if (arg.isDouble()) {
      std::ostringstream oss;
      oss << arg.asDouble();
      return BytecodeValue(oss.str());
    }
    if (arg.isStringValId())
      return arg;

    return BytecodeValue(std::string("unknown"));
  });

  // toNumber(value) - Convert value to number
  api.registerFunction("toNumber", [](const std::vector<BytecodeValue> &args) {
    if (args.empty())
      throw std::runtime_error("toNumber() requires an argument");

    const auto &arg = args[0];

    if (arg.isNull())
      return BytecodeValue(static_cast<int64_t>(0));
    if (arg.isBool())
      return BytecodeValue(static_cast<int64_t>(arg.asBool() ? 1 : 0));
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
        return BytecodeValue(result);
      } catch (...) {
        try {
          // TODO: string pool lookup
          std::string s = "<string:" + std::to_string(arg.asStringValId()) + ">";
          double result = std::stod(s);
          return BytecodeValue(result);
        } catch (...) {
          return BytecodeValue(static_cast<int64_t>(0));
        }
      }
    }

    return BytecodeValue(static_cast<int64_t>(0));
  });

  // Register type object
  auto typeObj = api.makeObject();
  api.setField(typeObj, "isNumber", api.makeFunctionRef("isNumber"));
  api.setField(typeObj, "isString", api.makeFunctionRef("isString"));
  api.setField(typeObj, "isArray", api.makeFunctionRef("isArray"));
  api.setField(typeObj, "isObject", api.makeFunctionRef("isObject"));
  api.setField(typeObj, "isNull", api.makeFunctionRef("isNull"));
  api.setField(typeObj, "isBoolean", api.makeFunctionRef("isBoolean"));
  api.setField(typeObj, "toString", api.makeFunctionRef("toString"));
  api.setField(typeObj, "toNumber", api.makeFunctionRef("toNumber"));
  api.setGlobal("Type", typeObj);
}

} // namespace havel::stdlib
