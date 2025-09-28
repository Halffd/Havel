#include "ProcessManager.hpp"
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <dirent.h>
#include <pwd.h>
#include <cstring>
#include <thread>
#include <algorithm>
#include <mutex>

namespace havel {

// Initialize static member
std::unordered_map<int32_t, ProcessManager::CpuSample> ProcessManager::cpu_samples_ = {};

// Helper functions
std::string ProcessManager::readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string ProcessManager::readFirstLine(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return "";
    }
    std::string line;
    std::getline(file, line);
    return line;
}

std::string ProcessManager::getUserName(uid_t uid) {
    struct passwd* pw = getpwuid(uid);
    if (pw) {
        return pw->pw_name;
    }
    return std::to_string(uid);
}

std::vector<std::string> ProcessManager::splitStatLine(const std::string& line) {
    std::vector<std::string> fields;

// Find the command field (enclosed in parentheses)
size_t start = line.find('(');
size_t end = line.rfind(')');

if (start == std::string::npos || end == std::string::npos || start >= end) {
    return fields;
}

// Add PID (before first parenthesis)
std::istringstream pre_iss(line.substr(0, start));
std::string token;
while (pre_iss >> token) {
    fields.push_back(token);
}

// Add command (between parentheses)
fields.push_back(line.substr(start + 1, end - start - 1));

// Add remaining fields (after closing parenthesis)
std::istringstream post_iss(line.substr(end + 1));
while (post_iss >> token) {
    fields.push_back(token);
}

return fields;

}

bool ProcessManager::parseProcStat(int32_t pid, ProcessInfo& info) {
    std::string stat_path = "/proc/" + std::to_string(pid) + "/stat";
    std::ifstream stat_file(stat_path);
    if (!stat_file.is_open()) {
        return false;
    }

std::string line;
if (!std::getline(stat_file, line)) {
    return false;
}

auto fields = splitStatLine(line);
if (fields.size() < 4) {
    return false;
}

try {
    info.pid = std::stoi(fields[0]);
    info.name = fields[1];
    info.ppid = std::stoi(fields[3]);
    return true;
} catch (const std::exception&) {
    return false;
}

}

uint64_t ProcessManager::readProcessCpuTime(int32_t pid) {
    std::string stat_path = "/proc/" + std::to_string(pid) + "/stat";
    std::ifstream stat_file(stat_path);
    if (!stat_file.is_open()) {
        return 0;
    }

std::string line;
if (!std::getline(stat_file, line)) {
    return 0;
}

auto fields = splitStatLine(line);
if (fields.size() < 15) {
    return 0;
}

try {
    // utime (field 13) + stime (field 14) in our 0-indexed array
    return std::stoull(fields[13]) + std::stoull(fields[14]);
} catch (const std::exception&) {
    return 0;
}

}

// Core functionality
bool ProcessManager::isProcessAlive(pid_t pid) {
    if (pid <= 0) return false;
    return kill(pid, 0) == 0;
}

bool ProcessManager::sendSignal(pid_t pid, int signal) {
    if (pid <= 0) {
        return false;
    }
    return kill(pid, signal) == 0;
}

ProcessManager::ProcessState ProcessManager::getProcessState(pid_t pid) {
    if (pid <= 0) return ProcessState::NOT_FOUND;

std::ifstream stat_file("/proc/" + std::to_string(pid) + "/stat");
if (!stat_file.is_open()) {
    return isProcessAlive(pid) ? ProcessState::NO_PERMISSION : ProcessState::NOT_FOUND;
}

std::string line;
if (!std::getline(stat_file, line)) {
    return ProcessState::NOT_FOUND;
}

auto fields = splitStatLine(line);
if (fields.size() < 3) {
    return ProcessState::NOT_FOUND;
}

// State is field 2 (0-indexed)
if (fields[2].empty()) return ProcessState::NOT_FOUND;

switch (fields[2][0]) {
    case 'R': return ProcessState::RUNNING;
    case 'S': case 'D': case 'I': return ProcessState::SLEEPING;
    case 'Z': return ProcessState::ZOMBIE;
    case 'T': case 't': return ProcessState::STOPPED;
    default: return ProcessState::NOT_FOUND;
}

}

