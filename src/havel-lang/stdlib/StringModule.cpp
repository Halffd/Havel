/* StringModule.cpp - VM-native stdlib module */
#include "StringModule.hpp"
#include "../compiler/vm/VM.hpp"
#include <regex>

using havel::compiler::Value;
using havel::compiler::VMApi;

namespace havel::stdlib {

static void registerStringFallback(const VMApi &api) {
  // Helper: convert string to lowercase
  auto toLower = [](const std::string &s) -> std::string {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
  };

  // Helper: convert string to uppercase
  auto toUpper = [](const std::string &s) -> std::string {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return result;
  };

  // Helper: trim whitespace
  auto trim = [](const std::string &s) -> std::string {
    size_t start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos)
      return "";
    size_t end = s.find_last_not_of(" \t\n\r");
    return s.substr(start, end - start + 1);
  };

  // Register string functions via VMApi
  api.registerFunction(
      "string.len", [api](const std::vector<Value> &args) {
        if (args.empty())
          throw std::runtime_error("string.len() requires 1 argument");
        return Value(static_cast<int64_t>(api.toString(args[0]).length()));
      });

  api.registerFunction(
      "string.lower",
      [&api, toLower](const std::vector<Value> &args) {
        if (args.empty())
          throw std::runtime_error("string.lower() requires 1 argument");
        std::string s = api.toString(args[0]);
        std::string result = toLower(s);
        return api.makeString(result);
      });

  api.registerFunction(
      "string.upper",
      [&api, toUpper](const std::vector<Value> &args) {
        if (args.empty())
          throw std::runtime_error("string.upper() requires 1 argument");
        std::string s = api.toString(args[0]);
        std::string result = toUpper(s);
        return api.makeString(result);
      });

  api.registerFunction(
      "string.trim", [&api, trim](const std::vector<Value> &args) {
        if (args.empty())
          throw std::runtime_error("string.trim() requires 1 argument");
        std::string s = api.toString(args[0]);
        std::string result = trim(s);
        return api.makeString(result);
      });

  api.registerFunction(
      "string.sub", [api](const std::vector<Value> &args) {
        if (args.size() < 2)
          throw std::runtime_error(
              "string.sub() requires at least 2 arguments");
        const std::string* strPtr = api.getStringPtr(args[0]);
        std::string tempStr;
        const std::string& str = strPtr ? *strPtr : (tempStr = api.toString(args[0]));

        int64_t start = args[1].asInt();
        int64_t len = (args.size() > 2) ? args[2].asInt()
                                        : str.length() - start;

        if (start < 0)
          start = 0;
        if (static_cast<size_t>(start) >= str.length()) {
          return api.makeString("");
        }
        if (len < 0)
          len = 0;
        if (static_cast<size_t>(start + len) > str.length()) {
          len = str.length() - start;
        }

        return api.makeString(str.substr(start, len));
      });

  api.registerFunction("string.find",
                       [api](const std::vector<Value> &args) {
                         if (args.size() < 2)
                           throw std::runtime_error(
                               "string.find() requires at least 2 arguments");
                         std::string str = api.toString(args[0]);
                         std::string substr = api.toString(args[1]);
                         size_t pos = str.find(substr);
                         return Value(static_cast<int64_t>(pos));
                       });

  api.registerFunction(
      "string.replace", [api](const std::vector<Value> &args) {
        if (args.size() < 3)
          throw std::runtime_error("string.replace() requires 3 arguments");
        std::string str = api.toString(args[0]);
        std::string from = api.toString(args[1]);
        std::string to = api.toString(args[2]);

        size_t pos = 0;
        if (!from.empty()) {
          while ((pos = str.find(from, pos)) != std::string::npos) {
            str.replace(pos, from.length(), to);
            pos += to.length();
          }
        }
        return api.makeString(str);
      });

  api.registerFunction(
      "string.split", [api](const std::vector<Value> &args) {
        if (args.empty())
          throw std::runtime_error(
              "string.split() requires at least 1 argument");
        std::string str = api.toString(args[0]);
        std::string delim = (args.size() > 1) ? api.toString(args[1]) : "";

        Value arr = api.makeArray();
        if (delim.empty()) {
          for (char c : str) {
            api.push(arr, api.makeString(std::string(1, c)));
          }
        } else {
          size_t pos = 0;
          size_t prev = 0;
          while ((pos = str.find(delim, prev)) != std::string::npos) {
            api.push(arr, api.makeString(str.substr(prev, pos - prev)));
            prev = pos + delim.length();
          }
          api.push(arr, api.makeString(str.substr(prev)));
        }
        return arr;
      });

