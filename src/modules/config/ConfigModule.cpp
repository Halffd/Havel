/*
 * ConfigModule.cpp - Configuration module for bytecode VM
 * Provides config.get, config.set, config.save functions
 * Auto-loads config from file to global conf object
 */
#include "ConfigModule.hpp"
#include "core/ConfigManager.hpp"
#include "havel-lang/compiler/bytecode/VMApi.hpp"
#include "utils/Logger.hpp"

#include <fstream>
#include <sstream>

namespace havel::modules {

using compiler::BytecodeValue;
using compiler::VMApi;

// Helper to convert BytecodeValue to string
static std::string toString(const BytecodeValue &v) {
  if (std::holds_alternative<std::nullptr_t>(v)) return "";
  if (std::holds_alternative<bool>(v)) return std::get<bool>(v) ? "true" : "false";
  if (std::holds_alternative<int64_t>(v)) return std::to_string(std::get<int64_t>(v));
  if (std::holds_alternative<double>(v)) {
    double val = std::get<double>(v);
    if (val == std::floor(val) && std::abs(val) < 1e15) {
      return std::to_string(static_cast<long long>(val));
    }
    std::ostringstream oss;
    oss.precision(15);
    oss << val;
    return oss.str();
  }
  if (std::holds_alternative<std::string>(v)) return std::get<std::string>(v);
  return "";
}

// Helper to convert string to appropriate BytecodeValue
static BytecodeValue fromString(const std::string &s) {
  // Try boolean
  if (s == "true") return BytecodeValue(true);
  if (s == "false") return BytecodeValue(false);

  // Try integer
  try {
    size_t pos;
    int64_t i = std::stoll(s, &pos);
    if (pos == s.length()) return BytecodeValue(i);
  } catch (...) {}

  // Try double
  try {
    size_t pos;
    double d = std::stod(s, &pos);
    if (pos == s.length()) return BytecodeValue(d);
  } catch (...) {}

  // Default to string
  return BytecodeValue(s);
}

// config.get(key, default?) - Get config value
BytecodeValue configGet(const std::vector<BytecodeValue> &args) {
  if (args.empty()) {
    throw std::runtime_error("config.get() requires at least 1 argument: key");
  }

  std::string key = toString(args[0]);

  // Get default value if provided
  std::string defaultVal = "";
  if (args.size() > 1) {
    defaultVal = toString(args[1]);
  }

  auto &config = Configs::Get();
  std::string value = config.Get<std::string>(key, defaultVal);

  return fromString(value);
}

// config.set(key, value) - Set config value (in memory only)
BytecodeValue configSet(const std::vector<BytecodeValue> &args) {
  if (args.size() < 2) {
    throw std::runtime_error("config.set() requires 2 arguments: key, value");
  }

  std::string key = toString(args[0]);
  std::string value = toString(args[1]);

  auto &config = Configs::Get();
  config.Set(key, value, false);  // Don't save yet

  return BytecodeValue(true);
}

// config.save() - Save config to file
BytecodeValue configSave(const std::vector<BytecodeValue> &args) {
  (void)args;
  
  auto &config = Configs::Get();
  config.Save();
  
  return BytecodeValue(true);
}

// config.getAll() - Get all config as object
BytecodeValue configGetAll(VMApi &api, const std::vector<BytecodeValue> &args) {
  (void)args;
  
  auto &config = Configs::Get();
  auto keys = config.GetAllKeys();
  
  auto obj = api.makeObject();
  for (const auto &key : keys) {
    std::string value = config.Get<std::string>(key, "");
    api.setField(obj, key, fromString(value));
  }
  
  return BytecodeValue(obj);
}

// config.load() - Reload config from file
BytecodeValue configLoad(const std::vector<BytecodeValue> &args) {
  (void)args;
  
  auto &config = Configs::Get();
  config.Load();
  
  return BytecodeValue(true);
}

// Register config module with VM
void registerConfigModule(VMApi &api) {
  // config.get(key, default?)
  api.registerFunction("config.get", [](const std::vector<BytecodeValue> &args) {
    return configGet(args);
  });

  // config.set(key, value)
  api.registerFunction("config.set", [](const std::vector<BytecodeValue> &args) {
    return configSet(args);
  });
  
  // config.save()
  api.registerFunction("config.save", [](const std::vector<BytecodeValue> &args) {
    return configSave(args);
  });
  
  // config.getAll()
  api.registerFunction("config.getAll", [&api](const std::vector<BytecodeValue> &args) {
    return configGetAll(api, args);
  });
  
  // config.load()
  api.registerFunction("config.load", [](const std::vector<BytecodeValue> &args) {
    return configLoad(args);
  });

  // Note: config.* functions are registered directly as host functions in StdLibModules.cpp
  // We don't register a config object here to avoid conflicts with module.function call syntax

  info("Config module registered");
}

// Auto-load config from file to global conf object
void autoLoadConfig(VMApi &api) {
  auto &config = Configs::Get();
  auto keys = config.GetAllKeys();
  
  // Create conf object with all config values
  auto confObj = api.makeObject();
  for (const auto &key : keys) {
    std::string value = config.Get<std::string>(key, "");
    api.setField(confObj, key, fromString(value));
  }
  
  // Set as global conf object
  api.setGlobal("conf", confObj);
  
  debug("Config auto-loaded to global conf object with " + 
        std::to_string(keys.size()) + " keys");
}

} // namespace havel::modules
