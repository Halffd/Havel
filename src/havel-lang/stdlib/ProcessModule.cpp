/*
 * ProcessModule.cpp
 *
 * Process management for Havel standard library.
 * Provides process enumeration, control, and metrics.
 */
#include "ProcessModule.hpp"
#include "core/process/ProcessManager.hpp"
#include <sstream>
#include <iomanip>
#include <cstdlib>

namespace havel::stdlib {

void registerProcessModule(Environment& env) {
    // Create process namespace
    auto processObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();

    // Helper: convert value to string
    auto valueToString = [](const HavelValue& v) -> std::string {
        if (v.isString()) return v.asString();
        if (v.isNumber()) {
            double val = v.asNumber();
            if (val == std::floor(val) && std::abs(val) < 1e15) {
                return std::to_string(static_cast<long long>(val));
            } else {
                std::ostringstream oss;
                oss.precision(15);
                oss << val;
                return oss.str();
            }
        }
        if (v.isBool()) return v.asBool() ? "true" : "false";
        return "";
    };

    // Helper: create ProcessInfo object
    auto createProcessInfo = [&valueToString](const ProcessManager::ProcessInfo& info) -> HavelValue {
        auto procObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
        (*procObj)["pid"] = HavelValue(static_cast<double>(info.pid));
        (*procObj)["ppid"] = HavelValue(static_cast<double>(info.ppid));
        (*procObj)["name"] = HavelValue(info.name);
        (*procObj)["command"] = HavelValue(info.command);
        (*procObj)["user"] = HavelValue(info.user);
        (*procObj)["cpu_usage"] = HavelValue(info.cpu_usage);
        (*procObj)["memory_usage"] = HavelValue(static_cast<double>(info.memory_usage));
        return HavelValue(procObj);
    };

    // ============================================================================
    // process.list() - List all processes
    // ============================================================================
    (*processObj)["list"] = HavelValue(BuiltinFunction([createProcessInfo](const std::vector<HavelValue>&) -> HavelResult {
        auto resultArray = std::make_shared<std::vector<HavelValue>>();
        auto procs = ProcessManager::listProcesses();
        for (const auto& proc : procs) {
            resultArray->push_back(createProcessInfo(proc));
        }
        return HavelValue(resultArray);
    }));

    // ============================================================================
    // process.find(name) - Find processes by name
    // ============================================================================
    (*processObj)["find"] = HavelValue(BuiltinFunction([valueToString, createProcessInfo](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("process.find() requires a process name");
        }

        std::string processName = valueToString(args[0]);
        auto resultArray = std::make_shared<std::vector<HavelValue>>();
        auto procs = ProcessManager::findProcesses(processName);
        for (const auto& proc : procs) {
            resultArray->push_back(createProcessInfo(proc));
        }
        return HavelValue(resultArray);
    }));

