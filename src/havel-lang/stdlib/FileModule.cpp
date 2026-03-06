/*
 * FileModule.cpp
 * 
 * File I/O functions for Havel standard library.
 * Extracted from Interpreter.cpp as part of runtime refactoring.
 */
#include "FileModule.hpp"
#include <fstream>
#include <filesystem>

namespace havel::stdlib {

void registerFileModule(Environment* env) {
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

  // ============================================================================
  // File functions
  // ============================================================================

  // file.read(path) - read entire file content
  env->Define("file.read", BuiltinFunction([valueToString](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.empty()) return HavelRuntimeError("file.read() requires path");
    std::string path = valueToString(args[0]);
    std::ifstream file(path);
    if (!file.is_open()) return HavelRuntimeError("Cannot open file: " + path);

    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    return HavelValue(content);
  }));

  // file.write(path, content) - write content to file
  env->Define("file.write", BuiltinFunction([valueToString](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() < 2) return HavelRuntimeError("file.write() requires (path, content)");
    std::string path = valueToString(args[0]);
    std::string content = valueToString(args[1]);

    std::ofstream file(path);
    if (!file.is_open()) return HavelRuntimeError("Cannot write to file: " + path);

    file << content;
    return HavelValue(true);
  }));

  // file.exists(path) - check if file exists
  env->Define("file.exists", BuiltinFunction([valueToString](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.empty()) return HavelRuntimeError("file.exists() requires path");
    std::string path = valueToString(args[0]);
    return HavelValue(std::filesystem::exists(path));
  }));
}

} // namespace havel::stdlib
