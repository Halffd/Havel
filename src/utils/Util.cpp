#include "./Util.hpp"
#include "./Logger.hpp"
#include <limits.h>
#ifdef _WIN32
#include <windows.h>
#endif
namespace havel {
void printStackTrace(int len) {
#ifndef _WIN32
    havel::debug("----------------");
    std::vector<void *> callstack(len);
    int frames = backtrace(callstack.data(), len);
    char **strs = backtrace_symbols(callstack.data(), frames);

    if (strs == nullptr) {
        havel::error("Failed to get backtrace symbols");
        havel::debug("----------------");
        return;
    }

    for (int i = 0; i < frames; ++i) {
        havel::debug(" {}: {}", i, strs[i]);
    }
    havel::debug("----------------");
    free(strs);
#else
    (void)len;
    havel::debug("Stack trace not supported on Windows");
#endif
}

std::string ToLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), 
                  [](unsigned char c) { return std::tolower(c); });
    return result;
}

std::string ToUpper(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), 
                  [](unsigned char c) { return std::toupper(c); });
    return result;
}

void Trim(std::string& str) {
    // Trim leading spaces
    str.erase(str.begin(), 
              std::find_if(str.begin(), str.end(), 
                          [](unsigned char ch) { return !std::isspace(ch); }));
    
    // Trim trailing spaces
    str.erase(std::find_if(str.rbegin(), str.rend(), 
                          [](unsigned char ch) { return !std::isspace(ch); }).base(), 
              str.end());
}

std::string TrimCopy(const std::string& str) {
    std::string result = str;
    Trim(result);
    return result;
}

void RemoveChars(std::string& str, const std::string& chars) {
    str.erase(std::remove_if(str.begin(), str.end(),
                            [&chars](char c) { return chars.find(c) != std::string::npos; }),
              str.end());
}

std::string GetExecutablePath() {
#ifdef __linux__
    char result[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
    return std::string(result, (count > 0) ? count : 0);
#elif defined(_WIN32)
    char result[MAX_PATH];
    DWORD count = GetModuleFileNameA(NULL, result, MAX_PATH);
    return std::string(result, (count > 0) ? count : 0);
#else
    return "";
#endif
}

std::string GetExecutableDir() {
    return std::filesystem::path(GetExecutablePath()).parent_path().string();
}

std::string GetCurrentDir() {
    return std::filesystem::current_path().string();
}

bool IsElevated() {
#ifdef __linux__
    return geteuid() == 0;
#else
    return false;
#endif
}

void ElevateProcess() {
#ifdef __linux__
    if (!IsElevated()) {
        // Relaunch with sudo
        std::string path = GetExecutablePath();
        execl("/usr/bin/sudo", "sudo", path.c_str(), nullptr);
    }
#endif
}

void SetProcessPriority(int priority) {
#ifdef __linux__
    nice(priority);
#endif
}
} // namespace havel
