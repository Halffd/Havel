/*
* ConfigModule.cpp - Configuration module for bytecode VM
* Provides config.get, config.set, config.save, config.has, config.keys
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

static std::string valueToString(VMApi &api, const Value &v) {
    return api.toString(v);
}

static Value stringToValue(VMApi &api, const std::string &s) {
    if (s == "true") return Value::makeBool(true);
    if (s == "false") return Value::makeBool(false);

    try {
        size_t pos;
        int64_t i = std::stoll(s, &pos);
        if (pos == s.length()) return Value::makeInt(i);
    } catch (...) {}

    try {
        size_t pos;
        double d = std::stod(s, &pos);
        if (pos == s.length()) return Value::makeDouble(d);
    } catch (...) {}

    return api.makeString(s);
}

Value configGet(VMApi &api, const std::vector<Value> &args) {
    if (args.empty()) {
        throw std::runtime_error("config.get() requires at least 1 argument: key");
    }

    std::string key = valueToString(api, args[0]);

    std::string defaultVal = "";
    if (args.size() > 1) {
        defaultVal = valueToString(api, args[1]);
    }

    auto &config = Configs::Get();
    std::string value = config.Get<std::string>(key, defaultVal);

    return stringToValue(api, value);
}

Value configSet(VMApi &api, const std::vector<Value> &args) {
    if (args.size() < 2) {
        throw std::runtime_error("config.set() requires 2 arguments: key, value");
    }

    std::string key = valueToString(api, args[0]);
    std::string value = valueToString(api, args[1]);

    auto &config = Configs::Get();
    config.Set(key, value, true);

    return Value::makeBool(true);
}

Value configSave(const std::vector<Value> &args) {
    (void)args;

    auto &config = Configs::Get();
    config.Save();

    return Value::makeBool(true);
}

Value configGetAll(VMApi &api, const std::vector<Value> &args) {
    (void)args;

    auto &config = Configs::Get();
    auto keys = config.GetAllKeys();

    auto obj = api.makeObject();
    for (const auto &key : keys) {
        std::string value = config.Get<std::string>(key, "");
        api.setField(obj, key, stringToValue(api, value));
    }

    return Value(obj);
}

Value configLoad(const std::vector<Value> &args) {
    (void)args;

    auto &config = Configs::Get();
    config.Load();

    return Value::makeBool(true);
}

Value configHas(const std::vector<Value> &args) {
    if (args.empty()) {
        throw std::runtime_error("config.has() requires 1 argument: key");
    }

    // Simple key check - need to handle string values from VM
    // For has(), we only need the key string; iterate keys
    auto &config = Configs::Get();
    auto allKeys = config.GetAllKeys();

    // Extract key string from Value - handle both string types
    // We need to check the key against all config keys
    // Since we don't have api here, use a simple approach:
    // try to get the key and check if it exists in the list
    // For now, use ConfigObject::has() via the config
    // We need the key as a string - only isStringId/isStringValId work for strings
    // But we can't resolve without VM. Let's use GetConfigs() instead.
    auto configs = config.GetConfigs();
    for (const auto &entry : configs) {
        size_t eqPos = entry.find('=');
        if (eqPos != std::string::npos) {
            // Can't compare key without resolving Value to string
            // This function needs api access
        }
    }

    return Value::makeBool(false);
}

Value configKeys(VMApi &api, const std::vector<Value> &args) {
    (void)args;

    auto &config = Configs::Get();
    auto keys = config.GetAllKeys();

    auto arr = api.makeArray();
    for (const auto &key : keys) {
        api.push(arr, api.makeString(key));
    }

    return Value(arr);
}

void registerConfigModule(VMApi &api) {
    api.registerFunction("config.get", [&api](const std::vector<Value> &args) {
        return configGet(api, args);
    });

    api.registerFunction("config.set", [&api](const std::vector<Value> &args) {
        return configSet(api, args);
    });

    api.registerFunction("config.save", [](const std::vector<Value> &args) {
        return configSave(args);
    });

    api.registerFunction("config.getAll", [&api](const std::vector<Value> &args) {
        return configGetAll(api, args);
    });

    api.registerFunction("config.load", [](const std::vector<Value> &args) {
        return configLoad(args);
    });

    api.registerFunction("config.has", [&api](const std::vector<Value> &args) {
        if (args.empty()) {
            throw std::runtime_error("config.has() requires 1 argument: key");
        }
        std::string key = valueToString(api, args[0]);
        auto &config = Configs::Get();
        auto allKeys = config.GetAllKeys();
        for (const auto &k : allKeys) {
            if (k == key) return Value::makeBool(true);
        }
        return Value::makeBool(false);
    });

    api.registerFunction("config.keys", [&api](const std::vector<Value> &args) {
        return configKeys(api, args);
    });

    info("Config module registered");
}

void autoLoadConfig(VMApi &api) {
    auto &config = Configs::Get();
    auto keys = config.GetAllKeys();

    auto confObj = api.makeObject();
    for (const auto &key : keys) {
        std::string value = config.Get<std::string>(key, "");
        api.setField(confObj, key, stringToValue(api, value));
    }

    api.setGlobal("conf", confObj);

    debug("Config auto-loaded to global conf object with {} keys", keys.size());
}

} // namespace havel::modules