bool ProcessManager::terminateProcess(pid_t pid, int timeout_ms) {
    if (!isProcessAlive(pid)) return true;

// Try SIGTERM first
if (!sendSignal(pid, SIGTERM)) {
    return false;
}

// Wait for graceful shutdown
for (int i = 0; i < timeout_ms / 100; ++i) {
    if (!isProcessAlive(pid)) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

// Force kill if still alive
return sendSignal(pid, SIGKILL);

}

bool ProcessManager::isZombie(pid_t pid) {
    return getProcessState(pid) == ProcessState::ZOMBIE;
}

bool ProcessManager::getExitStatus(pid_t pid, int& exit_status) {
    int status;
    pid_t result = waitpid(pid, &status, WNOHANG);

if (result == pid) {
    exit_status = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return true;
}
return false;

}

// Process enumeration
std::vector<ProcessManager::ProcessInfo> ProcessManager::listProcesses() {
    std::vector<ProcessInfo> processes;

DIR* dir = opendir("/proc");
if (!dir) {
    return processes;
}

struct dirent* entry;
while ((entry = readdir(dir)) != nullptr) {
    if (entry->d_type != DT_DIR) {
        continue;
    }

    char* endptr;
    long pid = strtol(entry->d_name, &endptr, 10);
    if (*endptr != '\0' || pid <= 0) {
        continue;
    }

    auto info = getProcessInfo(static_cast<int32_t>(pid));
    if (info) {
        processes.push_back(*info);
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

// Read basic process info from /proc/[pid]/stat
if (!parseProcStat(pid, info)) {
    return std::nullopt;
}

// Get additional info
info.command = getProcessCommand(pid);
info.user = getProcessUser(pid);
info.cpu_usage = getCpuUsage(pid);
info.memory_usage = getMemoryUsage(pid);
info.start_time = getProcessStartTime(pid);

return info;

}

// Process metrics
double ProcessManager::getCpuUsage(int32_t pid) {
    static std::mutex cpu_mutex;
    auto now = std::chrono::steady_clock::now();

    uint64_t current_time = readProcessCpuTime(pid);
    if (current_time == 0) return -1.0;

    // Protect access to the static cpu_samples_ map
    std::lock_guard<std::mutex> lock(cpu_mutex);
    auto it = cpu_samples_.find(pid);
    if (it == cpu_samples_.end()) {
        cpu_samples_[pid] = {current_time, now};
        return 0.0;
    }

    auto& prev_sample = it->second;
    auto time_delta = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - prev_sample.timestamp).count();

    if (time_delta < 100) {
        return -1.0;
    }

    uint64_t cpu_delta = current_time - prev_sample.total_time;
    prev_sample = {current_time, now};

    long hz = sysconf(_SC_CLK_TCK);
    double cpu_percent = (100.0 * cpu_delta * 1000.0) / (time_delta * hz);

    return std::min(cpu_percent, 100.0);

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
            return kb * 1024; // Convert to bytes
        }
        break;
    }
}

return 0;

}

int32_t ProcessManager::getThreadCount(int32_t pid) {
    std::string status_path = "/proc/" + std::to_string(pid) + "/status";
    std::ifstream status_file(status_path);
    if (!status_file.is_open()) {
        return -1;
    }

std::string line;
while (std::getline(status_file, line)) {
    if (line.compare(0, 8, "Threads:") == 0) {
        std::istringstream iss(line.substr(8));
        int32_t threads;
        if (iss >> threads) {
            return threads;
        }
        break;
    }
}
return -1;

}

// Process properties
std::string ProcessManager::getProcessName(int32_t pid) {
    std::string comm_path = "/proc/" + std::to_string(pid) + "/comm";
    std::string name = readFirstLine(comm_path);

// Remove trailing newline if present
if (!name.empty() && name.back() == '\n') {
    name.pop_back();
}

return name;

}
std::string ProcessManager::getProcessUser(int32_t pid) {
    std::string status_path = "/proc/" + std::to_string(pid) + "/status";
    std::ifstream status_file(status_path);
    if (!status_file.is_open()) {
        return "";
    }
    
    std::string line;
    while (std::getline(status_file, line)) {
        if (line.compare(0, 4, "Uid:") == 0) {
            std::istringstream iss(line.substr(4));
            uid_t uid;
            if (iss >> uid) {
                return getUserName(uid);
            }
            break;
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
        // Remove trailing space if present
        if (!cmdline.empty() && cmdline.back() == ' ') {
            cmdline.pop_back();
        }
    }
    return cmdline;
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

std::chrono::system_clock::time_point ProcessManager::getProcessStartTime(int32_t pid) {
    std::string stat_path = "/proc/" + std::to_string(pid) + "/stat";
    std::ifstream stat_file(stat_path);
    if (!stat_file.is_open()) {
        return std::chrono::system_clock::time_point{};
    }

    std::string line;
    if (!std::getline(stat_file, line)) {
        return std::chrono::system_clock::time_point{};
    }

    auto fields = splitStatLine(line);
    if (fields.size() < 22) {
        return std::chrono::system_clock::time_point{};
    }
    
    try {
        // starttime is field 21 (0-indexed)
        unsigned long long starttime = std::stoull(fields[21]);
        
        // Get system boot time
        std::ifstream stat_file("/proc/stat");
        if (!stat_file.is_open()) {
            return std::chrono::system_clock::time_point{};
        }
        
        std::string stat_line;
        unsigned long long btime = 0;
        while (std::getline(stat_file, stat_line)) {
            if (stat_line.compare(0, 5, "btime") == 0) {
                std::istringstream iss(stat_line.substr(5));
                iss >> btime;
                break;
            }
        }
        
        if (btime == 0) {
            return std::chrono::system_clock::time_point{};
        }
        
        // Calculate process start time
        long hz = sysconf(_SC_CLK_TCK);
        time_t start_time_sec = btime + (starttime / hz);
        
        return std::chrono::system_clock::from_time_t(start_time_sec);
        
    } catch (const std::exception&) {
        return std::chrono::system_clock::time_point{};
    }
}

} // namespace havel