/*
 * TypeModule.hpp - Type conversion stdlib for VM (no host/service)
 * Pure VM implementation using BytecodeValue
 */
#pragma once

#include "../compiler/bytecode/VM.hpp"
#include "../compiler/bytecode/HostBridge.hpp"

namespace havel {
    class Environment;
}


#include <algorithm>
#include <cmath>
#include <sstream>

namespace havel::stdlib {

// OLD: Register with Environment (for AST interpreter) - DECLARATION ONLY
void registerTypeModule(Environment& env);

// NEW: Register type module with VM's host bridge (VM-native)
inline void registerTypeModuleVM(compiler::HostBridgeRegistry& registry) {
    auto& vm = registry.vm();
    auto& options = registry.options();
    
    // Helper: convert BytecodeValue to number
    auto toNumber = [](const compiler::BytecodeValue& v) -> double {
        if (std::holds_alternative<int64_t>(v)) return static_cast<double>(std::get<int64_t>(v));
        if (std::holds_alternative<double>(v)) return std::get<double>(v);
        if (std::holds_alternative<std::string>(v)) {
            try { return std::stod(std::get<std::string>(v)); } catch (...) {}
        }
        if (std::holds_alternative<bool>(v)) return std::get<bool>(v) ? 1.0 : 0.0;
        return 0.0;
    };
    
    // Helper: convert BytecodeValue to string
    auto toString = [&toNumber](const compiler::BytecodeValue& v) -> std::string {
        if (std::holds_alternative<std::string>(v)) return std::get<std::string>(v);
        if (std::holds_alternative<int64_t>(v)) {
            long long val = std::get<int64_t>(v);
            if (val == std::floor(val) && std::abs(val) < 1e15) {
                return std::to_string(val);
            }
        }
        if (std::holds_alternative<double>(v)) {
            double val = toNumber(v);
            std::ostringstream oss;
            oss.precision(15);
            oss << val;
            return oss.str();
        }
        if (std::holds_alternative<bool>(v)) return std::get<bool>(v) ? "true" : "false";
        return "";
    };
    
    // int(x) - Convert to integer
    options.host_functions["int"] = [toNumber](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("int() requires an argument");
        return compiler::BytecodeValue(static_cast<int64_t>(toNumber(args[0])));
    };
    
    // float(x) - Convert to float
    options.host_functions["float"] = [toNumber](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("float() requires an argument");
        return compiler::BytecodeValue(toNumber(args[0]));
    };
    
    // str(x) - Convert to string
    options.host_functions["str"] = [toString](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("str() requires an argument");
        return compiler::BytecodeValue(toString(args[0]));
    };
    
    // bool(x) - Convert to boolean
    options.host_functions["bool"] = [](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("bool() requires an argument");
        const auto& v = args[0];
        bool result = false;
        if (std::holds_alternative<bool>(v)) result = std::get<bool>(v);
        else if (std::holds_alternative<int64_t>(v)) result = std::get<int64_t>(v) != 0;
        else if (std::holds_alternative<double>(v)) result = std::get<double>(v) != 0.0;
        else if (std::holds_alternative<std::string>(v)) result = !std::get<std::string>(v).empty();
        return compiler::BytecodeValue(result);
    };
    
    // type(x) - Get type name
    options.host_functions["type"] = [](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("type() requires an argument");
        const auto& v = args[0];
        std::string typeName = "unknown";
        if (std::holds_alternative<std::nullptr_t>(v)) typeName = "null";
        else if (std::holds_alternative<bool>(v)) typeName = "bool";
        else if (std::holds_alternative<int64_t>(v)) typeName = "int";
        else if (std::holds_alternative<double>(v)) typeName = "float";
        else if (std::holds_alternative<std::string>(v)) typeName = "string";
        else if (std::holds_alternative<compiler::ArrayRef>(v)) typeName = "array";
        else if (std::holds_alternative<compiler::ObjectRef>(v)) typeName = "object";
        return compiler::BytecodeValue(typeName);
    };
    
    // len(x) - Get length
    options.host_functions["len"] = [&vm](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("len() requires an argument");
        const auto& v = args[0];
        if (std::holds_alternative<std::string>(v)) {
            return compiler::BytecodeValue(static_cast<int64_t>(std::get<std::string>(v).length()));
        }
        if (std::holds_alternative<compiler::ArrayRef>(v)) {
            // Would need VM access to get array length
            return compiler::BytecodeValue(static_cast<int64_t>(0));
        }
        throw std::runtime_error("len() requires string or array");
    };
    
    // abs(x) - Absolute value
    options.host_functions["abs"] = [toNumber](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("abs() requires an argument");
        return compiler::BytecodeValue(std::abs(toNumber(args[0])));
    };
    
    // min(...) - Minimum value
    options.host_functions["min"] = [toNumber](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("min() requires at least 1 argument");
        double min = toNumber(args[0]);
        for (size_t i = 1; i < args.size(); ++i) {
            double val = toNumber(args[i]);
            if (val < min) min = val;
        }
        return compiler::BytecodeValue(min);
    };
    
    // max(...) - Maximum value
    options.host_functions["max"] = [toNumber](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("max() requires at least 1 argument");
        double max = toNumber(args[0]);
        for (size_t i = 1; i < args.size(); ++i) {
            double val = toNumber(args[i]);
            if (val > max) max = val;
        }
        return compiler::BytecodeValue(max);
    };
    
    // round(x) - Round to nearest integer
    options.host_functions["round"] = [toNumber](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("round() requires an argument");
        return compiler::BytecodeValue(static_cast<int64_t>(std::round(toNumber(args[0]))));
    };
    
    // ceil(x) - Round up
    options.host_functions["ceil"] = [toNumber](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("ceil() requires an argument");
        return compiler::BytecodeValue(static_cast<int64_t>(std::ceil(toNumber(args[0]))));
    };
    
    // floor(x) - Round down
    options.host_functions["floor"] = [toNumber](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("floor() requires an argument");
        return compiler::BytecodeValue(static_cast<int64_t>(std::floor(toNumber(args[0]))));
    };
    
    // Register type functions via vm_setup
    registry.addVmSetup([](compiler::VM& vm) {
        // Type conversion functions are global
        // No type object needed
    });
}

// Implementation of old registerTypeModule (placeholder)
inline void registerTypeModule(Environment& env) {
    (void)env;
}

} // namespace havel::stdlib
