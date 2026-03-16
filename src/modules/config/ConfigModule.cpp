/*
 * ConfigModule.cpp
 *
 * Config module for Havel language.
 * Exposes config.* API for scripts with nested access support.
 */
#include "ConfigModule.hpp"
#include "../../core/ConfigManager.hpp"
#include <memory>

namespace havel::modules {

void registerConfigModule(Environment &env, std::shared_ptr<IHostAPI> hostAPI) {
  auto configObj =
      std::make_shared<std::unordered_map<std::string, HavelValue>>();

  // config.get(key) - Get config value
  (*configObj)["get"] = HavelValue(BuiltinFunction(
      [hostAPI](const std::vector<HavelValue> &args) -> HavelResult {
        if (!hostAPI) {
          return HavelRuntimeError("HostAPI not available");
        }
        if (args.empty()) {
          return HavelRuntimeError("config.get() requires a key");
        }

        std::string key = args[0].asString();
        auto &config = hostAPI->GetConfig();

        // Try to get the value with empty string as default
        auto value = config.Get<std::string>(key, "");
        if (!value.empty()) {
          return HavelValue(value);
        }

        return HavelValue(nullptr);
      }));

  // config.set(key, value) - Set config value
  (*configObj)["set"] = HavelValue(BuiltinFunction(
      [hostAPI](const std::vector<HavelValue> &args) -> HavelResult {
        if (!hostAPI) {
          return HavelRuntimeError("HostAPI not available");
        }
        if (args.size() < 2) {
          return HavelRuntimeError("config.set() requires key and value");
        }

        std::string key = args[0].asString();
        HavelValue value = args[1];
        auto &config = hostAPI->GetConfig();

        config.Set(key, value);
        return HavelValue(true);
      }));

  // config.list(pattern) - List config keys matching pattern
  (*configObj)["list"] = HavelValue(BuiltinFunction(
      [hostAPI](const std::vector<HavelValue> &args) -> HavelResult {
        if (!hostAPI) {
          return HavelRuntimeError("HostAPI not available");
        }

        std::string pattern = args.empty() ? "" : args[0].asString();
        auto &config = hostAPI->GetConfig();

        auto resultArray = std::make_shared<std::vector<HavelValue>>();
        auto allKeys = config.GetAllKeys();
        for (const auto &key : allKeys) {
          if (pattern.empty() || key.find(pattern) != std::string::npos) {
            resultArray->push_back(HavelValue(key));
          }
        }

        return HavelValue(resultArray);
      }));

  // config.load(path) - Load config from file
  (*configObj)["load"] = HavelValue(BuiltinFunction(
      [hostAPI](const std::vector<HavelValue> &args) -> HavelResult {
        if (!hostAPI) {
          return HavelRuntimeError("HostAPI not available");
        }
        if (args.empty()) {
          return HavelRuntimeError("config.load() requires a path");
        }

        std::string path = args[0].asString();
        auto &config = hostAPI->GetConfig();

        config.Load(path);
        return HavelValue(true);
      }));

  // config.save(path) - Save config to file
  (*configObj)["save"] = HavelValue(BuiltinFunction(
      [hostAPI](const std::vector<HavelValue> &args) -> HavelResult {
        if (!hostAPI) {
          return HavelRuntimeError("HostAPI not available");
        }
        std::string path = args.empty() ? "" : args[0].asString();
        auto &config = hostAPI->GetConfig();

        config.Save(path);
        return HavelValue(true);
      }));

  // config.gaming, config.window, etc. - Nested config access
  // This creates proxy objects for nested access
  auto createNestedConfig = [hostAPI](const std::string &prefix) -> HavelValue {
    auto nestedObj =
        std::make_shared<std::unordered_map<std::string, HavelValue>>();

    (*nestedObj)["get"] = HavelValue(BuiltinFunction(
        [hostAPI, prefix](const std::vector<HavelValue> &args) -> HavelResult {
          if (!hostAPI || args.empty()) {
            return HavelRuntimeError("config." + prefix +
                                     ".get() requires a key");
          }
          std::string key = prefix + "." + args[0].asString();
          auto &config = hostAPI->GetConfig();
          auto value = config.Get<std::string>(key, "");
          return !value.empty() ? HavelValue(value) : HavelValue(nullptr);
        }));

    (*nestedObj)["set"] = HavelValue(BuiltinFunction(
        [hostAPI, prefix](const std::vector<HavelValue> &args) -> HavelResult {
          if (!hostAPI || args.size() < 2) {
            return HavelRuntimeError("config." + prefix +
                                     ".set() requires key and value");
          }
          std::string key = prefix + "." + args[0].asString();
          auto &config = hostAPI->GetConfig();
          config.Set(key, args[1]);
          return HavelValue(true);
        }));

    (*nestedObj)["list"] = HavelValue(BuiltinFunction(
        [hostAPI, prefix](const std::vector<HavelValue> &args) -> HavelResult {
          if (!hostAPI) {
            return HavelRuntimeError("HostAPI not available");
          }
          auto &config = hostAPI->GetConfig();
          auto resultArray = std::make_shared<std::vector<HavelValue>>();
          auto allKeys = config.GetAllKeys();
          std::string prefixWithDot = prefix + ".";
          for (const auto &key : allKeys) {
            if (key.find(prefixWithDot) == 0) {
              // Remove prefix from key name
              std::string shortKey = key.substr(prefixWithDot.length());
              resultArray->push_back(HavelValue(shortKey));
            }
          }
          return HavelValue(resultArray);
        }));

    return HavelValue(nestedObj);
  };

  // Create common nested config objects
  (*configObj)["gaming"] = createNestedConfig("Havel.gaming");
  (*configObj)["window"] = createNestedConfig("Havel.window");
  (*configObj)["hotkeys"] = createNestedConfig("Havel.hotkeys");
  (*configObj)["display"] = createNestedConfig("Havel.display");
  (*configObj)["audio"] = createNestedConfig("Havel.audio");

  env.Define("config", HavelValue(configObj));
}

} // namespace havel::modules
