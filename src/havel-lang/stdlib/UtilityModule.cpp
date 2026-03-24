/* UtilityModule.cpp - VM-native stdlib module */
#include "UtilityModule.hpp"

using havel::compiler::ArrayRef;
using havel::compiler::BytecodeValue;
using havel::compiler::ObjectRef;
using havel::compiler::VMApi;

namespace havel::stdlib {

// Register utility module with VMApi (stable API layer)
void registerUtilityModule(VMApi &api) {
  // keys(obj) - Get keys from object/map
  api.registerFunction("keys", [&api](const std::vector<BytecodeValue> &args) {
    if (args.empty())
      throw std::runtime_error("keys() requires an object");
    if (!std::holds_alternative<ObjectRef>(args[0]))
      throw std::runtime_error("keys() requires an object argument");

    auto objRef = std::get<ObjectRef>(args[0]);
    auto keys = api.getObjectKeys(objRef);
    auto result = api.makeArray();

    for (const auto &key : keys) {
      api.push(result, BytecodeValue(key));
    }

    return BytecodeValue(result);
  });

  // items(obj) - Get key-value pairs from object/map
  api.registerFunction("items", [&api](const std::vector<BytecodeValue> &args) {
    if (args.empty())
      throw std::runtime_error("items() requires an object");
    if (!std::holds_alternative<ObjectRef>(args[0]))
      throw std::runtime_error("items() requires an object argument");

    auto objRef = std::get<ObjectRef>(args[0]);
    auto keys = api.getObjectKeys(objRef);
    auto result = api.makeArray();

    for (const auto &key : keys) {
      auto pair = api.makeArray();
      api.push(pair, BytecodeValue(key));
      // Note: getObjectValue not available in VMApi, so we'll use hasField
      // and return a placeholder for the value
      if (api.hasField(objRef, key)) {
        api.push(pair, BytecodeValue(std::string("value")));
      } else {
        api.push(pair, BytecodeValue(nullptr));
      }
      api.push(result, BytecodeValue(pair));
    }

    return BytecodeValue(result);
  });

  // list(value) - Convert to list
  api.registerFunction("list", [&api](const std::vector<BytecodeValue> &args) {
    if (args.empty())
      throw std::runtime_error("list() requires an argument");

    const auto &arg = args[0];

    // If already an array, return it
    if (std::holds_alternative<ArrayRef>(arg)) {
      return arg;
    }

    // If string, convert to array of characters
    if (std::holds_alternative<std::string>(arg)) {
      const auto &str = std::get<std::string>(arg);
      auto result = api.makeArray();
      for (char c : str) {
        api.push(result, BytecodeValue(std::string(1, c)));
      }
      return BytecodeValue(result);
    }

    // Otherwise, wrap in array
    auto result = api.makeArray();
    api.push(result, arg);
    return BytecodeValue(result);
  });

  // type(value) - Get type of value
  api.registerFunction("type", [](const std::vector<BytecodeValue> &args) {
    if (args.empty())
      throw std::runtime_error("type() requires an argument");

    const auto &arg = args[0];

    if (std::holds_alternative<std::nullptr_t>(arg))
      return BytecodeValue(std::string("null"));
    if (std::holds_alternative<bool>(arg))
      return BytecodeValue(std::string("boolean"));
    if (std::holds_alternative<int64_t>(arg))
      return BytecodeValue(std::string("number"));
    if (std::holds_alternative<double>(arg))
      return BytecodeValue(std::string("number"));
    if (std::holds_alternative<std::string>(arg))
      return BytecodeValue(std::string("string"));
    if (std::holds_alternative<ArrayRef>(arg))
      return BytecodeValue(std::string("array"));
    if (std::holds_alternative<ObjectRef>(arg))
      return BytecodeValue(std::string("object"));

    return BytecodeValue(std::string("unknown"));
  });

  // len(value) - Get length of string or array
  api.registerFunction("len", [&api](const std::vector<BytecodeValue> &args) {
    if (args.empty())
      throw std::runtime_error("len() requires an argument");

    const auto &arg = args[0];

    if (std::holds_alternative<std::string>(arg)) {
      return BytecodeValue(
          static_cast<int64_t>(std::get<std::string>(arg).length()));
    }

    if (std::holds_alternative<ArrayRef>(arg)) {
      return BytecodeValue(static_cast<int64_t>(api.getArrayLength(arg)));
    }

    throw std::runtime_error("len() requires a string or array");
  });

  // Register utility object
  auto utilObj = api.makeObject();
  api.setField(utilObj, "keys", api.makeFunctionRef("keys"));
  api.setField(utilObj, "items", api.makeFunctionRef("items"));
  api.setField(utilObj, "list", api.makeFunctionRef("list"));
  api.setField(utilObj, "type", api.makeFunctionRef("type"));
  api.setField(utilObj, "len", api.makeFunctionRef("len"));
  api.setGlobal("Utility", utilObj);
}

} // namespace havel::stdlib
