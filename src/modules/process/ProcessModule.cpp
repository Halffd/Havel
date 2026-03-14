/*
 * ProcessModule.cpp
 * 
 * Process management module for Havel language.
 */
#include "ProcessModule.hpp"
#include "../../havel-lang/runtime/Environment.hpp"
#include <filesystem>
#include <fstream>
#include <algorithm>

namespace havel::modules {

void registerProcessModule(Environment& env, std::shared_ptr<IHostAPI> hostAPI) {
    auto proc = std::make_shared<std::unordered_map<std::string, HavelValue>>();
    
    // =========================================================================
    // process.list() - Get list of running processes
    // =========================================================================
    (*proc)["list"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>&) -> HavelResult {
        HavelArray processes = std::make_shared<std::vector<HavelValue>>();
        
        try {
            for (const auto& entry : std::filesystem::directory_iterator("/proc")) {
                if (entry.is_directory()) {
                    std::string dirname = entry.path().filename().string();
                    if (std::all_of(dirname.begin(), dirname.end(), ::isdigit)) {
                        int pid = std::stoi(dirname);
                        processes->push_back(HavelValue(static_cast<double>(pid)));
                    }
                }
            }
        } catch (...) {
            // Ignore errors
        }
        
        return HavelValue(processes);
    }));
    
    // =========================================================================
    // process.find(name) - Find processes by name, returns array of PIDs
    // =========================================================================
    (*proc)["find"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("process.find() requires process name");
        }
        
        std::string name = args[0].asString();
        HavelArray pids = std::make_shared<std::vector<HavelValue>>();
        
        try {
            for (const auto& entry : std::filesystem::directory_iterator("/proc")) {
                if (entry.is_directory()) {
                    std::string dirname = entry.path().filename().string();
                    if (std::all_of(dirname.begin(), dirname.end(), ::isdigit)) {
                        int pid = std::stoi(dirname);
                        
                        // Read process name from /proc/PID/comm
                        std::ifstream commFile("/proc/" + dirname + "/comm");
                        if (commFile) {
                            std::string commName;
                            std::getline(commFile, commName);
                            if (commName.find(name) != std::string::npos) {
                                pids->push_back(HavelValue(static_cast<double>(pid)));
                            }
                        }
                    }
                }
            }
        } catch (...) {
            // Ignore errors
        }
        
        return HavelValue(pids);
    }));
    
    // =========================================================================
    // process.exists(name) - Check if process is running
    // =========================================================================
    (*proc)["exists"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("process.exists() requires process name");
        }
        
        std::string name = args[0].asString();
        
        try {
            for (const auto& entry : std::filesystem::directory_iterator("/proc")) {
                if (entry.is_directory()) {
                    std::string dirname = entry.path().filename().string();
                    if (std::all_of(dirname.begin(), dirname.end(), ::isdigit)) {
                        std::ifstream commFile("/proc/" + dirname + "/comm");
                        if (commFile) {
                            std::string commName;
                            std::getline(commFile, commName);
                            if (commName.find(name) != std::string::npos) {
                                return HavelValue(true);
                            }
                        }
                    }
                }
            }
        } catch (...) {
            // Ignore errors
        }
        
        return HavelValue(false);
    }));
    
    // =========================================================================
    // process.name(pid) - Get process name by PID
    // =========================================================================
    (*proc)["name"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("process.name() requires PID");
        }
        
        int pid = static_cast<int>(args[0].asNumber());
        
        try {
            std::ifstream commFile("/proc/" + std::to_string(pid) + "/comm");
            if (commFile) {
                std::string commName;
                std::getline(commFile, commName);
                return HavelValue(commName);
            }
        } catch (...) {
            // Ignore errors
        }
        
        return HavelValue("");
    }));
    
    // Register process module
    env.Define("process", HavelValue(proc));
}

} // namespace havel::modules
