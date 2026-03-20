/*
 * RegexModule.cpp
 *
 * Regular expression functions for Havel standard library.
 * Extracted from Interpreter.cpp as part of runtime refactoring.
 */
#include "RegexModule.hpp"
#include <regex>

namespace havel::stdlib {

void registerRegexModule(Environment &env) {
  // Helper: convert value to string
  auto valueToString = [](const HavelValue &v) -> std::string {
    if (v.isString())
      return v.asString();
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
    if (v.isBool())
      return v.asBool() ? "true" : "false";
    return "";
  };

  // Create regex namespace object
  auto regexObj =
      std::make_shared<std::unordered_map<std::string, HavelValue>>();

  // ============================================================================
  // Regex functions
  // ============================================================================

  // regex.match(string, pattern) - returns true if pattern matches
  (*regexObj)["match"] = BuiltinFunction(
      [valueToString](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2)
          return HavelRuntimeError("regex.match() requires string and pattern");

        std::string str = valueToString(args[0]);
        std::string pattern = valueToString(args[1]);

        try {
          std::regex re(pattern);
          bool found = std::regex_search(str, re);
          return HavelValue(found);
        } catch (const std::regex_error &e) {
          return HavelRuntimeError(std::string("Invalid regex pattern: ") +
                                   e.what());
        }
      });

