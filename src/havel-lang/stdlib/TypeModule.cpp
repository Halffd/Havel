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
    return BytecodeValue(std::holds_alternative<int64_t>(arg) ||
                         std::holds_alternative<double>(arg));
  });

  // isString(value) - Check if value is a string
  api.registerFunction("isString", [](const std::vector<BytecodeValue> &args) {
    if (args.empty())
      throw std::runtime_error("isString() requires an argument");

    const auto &arg = args[0];
    return BytecodeValue(std::holds_alternative<std::string>(arg));
  });

  // isArray(value) - Check if value is an array
  api.registerFunction("isArray", [](const std::vector<BytecodeValue> &args) {
    if (args.empty())
      throw std::runtime_error("isArray() requires an argument");

    const auto &arg = args[0];
    return BytecodeValue(
        std::holds_alternative<havel::compiler::ArrayRef>(arg));
  });

  // isObject(value) - Check if value is an object
  api.registerFunction("isObject", [](const std::vector<BytecodeValue> &args) {
    if (args.empty())
      throw std::runtime_error("isObject() requires an argument");

    const auto &arg = args[0];
    return BytecodeValue(
        std::holds_alternative<havel::compiler::ObjectRef>(arg));
  });

  // isNull(value) - Check if value is null
  api.registerFunction("isNull", [](const std::vector<BytecodeValue> &args) {
    if (args.empty())
      throw std::runtime_error("isNull() requires an argument");

    const auto &arg = args[0];
    return BytecodeValue(std::holds_alternative<std::nullptr_t>(arg));
  });

  // isBoolean(value) - Check if value is a boolean
  api.registerFunction("isBoolean", [](const std::vector<BytecodeValue> &args) {
    if (args.empty())
      throw std::runtime_error("isBoolean() requires an argument");

    const auto &arg = args[0];
    return BytecodeValue(std::holds_alternative<bool>(arg));
  });

  // toString(value) - Convert value to string
  api.registerFunction("toString", [](const std::vector<BytecodeValue> &args) {
    if (args.empty())
      throw std::runtime_error("toString() requires an argument");

    const auto &arg = args[0];

    if (std::holds_alternative<std::nullptr_t>(arg))
      return BytecodeValue(std::string("null"));
    if (std::holds_alternative<bool>(arg))
      return BytecodeValue(std::get<bool>(arg) ? "true" : "false");
    if (std::holds_alternative<int64_t>(arg)) {
      std::ostringstream oss;
      oss << std::get<int64_t>(arg);
      return BytecodeValue(oss.str());
    }
    if (std::holds_alternative<double>(arg)) {
      std::ostringstream oss;
      oss << std::get<double>(arg);
      return BytecodeValue(oss.str());
    }
    if (std::holds_alternative<std::string>(arg))
      return arg;

    return BytecodeValue(std::string("unknown"));
  });

  // toNumber(value) - Convert value to number
  api.registerFunction("toNumber", [](const std::vector<BytecodeValue> &args) {
    if (args.empty())
      throw std::runtime_error("toNumber() requires an argument");

    const auto &arg = args[0];

    if (std::holds_alternative<std::nullptr_t>(arg))
      return BytecodeValue(static_cast<int64_t>(0));
    if (std::holds_alternative<bool>(arg))
      return BytecodeValue(static_cast<int64_t>(std::get<bool>(arg) ? 1 : 0));
    if (std::holds_alternative<int64_t>(arg))
      return arg;
    if (std::holds_alternative<double>(arg))
      return arg;
    if (std::holds_alternative<std::string>(arg)) {
      try {
        size_t pos;
        int64_t result = std::stoll(std::get<std::string>(arg), &pos);
        return BytecodeValue(result);
      } catch (...) {
        try {
          double result = std::stod(std::get<std::string>(arg));
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
