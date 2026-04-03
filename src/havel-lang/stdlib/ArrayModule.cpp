/* ArrayModule.cpp - VM-native stdlib module */
#include "ArrayModule.hpp"

using havel::compiler::ArrayRef;
using havel::compiler::Value;
using havel::compiler::VMApi;

namespace havel::stdlib {

// Register array module with VMApi (stable API layer)
void registerArrayModule(VMApi &api) {
  // Helper: convert Value to number
  auto toNumber = [](const Value &v) -> double {
    if (v.isInt())
      return static_cast<double>(v.asInt());
    if (v.isDouble())
      return v.asDouble();
    return 0.0;
  };

  // Register array functions via VMApi - capture VM reference directly
  api.registerFunction(
      "array.len", [&api](const std::vector<Value> &args) {
        if (args.empty())
          throw std::runtime_error("array.len() requires an array");
        if (!args[0].isArrayId())
          throw std::runtime_error("array.len() requires an array argument");

        return Value::makeInt(static_cast<int64_t>(api.getArrayLength(Value::makeArrayId(args[0].asArrayId()))));
      });

  api.registerFunction(
      "array.push", [&api](const std::vector<Value> &args) {
        if (args.size() < 2)
          throw std::runtime_error("array.push() requires array and value");
        if (!args[0].isArrayId())
          throw std::runtime_error("array.push() first argument must be array");

        const auto &arrRef = ArrayRef{args[0].asArrayId()};
        api.push(Value::makeArrayId(arrRef.id), args[1]);
        return Value::makeArrayId(arrRef.id);
      });

  api.registerFunction(
      "array.pop", [&api](const std::vector<Value> &args) {
        if (args.empty())
          throw std::runtime_error("array.pop() requires an array");
        if (!args[0].isArrayId())
          throw std::runtime_error("array.pop() requires an array argument");

        return api.popArrayValue(Value::makeArrayId(args[0].asArrayId()));
      });

  api.registerFunction(
      "array.join", [&api](const std::vector<Value> &args) {
        if (args.empty())
          throw std::runtime_error("array.join() requires at least 1 argument");
        if (!args[0].isArrayId())
          throw std::runtime_error("array.join() first argument must be array");

        auto arrRef = ArrayRef{args[0].asArrayId()};
        std::string delim =
            (args.size() > 1 && args[1].isStringValId())
                ? "<string:" + std::to_string(args[1].asStringValId()) + ">"
                : ",";
        size_t len = api.getArrayLength(Value::makeArrayId(arrRef.id));

        std::string result;
        for (size_t i = 0; i < len; ++i) {
          if (i > 0)
            result += delim;
          auto val = api.getArrayValue(Value::makeArrayId(arrRef.id), i);
          if (val.isStringValId()) {
            result += "<string:" + std::to_string(val.asStringValId()) + ">";
          } else if (val.isInt()) {
            result += std::to_string(val.asInt());
          } else if (val.isDouble()) {
            result += std::to_string(val.asDouble());
          }
        }
        // TODO: string pool integration - for now return null
        (void)result;
        return Value::makeNull();
      });

  api.registerFunction(
      "array.slice", [&api](const std::vector<Value> &args) {
        if (args.size() < 2)
          throw std::runtime_error(
              "array.slice() requires at least 2 arguments");
        if (!args[0].isArrayId())
          throw std::runtime_error(
              "array.slice() first argument must be array");

        auto arrRef = ArrayRef{args[0].asArrayId()};
        int64_t start = args[1].isInt()
                            ? args[1].asInt()
                            : 0;
        int64_t end =
            (args.size() > 2 && args[2].isInt())
                ? args[2].asInt()
                : api.getArrayLength(Value::makeArrayId(arrRef.id));

        if (start < 0)
          start = 0;
        if (end < 0)
          end = 0;
        if (start > end)
          start = end;

        auto result = api.makeArray();
        for (int64_t i = start; i < end; ++i) {
          api.push(result, api.getArrayValue(Value::makeArrayId(arrRef.id), i));
        }
        return result;
      });

  api.registerFunction("array.concat",
                       [&api](const std::vector<Value> &args) {
                         if (args.empty())
                           throw std::runtime_error(
                               "array.concat() requires at least 1 argument");

                         auto result = api.makeArray();
                         for (const auto &arg : args) {
                           if (!arg.isArrayId())
                             throw std::runtime_error(
                                 "array.concat() all arguments must be arrays");

                           auto arrRef = ArrayRef{arg.asArrayId()};
                           size_t len = api.getArrayLength(Value::makeArrayId(arg.asArrayId()));
                           for (size_t i = 0; i < len; ++i) {
                             api.push(result, api.getArrayValue(Value::makeArrayId(arrRef.id), i));
                           }
                         }
                         return result;
                       });

  api.registerFunction(
      "array.shift", [&api](const std::vector<Value> &args) {
        if (args.empty())
          throw std::runtime_error("array.shift() requires an array");
        if (!args[0].isArrayId())
          throw std::runtime_error("array.shift() requires an array argument");

        return api.removeArrayValue(Value::makeArrayId(args[0].asArrayId()), 0);
      });

  api.registerFunction(
      "array.reverse", [&api](const std::vector<Value> &args) {
        if (args.empty())
          throw std::runtime_error("array.reverse() requires an array");
        if (!args[0].isArrayId())
          throw std::runtime_error(
              "array.reverse() requires an array argument");

        auto arrRef = ArrayRef{args[0].asArrayId()};
        size_t len = api.getArrayLength(args[0]);

        auto result = api.makeArray();
        for (size_t i = 0; i < len; ++i) {
          api.push(result, api.getArrayValue(Value::makeArrayId(arrRef.id), len - 1 - i));
        }
        return result;
      });

  api.registerFunction(
      "array.map", [&api](const std::vector<Value> &args) {
        if (args.size() < 2)
          throw std::runtime_error("array.map() requires array and function");
        if (!args[0].isArrayId())
          throw std::runtime_error("array.map() first argument must be array");

        auto arrRef = ArrayRef{args[0].asArrayId()};
        const auto &mapper = args[1];
        size_t len = api.getArrayLength(Value::makeArrayId(arrRef.id));

        auto result = api.makeArray();
        for (size_t i = 0; i < len; ++i) {
          auto val = api.getArrayValue(Value::makeArrayId(arrRef.id), i);
          api.push(result, api.callFunction(mapper, {val}));
        }
        return result;
      });

  api.registerFunction("array.filter",
                       [&api](const std::vector<Value> &args) {
                         if (args.size() < 2)
                           throw std::runtime_error(
                               "array.filter() requires array and predicate");
                         if (!args[0].isArrayId())
                           throw std::runtime_error(
                               "array.filter() first argument must be array");

                         auto arrRef = ArrayRef{args[0].asArrayId()};
                         const auto &predicate = args[1];
                         size_t len = api.getArrayLength(Value::makeArrayId(arrRef.id));

                         auto result = api.makeArray();
                         for (size_t i = 0; i < len; ++i) {
                           auto val = api.getArrayValue(Value::makeArrayId(arrRef.id), i);
                           auto keep = api.callFunction(predicate, {val});
                           if (keep.isBool() && keep.asBool()) {
                             api.push(result, val);
                           }
                         }
                         return result;
                       });

  // array.flat() - Flatten one level
  api.registerFunction("array.flat",
                       [&api](const std::vector<Value> &args) {
                         if (args.empty())
                           throw std::runtime_error("array.flat() requires an array");
                         if (!args[0].isArrayId())
                           throw std::runtime_error("array.flat() first argument must be array");

                         auto arrRef = ArrayRef{args[0].asArrayId()};
                         size_t len = api.getArrayLength(Value::makeArrayId(arrRef.id));
                         auto result = api.makeArray();

                         for (size_t i = 0; i < len; ++i) {
                           auto val = api.getArrayValue(Value::makeArrayId(arrRef.id), i);
                           if (val.isArrayId()) {
                             auto innerRef = ArrayRef{val.asArrayId()};
                             size_t innerLen = api.getArrayLength(Value::makeArrayId(val.asArrayId()));
                             for (size_t j = 0; j < innerLen; ++j) {
                               api.push(result, api.getArrayValue(Value::makeArrayId(innerRef.id), j));
                             }
                           } else {
                             api.push(result, val);
                           }
                         }
                         return result;
                       });

  // array.smooth() - Recursively flatten all levels
  api.registerFunction("array.smooth",
                       [&api](const std::vector<Value> &args) {
                         if (args.empty())
                           throw std::runtime_error("array.smooth() requires an array");
                         if (!args[0].isArrayId())
                           throw std::runtime_error("array.smooth() first argument must be array");

                         auto arrRef = ArrayRef{args[0].asArrayId()};
                         size_t len = api.getArrayLength(Value::makeArrayId(arrRef.id));
                         auto result = api.makeArray();

                         std::function<void(const Value&)> flatten = [&](const Value& val) {
                           if (val.isArrayId()) {
                             auto innerRef = ArrayRef{val.asArrayId()};
                             size_t innerLen = api.getArrayLength(Value::makeArrayId(val.asArrayId()));
                             for (size_t j = 0; j < innerLen; ++j) {
                               flatten(api.getArrayValue(Value::makeArrayId(innerRef.id), j));
                             }
                           } else {
                             api.push(result, val);
                           }
                         };

                         for (size_t i = 0; i < len; ++i) {
                           flatten(api.getArrayValue(Value::makeArrayId(arrRef.id), i));
                         }
                         return result;
                       });

  // array.squeeze() - Remove null/undefined/empty values
  api.registerFunction("array.squeeze",
                       [&api](const std::vector<Value> &args) {
                         if (args.empty())
                           throw std::runtime_error("array.squeeze() requires an array");
                         if (!args[0].isArrayId())
                           throw std::runtime_error("array.squeeze() first argument must be array");

                         auto arrRef = ArrayRef{args[0].asArrayId()};
                         size_t len = api.getArrayLength(Value::makeArrayId(arrRef.id));
                         auto result = api.makeArray();

                         for (size_t i = 0; i < len; ++i) {
                           auto val = api.getArrayValue(Value::makeArrayId(arrRef.id), i);
                           // Keep non-null, non-undefined, non-empty values
                           bool keep = true;
                           if (val.isNull()) {
                             keep = false;
                           } else if (val.isStringValId()) {
                             // TODO: string pool lookup - assume non-empty
                             keep = true;
                           } else if (val.isArrayId()) {
                             keep = api.getArrayLength(Value::makeArrayId(val.asArrayId())) > 0;
                           }
                           if (keep) {
                             api.push(result, val);
                           }
                         }
                         return result;
                       });

  // array.flatMap() - Map then flatten one level
  api.registerFunction("array.flatMap",
                       [&api](const std::vector<Value> &args) {
                         if (args.size() < 2)
                           throw std::runtime_error("array.flatMap() requires array and function");
                         if (!args[0].isArrayId())
                           throw std::runtime_error("array.flatMap() first argument must be array");

                         auto arrRef = ArrayRef{args[0].asArrayId()};
                         const auto &fn = args[1];
                         size_t len = api.getArrayLength(Value::makeArrayId(arrRef.id));
                         auto result = api.makeArray();

                         for (size_t i = 0; i < len; ++i) {
                           auto val = api.getArrayValue(Value::makeArrayId(arrRef.id), i);
                           auto mapped = api.callFunction(fn, {val});
                           if (mapped.isArrayId()) {
                             auto innerRef = ArrayRef{mapped.asArrayId()};
                             size_t innerLen = api.getArrayLength(Value::makeArrayId(mapped.asArrayId()));
                             for (size_t j = 0; j < innerLen; ++j) {
                               api.push(result, api.getArrayValue(Value::makeArrayId(innerRef.id), j));
                             }
                           } else {
                             api.push(result, mapped);
                           }
                         }
                         return result;
                       });

  // Register array object
  auto arrObj = api.makeObject();
  api.setField(arrObj, "len", api.makeFunctionRef("array.len"));
  api.setField(arrObj, "push", api.makeFunctionRef("array.push"));
  api.setField(arrObj, "pop", api.makeFunctionRef("array.pop"));
  api.setField(arrObj, "shift", api.makeFunctionRef("array.shift"));
  api.setField(arrObj, "join", api.makeFunctionRef("array.join"));
  api.setField(arrObj, "slice", api.makeFunctionRef("array.slice"));
  api.setField(arrObj, "concat", api.makeFunctionRef("array.concat"));
  api.setField(arrObj, "reverse", api.makeFunctionRef("array.reverse"));
  api.setField(arrObj, "map", api.makeFunctionRef("array.map"));
  api.setField(arrObj, "filter", api.makeFunctionRef("array.filter"));
  api.setField(arrObj, "flat", api.makeFunctionRef("array.flat"));
  api.setField(arrObj, "smooth", api.makeFunctionRef("array.smooth"));
  api.setField(arrObj, "squeeze", api.makeFunctionRef("array.squeeze"));
  api.setField(arrObj, "flatMap", api.makeFunctionRef("array.flatMap"));
  api.setGlobal("Array", arrObj);

  // Register global aliases
  api.registerFunction("join", [&api](const std::vector<Value> &args) {
    if (args.size() < 1) {
      throw std::runtime_error("join() requires at least 1 argument");
    }
    if (!args[0].isArrayId()) {
      throw std::runtime_error("join() first argument must be array");
    }
    auto arrRef = ArrayRef{args[0].asArrayId()};

    std::string sep = (args.size() > 1 && args[1].isStringValId())
      ? "<string:" + std::to_string(args[1].asStringValId()) + ">" : "";

    size_t len = api.getArrayLength(Value::makeArrayId(args[0].asArrayId()));
    std::string result;
    for (size_t i = 0; i < len; ++i) {
      if (i > 0) result += sep;
      auto val = api.getArrayValue(Value::makeArrayId(arrRef.id), i);
      if (val.isStringValId()) {
        result += "<string:" + std::to_string(val.asStringValId()) + ">";
      } else if (val.isInt()) {
        result += std::to_string(val.asInt());
      } else if (val.isDouble()) {
        result += std::to_string(val.asDouble());
      }
    }
    // TODO: string pool integration - for now return null
    (void)result;
    return Value::makeNull();
  });

  // Global flat alias
  api.registerFunction("flat", [&api](const std::vector<Value> &args) {
    if (args.empty())
      throw std::runtime_error("flat() requires an array");
    if (!args[0].isArrayId())
      throw std::runtime_error("flat() first argument must be array");
    return api.callFunction(api.makeFunctionRef("array.flat"), args);
  });

  // Global smooth alias
  api.registerFunction("smooth", [&api](const std::vector<Value> &args) {
    if (args.empty())
      throw std::runtime_error("smooth() requires an array");
    if (!args[0].isArrayId())
      throw std::runtime_error("smooth() first argument must be array");
    return api.callFunction(api.makeFunctionRef("array.smooth"), args);
  });

  // Global squeeze alias
  api.registerFunction("squeeze", [&api](const std::vector<Value> &args) {
    if (args.empty())
      throw std::runtime_error("squeeze() requires an array");
    if (!args[0].isArrayId())
      throw std::runtime_error("squeeze() first argument must be array");
    return api.callFunction(api.makeFunctionRef("array.squeeze"), args);
  });

  // Global flatMap alias
  api.registerFunction("flatMap", [&api](const std::vector<Value> &args) {
    if (args.size() < 2)
      throw std::runtime_error("flatMap() requires array and function");
    if (!args[0].isArrayId())
      throw std::runtime_error("flatMap() first argument must be array");
    return api.callFunction(api.makeFunctionRef("array.flatMap"), args);
  });
}

} // namespace havel::stdlib
