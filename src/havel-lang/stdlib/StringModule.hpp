/*
 * StringModule.hpp - String stdlib for VM with method chaining support
 * Pure VM implementation using BytecodeValue
 */
#pragma once

#include "../compiler/bytecode/VM.hpp"
#include "../compiler/bytecode/HostBridge.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace havel {
    class Environment;
}

namespace havel::stdlib {

// OLD: Register with Environment (for AST interpreter) - DECLARATION ONLY
void registerStringModule(Environment& env);

// NEW: Register string module with VM's host bridge (VM-native)
inline void registerStringModuleVM(compiler::HostBridgeRegistry& registry) {
    auto& vm = registry.vm();
    
    // Helper: convert BytecodeValue to string
    auto toString = [](const compiler::BytecodeValue& v) -> std::string {
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
    
    // Helper: convert string to lowercase
    auto toLower = [](const std::string& s) -> std::string {
        std::string result = s;
        std::transform(result.begin(), result.end(), result.begin(),
                      [](unsigned char c) { return std::tolower(c); });
        return result;
    };
    
    // Helper: convert string to uppercase
    auto toUpper = [](const std::string& s) -> std::string {
        std::string result = s;
        std::transform(result.begin(), result.end(), result.begin(),
                      [](unsigned char c) { return std::toupper(c); });
        return result;
    };
    
    // Helper: trim whitespace
    auto trim = [](const std::string& s) -> std::string {
        size_t start = s.find_first_not_of(" \t\n\r");
        if (start == std::string::npos) return "";
        size_t end = s.find_last_not_of(" \t\n\r");
        return s.substr(start, end - start + 1);
    };
    
    // string.len(s) - Get string length
    registry.options().host_functions["string.len"] = [toString](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("string.len() requires 1 argument");
        std::string s = toString(args[0]);
        return compiler::BytecodeValue(static_cast<int64_t>(s.length()));
    };
    
    // string.lower(s) - Convert to lowercase (returns string for chaining)
    registry.options().host_functions["string.lower"] = [toLower, toString](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("string.lower() requires 1 argument");
        return compiler::BytecodeValue(toLower(toString(args[0])));
    };
    
    // string.upper(s) - Convert to uppercase (returns string for chaining)
    registry.options().host_functions["string.upper"] = [toUpper, toString](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("string.upper() requires 1 argument");
        return compiler::BytecodeValue(toUpper(toString(args[0])));
    };
    
    // string.trim(s) - Trim whitespace (returns string for chaining)
    registry.options().host_functions["string.trim"] = [trim, toString](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("string.trim() requires 1 argument");
        return compiler::BytecodeValue(trim(toString(args[0])));
    };
    
    // string.sub(s, start, end) - Substring (returns string for chaining)
    registry.options().host_functions["string.sub"] = [toString](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("string.sub() requires at least 1 argument");
        std::string s = toString(args[0]);
        int64_t start = args.size() > 1 && std::holds_alternative<int64_t>(args[1]) ? std::get<int64_t>(args[1]) : 0;
        int64_t end = args.size() > 2 && std::holds_alternative<int64_t>(args[2]) ? std::get<int64_t>(args[2]) : static_cast<int64_t>(s.length());

        if (start < 0) start = std::max(static_cast<int64_t>(0), static_cast<int64_t>(s.length()) + start);
        if (end < 0) end = static_cast<int64_t>(s.length()) + end;
        end = std::min(end, static_cast<int64_t>(s.length()));

        if (start >= end) return compiler::BytecodeValue(std::string(""));
        return compiler::BytecodeValue(s.substr(start, end - start));
    };
    
    // string.find(s, substr) - Find substring
    registry.options().host_functions["string.find"] = [toString](const std::vector<compiler::BytecodeValue>& args) {
        if (args.size() != 2) throw std::runtime_error("string.find() requires 2 arguments");
        std::string s = toString(args[0]);
        std::string substr = toString(args[1]);
        size_t pos = s.find(substr);
        return compiler::BytecodeValue(pos != std::string::npos ? static_cast<int64_t>(pos) : static_cast<int64_t>(-1));
    };
    
    // string.replace(s, old, new) - Replace substring (returns string for chaining)
    registry.options().host_functions["string.replace"] = [toString](const std::vector<compiler::BytecodeValue>& args) {
        if (args.size() != 3) throw std::runtime_error("string.replace() requires 3 arguments");
        std::string s = toString(args[0]);
        std::string oldStr = toString(args[1]);
        std::string newStr = toString(args[2]);

        size_t pos = s.find(oldStr);
        if (pos == std::string::npos) return compiler::BytecodeValue(s);

        std::string result = s;
        result.replace(pos, oldStr.length(), newStr);
        return compiler::BytecodeValue(result);
    };
    
    // string.split(s, delimiter) - Split string
    registry.options().host_functions["string.split"] = [toString, &vm](const std::vector<compiler::BytecodeValue>& args) {
        if (args.size() < 2) throw std::runtime_error("string.split() requires at least 2 arguments");
        std::string s = toString(args[0]);
        std::string delimiter = toString(args[1]);

        auto arr = vm.createHostArray();
        size_t pos = 0;
        size_t found;

        while ((found = s.find(delimiter, pos)) != std::string::npos) {
            vm.pushHostArrayValue(arr, compiler::BytecodeValue(s.substr(pos, found - pos)));
            pos = found + delimiter.length();
        }
        vm.pushHostArrayValue(arr, compiler::BytecodeValue(s.substr(pos)));

        return compiler::BytecodeValue(arr);
    };
    
    // string.join(arr, delimiter) - Join array
    registry.options().host_functions["string.join"] = [toString, &vm](const std::vector<compiler::BytecodeValue>& args) {
        if (args.size() < 2) throw std::runtime_error("string.join() requires at least 2 arguments");
        if (!std::holds_alternative<compiler::ArrayRef>(args[0])) {
            throw std::runtime_error("string.join() first argument must be array");
        }
        
        std::string delimiter = toString(args[1]);
        const auto& arr = std::get<compiler::ArrayRef>(args[0]);
        size_t len = vm.getHostArrayLength(arr);
        
        std::string result;
        for (size_t i = 0; i < len; ++i) {
            if (i > 0) result += delimiter;
            auto value = vm.getHostArrayValue(arr, i);
            result += toString(value);
        }
        return compiler::BytecodeValue(result);
    };
    
    // string.startswith(s, prefix) - Check if starts with
    registry.options().host_functions["string.startswith"] = [toString](const std::vector<compiler::BytecodeValue>& args) {
        if (args.size() != 2) throw std::runtime_error("string.startswith() requires 2 arguments");
        std::string s = toString(args[0]);
        std::string prefix = toString(args[1]);
        return compiler::BytecodeValue(s.rfind(prefix, 0) == 0);
    };
    
    // string.endswith(s, suffix) - Check if ends with
    registry.options().host_functions["string.endswith"] = [toString](const std::vector<compiler::BytecodeValue>& args) {
        if (args.size() != 2) throw std::runtime_error("string.endswith() requires 2 arguments");
        std::string s = toString(args[0]);
        std::string suffix = toString(args[1]);

        if (suffix.length() > s.length()) return compiler::BytecodeValue(false);
        return compiler::BytecodeValue(s.compare(s.length() - suffix.length(), suffix.length(), suffix) == 0);
    };
    
    // string.includes(s, substr) - Check if contains (returns bool)
    registry.options().host_functions["string.includes"] = [toString](const std::vector<compiler::BytecodeValue>& args) {
        if (args.size() != 2) throw std::runtime_error("string.includes() requires 2 arguments");
        std::string s = toString(args[0]);
        std::string substr = toString(args[1]);
        return compiler::BytecodeValue(s.find(substr) != std::string::npos);
    };

    // Register prototype methods for "string".method() syntax
    registry.vm().registerPrototypeMethod("String", "len", compiler::HostFunctionRef{.name = "string.len"});
    registry.vm().registerPrototypeMethod("String", "lower", compiler::HostFunctionRef{.name = "string.lower"});
    registry.vm().registerPrototypeMethod("String", "upper", compiler::HostFunctionRef{.name = "string.upper"});
    registry.vm().registerPrototypeMethod("String", "trim", compiler::HostFunctionRef{.name = "string.trim"});
    registry.vm().registerPrototypeMethod("String", "sub", compiler::HostFunctionRef{.name = "string.sub"});
    registry.vm().registerPrototypeMethod("String", "find", compiler::HostFunctionRef{.name = "string.find"});
    registry.vm().registerPrototypeMethod("String", "replace", compiler::HostFunctionRef{.name = "string.replace"});
    registry.vm().registerPrototypeMethod("String", "split", compiler::HostFunctionRef{.name = "string.split"});
    registry.vm().registerPrototypeMethod("String", "join", compiler::HostFunctionRef{.name = "string.join"});
    registry.vm().registerPrototypeMethod("String", "startswith", compiler::HostFunctionRef{.name = "string.startswith"});
    registry.vm().registerPrototypeMethod("String", "endswith", compiler::HostFunctionRef{.name = "string.endswith"});
    registry.vm().registerPrototypeMethod("String", "includes", compiler::HostFunctionRef{.name = "string.includes"});

    // Register string object via vm_setup (accumulated)
    registry.addVmSetup([](compiler::VM& vm) {
        auto strObj = vm.createHostObject();
        vm.setHostObjectField(strObj, "len", compiler::HostFunctionRef{.name = "string.len"});
        vm.setHostObjectField(strObj, "lower", compiler::HostFunctionRef{.name = "string.lower"});
        vm.setHostObjectField(strObj, "upper", compiler::HostFunctionRef{.name = "string.upper"});
        vm.setHostObjectField(strObj, "trim", compiler::HostFunctionRef{.name = "string.trim"});
        vm.setHostObjectField(strObj, "sub", compiler::HostFunctionRef{.name = "string.sub"});
        vm.setHostObjectField(strObj, "find", compiler::HostFunctionRef{.name = "string.find"});
        vm.setHostObjectField(strObj, "replace", compiler::HostFunctionRef{.name = "string.replace"});
        vm.setHostObjectField(strObj, "split", compiler::HostFunctionRef{.name = "string.split"});
        vm.setHostObjectField(strObj, "join", compiler::HostFunctionRef{.name = "string.join"});
        vm.setHostObjectField(strObj, "startswith", compiler::HostFunctionRef{.name = "string.startswith"});
        vm.setHostObjectField(strObj, "endswith", compiler::HostFunctionRef{.name = "string.endswith"});
        vm.setHostObjectField(strObj, "includes", compiler::HostFunctionRef{.name = "string.includes"});
        vm.setGlobal("String", strObj);
    });
}

// Implementation of old registerStringModule (placeholder)
inline void registerStringModule(Environment& env) {
    (void)env;
}

} // namespace havel::stdlib
