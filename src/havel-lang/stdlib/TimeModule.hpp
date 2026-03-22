/*
 * TimeModule.hpp - Time and date stdlib for VM (no host/service)
 * Pure VM implementation using BytecodeValue
 */
#pragma once

#include "../compiler/bytecode/VM.hpp"
#include "../compiler/bytecode/HostBridge.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <thread>

namespace havel {
    class Environment;
}

namespace havel::stdlib {

// OLD: Register with Environment (for AST interpreter) - DECLARATION ONLY
void registerTimeModule(Environment& env);

// NEW: Register time module with VM's host bridge (VM-native)
inline void registerTimeModuleVM(compiler::HostBridgeRegistry& registry) {
    auto& vm = registry.vm();
    auto& options = registry.options();
    
    // time.now() - Get current timestamp (seconds since epoch)
    options.host_functions["time.now"] = [](const std::vector<compiler::BytecodeValue>&) {
        auto now = std::chrono::system_clock::now();
        auto epoch = now.time_since_epoch();
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(epoch);
        return compiler::BytecodeValue(static_cast<double>(seconds.count()));
    };
    
    // time.nowMs() - Get current timestamp in milliseconds
    options.host_functions["time.nowMs"] = [](const std::vector<compiler::BytecodeValue>&) {
        auto now = std::chrono::system_clock::now();
        auto epoch = now.time_since_epoch();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(epoch);
        return compiler::BytecodeValue(static_cast<double>(ms.count()));
    };
    
    // time.sleep(seconds) - Sleep for specified seconds
    options.host_functions["time.sleep"] = [](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("time.sleep() requires duration in seconds");
        if (!std::holds_alternative<double>(args[0]) && !std::holds_alternative<int64_t>(args[0])) {
            throw std::runtime_error("time.sleep() requires a number");
        }
        double seconds = std::holds_alternative<double>(args[0]) ? std::get<double>(args[0]) : static_cast<double>(std::get<int64_t>(args[0]));
        if (seconds < 0) throw std::runtime_error("time.sleep() duration must be non-negative");
        std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(seconds * 1000)));
        return compiler::BytecodeValue(true);
    };
    
    // time.sleepMs(milliseconds) - Sleep for specified milliseconds
    options.host_functions["time.sleepMs"] = [](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("time.sleepMs() requires duration in milliseconds");
        if (!std::holds_alternative<double>(args[0]) && !std::holds_alternative<int64_t>(args[0])) {
            throw std::runtime_error("time.sleepMs() requires a number");
        }
        double ms = std::holds_alternative<double>(args[0]) ? std::get<double>(args[0]) : static_cast<double>(std::get<int64_t>(args[0]));
        if (ms < 0) throw std::runtime_error("time.sleepMs() duration must be non-negative");
        std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(ms)));
        return compiler::BytecodeValue(true);
    };
    
    // time.strftime(format) - Format current time
    options.host_functions["time.strftime"] = [](const std::vector<compiler::BytecodeValue>& args) {
        std::string format = args.empty() ? "%Y-%m-%d %H:%M:%S" : 
            (std::holds_alternative<std::string>(args[0]) ? std::get<std::string>(args[0]) : "%Y-%m-%d %H:%M:%S");
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::ostringstream oss;
        oss << std::put_time(std::localtime(&time), format.c_str());
        return compiler::BytecodeValue(oss.str());
    };
    
    // time.unix() - Get current Unix timestamp (alias for now())
    options.host_functions["time.unix"] = [](const std::vector<compiler::BytecodeValue>&) {
        auto now = std::chrono::system_clock::now();
        auto epoch = now.time_since_epoch();
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(epoch);
        return compiler::BytecodeValue(static_cast<int64_t>(seconds.count()));
    };
    
    // Register time functions via vm_setup
    options.vm_setup = [](compiler::VM& vm) {
        auto timeObj = vm.createHostObject();
        vm.setHostObjectField(timeObj, "now", compiler::HostFunctionRef{.name = "time.now"});
        vm.setHostObjectField(timeObj, "nowMs", compiler::HostFunctionRef{.name = "time.nowMs"});
        vm.setHostObjectField(timeObj, "sleep", compiler::HostFunctionRef{.name = "time.sleep"});
        vm.setHostObjectField(timeObj, "sleepMs", compiler::HostFunctionRef{.name = "time.sleepMs"});
        vm.setHostObjectField(timeObj, "strftime", compiler::HostFunctionRef{.name = "time.strftime"});
        vm.setHostObjectField(timeObj, "unix", compiler::HostFunctionRef{.name = "time.unix"});
        vm.setGlobal("time", timeObj);
    };
}

// Implementation of old registerTimeModule (placeholder)
inline void registerTimeModule(Environment& env) {
    (void)env;
}

} // namespace havel::stdlib