  // regex.test(string, pattern) - alias for match
  (*regexObj)["test"] = BuiltinFunction(
      [valueToString](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2)
          return HavelRuntimeError("regex.test() requires string and pattern");

        std::string str = valueToString(args[0]);
        std::string pattern = valueToString(args[1]);

        try {
          std::regex re(pattern);
          bool found = std::regex_search(str, re);
          return HavelValue(found);
        } catch (const std::regex_error &e) {
          return HavelRuntimeError(std::string("Invalid regex pattern: ") +
                                   e.what());
        }
      });

  // regex.search(string, pattern) - returns first match object or null
  (*regexObj)["search"] = BuiltinFunction(
      [valueToString](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2)
          return HavelRuntimeError(
              "regex.search() requires string and pattern");

        std::string str = valueToString(args[0]);
        std::string pattern = valueToString(args[1]);

        try {
          std::regex re(pattern);
          std::smatch match;
          if (std::regex_search(str, match, re)) {
            auto matchObj =
                std::make_shared<std::unordered_map<std::string, HavelValue>>();
            (*matchObj)["match"] = HavelValue(match.str());
            (*matchObj)["index"] =
                HavelValue(static_cast<int>(match.position()));
            (*matchObj)["input"] = HavelValue(str);

            auto groups = std::make_shared<std::vector<HavelValue>>();
            for (size_t i = 0; i < match.size(); ++i) {
              groups->push_back(HavelValue(match.str(i)));
            }
            (*matchObj)["groups"] = HavelValue(groups);

            return HavelValue(matchObj);
          }
          return HavelValue(nullptr);
        } catch (const std::regex_error &e) {
          return HavelRuntimeError(std::string("Invalid regex pattern: ") +
                                   e.what());
        }
      });

  // regex.findall(string, pattern) - returns array of all matches
  (*regexObj)["findall"] = BuiltinFunction(
      [valueToString](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2)
          return HavelRuntimeError(
              "regex.findall() requires string and pattern");

        std::string str = valueToString(args[0]);
        std::string pattern = valueToString(args[1]);

        try {
          std::regex re(pattern);
          auto matches = std::make_shared<std::vector<HavelValue>>();

          auto begin = std::sregex_iterator(str.begin(), str.end(), re);
          auto end = std::sregex_iterator();

          for (auto it = begin; it != end; ++it) {
            const std::smatch &match = *it;
            if (match.size() > 1) {
              auto groups = std::make_shared<std::vector<HavelValue>>();
              for (size_t i = 1; i < match.size(); ++i) {
                groups->push_back(HavelValue(match.str(i)));
              }
              matches->push_back(HavelValue(groups));
            } else {
              matches->push_back(HavelValue(match.str()));
            }
          }

          return HavelValue(matches);
        } catch (const std::regex_error &e) {
          return HavelRuntimeError(std::string("Invalid regex pattern: ") +
                                   e.what());
        }
      });

  // regex.replace(string, pattern, replacement) - replaces all occurrences
  (*regexObj)["replace"] = BuiltinFunction(
      [valueToString](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 3)
          return HavelRuntimeError(
              "regex.replace() requires string, pattern, and replacement");

        std::string str = valueToString(args[0]);
        std::string pattern = valueToString(args[1]);
        std::string replacement = valueToString(args[2]);

        try {
          std::regex re(pattern);
          std::string result = std::regex_replace(str, re, replacement);
          return HavelValue(result);
        } catch (const std::regex_error &e) {
          return HavelRuntimeError(std::string("Invalid regex pattern: ") +
                                   e.what());
        }
      });

  // regex.split(string, pattern) - splits string by pattern
  (*regexObj)["split"] = BuiltinFunction(
      [valueToString](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2)
          return HavelRuntimeError("regex.split() requires string and pattern");

        std::string str = valueToString(args[0]);
        std::string pattern = valueToString(args[1]);

        try {
          std::regex re(pattern);
          auto parts = std::make_shared<std::vector<HavelValue>>();

          auto begin = std::sregex_iterator(str.begin(), str.end(), re);
          auto end = std::sregex_iterator();

          size_t lastPos = 0;
          for (auto it = begin; it != end; ++it) {
            const std::smatch &match = *it;
            if (match.position() > static_cast<std::ptrdiff_t>(lastPos)) {
              parts->push_back(
                  HavelValue(str.substr(lastPos, match.position() - lastPos)));
            }
            lastPos = match.position() + match.length();
          }
          if (lastPos < str.length()) {
            parts->push_back(HavelValue(str.substr(lastPos)));
          }

          return HavelValue(parts);
        } catch (const std::regex_error &e) {
          return HavelRuntimeError(std::string("Invalid regex pattern: ") +
                                   e.what());
        }
      });

  // regex.compile(pattern) - returns a compiled regex object
  (*regexObj)["compile"] = BuiltinFunction(
      [valueToString](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("regex.compile() requires pattern");

        std::string pattern = valueToString(args[0]);

        try {
          std::regex re(pattern);
          auto regexInstance =
              std::make_shared<std::unordered_map<std::string, HavelValue>>();

          (*regexInstance)["pattern"] = HavelValue(pattern);

          // Match method for compiled regex
          (*regexInstance)["match"] = BuiltinFunction(
              [re, valueToString](
                  const std::vector<HavelValue> &args) mutable -> HavelResult {
                if (args.empty())
                  return HavelRuntimeError("regex.match() requires string");
                std::string str = valueToString(args[0]);
                bool found = std::regex_search(str, re);
                return HavelValue(found);
              });

          // Search method for compiled regex
          (*regexInstance)["search"] = BuiltinFunction(
              [re, valueToString](
                  const std::vector<HavelValue> &args) mutable -> HavelResult {
                if (args.empty())
                  return HavelRuntimeError("regex.search() requires string");
                std::string str = valueToString(args[0]);
                std::smatch match;
                if (std::regex_search(str, match, re)) {
                  auto matchObj = std::make_shared<
                      std::unordered_map<std::string, HavelValue>>();
                  (*matchObj)["match"] = HavelValue(match.str());
                  (*matchObj)["index"] =
                      HavelValue(static_cast<int>(match.position()));
                  (*matchObj)["input"] = HavelValue(str);
                  return HavelValue(matchObj);
                }
                return HavelValue(nullptr);
              });

          // Findall method for compiled regex
          (*regexInstance)["findall"] = BuiltinFunction(
              [re, valueToString](
                  const std::vector<HavelValue> &args) mutable -> HavelResult {
                if (args.empty())
                  return HavelRuntimeError("regex.findall() requires string");
                std::string str = valueToString(args[0]);
                auto matches = std::make_shared<std::vector<HavelValue>>();

                auto begin = std::sregex_iterator(str.begin(), str.end(), re);
                auto end = std::sregex_iterator();

                for (auto it = begin; it != end; ++it) {
                  const std::smatch &match = *it;
                  if (match.size() > 1) {
                    auto groups = std::make_shared<std::vector<HavelValue>>();
                    for (size_t i = 1; i < match.size(); ++i) {
                      groups->push_back(HavelValue(match.str(i)));
                    }
                    matches->push_back(HavelValue(groups));
                  } else {
                    matches->push_back(HavelValue(match.str()));
                  }
                }

                return HavelValue(matches);
              });

          return HavelValue(regexInstance);
        } catch (const std::regex_error &e) {
          return HavelRuntimeError(std::string("Invalid regex pattern: ") +
                                   e.what());
        }
      });

  // Register regex namespace
  env.Define("regex", HavelValue(regexObj));
}

} // namespace havel::stdlib
