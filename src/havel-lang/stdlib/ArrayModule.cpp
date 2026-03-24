/* ArrayModule.cpp - VM-native stdlib module */
#include "ArrayModule.hpp"

using havel::compiler::ArrayRef;
using havel::compiler::BytecodeValue;
using havel::compiler::VMApi;

namespace havel::stdlib {

// Register array module with VMApi (stable API layer)
void registerArrayModule(VMApi &api) {
  // Helper: convert BytecodeValue to number
  auto toNumber = [](const BytecodeValue &v) -> double {
    if (std::holds_alternative<int64_t>(v))
      return static_cast<double>(std::get<int64_t>(v));
    if (std::holds_alternative<double>(v))
      return std::get<double>(v);
    return 0.0;
  };

  // Register array functions via VMApi - capture VM reference directly
  api.registerFunction(
      "array.len", [&api](const std::vector<BytecodeValue> &args) {
        if (args.empty())
          throw std::runtime_error("array.len() requires an array");
        if (!std::holds_alternative<ArrayRef>(args[0]))
          throw std::runtime_error("array.len() requires an array argument");

        return BytecodeValue(static_cast<int64_t>(api.getArrayLength(args[0])));
      });

  api.registerFunction(
      "array.push", [&api](const std::vector<BytecodeValue> &args) {
        if (args.size() < 2)
          throw std::runtime_error("array.push() requires array and value");
        if (!std::holds_alternative<ArrayRef>(args[0]))
          throw std::runtime_error("array.push() first argument must be array");

        const auto &arrRef = std::get<ArrayRef>(args[0]);
        api.push(arrRef, args[1]);
        return BytecodeValue(arrRef);
      });

  api.registerFunction(
      "array.pop", [&api](const std::vector<BytecodeValue> &args) {
        if (args.empty())
          throw std::runtime_error("array.pop() requires an array");
        if (!std::holds_alternative<ArrayRef>(args[0]))
          throw std::runtime_error("array.pop() requires an array argument");

        return api.popArrayValue(args[0]);
      });

  api.registerFunction(
      "array.join", [&api](const std::vector<BytecodeValue> &args) {
        if (args.empty())
          throw std::runtime_error("array.join() requires at least 1 argument");
        if (!std::holds_alternative<ArrayRef>(args[0]))
          throw std::runtime_error("array.join() first argument must be array");

        auto arrRef = std::get<ArrayRef>(args[0]);
        std::string delim =
            (args.size() > 1 && std::holds_alternative<std::string>(args[1]))
                ? std::get<std::string>(args[1])
                : ",";
        size_t len = api.getArrayLength(args[0]);

        std::string result;
        for (size_t i = 0; i < len; ++i) {
          if (i > 0)
            result += delim;
          auto val = api.getArrayValue(arrRef, i);
          if (std::holds_alternative<std::string>(val)) {
            result += std::get<std::string>(val);
          } else if (std::holds_alternative<int64_t>(val)) {
            result += std::to_string(std::get<int64_t>(val));
          } else if (std::holds_alternative<double>(val)) {
            result += std::to_string(std::get<double>(val));
          }
        }
        return BytecodeValue(result);
      });

  api.registerFunction(
      "array.slice", [&api](const std::vector<BytecodeValue> &args) {
        if (args.size() < 2)
          throw std::runtime_error(
              "array.slice() requires at least 2 arguments");
        if (!std::holds_alternative<ArrayRef>(args[0]))
          throw std::runtime_error(
              "array.slice() first argument must be array");

        auto arrRef = std::get<ArrayRef>(args[0]);
        int64_t start = std::holds_alternative<int64_t>(args[1])
                            ? std::get<int64_t>(args[1])
                            : 0;
        int64_t end =
            (args.size() > 2 && std::holds_alternative<int64_t>(args[2]))
                ? std::get<int64_t>(args[2])
                : api.getArrayLength(arrRef);

        if (start < 0)
          start = 0;
        if (end < 0)
          end = 0;
        if (start > end)
          start = end;

        auto result = api.makeArray();
        for (int64_t i = start; i < end; ++i) {
          api.push(result, api.getArrayValue(arrRef, i));
        }
        return BytecodeValue(result);
      });

  api.registerFunction("array.concat",
                       [&api](const std::vector<BytecodeValue> &args) {
                         if (args.empty())
                           throw std::runtime_error(
                               "array.concat() requires at least 1 argument");

                         auto result = api.makeArray();
                         for (const auto &arg : args) {
                           if (!std::holds_alternative<ArrayRef>(arg))
                             throw std::runtime_error(
                                 "array.concat() all arguments must be arrays");

                           auto arrRef = std::get<ArrayRef>(arg);
                           size_t len = api.getArrayLength(arg);
                           for (size_t i = 0; i < len; ++i) {
                             api.push(result, api.getArrayValue(arrRef, i));
                           }
                         }
                         return BytecodeValue(result);
                       });

  api.registerFunction(
      "array.shift", [&api](const std::vector<BytecodeValue> &args) {
        if (args.empty())
          throw std::runtime_error("array.shift() requires an array");
        if (!std::holds_alternative<ArrayRef>(args[0]))
          throw std::runtime_error("array.shift() requires an array argument");

        return api.removeArrayValue(args[0], 0);
      });

  api.registerFunction(
      "array.reverse", [&api](const std::vector<BytecodeValue> &args) {
        if (args.empty())
          throw std::runtime_error("array.reverse() requires an array");
        if (!std::holds_alternative<ArrayRef>(args[0]))
          throw std::runtime_error(
              "array.reverse() requires an array argument");

        auto arrRef = std::get<ArrayRef>(args[0]);
        size_t len = api.getArrayLength(args[0]);

        auto result = api.makeArray();
        for (size_t i = 0; i < len; ++i) {
          api.push(result, api.getArrayValue(arrRef, len - 1 - i));
        }
        return BytecodeValue(result);
      });

  api.registerFunction(
      "array.map", [&api](const std::vector<BytecodeValue> &args) {
        if (args.size() < 2)
          throw std::runtime_error("array.map() requires array and function");
        if (!std::holds_alternative<ArrayRef>(args[0]))
          throw std::runtime_error("array.map() first argument must be array");

        auto arrRef = std::get<ArrayRef>(args[0]);
        size_t len = api.getArrayLength(args[0]);

        auto result = api.makeArray();
        for (size_t i = 0; i < len; ++i) {
          auto val = api.getArrayValue(arrRef, i);
          // Note: This is a simplified map - real implementation would need
          // function calling
          api.push(result, val);
        }
        return BytecodeValue(result);
      });

  api.registerFunction("array.filter",
                       [&api](const std::vector<BytecodeValue> &args) {
                         if (args.size() < 2)
                           throw std::runtime_error(
                               "array.filter() requires array and predicate");
                         if (!std::holds_alternative<ArrayRef>(args[0]))
                           throw std::runtime_error(
                               "array.filter() first argument must be array");

                         auto arrRef = std::get<ArrayRef>(args[0]);
                         size_t len = api.getArrayLength(args[0]);

                         auto result = api.makeArray();
                         for (size_t i = 0; i < len; ++i) {
                           auto val = api.getArrayValue(arrRef, i);
                           // Simplified filter - include all non-null values
                           if (!std::holds_alternative<std::nullptr_t>(val)) {
                             api.push(result, val);
                           }
                         }
                         return BytecodeValue(result);
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
  api.setGlobal("Array", arrObj);
}

} // namespace havel::stdlib
