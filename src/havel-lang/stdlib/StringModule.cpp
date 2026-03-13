/*
 * StringModule.cpp
 * 
 * String manipulation functions for Havel standard library.
 * Extracted from Interpreter.cpp as part of runtime refactoring.
 */
#include "StringModule.hpp"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace havel::stdlib {

void registerStringModule(Environment& env) {
  // Helper: convert value to string
  auto toString = [](const HavelValue& v) -> std::string {
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

  // Helper: format value with format specifier
  auto formatValue = [&toString](const HavelValue& value, const std::string& formatSpec) -> std::string {
    std::string result;
    char type = 'g';
    int precision = -1;

    if (!formatSpec.empty()) {
      char lastChar = formatSpec.back();
      if (lastChar == 'f' || lastChar == 'd' || lastChar == 's' || lastChar == 'g' || lastChar == 'e') {
        type = lastChar;
        if (formatSpec.length() > 1) {
          std::string precStr = formatSpec.substr(0, formatSpec.length() - 1);
          if (!precStr.empty() && precStr[0] == '.') {
            if (precStr.length() > 1) {
              try { precision = std::stoi(precStr.substr(1)); } catch (...) { precision = -1; }
            }
          }
        }
      } else if (formatSpec[0] == '.') {
        try { precision = std::stoi(formatSpec.substr(1)); } catch (...) { precision = -1; }
      }
    }

    if (value.is<double>()) {
      double num = value.get<double>();
      if (type == 'f' || precision >= 0) {
        int prec = precision >= 0 ? precision : 6;
        char buf[64];
        snprintf(buf, sizeof(buf), "%.*f", prec, num);
        result = buf;
      } else if (type == 'e') {
        char buf[64];
        snprintf(buf, sizeof(buf), "%e", num);
        result = buf;
      } else if (type == 'g') {
        result = toString(value);
      } else {
        result = std::to_string(static_cast<long long>(num));
      }
    } else if (value.is<int>()) {
      int num = value.get<int>();
      if (type == 'f') {
        int prec = precision >= 0 ? precision : 6;
        char buf[64];
        snprintf(buf, sizeof(buf), "%.*f", prec, static_cast<double>(num));
        result = buf;
      } else {
        result = std::to_string(num);
      }
    } else {
      result = toString(value);
    }

    return result;
  };

  // ============================================================================
  // String functions
  // ============================================================================

  // format(formatString, arg0, arg1, ...) - Python-style string formatting
  env.Define("format", BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.empty()) return HavelRuntimeError("format() requires at least a format string");

    std::string formatStr = toString(args[0]);
    std::string result;
    size_t pos = 0;
    size_t argIndex = 0;

    while (pos < formatStr.length()) {
      size_t openBrace = formatStr.find('{', pos);
      if (openBrace == std::string::npos) {
        result += formatStr.substr(pos);
        break;
      }

      result += formatStr.substr(pos, openBrace - pos);

      size_t closeBrace = formatStr.find('}', openBrace);
      if (closeBrace == std::string::npos) 
        return HavelRuntimeError("Unclosed placeholder in format string");

      std::string placeholder = formatStr.substr(openBrace + 1, closeBrace - openBrace - 1);

      size_t colonPos = placeholder.find(':');
      size_t index = 0;
      std::string formatSpec;

      if (colonPos == std::string::npos) {
        if (!placeholder.empty()) {
          try { index = std::stoul(placeholder); } catch (...) {
            return HavelRuntimeError("Invalid placeholder index");
          }
        } else {
          index = argIndex++;
        }
      } else {
        try { index = std::stoul(placeholder.substr(0, colonPos)); } catch (...) {
          return HavelRuntimeError("Invalid placeholder index");
        }
        formatSpec = placeholder.substr(colonPos + 1);
      }

      if (index + 1 > args.size()) 
        return HavelRuntimeError("Placeholder index out of range");

      const HavelValue& value = args[index + 1];
      result += formatValue(value, formatSpec);
      argIndex++;
      pos = closeBrace + 1;
    }

    return HavelValue(result);
  }));

  // upper(text) - convert to uppercase
  env.Define("upper", BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.empty()) return HavelRuntimeError("upper() requires text");
    std::string text = toString(args[0]);
    std::transform(text.begin(), text.end(), text.begin(), ::toupper);
    return HavelValue(text);
  }));

  // lower(text) - convert to lowercase
  env.Define("lower", BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.empty()) return HavelRuntimeError("lower() requires text");
    std::string text = toString(args[0]);
    std::transform(text.begin(), text.end(), text.begin(), ::tolower);
    return HavelValue(text);
  }));

  // trim(text) - remove leading/trailing whitespace
  env.Define("trim", BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.empty()) return HavelRuntimeError("trim() requires text");
    std::string text = toString(args[0]);
    text.erase(text.begin(), std::find_if(text.begin(), text.end(), [](unsigned char ch) {
      return !std::isspace(ch);
    }));
    text.erase(std::find_if(text.rbegin(), text.rend(), [](unsigned char ch) {
      return !std::isspace(ch);
    }).base(), text.end());
    return HavelValue(text);
  }));

  // length(text) - get string length
  env.Define("length", BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.empty()) return HavelRuntimeError("length() requires text");
    std::string text = toString(args[0]);
    return HavelValue(static_cast<double>(text.length()));
  }));

  // replace(text, search, replacement) - replace all occurrences
  env.Define("replace", BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() < 3) return HavelRuntimeError("replace() requires (text, search, replacement)");
    std::string text = toString(args[0]);
    std::string search = toString(args[1]);
    std::string replacement = toString(args[2]);

    size_t pos = 0;
    while ((pos = text.find(search, pos)) != std::string::npos) {
      text.replace(pos, search.length(), replacement);
      pos += replacement.length();
    }
    return HavelValue(text);
  }));

  // contains(text, search) - check if text contains substring
  env.Define("contains", BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() < 2) return HavelRuntimeError("contains() requires (text, search)");
    std::string text = toString(args[0]);
    std::string search = toString(args[1]);
    return HavelValue(text.find(search) != std::string::npos);
  }));
  
  // includes(text, search) - alias for contains (string method style)
  env.Define("includes", BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() < 2) return HavelRuntimeError("includes() requires (text, search)");
    std::string text = toString(args[0]);
    std::string search = toString(args[1]);
    return HavelValue(text.find(search) != std::string::npos);
  }));

  // substr(text, start[, length]) - extract substring
  env.Define("substr", BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() < 2) return HavelRuntimeError("substr() requires (text, start[, length])");
    std::string text = toString(args[0]);
    int start = static_cast<int>(std::stod(toString(args[1])));
    if (start < 0) start = 0;
    if (start > static_cast<int>(text.size())) start = static_cast<int>(text.size());

    if (args.size() >= 3) {
      int length = static_cast<int>(std::stod(toString(args[2])));
      if (length < 0) length = 0;
      return HavelValue(text.substr(static_cast<size_t>(start), static_cast<size_t>(length)));
    }
    return HavelValue(text.substr(static_cast<size_t>(start)));
  }));

  // left(text, count) - get leftmost characters
  env.Define("left", BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() < 2) return HavelRuntimeError("left() requires (text, count)");
    std::string text = toString(args[0]);
    int count = static_cast<int>(std::stod(toString(args[1])));
    if (count <= 0) return HavelValue(std::string(""));
    if (count >= static_cast<int>(text.size())) return HavelValue(text);
    return HavelValue(text.substr(0, static_cast<size_t>(count)));
  }));

  // right(text, count) - get rightmost characters
  env.Define("right", BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() < 2) return HavelRuntimeError("right() requires (text, count)");
    std::string text = toString(args[0]);
    int count = static_cast<int>(std::stod(toString(args[1])));
    if (count <= 0) return HavelValue(std::string(""));
    if (count >= static_cast<int>(text.size())) return HavelValue(text);
    return HavelValue(text.substr(text.size() - static_cast<size_t>(count)));
  }));

  // startsWith(text, prefix) - check if text starts with prefix
  env.Define("startsWith", BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() < 2) return HavelRuntimeError("startsWith() requires (text, prefix)");
    std::string text = toString(args[0]);
    std::string prefix = toString(args[1]);
    return HavelValue(text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0);
  }));

  // endsWith(text, suffix) - check if text ends with suffix
  env.Define("endsWith", BuiltinFunction([&](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() < 2) return HavelRuntimeError("endsWith() requires (text, suffix)");
    std::string text = toString(args[0]);
    std::string suffix = toString(args[1]);
    return HavelValue(text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0);
  }));
}

} // namespace havel::stdlib
