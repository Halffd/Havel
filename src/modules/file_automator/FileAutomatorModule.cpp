#include "FileAutomatorModule.hpp"
#include "modules/ModuleMacros.hpp"
#include "utils/Logger.hpp"
#include <filesystem>
#include <fstream>

namespace havel::modules {

using compiler::Value;
using compiler::VMApi;

static const char* MODULE_MARKER = "__file_automator_module";

static bool isModuleObject(const VMApi& api, const Value& val) {
    if (!val.isObjectId()) return false;
    auto marker = api.getField(val, MODULE_MARKER);
    return marker.isBool() && marker.asBool();
}

static std::vector<Value> stripReceiver(const VMApi& api, const std::vector<Value>& args) {
    if (!args.empty() && isModuleObject(api, args[0])) {
        return std::vector<Value>(args.begin() + 1, args.end());
    }
    return args;
}

static auto makeListFunc(const VMApi& api) {
    return [api](const std::vector<Value>& rawArgs) -> Value {
        auto args = stripReceiver(api, rawArgs);
        if (args.empty()) return api.makeNull();
        std::string path = args[0].isStringValId() ? api.resolveString(args[0]) : ".";
        bool recursive = args.size() > 1 ? args[1].asBool() : false;
        
        auto result = api.makeArray();
        try {
            if (recursive) {
                for (const auto& entry : std::filesystem::recursive_directory_iterator(path)) {
                    auto item = api.makeObject();
                    api.setField(item, "path", api.makeString(entry.path().string()));
                    api.setField(item, "name", api.makeString(entry.path().filename().string()));
                    api.setField(item, "isDir", Value::makeBool(entry.is_directory()));
                    api.setField(item, "size", Value::makeInt(static_cast<int>(entry.file_size())));
                    api.push(result, item);
                }
            } else {
                for (const auto& entry : std::filesystem::directory_iterator(path)) {
                    auto item = api.makeObject();
                    api.setField(item, "path", api.makeString(entry.path().string()));
                    api.setField(item, "name", api.makeString(entry.path().filename().string()));
                    api.setField(item, "isDir", Value::makeBool(entry.is_directory()));
                    api.setField(item, "size", Value::makeInt(static_cast<int>(entry.file_size())));
                    api.push(result, item);
                }
            }
        } catch (const std::exception& e) {
            debug("file_automator.list error: {}", e.what());
            return api.makeNull();
        }
        return result;
    };
}

static auto makeReadFunc(const VMApi& api) {
    return [api](const std::vector<Value>& rawArgs) -> Value {
        auto args = stripReceiver(api, rawArgs);
        if (args.empty()) return api.makeNull();
        std::string path = args[0].isStringValId() ? api.resolveString(args[0]) : "";
        
        try {
            std::ifstream file(path);
            if (!file) return api.makeNull();
            std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            return api.makeString(content);
        } catch (const std::exception& e) {
            debug("file_automator.read error: {}", e.what());
            return api.makeNull();
        }
    };
}

static auto makeWriteFunc(const VMApi& api) {
    return [api](const std::vector<Value>& rawArgs) -> Value {
        auto args = stripReceiver(api, rawArgs);
        if (args.size() < 2) return Value::makeBool(false);
        std::string path = args[0].isStringValId() ? api.resolveString(args[0]) : "";
        std::string content = args[1].isStringValId() ? api.resolveString(args[1]) : "";
        
        try {
            std::ofstream file(path);
            if (!file) return Value::makeBool(false);
            file << content;
            return Value::makeBool(true);
        } catch (const std::exception& e) {
            debug("file_automator.write error: {}", e.what());
            return Value::makeBool(false);
        }
    };
}

static auto makeCopyFunc(const VMApi& api) {
    return [api](const std::vector<Value>& rawArgs) -> Value {
        auto args = stripReceiver(api, rawArgs);
        if (args.size() < 2) return Value::makeBool(false);
        std::string src = args[0].isStringValId() ? api.resolveString(args[0]) : "";
        std::string dst = args[1].isStringValId() ? api.resolveString(args[1]) : "";
        
        try {
            std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing);
            return Value::makeBool(true);
        } catch (const std::exception& e) {
            debug("file_automator.copy error: {}", e.what());
            return Value::makeBool(false);
        }
    };
}

