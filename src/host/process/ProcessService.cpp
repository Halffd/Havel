/*
 * ProcessService.cpp
 *
 * Pure C++ process service implementation.
 * No VM, no interpreter, no HavelValue - just system logic.
 */
#include "ProcessService.hpp"
#ifndef _WIN32
#include "core/process/ProcessManager.hpp"
#include <filesystem>
#include <fstream>
#include <unistd.h>
#else
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <processthreadsapi.h>
#include <securitybaseapi.h>
#include <sddl.h>
#endif
#include <algorithm>
#include <cctype>

namespace havel::host {

#ifdef _WIN32
namespace {

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string getProcessExePathWin(int32_t pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE,
                           static_cast<DWORD>(pid));
    if (!h) {
        return "";
    }
    char buffer[MAX_PATH];
    DWORD size = static_cast<DWORD>(sizeof(buffer));
    std::string exePath;
    if (QueryFullProcessImageNameA(h, 0, buffer, &size) != 0) {
        exePath.assign(buffer, size);
    }
    CloseHandle(h);
    return exePath;
}

} // namespace
#endif

// =========================================================================
// Process queries
// =========================================================================

std::vector<int32_t> ProcessService::listProcesses() {
    std::vector<int32_t> pids;
#ifndef _WIN32
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
#else
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return pids;
    }
    PROCESSENTRY32 pe{};
    pe.dwSize = sizeof(pe);
    if (Process32First(snapshot, &pe)) {
        do {
            pids.push_back(static_cast<int32_t>(pe.th32ProcessID));
        } while (Process32Next(snapshot, &pe));
    }
    CloseHandle(snapshot);
#endif
    return pids;
}

std::vector<int32_t> ProcessService::findProcesses(const std::string& name) {
    std::vector<int32_t> pids;
#ifndef _WIN32
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
#else
    const std::string needle = toLower(name);
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return pids;
    }
    PROCESSENTRY32 pe{};
    pe.dwSize = sizeof(pe);
    if (Process32First(snapshot, &pe)) {
        do {
            std::string procName = toLower(pe.szExeFile);
            if (procName.find(needle) != std::string::npos) {
                pids.push_back(static_cast<int32_t>(pe.th32ProcessID));
            }
        } while (Process32Next(snapshot, &pe));
    }
    CloseHandle(snapshot);
#endif
    return pids;
}

std::optional<ProcessInfo> ProcessService::getProcessInfo(int32_t pid) {
#ifndef _WIN32
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
#else
    if (!isProcessAlive(pid)) {
        return std::nullopt;
    }
    ProcessInfo result;
    result.pid = pid;
    result.ppid = 0;
    result.name = getProcessName(pid);
    result.command = getProcessCommand(pid);
    result.user = getProcessUser(pid);
    result.cpu_usage = getCpuUsage(pid);
    result.memory_usage = getMemoryUsage(pid);
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32 pe{};
        pe.dwSize = sizeof(pe);
        if (Process32First(snapshot, &pe)) {
            do {
                if (static_cast<int32_t>(pe.th32ProcessID) == pid) {
                    result.ppid = static_cast<int32_t>(pe.th32ParentProcessID);
                    break;
                }
            } while (Process32Next(snapshot, &pe));
        }
        CloseHandle(snapshot);
    }
    return result;
#endif
}

std::string ProcessService::getProcessName(int32_t pid) {
#ifndef _WIN32
    return havel::ProcessManager::getProcessName(pid);
#else
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return "";
    }
    PROCESSENTRY32 pe{};
    pe.dwSize = sizeof(pe);
    std::string name;
    if (Process32First(snapshot, &pe)) {
        do {
            if (static_cast<int32_t>(pe.th32ProcessID) == pid) {
                name = pe.szExeFile;
                break;
            }
        } while (Process32Next(snapshot, &pe));
    }
    CloseHandle(snapshot);
    return name;
#endif
}

bool ProcessService::processExists(const std::string& name) {
    return !findProcesses(name).empty();
}

// =========================================================================
// Process control
// =========================================================================

bool ProcessService::isProcessAlive(int32_t pid) {
#ifndef _WIN32
    return havel::ProcessManager::isProcessAlive(pid);
#else
    if (pid <= 0) return false;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!h) return false;
    DWORD code = 0;
    BOOL ok = GetExitCodeProcess(h, &code);
    CloseHandle(h);
    return ok && code == STILL_ACTIVE;
#endif
}

bool ProcessService::sendSignal(int32_t pid, int signal) {
#ifndef _WIN32
    return havel::ProcessManager::sendSignal(pid, signal);
#else
    if (signal == 0) {
        return isProcessAlive(pid);
    }
    HANDLE h = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                           static_cast<DWORD>(pid));
    if (!h) return false;
    BOOL ok = TerminateProcess(h, static_cast<UINT>(signal));
    CloseHandle(h);
    return ok != 0;
#endif
}

bool ProcessService::setNice(int32_t pid, int nice) {
#ifndef _WIN32
    return havel::ProcessManager::setProcessNice(pid, nice);
#else
    HANDLE h = OpenProcess(PROCESS_SET_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!h) return false;
    DWORD cls = NORMAL_PRIORITY_CLASS;
    if (nice <= -15) cls = HIGH_PRIORITY_CLASS;
    else if (nice <= -5) cls = ABOVE_NORMAL_PRIORITY_CLASS;
    else if (nice >= 15) cls = IDLE_PRIORITY_CLASS;
    else if (nice >= 8) cls = BELOW_NORMAL_PRIORITY_CLASS;
    BOOL ok = SetPriorityClass(h, cls);
    CloseHandle(h);
    return ok != 0;
#endif
}

