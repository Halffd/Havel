/*
 * ConfigModule.cpp - Configuration module for bytecode VM
 * Provides config.get, config.set, config.save, config.has, config.list
 * Supports autovivification (__vivify) and auto-save (__autosave_root)
 */
#include "ConfigModule.hpp"
#include "core/config/ConfigManager.hpp"
#include "havel-lang/compiler/vm/VMApi.hpp"
#include "utils/Logger.hpp"

#include <cmath>
#include <fstream>
#include <sstream>

namespace havel::modules {

using compiler::Value;
using compiler::VMApi;

static std::string valueToString(const VMApi &api, const Value &v) {
    return api.toString(v);
}

static Value stringToValue(const VMApi &api, const std::string &s) {
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

static void setNestedField(const VMApi &api, Value obj, const std::string &key, Value value);

static std::vector<Value> getActualArgs(const VMApi &api, const std::vector<Value> &args) {
    if (args.empty()) return args;
    if (args[0].isObjectId()) {
        try {
            if (api.hasField(args[0], "__vivify") || api.hasField(args[0], "__autosave_root")) {
                std::vector<Value> actual;
                for (size_t i = 1; i < args.size(); ++i) {
                    actual.push_back(args[i]);
                }
                return actual;
            }
        } catch (...) {}
    }
    return args;
}

Value configGet(const VMApi &api, const std::vector<Value> &args) {
    auto actual = getActualArgs(api, args);
    if (actual.empty()) throw std::runtime_error("config.get() requires key");
    std::string key = valueToString(api, actual[0]);
    std::string defaultVal = (actual.size() > 1) ? valueToString(api, actual[1]) : "";
    return stringToValue(api, Configs::Get().Get<std::string>(key, defaultVal));
}

Value configSet(const VMApi &api, const std::vector<Value> &args) {
    auto actual = getActualArgs(api, args);
    if (actual.size() < 2) throw std::runtime_error("config.set() requires key, value");
    Configs::Get().Set(valueToString(api, actual[0]), valueToString(api, actual[1]), true);
    return Value::makeBool(true);
}

Value configSave(const std::vector<Value> &args) {
    (void)args;
    Configs::Get().ForceSave(); // Force immediate save
    return Value::makeBool(true);
}

Value configKeys(const VMApi &api, const std::vector<Value> &args) {
    (void)args;
    auto keys = Configs::Get().GetAllKeys();
    auto arr = api.makeArray();
    for (const auto &key : keys) api.push(arr, api.makeString(key));
    return arr;
}

void registerConfigModule(const VMApi &api) {
    auto configObj = api.makeObject();
    compiler::VM* vm = &api.vm();
    api.setGlobal("config", configObj);
    api.setGlobal("cfg", configObj);
    api.setGlobal("conf", configObj);

    for (const auto &key : Configs::Get().GetAllKeys()) {
        setNestedField(api, configObj, key, stringToValue(api, Configs::Get().Get<std::string>(key, "")));
    }
}

static void setNestedField(const VMApi &api, Value obj, const std::string &key, Value value) {
    size_t dot = key.find('.');
    if (dot == std::string::npos) { api.setField(obj, key, std::move(value)); return; }
    std::string head = key.substr(0, dot), tail = key.substr(dot + 1);
    Value sub = api.hasField(obj, head) ? api.getField(obj, head) : Value::makeNull();
    if (!sub.isObjectId()) { sub = api.makeObject(); api.setField(obj, head, sub); }
    if (api.hasField(obj, "__vivify")) api.setField(sub, "__vivify", api.getField(obj, "__vivify"));
    if (api.hasField(obj, "__autosave_root")) api.setField(sub, "__autosave_root", api.getField(obj, "__autosave_root"));
    std::string p = api.hasField(obj, "__cfg_path") ? (api.toString(api.getField(obj, "__cfg_path")) + "." + head) : head;
    api.setField(sub, "__cfg_path", api.makeString(p));
    setNestedField(api, sub, tail, std::move(value));
}

void autoLoadConfig(const VMApi &api) { (void)api; }
} // namespace havel::modules

#ifdef HAVEL_MODULE_PLUGIN
#include "c/ModulePlugin.h"

HAVEL_MODULE_PLUGIN_IMPL_A2(config, "1.0.0", "Configuration module", "cfg", "conf",
havel::modules::registerConfigModule(*api);
)
#endif
