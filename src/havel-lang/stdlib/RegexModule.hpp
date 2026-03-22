/*
 * RegexModule.hpp - Regular expression stdlib for VM (no host/service)
 * Pure VM implementation using BytecodeValue
 */
#pragma once

#include "../compiler/bytecode/VM.hpp"
#include "../compiler/bytecode/HostBridge.hpp"

#include <regex>

namespace havel {
    class Environment;
}

namespace havel::stdlib {

// OLD: Register with Environment (for AST interpreter) - DECLARATION ONLY
void registerRegexModule(Environment& env);

// NEW: Register regex module with VM's host bridge (VM-native)
inline void registerRegexModuleVM(compiler::HostBridgeRegistry& registry) {
    auto& vm = registry.vm();
    auto& options = registry.options();
    
    // Helper: convert BytecodeValue to string
    auto valueToString = [](const compiler::BytecodeValue& v) -> std::string {
        if (std::holds_alternative<std::string>(v)) return std::get<std::string>(v);
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
        if (std::holds_alternative<bool>(v)) return std::get<bool>(v) ? "true" : "false";
        return "";
    };
    
    // regex.match(string, pattern) - Returns true if pattern matches
    options.host_functions["regex.match"] = [valueToString](const std::vector<compiler::BytecodeValue>& args) {
        if (args.size() < 2) throw std::runtime_error("regex.match() requires string and pattern");
        std::string str = valueToString(args[0]);
        std::string pattern = valueToString(args[1]);
        try {
            std::regex re(pattern);
            bool found = std::regex_search(str, re);
            return compiler::BytecodeValue(found);
        } catch (const std::regex_error& e) {
            throw std::runtime_error(std::string("Invalid regex pattern: ") + e.what());
        }
    };
    
    // regex.test(string, pattern) - Alias for match
    options.host_functions["regex.test"] = [valueToString](const std::vector<compiler::BytecodeValue>& args) {
        if (args.size() < 2) throw std::runtime_error("regex.test() requires string and pattern");
        std::string str = valueToString(args[0]);
        std::string pattern = valueToString(args[1]);
        try {
            std::regex re(pattern);
            bool found = std::regex_search(str, re);
            return compiler::BytecodeValue(found);
        } catch (const std::regex_error& e) {
            throw std::runtime_error(std::string("Invalid regex pattern: ") + e.what());
        }
    };
    
    // regex.replace(string, pattern, replacement) - Replace matches
    options.host_functions["regex.replace"] = [valueToString](const std::vector<compiler::BytecodeValue>& args) {
        if (args.size() < 3) throw std::runtime_error("regex.replace() requires string, pattern, and replacement");
        std::string str = valueToString(args[0]);
        std::string pattern = valueToString(args[1]);
        std::string replacement = valueToString(args[2]);
        try {
            std::regex re(pattern);
            std::string result = std::regex_replace(str, re, replacement);
            return compiler::BytecodeValue(result);
        } catch (const std::regex_error& e) {
            throw std::runtime_error(std::string("Invalid regex pattern: ") + e.what());
        }
    };
    
    // regex.split(string, pattern) - Split string by pattern
    options.host_functions["regex.split"] = [valueToString, &vm](const std::vector<compiler::BytecodeValue>& args) {
        if (args.size() < 2) throw std::runtime_error("regex.split() requires string and pattern");
        std::string str = valueToString(args[0]);
        std::string pattern = valueToString(args[1]);
        auto arr = vm.createHostArray();
        try {
            std::regex re(pattern);
            std::sregex_token_iterator it(str.begin(), str.end(), re, -1);
            std::sregex_token_iterator end;
            while (it != end) {
                vm.pushHostArrayValue(arr, compiler::BytecodeValue(*it++));
            }
        } catch (const std::regex_error& e) {
            throw std::runtime_error(std::string("Invalid regex pattern: ") + e.what());
        }
        return compiler::BytecodeValue(arr);
    };
    
    // Register regex object via vm_setup
    registry.addVmSetup([](compiler::VM& vm) {
        auto regexObj = vm.createHostObject();
        vm.setHostObjectField(regexObj, "match", compiler::HostFunctionRef{.name = "regex.match"});
        vm.setHostObjectField(regexObj, "test", compiler::HostFunctionRef{.name = "regex.test"});
        vm.setHostObjectField(regexObj, "replace", compiler::HostFunctionRef{.name = "regex.replace"});
        vm.setHostObjectField(regexObj, "split", compiler::HostFunctionRef{.name = "regex.split"});
        vm.setGlobal("regex", regexObj);
    });
}

// Implementation of old registerRegexModule (placeholder)
inline void registerRegexModule(Environment& env) {
    (void)env;
}

} // namespace havel::stdlib
