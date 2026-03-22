/*
 * ProcessModule.hpp - Process management stdlib for VM (no host/service)
 * Pure VM implementation using BytecodeValue
 */
#pragma once

#include "../compiler/bytecode/VM.hpp"
#include "../compiler/bytecode/HostBridge.hpp"

#include "core/process/ProcessManager.hpp"
#include <cstdlib>
#include <sstream>

namespace havel {
    class Environment;
}

namespace havel::stdlib {

// OLD: Register with Environment (for AST interpreter) - DECLARATION ONLY
void registerProcessModule(Environment& env);

// NEW: Register process module with VM's host bridge (VM-native)
inline void registerProcessModuleVM(compiler::HostBridge& registry) {
    auto& vm = bridge.vm();
    auto& options = bridge.options();
    
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
    
    // Helper: create ProcessInfo object
    auto createProcessInfo = [&vm](const ProcessManager::ProcessInfo& info) -> compiler::BytecodeValue {
        auto procObj = vm.createHostObject();
        vm.setHostObjectField(procObj, "pid", compiler::BytecodeValue(static_cast<int64_t>(info.pid)));
        vm.setHostObjectField(procObj, "ppid", compiler::BytecodeValue(static_cast<int64_t>(info.ppid)));
        vm.setHostObjectField(procObj, "name", compiler::BytecodeValue(info.name));
        vm.setHostObjectField(procObj, "command", compiler::BytecodeValue(info.command));
        vm.setHostObjectField(procObj, "user", compiler::BytecodeValue(info.user));
        vm.setHostObjectField(procObj, "cpu_usage", compiler::BytecodeValue(info.cpu_usage));
        vm.setHostObjectField(procObj, "memory_usage", compiler::BytecodeValue(static_cast<double>(info.memory_usage)));
        return compiler::BytecodeValue(procObj);
    };
    
    // process.list() - List all processes
    options.host_functions["process.list"] = [createProcessInfo, &vm](const std::vector<compiler::BytecodeValue>&) {
        auto resultArray = vm.createHostArray();
        auto procs = ProcessManager::listProcesses();
        for (const auto& proc : procs) {
            vm.pushHostArrayValue(resultArray, createProcessInfo(proc));
        }
        return compiler::BytecodeValue(resultArray);
    };
    
    // process.find(name) - Find processes by name
    options.host_functions["process.find"] = [valueToString, createProcessInfo, &vm](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("process.find() requires a process name");
        std::string processName = valueToString(args[0]);
        auto resultArray = vm.createHostArray();
        auto procs = ProcessManager::listProcesses();
        for (const auto& proc : procs) {
            if (proc.name.find(processName) != std::string::npos) {
                vm.pushHostArrayValue(resultArray, createProcessInfo(proc));
            }
        }
        return compiler::BytecodeValue(resultArray);
    };
    
    // process.kill(pid) - Kill process by PID
    options.host_functions["process.kill"] = [](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("process.kill() requires a PID");
        if (!std::holds_alternative<int64_t>(args[0])) throw std::runtime_error("PID must be an integer");
        int pid = static_cast<int>(std::get<int64_t>(args[0]));
        // Use system kill command since ProcessManager doesn't have killProcess
        std::string cmd = "kill " + std::to_string(pid);
        int result = std::system(cmd.c_str());
        return compiler::BytecodeValue(result == 0);
    };
    
    // process.exec(command) - Execute command
    options.host_functions["process.exec"] = [valueToString](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("process.exec() requires a command");
        std::string command = valueToString(args[0]);
        int result = std::system(command.c_str());
        return compiler::BytecodeValue(static_cast<int64_t>(result));
    };
    
    // process.getPid() - Get current process PID
    options.host_functions["process.getPid"] = [](const std::vector<compiler::BytecodeValue>&) {
        return compiler::BytecodeValue(static_cast<int64_t>(getpid()));
    };
    
    // process.getPpid() - Get parent process PID
    options.host_functions["process.getPpid"] = [](const std::vector<compiler::BytecodeValue>&) {
        return compiler::BytecodeValue(static_cast<int64_t>(getppid()));
    };
    
    // Register process object via vm_setup
    registry.addVmSetup([](compiler::VM& vm) {
        auto procObj = vm.createHostObject();
        vm.setHostObjectField(procObj, "list", compiler::HostFunctionRef{.name = "process.list"});
        vm.setHostObjectField(procObj, "find", compiler::HostFunctionRef{.name = "process.find"});
        vm.setHostObjectField(procObj, "kill", compiler::HostFunctionRef{.name = "process.kill"});
        vm.setHostObjectField(procObj, "exec", compiler::HostFunctionRef{.name = "process.exec"});
        vm.setHostObjectField(procObj, "getPid", compiler::HostFunctionRef{.name = "process.getPid"});
        vm.setHostObjectField(procObj, "getPpid", compiler::HostFunctionRef{.name = "process.getPpid"});
        vm.setGlobal("process", procObj);
    });
}

// Implementation of old registerProcessModule (placeholder)
inline void registerProcessModule(Environment& env) {
    (void)env;
}

} // namespace havel::stdlib
