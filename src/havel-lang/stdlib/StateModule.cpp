#include "StateModule.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstdlib>
#include <unistd.h>
#include <filesystem>
#include <memory>

using json = nlohmann::json;
using havel::compiler::Value;
using havel::compiler::VMApi;

namespace {

struct StateStore {
    json cached = json::object();
    bool loaded = false;
    std::string path;
};

std::string getStatePath() {
    const char* env = std::getenv("HAVEL_STATE_PATH");
    if (env && env[0]) return env;

    const char* home = std::getenv("HOME");
    if (!home) home = "/tmp";

    std::string dir = std::string(home) + "/.config/havel";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir + "/state.json";
}

json loadFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return json::object();
    try {
        return json::parse(f);
    } catch (...) {
        return json::object();
    }
}

bool saveFile(const std::string& path, const json& data) {
    std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp);
        if (!f.is_open()) return false;
        f << data.dump(2) << std::flush;
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::error_code ec;
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

json valueToJson(const VMApi& api, const Value& val) {
    if (val.isNull()) return nullptr;
    if (val.isBool()) return val.asBool();
    if (val.isInt()) return val.asInt();
    if (val.isDouble()) return val.asDouble();
    if (val.isStringId() || val.isStringValId())
        return api.resolveString(val);
    if (val.isObjectId()) {
        json obj = json::object();
        for (const auto& key : api.getObjectKeys(val))
            obj[key] = valueToJson(api, api.getField(val, key));
        return obj;
    }
    if (val.isArrayId()) {
        json arr = json::array();
        uint32_t len = api.length(val);
        for (uint32_t i = 0; i < len; i++)
            arr.push_back(valueToJson(api, api.getAt(val, i)));
        return arr;
    }
    return nullptr;
}

Value jsonToValue(const VMApi& api, const json& j) {
    if (j.is_null()) return Value::makeNull();
    if (j.is_boolean()) return Value::makeBool(j.get<bool>());
    if (j.is_number_integer()) return Value::makeInt(j.get<int64_t>());
    if (j.is_number_unsigned()) return Value::makeInt(static_cast<int64_t>(j.get<uint64_t>()));
    if (j.is_number_float()) return Value::makeDouble(j.get<double>());
    if (j.is_string()) return api.makeString(j.get<std::string>());
    if (j.is_object()) {
        auto obj = api.makeObject();
        for (auto it = j.begin(); it != j.end(); ++it)
            api.setField(obj, it.key(), jsonToValue(api, it.value()));
        return obj;
    }
    if (j.is_array()) {
        auto arr = api.makeArray();
        for (const auto& item : j)
            api.push(arr, jsonToValue(api, item));
        return arr;
    }
    return Value::makeNull();
}

} // namespace

namespace havel::stdlib {

void registerStateModule(const VMApi &api) {
    auto store = std::make_shared<StateStore>();
    store->path = getStatePath();

    api.registerFunction("state.save", [api, store](const std::vector<Value> &args) {
        if (args.size() < 2)
            throw std::runtime_error("state.save(key, value) requires 2 arguments");
        std::string key = api.resolveString(args[0]);
        if (!store->loaded) {
            store->cached = loadFile(store->path);
            store->loaded = true;
        }
        store->cached[key] = valueToJson(api, args[1]);
        saveFile(store->path, store->cached);
        return Value::makeNull();
    });

    api.registerFunction("state.load", [api, store](const std::vector<Value> &args) {
        if (args.size() < 1)
            throw std::runtime_error("state.load(key) requires at least 1 argument");
        std::string key = api.resolveString(args[0]);
        if (!store->loaded) {
            store->cached = loadFile(store->path);
            store->loaded = true;
        }
        auto it = store->cached.find(key);
        if (it == store->cached.end()) {
            if (args.size() >= 2) return args[1];
            return Value::makeNull();
        }
        return jsonToValue(api, *it);
    });

    api.registerFunction("state.has", [api, store](const std::vector<Value> &args) {
        if (args.size() < 1)
            throw std::runtime_error("state.has(key) requires 1 argument");
        std::string key = api.resolveString(args[0]);
        if (!store->loaded) {
            store->cached = loadFile(store->path);
            store->loaded = true;
        }
        return Value::makeBool(store->cached.find(key) != store->cached.end());
    });

    api.registerFunction("state.remove", [api, store](const std::vector<Value> &args) {
        if (args.size() < 1)
            throw std::runtime_error("state.remove(key) requires 1 argument");
        std::string key = api.resolveString(args[0]);
        if (!store->loaded) {
            store->cached = loadFile(store->path);
            store->loaded = true;
        }
        bool existed = store->cached.find(key) != store->cached.end();
        if (existed) {
            store->cached.erase(key);
            saveFile(store->path, store->cached);
        }
        return Value::makeBool(existed);
    });

    api.registerFunction("state.keys", [api, store](const std::vector<Value> &) {
        if (!store->loaded) {
            store->cached = loadFile(store->path);
            store->loaded = true;
        }
        auto arr = api.makeArray();
        for (auto it = store->cached.begin(); it != store->cached.end(); ++it)
            api.push(arr, api.makeString(it.key()));
        return arr;
    });

    api.registerFunction("state.clear", [store](const std::vector<Value> &) {
        store->cached = json::object();
        store->loaded = true;
        saveFile(store->path, store->cached);
        return Value::makeNull();
    });

    auto stateObj = api.makeObject();
    api.setField(stateObj, "save", api.makeFunctionRef("state.save"));
    api.setField(stateObj, "load", api.makeFunctionRef("state.load"));
    api.setField(stateObj, "has", api.makeFunctionRef("state.has"));
    api.setField(stateObj, "remove", api.makeFunctionRef("state.remove"));
    api.setField(stateObj, "keys", api.makeFunctionRef("state.keys"));
    api.setField(stateObj, "clear", api.makeFunctionRef("state.clear"));
    api.setField(stateObj, "path", api.makeString(store->path));
    api.setGlobal("state", stateObj);
}

} // namespace havel::stdlib

#ifdef HAVEL_MODULE_PLUGIN
#include "c/ModulePlugin.h"

HAVEL_MODULE_PLUGIN_IMPL(state, "1.0.0", "Persistent state storage stdlib module",
    havel::stdlib::registerStateModule(*api);
)
#endif
