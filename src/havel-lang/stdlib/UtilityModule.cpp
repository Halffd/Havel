/* UtilityModule.cpp - VM-native stdlib module */
#include "UtilityModule.hpp"

using havel::compiler::ArrayRef;
using havel::compiler::Value;
using havel::compiler::ObjectRef;
using havel::compiler::VMApi;

namespace havel::stdlib {

// Register utility module with VMApi (stable API layer)
void registerUtilityModule(VMApi &api) {
  // keys(obj) - Get keys from object/map
  api.registerFunction("keys", [&api](const std::vector<Value> &args) {
    if (args.empty())
      throw std::runtime_error("keys() requires an object");
    if (!args[0].isObjectId())
      throw std::runtime_error("keys() requires an object argument");

    auto objRef = ObjectRef{args[0].asObjectId(), true};
    auto keys = api.getObjectKeys(args[0]);
    auto result = api.makeArray();

    for (const auto &key : keys) {
      // TODO: string pool registration
      api.push(result, Value::makeNull());
    }

    return Value(result);
  });

  // items(obj) - Get key-value pairs from object/map
  api.registerFunction("items", [&api](const std::vector<Value> &args) {
    if (args.empty())
      throw std::runtime_error("items() requires an object");
    if (!args[0].isObjectId())
      throw std::runtime_error("items() requires an object argument");

    auto objRef = ObjectRef{args[0].asObjectId(), true};
    auto keys = api.getObjectKeys(args[0]);
    auto result = api.makeArray();

    for (const auto &key : keys) {
      auto pair = api.makeArray();
      // TODO: string pool registration
      api.push(pair, Value::makeNull());
      // Note: getObjectValue not available in VMApi, so we'll use hasField
      // and return a placeholder for the value
      if (api.hasField(args[0], key)) {
        api.push(pair, api.getField(args[0], key));
      } else {
        api.push(pair, Value::makeNull());
      }
      api.push(result, pair);
    }

    return Value(result);
  });

  // list(value) - Convert to list
  api.registerFunction("list", [&api](const std::vector<Value> &args) {
    if (args.empty())
      throw std::runtime_error("list() requires an argument");

    const auto &arg = args[0];

    // If already an array, return it
    if (arg.isArrayId()) {
      return arg;
    }

    // If string, convert to array of characters
    if (arg.isStringValId()) {
      // TODO: string pool lookup
      std::string str = "<string:" + std::to_string(arg.asStringValId()) + ">";
      auto result = api.makeArray();
      for (char c : str) {
        // TODO: string pool registration
        api.push(result, Value::makeNull());
      }
      return result;
    }

    // Otherwise, wrap in array
    auto result = api.makeArray();
    api.push(result, arg);
    return Value(result);
  });

  // type(value) - Get type of value (alias for type.of)
  api.registerFunction("type", [](const std::vector<Value> &args) {
    if (args.empty())
      throw std::runtime_error("type() requires an argument");

    const auto &arg = args[0];

    if (arg.isNull())
      return Value::makeNull();
    if (arg.isBool())
      return Value::makeNull();
    if (arg.isInt())
      return Value::makeNull();
    if (arg.isDouble())
      return Value::makeNull();
    if (arg.isStringValId())
      return arg;
    if (arg.isArrayId())
      return Value::makeNull();
    if (arg.isObjectId())
      return Value::makeNull();

    return Value::makeNull();
  });

  // len(value) - Get length of string or array
  api.registerFunction("len", [&api](const std::vector<Value> &args) {
    if (args.empty())
      throw std::runtime_error("len() requires an argument");

    const auto &arg = args[0];

    if (arg.isStringValId()) {
      // TODO: string pool lookup
      std::string s = "<string:" + std::to_string(arg.asStringValId()) + ">";
      return Value::makeInt(static_cast<int64_t>(s.length()));
    }

    if (arg.isArrayId()) {
      return Value::makeInt(static_cast<int64_t>(api.getArrayLength(Value::makeArrayId(arg.asArrayId()))));
    }

    throw std::runtime_error("len() requires a string or array");
  });

  // tap(value, fn) - Call function with value, return value (for pipeline debugging)
  api.registerFunction("tap", [&api](const std::vector<Value> &args) {
    if (args.size() < 2)
      throw std::runtime_error("tap() requires value and function");

    const auto &value = args[0];
    const auto &fnArg = args[1];

    // Call the function with the value
    std::vector<Value> callArgs = {value};
    api.callFunction(fnArg, callArgs);

    // Return the original value
    return value;
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
