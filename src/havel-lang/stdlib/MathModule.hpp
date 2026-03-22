/*
 * MathModule.hpp - Math stdlib for VM (no host/service)
 * Pure VM implementation using BytecodeValue
 */
#pragma once

#include "../compiler/bytecode/VM.hpp"
#include "../compiler/bytecode/HostBridge.hpp"

#include <cmath>  // for std::ceil, std::floor, std::round, std::sin, etc.

namespace havel {
    class Environment;
}


namespace havel::stdlib {

// OLD: Register with Environment (for AST interpreter) - DECLARATION ONLY
void registerMathModule(Environment& env);

// NEW: Register math module with VM's host bridge (VM-native)
inline void registerMathModuleVM(compiler::HostBridge& bridge) {
    auto& vm = bridge.context().vm;
    auto& options = bridge.options();
    
    // Helper: convert BytecodeValue to double
    auto toNum = [](const compiler::BytecodeValue& v) -> double {
        if (std::holds_alternative<int64_t>(v)) return static_cast<double>(std::get<int64_t>(v));
        if (std::holds_alternative<double>(v)) return std::get<double>(v);
        return 0.0;
    };
    
    // Register math functions directly
    options.host_functions["math.abs"] = [toNum](const std::vector<compiler::BytecodeValue>& args) {
        if (args.size() != 1) throw std::runtime_error("math.abs() requires 1 argument");
        return compiler::BytecodeValue(std::abs(toNum(args[0])));
    };
    
    options.host_functions["math.ceil"] = [toNum](const std::vector<compiler::BytecodeValue>& args) {
        if (args.size() != 1) throw std::runtime_error("math.ceil() requires 1 argument");
        return compiler::BytecodeValue(std::ceil(toNum(args[0])));
    };
    
    options.host_functions["math.floor"] = [toNum](const std::vector<compiler::BytecodeValue>& args) {
        if (args.size() != 1) throw std::runtime_error("math.floor() requires 1 argument");
        return compiler::BytecodeValue(std::floor(toNum(args[0])));
    };
    
    options.host_functions["math.round"] = [toNum](const std::vector<compiler::BytecodeValue>& args) {
        if (args.size() != 1) throw std::runtime_error("math.round() requires 1 argument");
        return compiler::BytecodeValue(std::round(toNum(args[0])));
    };
    
    options.host_functions["math.sin"] = [toNum](const std::vector<compiler::BytecodeValue>& args) {
        if (args.size() != 1) throw std::runtime_error("math.sin() requires 1 argument");
        return compiler::BytecodeValue(std::sin(toNum(args[0])));
    };
    
    options.host_functions["math.cos"] = [toNum](const std::vector<compiler::BytecodeValue>& args) {
        if (args.size() != 1) throw std::runtime_error("math.cos() requires 1 argument");
        return compiler::BytecodeValue(std::cos(toNum(args[0])));
    };
    
    options.host_functions["math.tan"] = [toNum](const std::vector<compiler::BytecodeValue>& args) {
        if (args.size() != 1) throw std::runtime_error("math.tan() requires 1 argument");
        return compiler::BytecodeValue(std::tan(toNum(args[0])));
    };
    
    options.host_functions["math.sqrt"] = [toNum](const std::vector<compiler::BytecodeValue>& args) {
        if (args.size() != 1) throw std::runtime_error("math.sqrt() requires 1 argument");
        double val = toNum(args[0]);
        if (val < 0) throw std::runtime_error("math.sqrt() argument must be non-negative");
        return compiler::BytecodeValue(std::sqrt(val));
    };
    
    options.host_functions["math.log"] = [toNum](const std::vector<compiler::BytecodeValue>& args) {
        if (args.size() != 1) throw std::runtime_error("math.log() requires 1 argument");
        double val = toNum(args[0]);
        if (val <= 0) throw std::runtime_error("math.log() argument must be positive");
        return compiler::BytecodeValue(std::log(val));
    };
    
    options.host_functions["math.exp"] = [toNum](const std::vector<compiler::BytecodeValue>& args) {
        if (args.size() != 1) throw std::runtime_error("math.exp() requires 1 argument");
        return compiler::BytecodeValue(std::exp(toNum(args[0])));
    };
    
    options.host_functions["math.pow"] = [toNum](const std::vector<compiler::BytecodeValue>& args) {
        if (args.size() != 2) throw std::runtime_error("math.pow() requires 2 arguments");
        return compiler::BytecodeValue(std::pow(toNum(args[0]), toNum(args[1])));
    };
    
    options.host_functions["math.min"] = [toNum](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("math.min() requires at least 1 argument");
        double min = toNum(args[0]);
        for (size_t i = 1; i < args.size(); ++i) {
            double val = toNum(args[i]);
            if (val < min) min = val;
        }
        return compiler::BytecodeValue(min);
    };
    
    options.host_functions["math.max"] = [toNum](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("math.max() requires at least 1 argument");
        double max = toNum(args[0]);
        for (size_t i = 1; i < args.size(); ++i) {
            double val = toNum(args[i]);
            if (val > max) max = val;
        }
        return compiler::BytecodeValue(max);
    };
    
    options.host_functions["math.random"] = [](const std::vector<compiler::BytecodeValue>&) {
        return compiler::BytecodeValue(static_cast<double>(std::rand()) / RAND_MAX);
    };
    
    // Constants
    vm.setGlobal("PI", compiler::BytecodeValue(3.14159265358979323846));
    vm.setGlobal("E", compiler::BytecodeValue(2.71828182845904523536));
    
    // Register math object via vm_setup
    bridge.addVmSetup([](compiler::VM& vm) {
        auto mathObj = vm.createHostObject();
        vm.setHostObjectField(mathObj, "abs", compiler::HostFunctionRef{.name = "math.abs"});
        vm.setHostObjectField(mathObj, "ceil", compiler::HostFunctionRef{.name = "math.ceil"});
        vm.setHostObjectField(mathObj, "floor", compiler::HostFunctionRef{.name = "math.floor"});
        vm.setHostObjectField(mathObj, "round", compiler::HostFunctionRef{.name = "math.round"});
        vm.setHostObjectField(mathObj, "sin", compiler::HostFunctionRef{.name = "math.sin"});
        vm.setHostObjectField(mathObj, "cos", compiler::HostFunctionRef{.name = "math.cos"});
        vm.setHostObjectField(mathObj, "tan", compiler::HostFunctionRef{.name = "math.tan"});
        vm.setHostObjectField(mathObj, "sqrt", compiler::HostFunctionRef{.name = "math.sqrt"});
        vm.setHostObjectField(mathObj, "log", compiler::HostFunctionRef{.name = "math.log"});
        vm.setHostObjectField(mathObj, "exp", compiler::HostFunctionRef{.name = "math.exp"});
        vm.setHostObjectField(mathObj, "pow", compiler::HostFunctionRef{.name = "math.pow"});
        vm.setHostObjectField(mathObj, "min", compiler::HostFunctionRef{.name = "math.min"});
        vm.setHostObjectField(mathObj, "max", compiler::HostFunctionRef{.name = "math.max"});
        vm.setHostObjectField(mathObj, "random", compiler::HostFunctionRef{.name = "math.random"});
        vm.setGlobal("math", mathObj);
    });
}

// Implementation of old registerMathModule (placeholder)
inline void registerMathModule(Environment& env) {
    (void)env;
}

} // namespace havel::stdlib
