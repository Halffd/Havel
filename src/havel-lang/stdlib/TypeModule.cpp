/*
 * TypeModule.cpp
 *
 * Type conversion and utility functions for Havel standard library.
 * Extracted from Interpreter.cpp as part of runtime refactoring.
 */
#include "TypeModule.hpp"
#include <cmath>
#include <sstream>
#include <algorithm>
#include <set>
#include <thread>
#include <chrono>

namespace havel::stdlib {

void registerTypeModule(Environment* env) {
  // Helper: convert value to number
  auto toNumber = [](const HavelValue& v) -> double {
    if (v.is<double>()) return v.get<double>();
    if (v.is<int>()) return static_cast<double>(v.get<int>());
    if (v.isString()) {
      try { return std::stod(v.asString()); } catch (...) {}
    }
    if (v.isBool()) return v.asBool() ? 1.0 : 0.0;
    return 0.0;
  };

  // Helper: convert value to string
  auto toString = [&toNumber](const HavelValue& v) -> std::string {
    if (v.isString()) return v.asString();
    if (v.isNumber()) {
      double val = toNumber(v);
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

  // Helper: convert iterable to array
  auto iterableToArray = [](const HavelValue& val) -> std::shared_ptr<std::vector<HavelValue>> {
    auto result = std::make_shared<std::vector<HavelValue>>();
    
    if (val.is<HavelArray>()) {
      auto arrPtr = val.get_if<HavelArray>();
      if (arrPtr && *arrPtr) {
        result = std::make_shared<std::vector<HavelValue>>(**arrPtr);
      }
    } else if (val.is<HavelSet>()) {
      auto setPtr = val.get_if<HavelSet>();
      if (setPtr && setPtr->elements) {
        for (const auto& item : *(setPtr->elements)) {
          result->push_back(item);
        }
      }
    } else if (val.is<HavelObject>()) {
      auto objPtr = val.get_if<HavelObject>();
      if (objPtr && *objPtr) {
        for (const auto& pair : **objPtr) {
          result->push_back(pair.second);
        }
      }
    }
    
    return result;
  };

  // ============================================================================
  // Type conversion functions
  // ============================================================================

  // int(x) - truncate to integer
  env->Define("int", BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.empty()) return HavelRuntimeError("int() requires an argument");
    const auto& arg = args[0];
    if (arg.isNumber()) {
      double val = toNumber(arg);
      return HavelValue(val >= 0 ? std::floor(val) : std::ceil(val));
    } else if (arg.isString()) {
      try {
        return HavelValue(static_cast<double>(std::stoll(arg.asString())));
      } catch (...) {
        return HavelRuntimeError("int(): cannot convert '" + arg.asString() + "' to integer");
      }
    } else if (arg.isBool()) {
      return HavelValue(arg.asBool() ? 1.0 : 0.0);
    }
    return HavelRuntimeError("int(): cannot convert type to integer");
  }));