  api.registerFunction(
      "string.join", [api](const std::vector<Value> &args) {
        if (args.empty())
          throw std::runtime_error(
              "string.join() requires at least 1 argument");
        if (!args[0].isArrayId()) {
          throw std::runtime_error(
              "string.join() first argument must be array");
        }

        std::string delim = (args.size() > 1) ? api.toString(args[1]) : "";
        Value arr = args[0];
        uint32_t len = api.length(arr);
        std::string result;
        for (uint32_t i = 0; i < len; ++i) {
          if (i > 0) {
            result += delim;
          }
          result += api.toString(api.getAt(arr, i));
        }
        return api.makeString(result);
      });

  api.registerFunction(
      "string.startswith", [api](const std::vector<Value> &args) {
        if (args.size() < 2)
          throw std::runtime_error("string.startswith() requires 2 arguments");
        std::string str = api.toString(args[0]);
        std::string prefix = api.toString(args[1]);
        return Value::makeBool(str.rfind(prefix, 0) == 0);
      });

  api.registerFunction(
      "string.endswith", [api](const std::vector<Value> &args) {
        if (args.size() < 2)
          throw std::runtime_error("string.endswith() requires 2 arguments");
        std::string str = api.toString(args[0]);
        std::string suffix = api.toString(args[1]);
        if (suffix.length() > str.length())
          return Value::makeBool(false);
        return Value::makeBool(str.compare(str.length() - suffix.length(),
                                         suffix.length(), suffix) == 0);
      });

  api.registerFunction(
      "string.includes", [api](const std::vector<Value> &args) {
        if (args.size() < 2)
          throw std::runtime_error("string.includes() requires 2 arguments");
        std::string str = api.toString(args[0]);
        std::string substr = api.toString(args[1]);
        return Value::makeBool(str.find(substr) != std::string::npos);
      });

  api.registerFunction("replace", [api](const std::vector<Value>& args) {
    if (args.size() < 3)
      throw std::runtime_error("replace() requires string, pattern, and replacement");
    std::string s = api.toString(args[0]);
    std::string pattern = api.toString(args[1]);
    std::string replacement = api.toString(args[2]);

    bool isRegex = !pattern.empty() && pattern.front() == '/' && pattern.size() > 2 && pattern.back() == '/';
    if (isRegex) {
      std::string regexPattern = pattern.substr(1, pattern.size() - 2);
      try {
        std::regex re(regexPattern);
        std::string result = std::regex_replace(s, re, replacement);
        return api.makeString(std::move(result));
      } catch (const std::regex_error&) {
        return Value::makeNull();
      }
    }

    size_t pos = s.find(pattern);
    if (pos == std::string::npos) return api.makeString(s);
    s.replace(pos, pattern.size(), replacement);
    return api.makeString(std::move(s));
  });

  auto strObj = api.makeObject();
  api.setField(strObj, "len", api.makeFunctionRef("string.len"));
  api.setField(strObj, "lower", api.makeFunctionRef("string.lower"));
  api.setField(strObj, "upper", api.makeFunctionRef("string.upper"));
  api.setField(strObj, "trim", api.makeFunctionRef("string.trim"));
  api.setField(strObj, "sub", api.makeFunctionRef("string.sub"));
  api.setField(strObj, "find", api.makeFunctionRef("string.find"));
  api.setField(strObj, "replace", api.makeFunctionRef("string.replace"));
  api.setField(strObj, "split", api.makeFunctionRef("string.split"));
  api.setField(strObj, "join", api.makeFunctionRef("string.join"));
  api.setField(strObj, "startswith", api.makeFunctionRef("string.startswith"));
  api.setField(strObj, "endswith", api.makeFunctionRef("string.endswith"));
  api.setField(strObj, "includes", api.makeFunctionRef("string.includes"));
  api.setGlobal("String", strObj);
  api.setGlobal("string", strObj);
}

void registerStringModule(const VMApi &api) {
auto &vm = api.vm();
Value exports;
try {
fprintf(stderr, "[STRMOD] Attempting vm.loadModule(\"string\")...\n");
exports = vm.loadModule("string");
fprintf(stderr, "[STRMOD] loadModule succeeded, isObj=%d\n", exports.isObjectId());
} catch (const std::exception& e) {
fprintf(stderr, "[STRMOD] loadModule FAILED: %s\n", e.what());
registerStringFallback(api);
return;
} catch (...) {
fprintf(stderr, "[STRMOD] loadModule FAILED with unknown exception\n");
registerStringFallback(api);
return;
}

  if (!exports.isObjectId()) {
    registerStringFallback(api);
    return;
  }

  api.setGlobal("String", exports);
  api.setGlobal("string", exports);

  auto *obj = vm.getHeap().object(exports.asObjectId());
  if (obj) {
    for (const auto& [name, value] : *obj) {
      if (name.empty() || name[0] == '_') continue;
      api.setGlobal(name, value);
    }
  }
}

} // namespace havel::stdlib