bool ProcessService::terminateProcess(int32_t pid, int timeout_ms) {
#ifndef _WIN32
    return havel::ProcessManager::terminateProcess(pid, timeout_ms);
#else
    HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (!h) return false;
    BOOL ok = TerminateProcess(h, 1);
    if (ok) {
        WaitForSingleObject(h, timeout_ms > 0 ? static_cast<DWORD>(timeout_ms) : INFINITE);
    }
    CloseHandle(h);
    return ok != 0;
#endif
}

// =========================================================================
// Process metrics
// =========================================================================

double ProcessService::getCpuUsage(int32_t pid) {
#ifndef _WIN32
    return havel::ProcessManager::getCpuUsage(pid);
#else
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!h) return -1.0;

    FILETIME createTime{}, exitTime{}, kernelTime{}, userTime{};
    if (!GetProcessTimes(h, &createTime, &exitTime, &kernelTime, &userTime)) {
        CloseHandle(h);
        return -1.0;
    }
    CloseHandle(h);

    ULARGE_INTEGER k{}, u{};
    k.LowPart = kernelTime.dwLowDateTime;
    k.HighPart = kernelTime.dwHighDateTime;
    u.LowPart = userTime.dwLowDateTime;
    u.HighPart = userTime.dwHighDateTime;
    // Return CPU time in seconds as a monotonic metric.
    return static_cast<double>(k.QuadPart + u.QuadPart) / 10000000.0;
#endif
}

uint64_t ProcessService::getMemoryUsage(int32_t pid) {
#ifndef _WIN32
    return havel::ProcessManager::getMemoryUsage(pid);
#else
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE,
                           static_cast<DWORD>(pid));
    if (!h) return 0;
    PROCESS_MEMORY_COUNTERS pmc{};
    pmc.cb = sizeof(pmc);
    uint64_t mem = 0;
    if (GetProcessMemoryInfo(h, &pmc, sizeof(pmc))) {
        mem = static_cast<uint64_t>(pmc.WorkingSetSize);
    }
    CloseHandle(h);
    return mem;
#endif
}

int32_t ProcessService::getThreadCount(int32_t pid) {
#ifndef _WIN32
    return havel::ProcessManager::getThreadCount(pid);
#else
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return -1;
    PROCESSENTRY32 pe{};
    pe.dwSize = sizeof(pe);
    int32_t threads = -1;
    if (Process32First(snapshot, &pe)) {
        do {
            if (static_cast<int32_t>(pe.th32ProcessID) == pid) {
                threads = static_cast<int32_t>(pe.cntThreads);
                break;
            }
        } while (Process32Next(snapshot, &pe));
    }
    CloseHandle(snapshot);
    return threads;
#endif
}

// =========================================================================
// Process properties
// =========================================================================

std::string ProcessService::getProcessCommand(int32_t pid) {
#ifndef _WIN32
    return havel::ProcessManager::getProcessCommand(pid);
#else
    return getProcessExePathWin(pid);
#endif
}

std::string ProcessService::getProcessUser(int32_t pid) {
#ifndef _WIN32
    return havel::ProcessManager::getProcessUser(pid);
#else
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!h) return "";
    HANDLE token = nullptr;
    if (!OpenProcessToken(h, TOKEN_QUERY, &token)) {
        CloseHandle(h);
        return "";
    }
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    if (size == 0) {
        CloseHandle(token);
        CloseHandle(h);
        return "";
    }
    std::vector<unsigned char> buffer(size);
    if (!GetTokenInformation(token, TokenUser, buffer.data(), size, &size)) {
        CloseHandle(token);
        CloseHandle(h);
        return "";
    }
    auto* tu = reinterpret_cast<TOKEN_USER*>(buffer.data());
    char name[256];
    char domain[256];
    DWORD nameSize = sizeof(name);
    DWORD domainSize = sizeof(domain);
    SID_NAME_USE sidType;
    std::string result;
    if (LookupAccountSidA(nullptr, tu->User.Sid, name, &nameSize, domain, &domainSize, &sidType)) {
        result = std::string(domain) + "\\" + std::string(name);
    }
    CloseHandle(token);
    CloseHandle(h);
    return result;
#endif
}

std::string ProcessService::getProcessWorkingDirectory(int32_t pid) {
#ifndef _WIN32
    return havel::ProcessManager::getProcessWorkingDirectory(pid);
#else
    std::string exe = getProcessExePathWin(pid);
    if (exe.empty()) return "";
    const auto pos = exe.find_last_of("\\/");
    if (pos == std::string::npos) return "";
    return exe.substr(0, pos);
#endif
}

std::string ProcessService::getProcessExecutablePath(int32_t pid) {
#ifndef _WIN32
    return havel::ProcessManager::getProcessExecutablePath(pid);
#else
    return getProcessExePathWin(pid);
#endif
}

int32_t ProcessService::getCurrentPid() {
#ifndef _WIN32
    return havel::ProcessManager::getCurrentPid();
#else
    return static_cast<int32_t>(GetCurrentProcessId());
#endif
}

int32_t ProcessService::getParentPid() {
#ifndef _WIN32
    return havel::ProcessManager::getParentPid();
#else
    const DWORD current = GetCurrentProcessId();
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32 pe{};
    pe.dwSize = sizeof(pe);
    int32_t ppid = 0;
    if (Process32First(snapshot, &pe)) {
        do {
            if (pe.th32ProcessID == current) {
                ppid = static_cast<int32_t>(pe.th32ParentProcessID);
                break;
            }
        } while (Process32Next(snapshot, &pe));
    }
    CloseHandle(snapshot);
    return ppid;
#endif
}

} // namespace havel::host
