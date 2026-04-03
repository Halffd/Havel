/* RegexModule.cpp - VM-native stdlib module */
#include "RegexModule.hpp"

using havel::compiler::BytecodeValue;
using havel::compiler::VMApi;

namespace havel::stdlib {

// Register regex module with VMApi (stable API layer)
void registerRegexModule(VMApi &api) {
  // Helper to get string from Value (TODO: string pool lookup)
  auto getString = [](const BytecodeValue &v) -> std::string {
    if (v.isStringValId()) {
      return "<string:" + std::to_string(v.asStringValId()) + ">";
    }
    return "";
  };

  // regex_match(pattern, text) - Test if text matches pattern
  api.registerFunction(
      "regex_match", [&api, getString](const std::vector<BytecodeValue> &args) {
        if (args.size() < 2)
          throw std::runtime_error("regex_match() requires pattern and text");

        if (!args[0].isStringValId() || !args[1].isStringValId())
          throw std::runtime_error("regex_match() requires string arguments");

        const auto &pattern = getString(args[0]);
        const auto &text = getString(args[1]);

        try {
          std::regex re(pattern);
          bool matches = std::regex_match(text, re);
          return BytecodeValue(matches);
        } catch (const std::regex_error &e) {
          throw std::runtime_error("Invalid regex pattern: " +
                                   std::string(e.what()));
        }
      });

  // regex_search(pattern, text) - Search for substring pattern in text
  api.registerFunction(
      "regex_search", [&api, getString](const std::vector<BytecodeValue> &args) {
        if (args.size() < 2)
          throw std::runtime_error("regex_search() requires pattern and text");

        if (!args[0].isStringValId() || !args[1].isStringValId())
          throw std::runtime_error("regex_search() requires string arguments");

        // ByteCompiler pushes: left (text), then right (pattern)
        // So args[0] = text (string to search in), args[1] = pattern (substring)
        const auto &text = getString(args[0]);
        const auto &pattern = getString(args[1]);

        // Simple substring search (not regex)
        bool found = text.find(pattern) != std::string::npos;
        return BytecodeValue(found);
      });

  // regex_replace(pattern, text, replacement) - Replace pattern matches
  api.registerFunction(
      "regex_replace", [&api, getString](const std::vector<BytecodeValue> &args) {
        if (args.size() < 3)
          throw std::runtime_error(
              "regex_replace() requires pattern, text, and replacement");

        if (!args[0].isStringValId() || !args[1].isStringValId() || !args[2].isStringValId())
          throw std::runtime_error("regex_replace() requires string arguments");

        const auto &pattern = getString(args[0]);
        const auto &text = getString(args[1]);
        const auto &replacement = getString(args[2]);

        try {
          std::regex re(pattern);
          std::string result = std::regex_replace(text, re, replacement);
          // TODO: string pool registration
          return Value::makeNull();
        } catch (const std::regex_error &e) {
          throw std::runtime_error("Invalid regex pattern: " +
                                   std::string(e.what()));
        }
      });

  // regex_extract(pattern, text) - Extract all matches as array
  api.registerFunction(
      "regex_extract", [&api, getString](const std::vector<BytecodeValue> &args) {
        if (args.size() < 2)
          throw std::runtime_error("regex_extract() requires pattern and text");

        if (!args[0].isStringValId() || !args[1].isStringValId())
          throw std::runtime_error("regex_extract() requires string arguments");

        const auto &pattern = getString(args[0]);
        const auto &text = getString(args[1]);

        try {
          std::regex re(pattern);
          auto result = api.makeArray();

          auto words_begin = std::sregex_iterator(text.begin(), text.end(), re);
          auto words_end = std::sregex_iterator();

          for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
            std::smatch match = *i;
            // TODO: string pool registration
            api.push(result, Value::makeNull());
          }

          return BytecodeValue(result);
        } catch (const std::regex_error &e) {
          throw std::runtime_error("Invalid regex pattern: " +
                                   std::string(e.what()));
        }
      });

  // regex_split(pattern, text) - Split text by pattern
  api.registerFunction(
      "regex_split", [&api, getString](const std::vector<BytecodeValue> &args) {
        if (args.size() < 2)
          throw std::runtime_error("regex_split() requires pattern and text");

        if (!args[0].isStringValId() || !args[1].isStringValId())
          throw std::runtime_error("regex_split() requires string arguments");

        const auto &pattern = getString(args[0]);
        const auto &text = getString(args[1]);

        try {
          std::regex re(pattern);
          auto result = api.makeArray();

          std::sregex_token_iterator iter(text.begin(), text.end(), re, -1);
          std::sregex_token_iterator end;

          for (; iter != end; ++iter) {
            // TODO: string pool registration
            api.push(result, Value::makeNull());
          }

          return BytecodeValue(result);
        } catch (const std::regex_error &e) {
          throw std::runtime_error("Invalid regex pattern: " +
                                   std::string(e.what()));
        }
      });

  // escape_regex(text) - Escape regex special characters
  api.registerFunction(
      "escape_regex", [&api, getString](const std::vector<BytecodeValue> &args) {
        if (args.empty())
          throw std::runtime_error("escape_regex() requires text");

        if (!args[0].isStringValId())
          throw std::runtime_error("escape_regex() requires a string");

        const auto &text = getString(args[0]);
        std::string result;

        // Escape regex special characters
        for (char c : text) {
          if (c == '.' || c == '^' || c == '$' || c == '*' || c == '+' ||
              c == '?' || c == '(' || c == ')' || c == '[' || c == ']' ||
              c == '{' || c == '}' || c == '|' || c == '\\' || c == '/') {
            result += '\\';
          }
          result += c;
        }

        // TODO: string pool registration
        return Value::makeNull();
      });

  // Register regex object
  auto regexObj = api.makeObject();
  api.setField(regexObj, "match", api.makeFunctionRef("regex_match"));
  api.setField(regexObj, "search", api.makeFunctionRef("regex_search"));
  api.setField(regexObj, "replace", api.makeFunctionRef("regex_replace"));
  api.setField(regexObj, "extract", api.makeFunctionRef("regex_extract"));
  api.setField(regexObj, "split", api.makeFunctionRef("regex_split"));
  api.setField(regexObj, "escape", api.makeFunctionRef("escape_regex"));
  api.setGlobal("Regex", regexObj);
}

} // namespace havel::stdlib