  // num(x) - convert to double
  env->Define("num", BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.empty()) return HavelRuntimeError("num() requires an argument");
    const auto& arg = args[0];
    if (arg.isNumber()) {
      return HavelValue(toNumber(arg));
    } else if (arg.isString()) {
      try {
        return HavelValue(std::stod(arg.asString()));
      } catch (...) {
        return HavelRuntimeError("num(): cannot convert '" + arg.asString() + "' to number");
      }
    } else if (arg.isBool()) {
      return HavelValue(arg.asBool() ? 1.0 : 0.0);
    }
    return HavelRuntimeError("num(): cannot convert type to number");
  }));

  // str(x) - convert to string
  env->Define("str", BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.empty()) return HavelRuntimeError("str() requires an argument");
    return HavelValue(toString(args[0]));
  }));

  // list(...) - construct list from arguments or convert iterable
  env->Define("list", BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() == 1) {
      auto result = iterableToArray(args[0]);
      if (!result->empty()) {
        return HavelValue(result);
      }
    }
    auto result = std::make_shared<std::vector<HavelValue>>();
    for (const auto& arg : args) {
      result->push_back(arg);
    }
    return HavelValue(result);
  }));

  // tuple(...) - construct tuple (alias for list, documented as immutable convention)
  env->Define("tuple", BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() == 1) {
      auto result = iterableToArray(args[0]);
      if (!result->empty()) {
        return HavelValue(result);
      }
    }
    auto result = std::make_shared<std::vector<HavelValue>>();
    for (const auto& arg : args) {
      result->push_back(arg);
    }
    return HavelValue(result);
  }));

  // set(...) - construct set from arguments or convert iterable
  env->Define("set", BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    auto elements = std::make_shared<std::vector<HavelValue>>();
    
    if (args.size() == 1) {
      const auto& arg = args[0];
      if (arg.is<HavelSet>()) {
        auto setPtr = arg.get_if<HavelSet>();
        if (setPtr && setPtr->elements) {
          return HavelValue(HavelSet(setPtr->elements));
        }
      } else if (arg.is<HavelArray>()) {
        auto arrPtr = arg.get_if<HavelArray>();
        if (arrPtr && *arrPtr) {
          for (const auto& item : **arrPtr) {
            bool found = false;
            for (const auto& existing : *elements) {
              if (existing.isString() && item.isString() && existing.asString() == item.asString()) {
                found = true; break;
              } else if (existing.isNumber() && item.isNumber() && existing.asNumber() == item.asNumber()) {
                found = true; break;
              }
            }
            if (!found) elements->push_back(item);
          }
          return HavelValue(HavelSet(elements));
        }
      }
    }
    
    for (const auto& arg : args) {
      bool found = false;
      for (const auto& existing : *elements) {
        if (existing.isString() && arg.isString() && existing.asString() == arg.asString()) {
          found = true; break;
        } else if (existing.isNumber() && arg.isNumber() && existing.asNumber() == arg.asNumber()) {
          found = true; break;
        }
      }
      if (!found) elements->push_back(arg);
    }
    return HavelValue(HavelSet(elements));
  }));

  // type(x) - get type name
  env->Define("type", BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.empty()) return HavelRuntimeError("type() requires an argument");
    const auto& arg = args[0];
    if (arg.isNull()) return HavelValue("null");
    if (arg.isBool()) return HavelValue("boolean");
    if (arg.is<int>()) return HavelValue("number");
    if (arg.is<double>()) return HavelValue("number");
    if (arg.isString()) return HavelValue("string");
    if (arg.isArray()) return HavelValue("array");
    if (arg.isObject()) return HavelValue("object");
    if (arg.is<HavelSet>()) return HavelValue("set");
    if (arg.isFunction()) return HavelValue("function");
    return HavelValue("unknown");
  }));

  // len(x) - get length
  env->Define("len", BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.empty()) return HavelRuntimeError("len() requires an argument");
    const auto& arg = args[0];
    if (arg.isString()) {
      return HavelValue(static_cast<double>(arg.asString().length()));
    } else if (arg.isArray()) {
      auto arrPtr = arg.get_if<HavelArray>();
      return HavelValue(arrPtr && *arrPtr ? static_cast<double>((**arrPtr).size()) : 0.0);
    } else if (arg.isObject()) {
      auto objPtr = arg.get_if<HavelObject>();
      return HavelValue(objPtr && *objPtr ? static_cast<double>((**objPtr).size()) : 0.0);
    } else if (arg.is<HavelSet>()) {
      auto setPtr = arg.get_if<HavelSet>();
      return HavelValue(setPtr && setPtr->elements ? static_cast<double>(setPtr->elements->size()) : 0.0);
    }
    return HavelRuntimeError("len() requires string, array, object, or set");
  }));

  // typeof(x) - alias for type(x)
  env->Define("typeof", BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.empty()) return HavelRuntimeError("typeof() requires an argument");
    const auto& arg = args[0];
    if (arg.isNull()) return HavelValue("null");
    if (arg.isBool()) return HavelValue("boolean");
    if (arg.is<int>()) return HavelValue("number");
    if (arg.is<double>()) return HavelValue("number");
    if (arg.isString()) return HavelValue("string");
    if (arg.isArray()) return HavelValue("array");
    if (arg.isObject()) return HavelValue("object");
    if (arg.is<HavelSet>()) return HavelValue("set");
    if (arg.isFunction()) return HavelValue("function");
    return HavelValue("unknown");
  }));

  // print(...) - print values to stdout
  env->Define("print", BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    for (const auto& arg : args) {
      if (arg.isString()) {
        std::cout << arg.asString();
      } else if (arg.isNumber()) {
        std::cout << arg.asNumber();
      } else if (arg.isBool()) {
        std::cout << (arg.asBool() ? "true" : "false");
      } else if (arg.isNull()) {
        std::cout << "null";
      } else {
        std::cout << "[object]";
      }
    }
    std::cout << std::endl;
    return HavelValue(nullptr);
  }));

  // println(...) - print values to stdout with newline
  env->Define("println", BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    for (const auto& arg : args) {
      if (arg.isString()) {
        std::cout << arg.asString();
      } else if (arg.isNumber()) {
        std::cout << arg.asNumber();
      } else if (arg.isBool()) {
        std::cout << (arg.asBool() ? "true" : "false");
      } else if (arg.isNull()) {
        std::cout << "null";
      }
    }
    std::cout << std::endl;
    return HavelValue(nullptr);
  }));

  // sleep(ms) - Sleep for specified milliseconds
  env->Define("sleep", BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() != 1) return HavelRuntimeError("sleep() requires 1 argument (milliseconds)");
    long long ms = static_cast<long long>(args[0].asNumber());
    if (ms < 0) return HavelRuntimeError("sleep() requires non-negative milliseconds");
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    return HavelValue(nullptr);
  }));

  // range(start, end) - Generate array of numbers from start to end
  env->Define("range", BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() < 1 || args.size() > 2) {
      return HavelRuntimeError("range() requires 1 or 2 arguments (start, end)");
    }
    int start = args.size() == 2 ? static_cast<int>(args[0].asNumber()) : 0;
    int end = args.size() == 2 ? static_cast<int>(args[1].asNumber()) : static_cast<int>(args[0].asNumber());
    
    auto arr = std::make_shared<std::vector<HavelValue>>();
    for (int i = start; i < end; ++i) {
      arr->push_back(HavelValue(i));
    }
    return HavelValue(arr);
  }));
}

} // namespace havel::stdlib
