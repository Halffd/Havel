/*
 * OSInfo.cpp
 * 
 * Operating system information utilities for Havel language.
 * Implementation using system calls and /proc filesystem (Linux).
 */
#include "OSInfo.hpp"
#include <unistd.h>
#include <sys/utsname.h>
#include <fstream>
#include <sstream>

namespace havel {

std::string OSInfo::name() {
    struct utsname buffer;
    if (uname(&buffer) == 0) {
        return std::string(buffer.sysname);
    }
    return "Unknown";
}

std::string OSInfo::distro() {
    std::ifstream osRelease("/etc/os-release");
    std::string line;
    std::string name, version;
    
    while (std::getline(osRelease, line)) {
        if (line.find("PRETTY_NAME=") == 0) {
            // Extract value between quotes
            size_t start = line.find('"');
            size_t end = line.rfind('"');
            if (start != std::string::npos && end > start) {
                return line.substr(start + 1, end - start - 1);
            }
        }
    }
    
    // Fallback to NAME and VERSION
    osRelease.clear();
    osRelease.seekg(0);
    
    while (std::getline(osRelease, line)) {
        if (line.find("NAME=") == 0) {
            size_t pos = line.find('=');
            if (pos != std::string::npos) {
                name = line.substr(pos + 1);
                // Remove quotes
                if (!name.empty() && name[0] == '"') name = name.substr(1);
                if (!name.empty() && name.back() == '"') name.pop_back();
            }
        } else if (line.find("VERSION=") == 0) {
            size_t pos = line.find('=');
            if (pos != std::string::npos) {
                version = line.substr(pos + 1);
                // Remove quotes
                if (!version.empty() && version[0] == '"') version = version.substr(1);
                if (!version.empty() && version.back() == '"') version.pop_back();
            }
        }
    }
    
    if (!name.empty()) {
        return version.empty() ? name : name + " " + version;
    }
    
    return "Unknown";
}

std::string OSInfo::kernel() {
    struct utsname buffer;
    if (uname(&buffer) == 0) {
        return std::string(buffer.release);
    }
    return "Unknown";
}

std::string OSInfo::hostname() {
    char buffer[256];
    if (gethostname(buffer, sizeof(buffer)) == 0) {
        return std::string(buffer);
    }
    return "Unknown";
}

std::string OSInfo::arch() {
    struct utsname buffer;
    if (uname(&buffer) == 0) {
        return std::string(buffer.machine);
    }
    return "Unknown";
}

double OSInfo::uptime() {
    std::ifstream procUptime("/proc/uptime");
    double uptime = 0.0;
    
    if (procUptime >> uptime) {
        return uptime;
    }
    
    return 0.0;
}

} // namespace havel
