/*
 * ConfigModule.cpp - Configuration module for bytecode VM
 * Provides config.get, config.set, config.save functions
 * Auto-loads config from file to global conf object
 */
#include "ConfigModule.hpp"
#include "core/ConfigManager.hpp"
#include "havel-lang/compiler/vm/VMApi.hpp"
#include "utils/Logger.hpp"

#include <cmath>
#include <fstream>
#include <sstream>

namespace havel::modules {

using compiler::Value;
using compiler::VMApi;

// Helper to convert Value to string
static std::string toString(const Value &v) {
  if (v.isNull()) return "";
  if (v.isBool()) return v.asBool() ? "true" : "false";
  if (v.isInt()) return std::to_string(v.asInt());
  if (v.isDouble()) {
    double val = v.asDouble();
    if (val == std::floor(val) && std::abs(val) < 1e15) {
      return std::to_string(static_cast<long long>(val));
    }
    std::ostringstream oss;
    oss.precision(15);
    oss << val;
    return oss.str();
  }
  if (v.isStringValId()) {
    // TODO: string pool lookup - for now return placeholder
    return "<string>";
  }
  return "";
}

// Helper to convert string to appropriate Value
static Value fromString(const std::string &s) {
  // Try boolean
  if (s == "true") return Value::makeBool(true);
  if (s == "false") return Value::makeBool(false);

  // Try integer
  try {
    size_t pos;
    int64_t i = std::stoll(s, &pos);
    if (pos == s.length()) return Value::makeInt(i);
  } catch (...) {}

  // Try double
  try {
    size_t pos;
    double d = std::stod(s, &pos);
    if (pos == s.length()) return Value::makeDouble(d);
  } catch (...) {}

  // Default to string
  // TODO: string pool integration - for now return null
  (void)s;
  return Value::makeNull();
}

// config.get(key, default?) - Get config value
Value configGet(const std::vector<Value> &args) {
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
Value configSet(const std::vector<Value> &args) {
  if (args.size() < 2) {
    throw std::runtime_error("config.set() requires 2 arguments: key, value");
  }

  std::string key = toString(args[0]);
  std::string value = toString(args[1]);

  auto &config = Configs::Get();
  config.Set(key, value, false);  // Don't save yet

  return Value::makeBool(true);
}

// config.save() - Save config to file
Value configSave(const std::vector<Value> &args) {
  (void)args;
  
  auto &config = Configs::Get();
  config.Save();
  
  return Value::makeBool(true);
}

// config.getAll() - Get all config as object
Value configGetAll(VMApi &api, const std::vector<Value> &args) {
  (void)args;
  
  auto &config = Configs::Get();
  auto keys = config.GetAllKeys();
  
  auto obj = api.makeObject();
  for (const auto &key : keys) {
    std::string value = config.Get<std::string>(key, "");
    api.setField(obj, key, fromString(value));
  }
  
  return Value(obj);
}

// config.load() - Reload config from file
Value configLoad(const std::vector<Value> &args) {
  (void)args;
  
  auto &config = Configs::Get();
  config.Load();
  
  return Value(true);
}

// Register config module with VM
void registerConfigModule(VMApi &api) {
  // config.get(key, default?)
  api.registerFunction("config.get", [](const std::vector<Value> &args) {
    return configGet(args);
  });

  // config.set(key, value)
  api.registerFunction("config.set", [](const std::vector<Value> &args) {
    return configSet(args);
  });
  
  // config.save()
  api.registerFunction("config.save", [](const std::vector<Value> &args) {
    return configSave(args);
  });
  
  // config.getAll()
  api.registerFunction("config.getAll", [&api](const std::vector<Value> &args) {
    return configGetAll(api, args);
  });
  
  // config.load()
  api.registerFunction("config.load", [](const std::vector<Value> &args) {
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