    // ============================================================================
    // process.info(pid) - Get detailed process information
    // ============================================================================
    (*processObj)["info"] = HavelValue(BuiltinFunction([createProcessInfo](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("process.info() requires a PID");
        }

        int32_t pid = static_cast<int32_t>(args[0].asNumber());
        auto infoOpt = ProcessManager::getProcessInfo(pid);
        
        if (!infoOpt.has_value()) {
            return HavelValue(nullptr);  // Process not found
        }
        
        return createProcessInfo(infoOpt.value());
    }));

    // ============================================================================
    // process.exists(pid) - Check if process exists
    // ============================================================================
    (*processObj)["exists"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("process.exists() requires a PID");
        }

        int32_t pid = static_cast<int32_t>(args[0].asNumber());
        return HavelValue(ProcessManager::isProcessAlive(pid));
    }));

    // ============================================================================
    // process.kill(pid, signal?) - Send signal to process
    // ============================================================================
    (*processObj)["kill"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("process.kill() requires a PID");
        }

        int32_t pid = static_cast<int32_t>(args[0].asNumber());
        int signal = SIGTERM;
        
        if (args.size() > 1) {
            if (args[1].isString()) {
                std::string sigName = args[1].asString();
                if (sigName == "SIGKILL" || sigName == "kill") signal = SIGKILL;
                else if (sigName == "SIGTERM" || sigName == "term") signal = SIGTERM;
                else if (sigName == "SIGHUP" || sigName == "hangup") signal = SIGHUP;
                else if (sigName == "SIGINT" || sigName == "interrupt") signal = SIGINT;
                else if (sigName == "SIGSTOP") signal = SIGSTOP;
                else if (sigName == "SIGCONT") signal = SIGCONT;
            } else {
                signal = static_cast<int>(args[1].asNumber());
            }
        }

        return HavelValue(ProcessManager::sendSignal(pid, signal));
    }));

    // ============================================================================
    // process.terminate(pid, timeout?) - Terminate process gracefully
    // ============================================================================
    (*processObj)["terminate"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("process.terminate() requires a PID");
        }

        int32_t pid = static_cast<int32_t>(args[0].asNumber());
        int timeout = 5000;  // 5 seconds default
        
        if (args.size() > 1) {
            timeout = static_cast<int>(args[1].asNumber());
        }

        return HavelValue(ProcessManager::terminateProcess(pid, timeout));
    }));

    // ============================================================================
    // process.state(pid) - Get process state
    // ============================================================================
    (*processObj)["state"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("process.state() requires a PID");
        }

        int32_t pid = static_cast<int32_t>(args[0].asNumber());
        auto state = ProcessManager::getProcessState(pid);
        
        std::string stateStr;
        switch (state) {
            case ProcessManager::ProcessState::RUNNING: stateStr = "running"; break;
            case ProcessManager::ProcessState::SLEEPING: stateStr = "sleeping"; break;
            case ProcessManager::ProcessState::ZOMBIE: stateStr = "zombie"; break;
            case ProcessManager::ProcessState::STOPPED: stateStr = "stopped"; break;
            case ProcessManager::ProcessState::NOT_FOUND: stateStr = "not_found"; break;
            case ProcessManager::ProcessState::NO_PERMISSION: stateStr = "no_permission"; break;
        }
        
        return HavelValue(stateStr);
    }));

    // ============================================================================
    // process.isZombie(pid) - Check if process is zombie
    // ============================================================================
    (*processObj)["isZombie"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("process.isZombie() requires a PID");
        }

        int32_t pid = static_cast<int32_t>(args[0].asNumber());
        return HavelValue(ProcessManager::isZombie(pid));
    }));

    // ============================================================================
    // process.cpu(pid) - Get CPU usage
    // ============================================================================
    (*processObj)["cpu"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("process.cpu() requires a PID");
        }

        int32_t pid = static_cast<int32_t>(args[0].asNumber());
        return HavelValue(ProcessManager::getCpuUsage(pid));
    }));

    // ============================================================================
    // process.memory(pid) - Get memory usage in bytes
    // ============================================================================
    (*processObj)["memory"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("process.memory() requires a PID");
        }

        int32_t pid = static_cast<int32_t>(args[0].asNumber());
        return HavelValue(static_cast<double>(ProcessManager::getMemoryUsage(pid)));
    }));

    // ============================================================================
    // process.threads(pid) - Get thread count
    // ============================================================================
    (*processObj)["threads"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("process.threads() requires a PID");
        }

        int32_t pid = static_cast<int32_t>(args[0].asNumber());
        return HavelValue(static_cast<double>(ProcessManager::getThreadCount(pid)));
    }));

    // ============================================================================
    // process.name(pid) - Get process name
    // ============================================================================
    (*processObj)["name"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("process.name() requires a PID");
        }

        int32_t pid = static_cast<int32_t>(args[0].asNumber());
        return HavelValue(ProcessManager::getProcessName(pid));
    }));

    // ============================================================================
    // process.command(pid) - Get full command line
    // ============================================================================
    (*processObj)["command"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("process.command() requires a PID");
        }

        int32_t pid = static_cast<int32_t>(args[0].asNumber());
        return HavelValue(ProcessManager::getProcessCommand(pid));
    }));

    // ============================================================================
    // process.user(pid) - Get process user
    // ============================================================================
    (*processObj)["user"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("process.user() requires a PID");
        }

        int32_t pid = static_cast<int32_t>(args[0].asNumber());
        return HavelValue(ProcessManager::getProcessUser(pid));
    }));

    // ============================================================================
    // process.cwd(pid) - Get working directory
    // ============================================================================
    (*processObj)["cwd"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("process.cwd() requires a PID");
        }

        int32_t pid = static_cast<int32_t>(args[0].asNumber());
        return HavelValue(ProcessManager::getProcessWorkingDirectory(pid));
    }));

    // ============================================================================
    // process.exe(pid) - Get executable path
    // ============================================================================
    (*processObj)["exe"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("process.exe() requires a PID");
        }

        int32_t pid = static_cast<int32_t>(args[0].asNumber());
        return HavelValue(ProcessManager::getProcessExecutablePath(pid));
    }));

    // ============================================================================
    // process.env(pid, var) - Get environment variable
    // ============================================================================
    (*processObj)["env"] = HavelValue(BuiltinFunction([valueToString](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() < 2) {
            return HavelRuntimeError("process.env() requires PID and variable name");
        }

        int32_t pid = static_cast<int32_t>(args[0].asNumber());
        std::string varName = valueToString(args[1]);
        return HavelValue(ProcessManager::getProcessEnvironment(pid, varName));
    }));

    // ============================================================================
    // process.startTime(pid) - Get process start time
    // ============================================================================
    (*processObj)["startTime"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("process.startTime() requires a PID");
        }

        int32_t pid = static_cast<int32_t>(args[0].asNumber());
        auto startTime = ProcessManager::getProcessStartTime(pid);
        auto epoch = startTime.time_since_epoch();
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(epoch).count();
        return HavelValue(static_cast<double>(seconds));
    }));

    // ============================================================================
    // process.nice(pid, value?) - Get/set nice value
    // ============================================================================
    (*processObj)["nice"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("process.nice() requires a PID");
        }

        int32_t pid = static_cast<int32_t>(args[0].asNumber());
        
        if (args.size() > 1) {
            // Set nice value
            int niceValue = static_cast<int>(args[1].asNumber());
            return HavelValue(ProcessManager::setProcessNice(pid, niceValue));
        } else {
            // Get nice value - not implemented in ProcessManager, return 0
            return HavelValue(0.0);
        }
    }));

    // ============================================================================
    // process.pid() - Get current process PID
    // ============================================================================
    (*processObj)["pid"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>&) -> HavelResult {
        return HavelValue(static_cast<double>(ProcessManager::getCurrentPid()));
    }));

    // ============================================================================
    // process.ppid() - Get parent process PID
    // ============================================================================
    (*processObj)["ppid"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>&) -> HavelResult {
        return HavelValue(static_cast<double>(ProcessManager::getParentPid()));
    }));

    // Register process namespace
    env.Define("process", HavelValue(processObj));
}

} // namespace havel::stdlib
