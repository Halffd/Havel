#include "ProcessManager.hpp"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <pwd.h>
#include <signal.h>
#include <cstring>
#include <chrono>
#include <thread>
#include <cassert>
#include <unordered_map>

namespace fs = std::filesystem;
namespace chrono = std::chrono;

namespace havel {

namespace {
    // Helper function to read a file into a string
    std::string readFile(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            return "";
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    // Helper function to read the first line of a file into a string
    std::string readFirstLine(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            return "";
        }
        std::string line;
        std::getline(file, line);
        return line;
    }

    // Helper function to get username from UID
    std::string getUserName(uid_t uid) {
        struct passwd* pw = getpwuid(uid);
        if (pw) {
            return pw->pw_name;
        }
        return std::to_string(uid);
    }

    // Helper function to parse /proc/[pid]/stat file
    bool parseProcStat(int32_t pid, ProcessManager::ProcessInfo& info) {
        std::string stat_path = "/proc/" + std::to_string(pid) + "/stat";
        std::ifstream stat_file(stat_path);
        if (!stat_file.is_open()) {
            return false;
        }

        std::string line;
        if (!std::getline(stat_file, line)) {
            return false;
        }

        std::istringstream iss(line);
        std::string token;
        int field = 0;

        // PID is field 1
        if (!(iss >> token)) return false;
        info.pid = std::stoi(token);
        
        // Command is field 2 (enclosed in parentheses)
        std::size_t start = line.find('(');
        std::size_t end = line.rfind(')');
        if (start == std::string::npos || end == std::string::npos || start >= end) {
            return false;
        }
        info.name = line.substr(start + 1, end - start - 1);
        
        // Skip to field 4 (ppid)
        for (int i = 3; i <= 4; ++i) {
            if (!(iss >> token)) return false;
            if (i == 4) {
                info.ppid = std::stoi(token);
            }
        }

        return true;
    }
}

std::vector<ProcessManager::ProcessInfo> ProcessManager::listProcesses() {
    std::vector<ProcessInfo> processes;
    
    DIR* dir = opendir("/proc");
    if (!dir) {
        return processes;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        // Check if the directory name is a number (PID)
        if (entry->d_type != DT_DIR) {
            continue;
        }

        char* endptr;
        long pid = strtol(entry->d_name, &endptr, 10);
        if (*endptr != '\0') {
            continue;
        }

        ProcessInfo info;
        if (getProcessInfo(pid)) {
            processes.push_back(*getProcessInfo(pid));
        }
    }

    closedir(dir);
    return processes;
}

std::vector<ProcessManager::ProcessInfo> ProcessManager::findProcesses(const std::string& name) {
    std::vector<ProcessInfo> result;
    auto processes = listProcesses();
    
    for (const auto& process : processes) {
        if (process.name.find(name) != std::string::npos) {
            result.push_back(process);
        }
    }
    
    return result;
}

std::optional<ProcessManager::ProcessInfo> ProcessManager::getProcessInfo(int32_t pid) {
    ProcessInfo info;
    info.pid = pid;
    
    // Read basic process info from /proc/[pid]/stat
    if (!parseProcStat(pid, info)) {
        return std::nullopt;
    }
    
    // Get command line
    info.command = getProcessCommand(pid);
    
    // Get user info
    info.user = getProcessUser(pid);
    
    // Get CPU and memory usage
    info.cpu_usage = getCpuUsage(pid);
    info.memory_usage = getMemoryUsage(pid);
    
    // Get start time
    info.start_time = getProcessStartTime(pid);
    
    return info;
}

bool ProcessManager::killProcess(int32_t pid, bool force) {
    return sendSignal(pid, force ? SIGKILL : SIGTERM);
}

bool ProcessManager::sendSignal(int32_t pid, int signal) {
    if (kill(pid, 0) != 0) {
        return false; // Process doesn't exist or no permission
    }
    return kill(pid, signal) == 0;
}

double ProcessManager::getCpuUsage(int32_t pid) {
    static std::unordered_map<int32_t, std::pair<unsigned long long, unsigned long long>> prev_times;
    
    std::string stat_path = "/proc/" + std::to_string(pid) + "/stat";
    std::ifstream stat_file(stat_path);
    if (!stat_file.is_open()) {
        return -1.0;
    }
    
    std::string line;
    if (!std::getline(stat_file, line)) {
        return -1.0;
    }
    
    std::istringstream iss(line);
    std::string token;
    
    // Skip fields until we get to utime (14), stime (15), cutime (16), cstime (17)
    for (int i = 1; i <= 13; ++i) {
        if (!(iss >> token)) return -1.0;
    }
    
    unsigned long utime, stime, cutime, cstime;
    if (!(iss >> utime >> stime >> cutime >> cstime)) {
        return -1.0;
    }
    
    unsigned long long total_time = utime + stime + cutime + cstime;
    
    // Get system uptime for total time calculation
    std::ifstream uptime_file("/proc/uptime");
    if (!uptime_file.is_open()) {
        return -1.0;
    }
    
    double uptime;
    if (!(uptime_file >> uptime)) {
        return -1.0;
    }
    
    // Convert uptime to clock ticks (usually 100 per second)
    long hz = sysconf(_SC_CLK_TCK);
    unsigned long long total_ticks = static_cast<unsigned long long>(uptime * hz);
    
    // Calculate CPU usage percentage
    double cpu_usage = -1.0;
    
    auto it = prev_times.find(pid);
    if (it != prev_times.end()) {
        unsigned long long prev_total = it->second.first;
        unsigned long long prev_ticks = it->second.second;
        
        if (total_time > prev_total && total_ticks > prev_ticks) {
            cpu_usage = 100.0 * (total_time - prev_total) / (total_ticks - prev_ticks);
        }
    }
    
    // Update previous values
    prev_times[pid] = {total_time, total_ticks};
    
    return cpu_usage;
}

uint64_t ProcessManager::getMemoryUsage(int32_t pid) {
    std::string status_path = "/proc/" + std::to_string(pid) + "/status";
    std::ifstream status_file(status_path);
    if (!status_file.is_open()) {
        return 0;
    }
    
    std::string line;
    while (std::getline(status_file, line)) {
        if (line.compare(0, 6, "VmRSS:") == 0) {
            std::istringstream iss(line.substr(6));
            uint64_t kb;
            if (iss >> kb) {
                return kb; // Returns memory usage in KB
            }
            break;
        }
    }
    
    return 0;
}

bool ProcessManager::isProcessRunning(int32_t pid) {
    return kill(pid, 0) == 0;
}

int32_t ProcessManager::getCurrentPid() {
    return getpid();
}

int32_t ProcessManager::getParentPid() {
    return getppid();
}

std::string ProcessManager::getProcessName(int32_t pid) {
    std::string comm_path = "/proc/" + std::to_string(pid) + "/comm";
    std::string name = readFirstLine(comm_path);
    if (name.empty()) {
        // Fallback to parsing /proc/[pid]/status
        std::string status_path = "/proc/" + std::to_string(pid) + "/status";
        std::ifstream status_file(status_path);
        if (status_file.is_open()) {
            std::string line;
            while (std::getline(status_file, line)) {
                if (line.compare(0, 6, "Name:	") == 0) {
                    return line.substr(6);
                }
            }
        }
    }
    return name;
}

std::string ProcessManager::getProcessUser(int32_t pid) {
    std::string status_path = "/proc/" + std::to_string(pid) + "/status";
    std::ifstream status_file(status_path);
    if (status_file.is_open()) {
        std::string line;
        while (std::getline(status_file, line)) {
            if (line.compare(0, 5, "Uid:	") == 0) {
                std::istringstream iss(line.substr(5));
                int uid;
                if (iss >> uid) {
                    return getUserName(uid);
                }
                break;
            }
        }
    }
    return "";
}

std::string ProcessManager::getProcessCommand(int32_t pid) {
    std::string cmdline_path = "/proc/" + std::to_string(pid) + "/cmdline";
    std::string cmdline = readFile(cmdline_path);
    if (!cmdline.empty()) {
        // Replace null terminators with spaces
        for (char& c : cmdline) {
            if (c == '\0') {
                c = ' ';
            }
        }
        return cmdline;
    }
    return "";
}

std::chrono::system_clock::time_point ProcessManager::getProcessStartTime(int32_t pid) {
    std::string stat_path = "/proc/" + std::to_string(pid) + "/stat";
    std::ifstream stat_file(stat_path);
    if (!stat_file.is_open()) {
        return chrono::system_clock::time_point{};
    }

    std::string line;
    if (!std::getline(stat_file, line)) {
        return chrono::system_clock::time_point{};
    }

    std::istringstream iss(line);
    std::string token;
    
    // Skip fields until we get to starttime (field 22)
    for (int i = 1; i <= 21; ++i) {
        if (!(iss >> token)) {
            return chrono::system_clock::time_point{};
        }
    }
    
    unsigned long long starttime;
    if (!(iss >> starttime)) {
        return chrono::system_clock::time_point{};
    }
    
    // Get system uptime
    std::ifstream uptime_file("/proc/uptime");
    if (!uptime_file.is_open()) {
        return chrono::system_clock::time_point{};
    }
    
    double uptime;
    if (!(uptime_file >> uptime)) {
        return chrono::system_clock::time_point{};
    }
    
    // Calculate process start time
    long hz = sysconf(_SC_CLK_TCK);
    double process_uptime = static_cast<double>(starttime) / hz;
    auto now = chrono::system_clock::now();
    auto start_time = now - chrono::duration_cast<chrono::system_clock::duration>(
        chrono::duration<double>(uptime - process_uptime));
    
    return start_time;
}

int32_t ProcessManager::getThreadCount(int32_t pid) {
    std::string status_path = "/proc/" + std::to_string(pid) + "/status";
    std::ifstream status_file(status_path);
    if (status_file.is_open()) {
        std::string line;
        while (std::getline(status_file, line)) {
            if (line.compare(0, 10, "Threads:\t") == 0) {
                return std::stoi(line.substr(10));
            }
        }
    }
    return -1;
}

std::string ProcessManager::getProcessWorkingDirectory(int32_t pid) {
    std::string path = "/proc/" + std::to_string(pid) + "/cwd";
    char buf[PATH_MAX];
    ssize_t count = readlink(path.c_str(), buf, sizeof(buf) - 1);
    if (count != -1) {
        buf[count] = '\0';
        return std::string(buf);
    }
    return "";
}

std::string ProcessManager::getProcessExecutablePath(int32_t pid) {
    std::string path = "/proc/" + std::to_string(pid) + "/exe";
    char buf[PATH_MAX];
    ssize_t count = readlink(path.c_str(), buf, sizeof(buf) - 1);
    if (count != -1) {
        buf[count] = '\0';
        return std::string(buf);
    }
    return "";
}

} // namespace havel