static auto makeMoveFunc(const VMApi& api) {
    return [api](const std::vector<Value>& rawArgs) -> Value {
        auto args = stripReceiver(api, rawArgs);
        if (args.size() < 2) return Value::makeBool(false);
        std::string src = args[0].isStringValId() ? api.resolveString(args[0]) : "";
        std::string dst = args[1].isStringValId() ? api.resolveString(args[1]) : "";
        
        try {
            std::filesystem::rename(src, dst);
            return Value::makeBool(true);
        } catch (const std::exception& e) {
            debug("file_automator.move error: {}", e.what());
            return Value::makeBool(false);
        }
    };
}

static auto makeRemoveFunc(const VMApi& api) {
    return [api](const std::vector<Value>& rawArgs) -> Value {
        auto args = stripReceiver(api, rawArgs);
        if (args.empty()) return Value::makeBool(false);
        std::string path = args[0].isStringValId() ? api.resolveString(args[0]) : "";
        bool recursive = args.size() > 1 ? args[1].asBool() : false;
        
        try {
            if (recursive) {
                std::filesystem::remove_all(path);
            } else {
                std::filesystem::remove(path);
            }
            return Value::makeBool(true);
        } catch (const std::exception& e) {
            debug("file_automator.remove error: {}", e.what());
            return Value::makeBool(false);
        }
    };
}

static auto makeExistsFunc(const VMApi& api) {
    return [api](const std::vector<Value>& rawArgs) -> Value {
        auto args = stripReceiver(api, rawArgs);
        if (args.empty()) return Value::makeBool(false);
        std::string path = args[0].isStringValId() ? api.resolveString(args[0]) : "";
        return Value::makeBool(std::filesystem::exists(path));
    };
}

static auto makeInfoFunc(const VMApi& api) {
    return [api](const std::vector<Value>& rawArgs) -> Value {
        auto args = stripReceiver(api, rawArgs);
        if (args.empty()) return api.makeNull();
        std::string path = args[0].isStringValId() ? api.resolveString(args[0]) : "";
        
        auto result = api.makeObject();
        try {
            if (!std::filesystem::exists(path)) return api.makeNull();
            auto status = std::filesystem::status(path);
            api.setField(result, "path", api.makeString(path));
            api.setField(result, "name", api.makeString(std::filesystem::path(path).filename().string()));
            api.setField(result, "isDir", Value::makeBool(std::filesystem::is_directory(status)));
            api.setField(result, "size", Value::makeInt(static_cast<int>(std::filesystem::file_size(path))));
            api.setField(result, "lastWrite", Value::makeInt(static_cast<int>(std::filesystem::last_write_time(path).time_since_epoch().count())));
        } catch (const std::exception& e) {
            debug("file_automator.info error: {}", e.what());
            return api.makeNull();
        }
        return result;
    };
}

static auto makeWatchFunc(const VMApi& api) {
    return [api](const std::vector<Value>& rawArgs) -> Value {
        // Simple placeholder - real watch would need async/callback support
        return Value::makeBool(false);
    };
}

void registerFileAutomatorModule(const VMApi& api) {
    HAVEL_BEGIN_MODULE("FileAutomator");

    HAVEL_REGISTER_FUNCTION(api, "file_automator.list", makeListFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "file_automator.read", makeReadFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "file_automator.write", makeWriteFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "file_automator.copy", makeCopyFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "file_automator.move", makeMoveFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "file_automator.remove", makeRemoveFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "file_automator.exists", makeExistsFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "file_automator.info", makeInfoFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "file_automator.watch", makeWatchFunc(api));

    auto obj = api.makeObject();
    api.setGlobal("file_automator", obj);
    api.setField(obj, MODULE_MARKER, Value::makeBool(true));
    api.setField(obj, "list", api.makeFunctionRef("file_automator.list"));
    api.setField(obj, "read", api.makeFunctionRef("file_automator.read"));
    api.setField(obj, "write", api.makeFunctionRef("file_automator.write"));
    api.setField(obj, "copy", api.makeFunctionRef("file_automator.copy"));
    api.setField(obj, "move", api.makeFunctionRef("file_automator.move"));
    api.setField(obj, "remove", api.makeFunctionRef("file_automator.remove"));
    api.setField(obj, "exists", api.makeFunctionRef("file_automator.exists"));
    api.setField(obj, "info", api.makeFunctionRef("file_automator.info"));
    api.setField(obj, "watch", api.makeFunctionRef("file_automator.watch"));

    HAVEL_END_MODULE();
}

} // namespace havel::modules

#ifdef HAVEL_MODULE_PLUGIN
#include "c/ModulePlugin.h"

HAVEL_MODULE_PLUGIN_IMPL(file_automator, "1.0.0", "File automation module",
    havel::modules::registerFileAutomatorModule(*api);
)
#endif
