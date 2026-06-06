/*
 * LogModule.cpp - VM-native logging stdlib module
 */
#include "LogModule.hpp"

#ifdef HAVEL_CORE_PROFILE
namespace havel::stdlib {

void registerLogModule(const VMApi &) {}
void registerDebugModule(const VMApi &) {}

} // namespace havel::stdlib
#else
#include "../../utils/Logger.hpp"
#include "core/config/ConfigManager.hpp"
#include <fstream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <unordered_map>

using havel::compiler::Value;
using havel::compiler::VMApi;

namespace havel::stdlib {

static std::string valueToString(const Value &v, const VMApi &api) {
    if (v.isNull()) return "null";
    if (v.isBool()) return v.asBool() ? "true" : "false";
    if (v.isInt()) return std::to_string(v.asInt());
    if (v.isDouble()) {
        std::ostringstream ss;
        ss << v.asDouble();
        return ss.str();
    }
    if (v.isStringValId() || v.isStringId()) {
        return api.toString(v);
    }
    if (v.isArrayId()) {
        return "[array]";
    }
    if (v.isObjectId()) {
        return "[object]";
    }
    if (v.isFunctionObjId()) {
        return "[function]";
    }
    return "?";
}

static std::string formatMessage(const std::vector<Value> &args, const VMApi &api) {
    std::ostringstream ss;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) ss << " ";
        ss << valueToString(args[i], api);
    }
    return ss.str();
}

