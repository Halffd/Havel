/*
 * ArrayModule.cpp
 * 
 * Array manipulation functions for Havel standard library.
 * Extracted from Interpreter.cpp as part of runtime refactoring.
 */
#include "ArrayModule.hpp"
#include <sstream>
#include <cmath>

namespace havel::stdlib {

void registerArrayModule(Environment* env) {
  // Helper: convert value to string
  auto valueToString = [](const HavelValue& v) -> std::string {
    if (v.isString()) return v.asString();
    if (v.isNumber()) {
      double val = v.asNumber();
      if (val == std::floor(val) && std::abs(val) < 1e15) {
        return std::to_string(static_cast<long long>(val));
      } else {
        std::ostringstream oss;
        oss.precision(15);
        oss << val;
        std::string s = oss.str();
        if (s.find('.') != std::string::npos) {
          size_t last = s.find_last_not_of('0');
          if (last != std::string::npos && s[last] == '.') {
            s = s.substr(0, last);
          } else if (last != std::string::npos) {
            s = s.substr(0, last + 1);
          }
        }
        return s;
      }
    }
    if (v.isBool()) return v.asBool() ? "true" : "false";
    return "";
  };

  // Helper: convert value to boolean
  auto valueToBool = [](const HavelValue& v) -> bool {
    if (v.isBool()) return v.asBool();
    if (v.isNumber()) return v.asNumber() != 0.0;
    if (v.isString()) return !v.asString().empty();
    if (v.isNull()) return false;
    return true;
  };

  // Helper: check if result is error
  auto isError = [](const HavelResult& res) -> bool {
    return std::holds_alternative<HavelRuntimeError>(res);
  };

  // Helper: unwrap result to value
  auto unwrap = [](const HavelResult& res) -> HavelValue {
    if (auto* val = std::get_if<HavelValue>(&res)) {
      return *val;
    }
    return HavelValue(nullptr);
  };

  // Helper: call function with arguments
  auto callFunction = [env](const HavelValue& fn, const std::vector<HavelValue>& args) -> HavelResult {
    if (auto* builtin = fn.get_if<BuiltinFunction>()) {
      return (*builtin)(args);
    } else if (auto* userFunc = fn.get_if<std::shared_ptr<HavelFunction>>()) {
      auto& func = *userFunc;
      auto funcEnv = std::make_shared<Environment>();
      for (size_t i = 0; i < args.size() && i < func->declaration->parameters.size(); ++i) {
        funcEnv->Define(func->declaration->parameters[i]->paramName->symbol, args[i]);
      }
      // Note: This is simplified - full implementation needs interpreter access
      return HavelRuntimeError("User function calls require interpreter context");
    }
    return HavelRuntimeError("Not a callable function");
  };

  // ============================================================================
  // Array transformation functions
  // ============================================================================

  // map(array, function) - transform array elements
  env->Define("map", BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() < 2) return HavelRuntimeError("map() requires (array, function)");
    if (!args[0].is<HavelArray>()) return HavelRuntimeError("map() first arg must be array");

    auto array = args[0].get<HavelArray>();
    auto& fn = args[1];
    auto result = std::make_shared<std::vector<HavelValue>>();

    if (array) {
      for (const auto& item : *array) {
        auto res = callFunction(fn, {item});
        if (isError(res)) return res;
        result->push_back(unwrap(res));
      }
    }
    return HavelValue(result);
  }));

  // filter(array, predicate) - filter array elements
  env->Define("filter", BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() < 2) return HavelRuntimeError("filter() requires (array, predicate)");
    if (!args[0].isArray()) return HavelRuntimeError("filter() first arg must be array");

    auto array = args[0].asArray();
    auto& fn = args[1];
    auto result = std::make_shared<std::vector<HavelValue>>();

    if (array) {
      for (const auto& item : *array) {
        auto res = callFunction(fn, {item});
        if (isError(res)) return res;
        if (valueToBool(unwrap(res))) {
          result->push_back(item);
        }
      }
    }
    return HavelValue(result);
  }));

  // reduce(array, function, initial) - reduce array to single value
  env->Define("reduce", BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() < 3) return HavelRuntimeError("reduce() requires (array, function, initial)");
    if (!args[0].is<HavelArray>()) return HavelRuntimeError("reduce() first arg must be array");

    auto array = args[0].get<HavelArray>();
    auto& fn = args[1];
    HavelValue accumulator = args[2];

    if (array) {
      for (const auto& item : *array) {
        auto res = callFunction(fn, {accumulator, item});
        if (isError(res)) return res;
        accumulator = unwrap(res);
      }
    }
    return accumulator;
  }));

  // forEach(array, function) - execute function for each element
  env->Define("forEach", BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() < 2) return HavelRuntimeError("forEach() requires (array, function)");
    if (!args[0].is<HavelArray>()) return HavelRuntimeError("forEach() first arg must be array");

    auto array = args[0].get<HavelArray>();
    auto& fn = args[1];

    if (array) {
      for (const auto& item : *array) {
        callFunction(fn, {item});
      }
    }
    return HavelValue(nullptr);
  }));

  // find(array, predicate) - find first matching element
  env->Define("find", BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() < 2) return HavelRuntimeError("find() requires (array, predicate)");
    if (!args[0].is<HavelArray>()) return HavelRuntimeError("find() first arg must be array");

    auto array = args[0].get<HavelArray>();
    auto& fn = args[1];

    if (array) {
      for (const auto& item : *array) {
        auto res = callFunction(fn, {item});
        if (isError(res)) return res;
        if (valueToBool(unwrap(res))) {
          return item;
        }
      }
    }
    return HavelValue(nullptr);
  }));

  // some(array, predicate) - check if any element matches
  env->Define("some", BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() < 2) return HavelRuntimeError("some() requires (array, predicate)");
    if (!args[0].is<HavelArray>()) return HavelRuntimeError("some() first arg must be array");

    auto array = args[0].get<HavelArray>();
    auto& fn = args[1];

    if (array) {
      for (const auto& item : *array) {
        auto res = callFunction(fn, {item});
        if (isError(res)) return res;
        if (valueToBool(unwrap(res))) {
          return HavelValue(true);
        }
      }
    }
    return HavelValue(false);
  }));

  // every(array, predicate) - check if all elements match
  env->Define("every", BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() < 2) return HavelRuntimeError("every() requires (array, predicate)");
    if (!args[0].is<HavelArray>()) return HavelRuntimeError("every() first arg must be array");

    auto array = args[0].get<HavelArray>();
    auto& fn = args[1];

    if (array) {
      for (const auto& item : *array) {
        auto res = callFunction(fn, {item});
        if (isError(res)) return res;
        if (!valueToBool(unwrap(res))) {
          return HavelValue(false);
        }
      }
      return HavelValue(true);
    }
    return HavelValue(true);
  }));

  // includes(array, value) - check if array contains value
  env->Define("includes", BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() < 2) return HavelRuntimeError("includes() requires (array, value)");
    if (!args[0].is<HavelArray>()) return HavelRuntimeError("includes() first arg must be array");

    auto array = args[0].get<HavelArray>();
    auto& value = args[1];

    if (array) {
      for (const auto& item : *array) {
        if (item.isString() && value.isString() && item.asString() == value.asString()) {
          return HavelValue(true);
        } else if (item.isNumber() && value.isNumber() && item.asNumber() == value.asNumber()) {
          return HavelValue(true);
        } else if (item.isBool() && value.isBool() && item.asBool() == value.asBool()) {
          return HavelValue(true);
        }
      }
    }
    return HavelValue(false);
  }));

  // indexOf(array, value) - find index of value
  env->Define("indexOf", BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() < 2) return HavelRuntimeError("indexOf() requires (array, value)");
    if (!args[0].is<HavelArray>()) return HavelRuntimeError("indexOf() first arg must be array");

    auto array = args[0].get<HavelArray>();
    auto& value = args[1];

    if (array) {
      for (size_t i = 0; i < array->size(); ++i) {
        const auto& item = (*array)[i];
        if (item.isString() && value.isString() && item.asString() == value.asString()) {
          return HavelValue(static_cast<double>(i));
        } else if (item.isNumber() && value.isNumber() && item.asNumber() == value.asNumber()) {
          return HavelValue(static_cast<double>(i));
        } else if (item.isBool() && value.isBool() && item.asBool() == value.asBool()) {
          return HavelValue(static_cast<double>(i));
        }
      }
    }
    return HavelValue(-1.0);
  }));

  // ============================================================================
  // Array mutation functions
  // ============================================================================

  // push(array, value) - add element to end
  env->Define("push", BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() < 2) return HavelRuntimeError("push() requires (array, value)");
    if (!args[0].is<HavelArray>()) return HavelRuntimeError("push() first arg must be array");

    auto array = args[0].get<HavelArray>();
    if (!array) return HavelRuntimeError("push() received null array");
    array->push_back(args[1]);
    return HavelValue(static_cast<double>(array->size()));  // Return new length like JS
  }));

  // pop(array) - remove and return last element
  env->Define("pop", BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.empty()) return HavelRuntimeError("pop() requires array");
    if (!args[0].is<HavelArray>()) return HavelRuntimeError("pop() arg must be array");

    auto array = args[0].get<HavelArray>();
    if (!array || array->empty()) return HavelRuntimeError("Cannot pop from empty array");

    HavelValue last = array->back();
    array->pop_back();
    return last;
  }));

  // shift(array) - remove and return first element
  env->Define("shift", BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.empty()) return HavelRuntimeError("shift() requires array");
    if (!args[0].is<HavelArray>()) return HavelRuntimeError("shift() arg must be array");

    auto array = args[0].get<HavelArray>();
    if (!array || array->empty()) return HavelRuntimeError("Cannot shift from empty array");

    HavelValue first = array->front();
    array->erase(array->begin());
    return first;
  }));

  // unshift(array, value) - add element to beginning
  env->Define("unshift", BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() < 2) return HavelRuntimeError("unshift() requires (array, value)");
    if (!args[0].is<HavelArray>()) return HavelRuntimeError("unshift() first arg must be array");

    auto array = args[0].get<HavelArray>();
    if (!array) return HavelRuntimeError("unshift() received null array");
    array->insert(array->begin(), args[1]);
    return HavelValue(static_cast<double>(array->size()));  // Return new length
  }));

  // concat(array1, array2, ...) - concatenate arrays
  env->Define("concat", BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.empty()) return HavelRuntimeError("concat() requires at least one array");
    
    auto result = std::make_shared<std::vector<HavelValue>>();
    
    for (const auto& arg : args) {
      if (arg.is<HavelArray>()) {
        auto array = arg.get<HavelArray>();
        if (array) {
          result->insert(result->end(), array->begin(), array->end());
        }
      } else {
        // Non-array values are added as-is
        result->push_back(arg);
      }
    }
    return HavelValue(result);
  }));

  // slice(array, start, end) - extract portion of array
  env->Define("slice", BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.empty()) return HavelRuntimeError("slice() requires array");
    if (!args[0].is<HavelArray>()) return HavelRuntimeError("slice() first arg must be array");

    auto array = args[0].get<HavelArray>();
    if (!array) return HavelRuntimeError("slice() received null array");

    int start = 0;
    int end = static_cast<int>(array->size());
    
    if (args.size() > 1 && args[1].isNumber()) {
      start = static_cast<int>(args[1].asNumber());
      if (start < 0) start = std::max(0, static_cast<int>(array->size()) + start);
    }
    if (args.size() > 2 && args[2].isNumber()) {
      end = static_cast<int>(args[2].asNumber());
      if (end < 0) end = std::max(0, static_cast<int>(array->size()) + end);
    }

    auto result = std::make_shared<std::vector<HavelValue>>();
    start = std::max(0, std::min(start, static_cast<int>(array->size())));
    end = std::max(0, std::min(end, static_cast<int>(array->size())));
    
    for (int i = start; i < end && i < static_cast<int>(array->size()); ++i) {
      result->push_back((*array)[i]);
    }
    return HavelValue(result);
  }));

  // splice(array, start, deleteCount, ...items) - modify array in place
  env->Define("splice", BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() < 2) return HavelRuntimeError("splice() requires (array, start)");
    if (!args[0].is<HavelArray>()) return HavelRuntimeError("splice() first arg must be array");

    auto array = args[0].get<HavelArray>();
    if (!array) return HavelRuntimeError("splice() received null array");

    int start = static_cast<int>(args[1].asNumber());
    if (start < 0) start = std::max(0, static_cast<int>(array->size()) + start);
    
    int deleteCount = 0;
    if (args.size() > 2 && args[2].isNumber()) {
      deleteCount = static_cast<int>(args[2].asNumber());
    }

    // Collect deleted elements
    auto deleted = std::make_shared<std::vector<HavelValue>>();
    start = std::max(0, std::min(start, static_cast<int>(array->size())));
    deleteCount = std::max(0, deleteCount);
    
    for (int i = 0; i < deleteCount && start < static_cast<int>(array->size()); ++i) {
      deleted->push_back((*array)[start]);
      array->erase(array->begin() + start);
    }

    // Insert new items
    for (size_t i = 3; i < args.size(); ++i) {
      array->insert(array->begin() + start, args[i]);
      start++;
    }

    return HavelValue(deleted);
  }));

  // reverse(array) - reverse array in place
  env->Define("reverse", BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.empty()) return HavelRuntimeError("reverse() requires array");
    if (!args[0].is<HavelArray>()) return HavelRuntimeError("reverse() arg must be array");

    auto array = args[0].get<HavelArray>();
    if (!array) return HavelRuntimeError("reverse() received null array");

    std::reverse(array->begin(), array->end());
    return HavelValue(array);
  }));

  // flat(array, depth) - flatten nested arrays
  env->Define("flat", BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.empty()) return HavelRuntimeError("flat() requires array");
    if (!args[0].is<HavelArray>()) return HavelRuntimeError("flat() first arg must be array");

    auto array = args[0].get<HavelArray>();
    if (!array) return HavelRuntimeError("flat() received null array");

    int depth = 1;
    if (args.size() > 1 && args[1].isNumber()) {
      depth = static_cast<int>(args[1].asNumber());
    }

    auto result = std::make_shared<std::vector<HavelValue>>();

    std::function<void(const std::vector<HavelValue>&, int)> flatten = [&](const std::vector<HavelValue>& arr, int d) {
      for (const auto& item : arr) {
        if (item.is<HavelArray>() && d > 0) {
          auto nested = item.get<HavelArray>();
          if (nested) flatten(*nested, d - 1);
        } else {
          result->push_back(item);
        }
      }
    };

    if (array) {
      flatten(*array, depth);
    }
    return HavelValue(result);
  }));

  // flatMap(array, function) - map then flatten
  env->Define("flatMap", BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() < 2) return HavelRuntimeError("flatMap() requires (array, function)");
    if (!args[0].is<HavelArray>()) return HavelRuntimeError("flatMap() first arg must be array");

    auto array = args[0].get<HavelArray>();
    auto& fn = args[1];
    auto result = std::make_shared<std::vector<HavelValue>>();

    if (array) {
      for (const auto& item : *array) {
        auto res = callFunction(fn, {item});
        if (isError(res)) return res;
        auto mapped = unwrap(res);
        
        // Flatten one level
        if (mapped.is<HavelArray>()) {
          auto mappedArray = mapped.get<HavelArray>();
          if (mappedArray) {
            result->insert(result->end(), mappedArray->begin(), mappedArray->end());
          }
        } else {
          result->push_back(mapped);
        }
      }
    }
    return HavelValue(result);
  }));

  // ============================================================================
  // Array conversion functions
  // ============================================================================

  // join(array, separator) - join array elements into string
  // Also supports strings (passthrough for pipeline compatibility)
  env->Define("join", BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.empty()) return HavelRuntimeError("join() requires array or string");
    
    // If first arg is string, return it unchanged (pipeline passthrough)
    if (args[0].isString()) {
      return HavelValue(args[0].asString());
    }
    
    if (!args[0].is<HavelArray>()) return HavelRuntimeError("join() first arg must be array or string");

    auto array = args[0].get<HavelArray>();
    std::string separator = args.size() > 1 ? valueToString(args[1]) : ",";

    std::string result;
    if (array) {
      for (size_t i = 0; i < array->size(); ++i) {
        result += valueToString((*array)[i]);
        if (i < array->size() - 1) result += separator;
      }
    }
    return HavelValue(result);
  }));

  // split(text, delimiter) - split string into array
  env->Define("split", BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.empty()) return HavelRuntimeError("split() requires string");
    std::string text = valueToString(args[0]);
    std::string delimiter = args.size() > 1 ? valueToString(args[1]) : ",";

    auto result = std::make_shared<std::vector<HavelValue>>();
    size_t start = 0;
    size_t end = text.find(delimiter);

    while (end != std::string::npos) {
      result->push_back(HavelValue(text.substr(start, end - start)));
      start = end + delimiter.length();
      end = text.find(delimiter, start);
    }
    result->push_back(HavelValue(text.substr(start)));

    return HavelValue(result);
  }));
}

} // namespace havel::stdlib
