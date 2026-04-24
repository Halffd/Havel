/*
 * LogModule.cpp - VM-native logging stdlib module
 */
#include "LogModule.hpp"
#include "../../utils/Logger.hpp"
#include <sstream>
#include <chrono>
#include <iomanip>
#include <iostream>

using havel::compiler::Value;
using havel::compiler::VMApi;

namespace havel::stdlib {

static std::string valueToString(const Value &v, VMApi &api) {
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

static std::string formatMessage(const std::vector<Value> &args, VMApi &api) {
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

void registerLogModule(VMApi &api) {
    auto &logger = havel::Logger::getInstance();

    api.registerFunction("log.info", [&api](const std::vector<Value> &args) -> Value {
        std::string msg = formatMessage(args, api);
        ::havel::info("{}", msg);
        return Value::makeNull();
    });

    api.registerFunction("log.error", [&api](const std::vector<Value> &args) -> Value {
        std::string msg = formatMessage(args, api);
        ::havel::error("{}", msg);
        return Value::makeNull();
    });

    api.registerFunction("log.warn", [&api](const std::vector<Value> &args) -> Value {
        std::string msg = formatMessage(args, api);
        havel::warn("{}", msg);
        return Value::makeNull();
    });

    api.registerFunction("log.debug", [&api](const std::vector<Value> &args) -> Value {
        std::string msg = formatMessage(args, api);
        ::havel::debug("{}", msg);
        return Value::makeNull();
    });

    api.registerFunction("log.critical", [&api](const std::vector<Value> &args) -> Value {
        std::string msg = formatMessage(args, api);
        havel::critical("{}", msg);
        return Value::makeNull();
    });

    api.registerFunction("log.get", [&api, &logger](const std::vector<Value> &) -> Value {
        return api.makeString(logger.getLogFilePath());
    });

    api.registerFunction("log.set", [&logger, &api](const std::vector<Value> &args) -> Value {
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

    api.registerFunction("log.history", [&logger, &api](const std::vector<Value> &) -> Value {
        auto history = logger.getHistory();
        auto arrId = api.makeArray();
        for (const auto &entry : history) {
            api.push(arrId, api.makeString(entry));
        }
        return arrId;
    });

    api.registerFunction("log.log", [&api](const std::vector<Value> &args) -> Value {
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

} // namespace havel::stdlib
