/* StringModule.cpp - VM-native stdlib module */
#include "StringModule.hpp"
#include "../compiler/vm/VM.hpp"
#include <regex>

using havel::compiler::Value;
using havel::compiler::VMApi;

namespace havel::stdlib {

static Value registerStringFallback(const VMApi &api) {
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
        if (pos == std::string::npos) return Value::makeInt(-1);
        return Value::makeInt(static_cast<int64_t>(pos));
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

  api.registerFunction(
    "string.codePointAt", [api](const std::vector<Value> &args) {
        if (args.size() < 2)
            throw std::runtime_error("string.codePointAt() requires 2 arguments");
        const std::string* strPtr = api.getStringPtr(args[0]);
        std::string tempStr;
        const std::string& str = strPtr ? *strPtr : (tempStr = api.toString(args[0]));
        int64_t idx = args[1].asInt();

        size_t bytePos = 0;
        int64_t cpIdx = 0;
        while (bytePos < str.size() && cpIdx < idx) {
            unsigned char c = static_cast<unsigned char>(str[bytePos]);
            if (c < 0x80) bytePos += 1;
            else if ((c & 0xE0) == 0xC0) bytePos += 2;
            else if ((c & 0xF0) == 0xE0) bytePos += 3;
            else if ((c & 0xF8) == 0xF0) bytePos += 4;
            else bytePos += 1;
            cpIdx++;
        }
        if (bytePos >= str.size()) return Value::makeInt(-1);

        unsigned char c = static_cast<unsigned char>(str[bytePos]);
        int32_t codepoint;
        size_t cpLen;
        if (c < 0x80) { codepoint = c; cpLen = 1; }
        else if ((c & 0xE0) == 0xC0) { codepoint = (c & 0x1F); cpLen = 2; }
        else if ((c & 0xF0) == 0xE0) { codepoint = (c & 0x0F); cpLen = 3; }
        else if ((c & 0xF8) == 0xF0) { codepoint = (c & 0x07); cpLen = 4; }
        else { codepoint = c; cpLen = 1; }

        for (size_t i = 1; i < cpLen && bytePos + i < str.size(); ++i) {
            codepoint = (codepoint << 6) | (static_cast<unsigned char>(str[bytePos + i]) & 0x3F);
        }
        return Value::makeInt(static_cast<int64_t>(codepoint));
    });

api.registerFunction(
    "string.codePointLen", [api](const std::vector<Value> &args) {
        if (args.empty())
            throw std::runtime_error("string.codePointLen() requires 1 argument");
        const std::string* strPtr = api.getStringPtr(args[0]);
        std::string tempStr;
        const std::string& str = strPtr ? *strPtr : (tempStr = api.toString(args[0]));
        int64_t count = 0;
        size_t bytePos = 0;
        while (bytePos < str.size()) {
            unsigned char c = static_cast<unsigned char>(str[bytePos]);
            if (c < 0x80) bytePos += 1;
            else if ((c & 0xE0) == 0xC0) bytePos += 2;
            else if ((c & 0xF0) == 0xE0) bytePos += 3;
            else if ((c & 0xF8) == 0xF0) bytePos += 4;
            else bytePos += 1;
            count++;
        }
        return Value::makeInt(count);
    });

api.registerFunction(
    "string.fromCodePoint", [api](const std::vector<Value> &args) {
        if (args.empty())
            throw std::runtime_error("string.fromCodePoint() requires 1 argument");
        int64_t cp = args[0].isInt() ? args[0].asInt() : 0;
        std::string result;
        if (cp < 0) return api.makeString("");
        if (cp < 0x80) {
            result += static_cast<char>(cp);
        } else if (cp < 0x800) {
            result += static_cast<char>(0xC0 | (cp >> 6));
            result += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            result += static_cast<char>(0xE0 | (cp >> 12));
            result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x110000) {
            result += static_cast<char>(0xF0 | (cp >> 18));
            result += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (cp & 0x3F));
        }
        return api.makeString(result);
    });

api.registerFunction(
    "string.cpAtByte", [api](const std::vector<Value> &args) {
        if (args.size() < 2)
            throw std::runtime_error("string.cpAtByte() requires 2 arguments");
        const std::string* strPtr = api.getStringPtr(args[0]);
        std::string tempStr;
        const std::string& str = strPtr ? *strPtr : (tempStr = api.toString(args[0]));
        int64_t bytePos = args[1].asInt();
        if (bytePos < 0 || static_cast<size_t>(bytePos) >= str.size()) return Value::makeInt(-1);
        unsigned char c = static_cast<unsigned char>(str[static_cast<size_t>(bytePos)]);
        int32_t codepoint;
        size_t cpLen;
        if (c < 0x80) { codepoint = c; cpLen = 1; }
        else if ((c & 0xE0) == 0xC0) { codepoint = (c & 0x1F); cpLen = 2; }
        else if ((c & 0xF0) == 0xE0) { codepoint = (c & 0x0F); cpLen = 3; }
        else if ((c & 0xF8) == 0xF0) { codepoint = (c & 0x07); cpLen = 4; }
        else { codepoint = c; cpLen = 1; }
        for (size_t i = 1; i < cpLen && static_cast<size_t>(bytePos) + i < str.size(); ++i) {
            codepoint = (codepoint << 6) | (static_cast<unsigned char>(str[static_cast<size_t>(bytePos) + i]) & 0x3F);
        }
        return Value::makeInt(static_cast<int64_t>(codepoint));
    });

api.registerFunction(
    "string.cpByteLen", [api](const std::vector<Value> &args) {
        if (args.size() < 2)
            throw std::runtime_error("string.cpByteLen() requires 2 arguments");
        const std::string* strPtr = api.getStringPtr(args[0]);
        std::string tempStr;
        const std::string& str = strPtr ? *strPtr : (tempStr = api.toString(args[0]));
        int64_t bytePos = args[1].asInt();
        if (bytePos < 0 || static_cast<size_t>(bytePos) >= str.size()) return Value::makeInt(0);
        unsigned char c = static_cast<unsigned char>(str[static_cast<size_t>(bytePos)]);
        if (c < 0x80) return Value::makeInt(1);
        else if ((c & 0xE0) == 0xC0) return Value::makeInt(2);
        else if ((c & 0xF0) == 0xE0) return Value::makeInt(3);
        else if ((c & 0xF8) == 0xF0) return Value::makeInt(4);
        return Value::makeInt(1);
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
    api.setField(strObj, "startsWith", api.makeFunctionRef("string.startswith"));
    api.setField(strObj, "endsWith", api.makeFunctionRef("string.endswith"));
    api.setField(strObj, "includes", api.makeFunctionRef("string.includes"));
    api.setField(strObj, "startswith", api.makeFunctionRef("string.startswith"));
  api.setField(strObj, "endswith", api.makeFunctionRef("string.endswith"));
  api.setField(strObj, "codePointAt", api.makeFunctionRef("string.codePointAt"));
  api.setField(strObj, "codePointLen", api.makeFunctionRef("string.codePointLen"));
  api.setField(strObj, "fromCodePoint", api.makeFunctionRef("string.fromCodePoint"));
  api.setField(strObj, "cpAtByte", api.makeFunctionRef("string.cpAtByte"));
api.setField(strObj, "cpByteLen", api.makeFunctionRef("string.cpByteLen"));

    api.registerFunction(
        "string.toCodePointArray", [api](const std::vector<Value> &args) {
            if (args.empty())
                throw std::runtime_error("string.toCodePointArray() requires 1 argument");
            std::string s = api.toString(args[0]);
            auto arr = api.makeArray();
            size_t i = 0;
            while (i < s.size()) {
                auto cpArr = api.makeArray();
                unsigned char c = static_cast<unsigned char>(s[i]);
                int64_t cp;
                if (c < 0x80) {
                    cp = static_cast<int64_t>(c);
                    api.push(cpArr, Value(cp));
                    api.push(cpArr, Value(static_cast<int64_t>(i)));
                    api.push(cpArr, Value(static_cast<int64_t>(1)));
                    i += 1;
                } else if ((c & 0xE0) == 0xC0) {
                    cp = static_cast<int64_t>(c & 0x1F);
                    if (i + 1 < s.size()) cp = (cp << 6) | (static_cast<unsigned char>(s[i+1]) & 0x3F);
                    api.push(cpArr, Value(cp));
                    api.push(cpArr, Value(static_cast<int64_t>(i)));
                    api.push(cpArr, Value(static_cast<int64_t>(2)));
                    i += 2;
                } else if ((c & 0xF0) == 0xE0) {
                    cp = static_cast<int64_t>(c & 0x0F);
                    if (i + 1 < s.size()) cp = (cp << 6) | (static_cast<unsigned char>(s[i+1]) & 0x3F);
                    if (i + 2 < s.size()) cp = (cp << 6) | (static_cast<unsigned char>(s[i+2]) & 0x3F);
                    api.push(cpArr, Value(cp));
                    api.push(cpArr, Value(static_cast<int64_t>(i)));
                    api.push(cpArr, Value(static_cast<int64_t>(3)));
                    i += 3;
                } else if ((c & 0xF8) == 0xF0) {
                    cp = static_cast<int64_t>(c & 0x07);
                    if (i + 1 < s.size()) cp = (cp << 6) | (static_cast<unsigned char>(s[i+1]) & 0x3F);
                    if (i + 2 < s.size()) cp = (cp << 6) | (static_cast<unsigned char>(s[i+2]) & 0x3F);
                    if (i + 3 < s.size()) cp = (cp << 6) | (static_cast<unsigned char>(s[i+3]) & 0x3F);
                    api.push(cpArr, Value(cp));
                    api.push(cpArr, Value(static_cast<int64_t>(i)));
                    api.push(cpArr, Value(static_cast<int64_t>(4)));
                    i += 4;
                } else {
                    cp = static_cast<int64_t>(c);
                    api.push(cpArr, Value(cp));
                    api.push(cpArr, Value(static_cast<int64_t>(i)));
                    api.push(cpArr, Value(static_cast<int64_t>(1)));
                    i += 1;
                }
                api.push(arr, cpArr);
            }
            return arr;
        });
    api.setField(strObj, "toCodePointArray", api.makeFunctionRef("string.toCodePointArray"));

    api.setGlobal("String", strObj);
    api.setGlobal("string", strObj);
    return strObj;
}

void registerStringModule(const VMApi &api) {
    registerStringFallback(api);
}

} // namespace havel::stdlib

#ifdef HAVEL_MODULE_PLUGIN
#include "c/ModulePlugin.h"

HAVEL_MODULE_PLUGIN_EAGER(string, "1.0.0", "String operations stdlib module",
    havel::stdlib::registerStringModule(*api);
)
#endif
