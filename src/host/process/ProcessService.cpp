/*
 * ProcessService.cpp
 *
 * Pure C++ process service implementation.
 * No VM, no interpreter, no HavelValue - just system logic.
 */
#include "ProcessService.hpp"
#include "core/process/ProcessManager.hpp"
#include <filesystem>
#include <fstream>
#include <algorithm>

namespace havel::host {

// =========================================================================
// Process queries
// =========================================================================

std::vector<int32_t> ProcessService::listProcesses() {
    std::vector<int32_t> pids;
    try {
        for (const auto& entry : std::filesystem::directory_iterator("/proc")) {
            if (entry.is_directory()) {
                std::string dirname = entry.path().filename().string();
                if (std::all_of(dirname.begin(), dirname.end(), ::isdigit)) {
                    pids.push_back(std::stoi(dirname));
                }
            }
        }
    } catch (...) {
        // Ignore errors
    }
    return pids;
}

std::vector<int32_t> ProcessService::findProcesses(const std::string& name) {
    std::vector<int32_t> pids;
    try {
        for (const auto& entry : std::filesystem::directory_iterator("/proc")) {
            if (entry.is_directory()) {
                std::string dirname = entry.path().filename().string();
                if (std::all_of(dirname.begin(), dirname.end(), ::isdigit)) {
                    int pid = std::stoi(dirname);
                    std::ifstream commFile("/proc/" + dirname + "/comm");
                    if (commFile) {
                        std::string commName;
                        std::getline(commFile, commName);
                        if (commName.find(name) != std::string::npos) {
                            pids.push_back(pid);
                        }
                    }
                }
            }
        }
    } catch (...) {
        // Ignore errors
    }
    return pids;
}

std::optional<ProcessInfo> ProcessService::getProcessInfo(int32_t pid) {
    auto info = havel::ProcessManager::getProcessInfo(pid);
    if (!info) return std::nullopt;
    
    ProcessInfo result;
    result.pid = info->pid;
    result.ppid = info->ppid;
    result.name = info->name;
    result.command = info->command;
    result.user = info->user;
    result.cpu_usage = info->cpu_usage;
    result.memory_usage = info->memory_usage;
    return result;
}

std::string ProcessService::getProcessName(int32_t pid) {
    return havel::ProcessManager::getProcessName(pid);
}

bool ProcessService::processExists(const std::string& name) {
    return !findProcesses(name).empty();
}

// =========================================================================
// Process control
// =========================================================================

bool ProcessService::isProcessAlive(int32_t pid) {
    return havel::ProcessManager::isProcessAlive(pid);
}

bool ProcessService::sendSignal(int32_t pid, int signal) {
    return havel::ProcessManager::sendSignal(pid, signal);
}

bool ProcessService::terminateProcess(int32_t pid, int timeout_ms) {
    return havel::ProcessManager::terminateProcess(pid, timeout_ms);
}

// =========================================================================
// Process metrics
// =========================================================================

double ProcessService::getCpuUsage(int32_t pid) {
    return havel::ProcessManager::getCpuUsage(pid);
}

uint64_t ProcessService::getMemoryUsage(int32_t pid) {
    return havel::ProcessManager::getMemoryUsage(pid);
}

int32_t ProcessService::getThreadCount(int32_t pid) {
    return havel::ProcessManager::getThreadCount(pid);
}

// =========================================================================
// Process properties
// =========================================================================

std::string ProcessService::getProcessCommand(int32_t pid) {
    return havel::ProcessManager::getProcessCommand(pid);
}

std::string ProcessService::getProcessUser(int32_t pid) {
    return havel::ProcessManager::getProcessUser(pid);
}

std::string ProcessService::getProcessWorkingDirectory(int32_t pid) {
    return havel::ProcessManager::getProcessWorkingDirectory(pid);
}

std::string ProcessService::getProcessExecutablePath(int32_t pid) {
    return havel::ProcessManager::getProcessExecutablePath(pid);
}

int32_t ProcessService::getCurrentPid() {
    return havel::ProcessManager::getCurrentPid();
}

int32_t ProcessService::getParentPid() {
    return havel::ProcessManager::getParentPid();
}

} // namespace havel::host
