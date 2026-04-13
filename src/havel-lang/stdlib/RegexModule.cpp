/* RegexModule.cpp - VM-native stdlib module */
#include "RegexModule.hpp"

using havel::compiler::Value;
using havel::compiler::VMApi;

namespace havel::stdlib {

// Helper: extract string from Value using VMApi
static std::string getString(VMApi &api, const Value &v) {
  return api.toString(v);
}

// Register regex module with VMApi (stable API layer)
void registerRegexModule(VMApi &api) {

  // regex_match(pattern, text) - Test if entire text matches pattern
  api.registerFunction("regex_match", [&api](const std::vector<Value> &args) {
    if (args.size() < 2)
      throw std::runtime_error("regex_match() requires pattern and text");

    std::string pattern = getString(api, args[0]);
    std::string text = getString(api, args[1]);

    try {
      std::regex re(pattern);
      return api.makeBool(std::regex_match(text, re));
    } catch (const std::regex_error &e) {
      throw std::runtime_error("Invalid regex pattern: " + std::string(e.what()));
    }
  });

  // regex_search(pattern, text) - Search for pattern anywhere in text
  api.registerFunction("regex_search", [&api](const std::vector<Value> &args) {
    if (args.size() < 2)
      throw std::runtime_error("regex_search() requires pattern and text");

    std::string text = getString(api, args[0]);
    std::string pattern = getString(api, args[1]);

    try {
      std::regex re(pattern);
      return api.makeBool(std::regex_search(text, re));
    } catch (const std::regex_error &e) {
      throw std::runtime_error("Invalid regex pattern: " + std::string(e.what()));
    }
  });

  // regex_replace(pattern, text, replacement) - Replace all pattern matches
  api.registerFunction("regex_replace", [&api](const std::vector<Value> &args) {
    if (args.size() < 3)
      throw std::runtime_error("regex_replace() requires pattern, text, and replacement");

    std::string pattern = getString(api, args[0]);
    std::string text = getString(api, args[1]);
    std::string replacement = getString(api, args[2]);

    try {
      std::regex re(pattern);
      std::string result = std::regex_replace(text, re, replacement);
      return api.makeString(std::move(result));
    } catch (const std::regex_error &e) {
      throw std::runtime_error("Invalid regex pattern: " + std::string(e.what()));
    }
  });

  // regex_extract(pattern, text) - Extract all matches as array of strings
  api.registerFunction("regex_extract", [&api](const std::vector<Value> &args) {
    if (args.size() < 2)
      throw std::runtime_error("regex_extract() requires pattern and text");

    std::string pattern = getString(api, args[0]);
    std::string text = getString(api, args[1]);

    try {
      std::regex re(pattern);
      auto result = api.makeArray();
      auto it = std::sregex_iterator(text.begin(), text.end(), re);
      auto end = std::sregex_iterator();

      for (; it != end; ++it) {
        std::smatch match = *it;
        // If there are capture groups, return array of groups; else return full match
        if (match.size() > 1) {
          auto groups = api.makeArray();
          for (size_t i = 1; i < match.size(); ++i) {
            api.push(groups, api.makeString(match[i].str()));
          }
          api.push(result, groups);
        } else {
          api.push(result, api.makeString(match[0].str()));
        }
      }

      return result;
    } catch (const std::regex_error &e) {
      throw std::runtime_error("Invalid regex pattern: " + std::string(e.what()));
    }
  });

  // regex_split(pattern, text) - Split text by pattern into array
  api.registerFunction("regex_split", [&api](const std::vector<Value> &args) {
    if (args.size() < 2)
      throw std::runtime_error("regex_split() requires pattern and text");

    std::string pattern = getString(api, args[0]);
    std::string text = getString(api, args[1]);

    try {
      std::regex re(pattern);
      auto result = api.makeArray();
      auto it = std::sregex_token_iterator(text.begin(), text.end(), re, -1);
      auto end = std::sregex_token_iterator();

      for (; it != end; ++it) {
        api.push(result, api.makeString(*it));
      }

      return result;
    } catch (const std::regex_error &e) {
      throw std::runtime_error("Invalid regex pattern: " + std::string(e.what()));
    }
  });

  // escape_regex(text) - Escape regex special characters
  api.registerFunction("escape_regex", [&api](const std::vector<Value> &args) {
    if (args.empty())
      throw std::runtime_error("escape_regex() requires text");

    std::string text = getString(api, args[0]);
    std::string result;

    static const std::string specialChars = R"(.^$|()\[]{}*+?/\)";
    for (char c : text) {
      if (specialChars.find(c) != std::string::npos) {
        result += '\\';
      }
      result += c;
    }

    return api.makeString(std::move(result));
  });

  // Register regex object for Regex.match(), Regex.search(), etc.
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
