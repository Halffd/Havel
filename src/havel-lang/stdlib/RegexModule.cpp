/* RegexModule.cpp - VM-native stdlib module */
#include "RegexModule.hpp"

using havel::compiler::BytecodeValue;
using havel::compiler::VMApi;

namespace havel::stdlib {

// Register regex module with VMApi (stable API layer)
void registerRegexModule(VMApi &api) {
  // regex_match(pattern, text) - Test if text matches pattern
  api.registerFunction(
      "regex_match", [&api](const std::vector<BytecodeValue> &args) {
        if (args.size() < 2)
          throw std::runtime_error("regex_match() requires pattern and text");

        if (!std::holds_alternative<std::string>(args[0]) ||
            !std::holds_alternative<std::string>(args[1]))
          throw std::runtime_error("regex_match() requires string arguments");

        const auto &pattern = std::get<std::string>(args[0]);
        const auto &text = std::get<std::string>(args[1]);

        try {
          std::regex re(pattern);
          bool matches = std::regex_match(text, re);
          return BytecodeValue(matches);
        } catch (const std::regex_error &e) {
          throw std::runtime_error("Invalid regex pattern: " +
                                   std::string(e.what()));
        }
      });

  // regex_search(pattern, text) - Search for pattern in text
  api.registerFunction(
      "regex_search", [&api](const std::vector<BytecodeValue> &args) {
        if (args.size() < 2)
          throw std::runtime_error("regex_search() requires pattern and text");

        if (!std::holds_alternative<std::string>(args[0]) ||
            !std::holds_alternative<std::string>(args[1]))
          throw std::runtime_error("regex_search() requires string arguments");

        const auto &pattern = std::get<std::string>(args[0]);
        const auto &text = std::get<std::string>(args[1]);

        try {
          std::regex re(pattern);
          bool found = std::regex_search(text, re);
          return BytecodeValue(found);
        } catch (const std::regex_error &e) {
          throw std::runtime_error("Invalid regex pattern: " +
                                   std::string(e.what()));
        }
      });

  // regex_replace(pattern, text, replacement) - Replace pattern matches
  api.registerFunction(
      "regex_replace", [&api](const std::vector<BytecodeValue> &args) {
        if (args.size() < 3)
          throw std::runtime_error(
              "regex_replace() requires pattern, text, and replacement");

        if (!std::holds_alternative<std::string>(args[0]) ||
            !std::holds_alternative<std::string>(args[1]) ||
            !std::holds_alternative<std::string>(args[2]))
          throw std::runtime_error("regex_replace() requires string arguments");

        const auto &pattern = std::get<std::string>(args[0]);
        const auto &text = std::get<std::string>(args[1]);
        const auto &replacement = std::get<std::string>(args[2]);

        try {
          std::regex re(pattern);
          std::string result = std::regex_replace(text, re, replacement);
          return BytecodeValue(result);
        } catch (const std::regex_error &e) {
          throw std::runtime_error("Invalid regex pattern: " +
                                   std::string(e.what()));
        }
      });

  // regex_extract(pattern, text) - Extract all matches as array
  api.registerFunction(
      "regex_extract", [&api](const std::vector<BytecodeValue> &args) {
        if (args.size() < 2)
          throw std::runtime_error("regex_extract() requires pattern and text");

        if (!std::holds_alternative<std::string>(args[0]) ||
            !std::holds_alternative<std::string>(args[1]))
          throw std::runtime_error("regex_extract() requires string arguments");

        const auto &pattern = std::get<std::string>(args[0]);
        const auto &text = std::get<std::string>(args[1]);

        try {
          std::regex re(pattern);
          auto result = api.makeArray();

          auto words_begin = std::sregex_iterator(text.begin(), text.end(), re);
          auto words_end = std::sregex_iterator();

          for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
            std::smatch match = *i;
            api.push(result, BytecodeValue(match.str()));
          }

          return BytecodeValue(result);
        } catch (const std::regex_error &e) {
          throw std::runtime_error("Invalid regex pattern: " +
                                   std::string(e.what()));
        }
      });

  // regex_split(pattern, text) - Split text by pattern
  api.registerFunction(
      "regex_split", [&api](const std::vector<BytecodeValue> &args) {
        if (args.size() < 2)
          throw std::runtime_error("regex_split() requires pattern and text");

        if (!std::holds_alternative<std::string>(args[0]) ||
            !std::holds_alternative<std::string>(args[1]))
          throw std::runtime_error("regex_split() requires string arguments");

        const auto &pattern = std::get<std::string>(args[0]);
        const auto &text = std::get<std::string>(args[1]);

        try {
          std::regex re(pattern);
          auto result = api.makeArray();

          std::sregex_token_iterator iter(text.begin(), text.end(), re, -1);
          std::sregex_token_iterator end;

          for (; iter != end; ++iter) {
            api.push(result, BytecodeValue(*iter));
          }

          return BytecodeValue(result);
        } catch (const std::regex_error &e) {
          throw std::runtime_error("Invalid regex pattern: " +
                                   std::string(e.what()));
        }
      });

  // escape_regex(text) - Escape regex special characters
  api.registerFunction(
      "escape_regex", [&api](const std::vector<BytecodeValue> &args) {
        if (args.empty())
          throw std::runtime_error("escape_regex() requires text");

        if (!std::holds_alternative<std::string>(args[0]))
          throw std::runtime_error("escape_regex() requires a string");

        const auto &text = std::get<std::string>(args[0]);
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

        return BytecodeValue(result);
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