static std::string getTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream ss;
    ss << std::put_time(std::localtime(&now_time_t), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

void registerLogModule(const VMApi &api) {
    auto &logger = havel::Logger::getInstance();

    api.registerFunction("log.info", [api](const std::vector<Value> &args) -> Value {
        std::string msg = formatMessage(args, api);
        ::havel::info("{}", msg);
        return Value::makeNull();
    });

    api.registerFunction("log.error", [api](const std::vector<Value> &args) -> Value {
        std::string msg = formatMessage(args, api);
        ::havel::error("{}", msg);
        return Value::makeNull();
    });

    api.registerFunction("log.warn", [api](const std::vector<Value> &args) -> Value {
        std::string msg = formatMessage(args, api);
        havel::warn("{}", msg);
        return Value::makeNull();
    });

    api.registerFunction("log.debug", [api](const std::vector<Value> &args) -> Value {
        std::string msg = formatMessage(args, api);
        ::havel::debug("{}", msg);
        return Value::makeNull();
    });

    api.registerFunction("log.critical", [api](const std::vector<Value> &args) -> Value {
        std::string msg = formatMessage(args, api);
        havel::critical("{}", msg);
        return Value::makeNull();
    });

    api.registerFunction("log.get", [api, &logger](const std::vector<Value> &) -> Value {
        return api.makeString(logger.getLogFilePath());
    });

    api.registerFunction("log.set", [&logger, api](const std::vector<Value> &args) -> Value {
        if (args.empty()) {
            throw std::runtime_error("log.set() requires a file path");
        }
        std::string path;
        if (args[0].isStringValId() || args[0].isStringId()) {
            path = api.toString(args[0]);
        } else if (args[0].isInt()) {
            int mode = args[0].asInt();
            if (mode == 0) {
                path = "stdout";
            } else if (mode == 1) {
                path = "stderr";
            } else {
                throw std::runtime_error("log.set(): use file path string, or 0 for stdout, 1 for stderr");
            }
        } else {
            throw std::runtime_error("log.set() requires a file path string or mode (0=stdout, 1=stderr)");
        }
        logger.setLogFile(path);
        return Value::makeNull();
    });

    api.registerFunction("log.history", [&logger, api](const std::vector<Value> &) -> Value {
        auto history = logger.getHistory();
        auto arrId = api.makeArray();
        for (const auto &entry : history) {
            api.push(arrId, api.makeString(entry));
        }
        return arrId;
    });

    api.registerFunction("log.log", [api](const std::vector<Value> &args) -> Value {
        if (args.empty()) {
            throw std::runtime_error("log.log() requires at least a target");
        }

        std::string target = "stdout";
        std::vector<Value> msgArgs;

        if (args[0].isStringValId() || args[0].isStringId()) {
            target = api.toString(args[0]);
            msgArgs = std::vector<Value>(args.begin() + 1, args.end());
        } else {
            msgArgs = args;
        }

        std::string msg = formatMessage(msgArgs, api);
        std::string timestamp = getTimestamp();
        std::string fullMsg = "[" + timestamp + "] " + msg + "\n";

        if (target == "stdout" || target == "") {
            std::cout << fullMsg;
            std::cout.flush();
        } else if (target == "stderr") {
            std::cerr << fullMsg;
            std::cerr.flush();
        } else {
            std::ofstream file(target, std::ios::app);
            if (!file) {
                throw std::runtime_error("log.log(): cannot open file: " + target);
            }
            file << fullMsg;
        }

        return Value::makeNull();
    });

    auto logObj = api.makeObject();
    api.setField(logObj, "info", api.makeFunctionRef("log.info"));
    api.setField(logObj, "error", api.makeFunctionRef("log.error"));
    api.setField(logObj, "warn", api.makeFunctionRef("log.warn"));
    api.setField(logObj, "debug", api.makeFunctionRef("log.debug"));
    api.setField(logObj, "critical", api.makeFunctionRef("log.critical"));
    api.setField(logObj, "get", api.makeFunctionRef("log.get"));
    api.setField(logObj, "set", api.makeFunctionRef("log.set"));
    api.setField(logObj, "history", api.makeFunctionRef("log.history"));
    api.setField(logObj, "log", api.makeFunctionRef("log.log"));
    api.setGlobal("log", logObj);
}

static std::unordered_map<std::string, bool> &debugFlags() {
static std::unordered_map<std::string, bool> flags = {
{"lexer", false}, {"parser", false}, {"ast", false},
{"bytecode", false}, {"gc", false}, {"scope", false},
{"emitter", false}, {"types", false}, {"all", false},
{"color", true}, {"timing", false}, {"verbose", false},
};
return flags;
}

void registerDebugModule(const VMApi &api) {
api.registerFunction("debug.toggleVerboseConditionLogging", [](const std::vector<Value> &) -> Value {
bool current = Configs::Get().Get<bool>("Debug.VerboseConditionLogging", false);
Configs::Get().Set("Debug.VerboseConditionLogging", !current, true);
return Value::makeBool(!current);
});

api.registerFunction("debug.toggleVerboseKeyLogging", [](const std::vector<Value> &) -> Value {
bool current = Configs::Get().Get<bool>("Debug.VerboseKeyLogging", false);
Configs::Get().Set("Debug.VerboseKeyLogging", !current, true);
return Value::makeBool(!current);
});

api.registerFunction("debug.isOn", [api](const std::vector<Value> &args) -> Value {
if (args.empty()) return Value::makeBool(false);
std::string name = api.toString(args[0]);
auto &f = debugFlags();
if (f.count("all") && f["all"]) return Value::makeBool(true);
auto it = f.find(name);
if (it != f.end()) return Value::makeBool(it->second);
return Value::makeBool(false);
});

api.registerFunction("debug.setFlag", [api](const std::vector<Value> &args) -> Value {
if (args.size() < 2) return Value::makeNull();
std::string name = api.toString(args[0]);
bool val = args[1].isBool() ? args[1].asBool() : (args[1].isInt() && args[1].asInt() != 0);
auto &f = debugFlags();
f[name] = val;
if (name == "all" && val) {
f["lexer"] = f["parser"] = f["ast"] = f["bytecode"] = true;
f["scope"] = f["emitter"] = f["types"] = true;
}
return Value::makeNull();
});

api.registerFunction("debug.parseDebugArgs", [](const std::vector<Value> &args) -> Value {
auto &f = debugFlags();
if (!args.empty() && args[0].isArrayId()) {
// iterate not available here, just set all from known flags
}
f["all"] = false;
return Value::makeNull();
});

api.registerFunction("debug.trace", [api](const std::vector<Value> &args) -> Value {
std::string stage = (!args.empty() && (args[0].isStringValId() || args[0].isStringId())) ? api.toString(args[0]) : "?";
std::string msg = (args.size() > 1 && (args[1].isStringValId() || args[1].isStringId())) ? api.toString(args[1]) : "";
std::cerr << "[" << stage << "] " << msg << "\n";
return Value::makeNull();
});

api.registerFunction("debug.traceEmitter", [api](const std::vector<Value> &args) -> Value {
auto &f = debugFlags();
if (!f.count("emitter") || !f["emitter"]) return Value::makeNull();
if (!f.count("all") || !f["all"]) {
if (!f["emitter"]) return Value::makeNull();
}
std::string msg = (!args.empty() && (args[0].isStringValId() || args[0].isStringId())) ? api.toString(args[0]) : "";
std::cerr << "[EMIT] " << msg << "\n";
return Value::makeNull();
});

api.registerFunction("debug.traceBytecode", [api](const std::vector<Value> &args) -> Value {
auto &f = debugFlags();
if (!f["bytecode"] && !f["all"]) return Value::makeNull();
std::string msg = (!args.empty() && (args[0].isStringValId() || args[0].isStringId())) ? api.toString(args[0]) : "";
std::cerr << "[BC] " << msg << "\n";
return Value::makeNull();
});

api.registerFunction("debug.traceLexer", [api](const std::vector<Value> &args) -> Value {
auto &f = debugFlags();
if (!f["lexer"] && !f["all"]) return Value::makeNull();
std::string msg = (!args.empty() && (args[0].isStringValId() || args[0].isStringId())) ? api.toString(args[0]) : "";
std::cerr << "[LEXER] " << msg << "\n";
return Value::makeNull();
});

api.registerFunction("debug.traceParser", [api](const std::vector<Value> &args) -> Value {
auto &f = debugFlags();
if (!f["parser"] && !f["all"]) return Value::makeNull();
std::string msg = (!args.empty() && (args[0].isStringValId() || args[0].isStringId())) ? api.toString(args[0]) : "";
std::cerr << "[PARSER] " << msg << "\n";
return Value::makeNull();
});

api.registerFunction("debug.traceAst", [api](const std::vector<Value> &args) -> Value {
auto &f = debugFlags();
if (!f["ast"] && !f["all"]) return Value::makeNull();
std::string msg = (!args.empty() && (args[0].isStringValId() || args[0].isStringId())) ? api.toString(args[0]) : "";
std::cerr << "[AST] " << msg << "\n";
return Value::makeNull();
});

api.registerFunction("debug.traceScope", [api](const std::vector<Value> &args) -> Value {
auto &f = debugFlags();
if (!f["scope"] && !f["all"]) return Value::makeNull();
std::string msg = (!args.empty() && (args[0].isStringValId() || args[0].isStringId())) ? api.toString(args[0]) : "";
std::cerr << "[SCOPE] " << msg << "\n";
return Value::makeNull();
});

api.registerFunction("debug.traceTypes", [api](const std::vector<Value> &args) -> Value {
auto &f = debugFlags();
if (!f["types"] && !f["all"]) return Value::makeNull();
std::string msg = (!args.empty() && (args[0].isStringValId() || args[0].isStringId())) ? api.toString(args[0]) : "";
std::cerr << "[TYPES] " << msg << "\n";
return Value::makeNull();
});

api.registerFunction("debug.traceGc", [api](const std::vector<Value> &args) -> Value {
auto &f = debugFlags();
if (!f["gc"] && !f["all"]) return Value::makeNull();
std::string msg = (!args.empty() && (args[0].isStringValId() || args[0].isStringId())) ? api.toString(args[0]) : "";
std::cerr << "[GC] " << msg << "\n";
return Value::makeNull();
});

api.registerFunction("debug.dumpAst", [](const std::vector<Value> &) -> Value {
return Value::makeNull();
});

api.registerFunction("debug.dumpBytecode", [](const std::vector<Value> &) -> Value {
return Value::makeNull();
});

api.registerFunction("debug.startTimer", [](const std::vector<Value> &) -> Value {
return Value::makeNull();
});

api.registerFunction("debug.endTimer", [](const std::vector<Value> &) -> Value {
return Value::makeNull();
});

api.registerFunction("debug.colorize", [](const std::vector<Value> &args) -> Value {
if (args.empty()) return Value::makeNull();
return args[0];
});

auto debugObj = api.makeObject();
api.setField(debugObj, "toggleVerboseConditionLogging", api.makeFunctionRef("debug.toggleVerboseConditionLogging"));
api.setField(debugObj, "toggleVerboseKeyLogging", api.makeFunctionRef("debug.toggleVerboseKeyLogging"));
api.setField(debugObj, "isOn", api.makeFunctionRef("debug.isOn"));
api.setField(debugObj, "setFlag", api.makeFunctionRef("debug.setFlag"));
api.setField(debugObj, "parseDebugArgs", api.makeFunctionRef("debug.parseDebugArgs"));
api.setField(debugObj, "trace", api.makeFunctionRef("debug.trace"));
api.setField(debugObj, "traceEmitter", api.makeFunctionRef("debug.traceEmitter"));
api.setField(debugObj, "traceBytecode", api.makeFunctionRef("debug.traceBytecode"));
api.setField(debugObj, "traceLexer", api.makeFunctionRef("debug.traceLexer"));
api.setField(debugObj, "traceParser", api.makeFunctionRef("debug.traceParser"));
api.setField(debugObj, "traceAst", api.makeFunctionRef("debug.traceAst"));
api.setField(debugObj, "traceScope", api.makeFunctionRef("debug.traceScope"));
api.setField(debugObj, "traceTypes", api.makeFunctionRef("debug.traceTypes"));
api.setField(debugObj, "traceGc", api.makeFunctionRef("debug.traceGc"));
api.setField(debugObj, "dumpAst", api.makeFunctionRef("debug.dumpAst"));
api.setField(debugObj, "dumpBytecode", api.makeFunctionRef("debug.dumpBytecode"));
api.setField(debugObj, "startTimer", api.makeFunctionRef("debug.startTimer"));
api.setField(debugObj, "endTimer", api.makeFunctionRef("debug.endTimer"));
api.setField(debugObj, "colorize", api.makeFunctionRef("debug.colorize"));
api.setGlobal("debug", debugObj);
}

} // namespace havel::stdlib
#endif // HAVEL_CORE_PROFILE

#ifdef HAVEL_MODULE_PLUGIN
#include "c/ModulePlugin.h"
HAVEL_MODULE_PLUGIN_IMPL_A1(log, "1.0.0", "Logging stdlib module", "debug",
havel::stdlib::registerLogModule(*api);
havel::stdlib::registerDebugModule(*api);
)
#endif
