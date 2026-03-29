/*
 * HardwareDetector.cpp - System and hardware detection
 */
#include "HardwareDetector.hpp"
#include "DisplayManager.hpp"
#include "../window/WindowManagerDetector.hpp"
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <algorithm>
#include <cctype>
#include <array>

#ifdef __linux__
#include <unistd.h>
#include <sys/utsname.h>
#endif

namespace havel {

// Helper to run shell command safely
static std::string runCommand(const char* cmd) {
    FILE* pipe = popen(cmd, "r");
    if (!pipe) return "Unknown";

    char buffer[256];
    std::string result;

    if (fgets(buffer, sizeof(buffer), pipe)) {
        result = buffer;
    }

    pclose(pipe);

    // Trim whitespace
    result.erase(result.find_last_not_of(" \n\r\t") + 1);
    result.erase(0, result.find_first_not_of(" \n\r\t"));

    return result.empty() ? "Unknown" : result;
}

// Helper to trim whitespace
static std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, last - first + 1);
}

// Helper to convert string to lowercase
static std::string toLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

HardwareDetector::SystemInfo HardwareDetector::detectSystem() noexcept {
    SystemInfo info;
    
    // Detect OS
#ifdef _WIN32
    info.os = "Windows";
#elif defined(__APPLE__)
    info.os = "MacOS";
#elif defined(__linux__)
    info.os = "Linux";
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    info.os = "BSD";
#else
    info.os = "Unknown";
#endif
    
    // Get environment variables
    const char* shell = std::getenv("SHELL");
    if (shell) info.shell = shell;
    
    const char* user = std::getenv("USER");
    if (!user) user = std::getenv("LOGNAME");
    if (user) info.user = user;
    
    const char* home = std::getenv("HOME");
    if (home) info.home = home;
    
    const char* hostname = std::getenv("HOSTNAME");
    if (hostname) info.hostname = hostname;
    
#ifdef __linux__
    // Get kernel version
    struct utsname uts;
    if (uname(&uts) == 0) {
        info.kernel = uts.release;
        info.osVersion = uts.version;
    }
    
    // Detect display server
    const char* xdgSession = std::getenv("XDG_SESSION_TYPE");
    if (xdgSession) {
        info.displayProtocol = xdgSession;
        // Normalize to X11/Wayland
        std::string sessionLower = toLower(xdgSession);
        if (sessionLower.find("wayland") != std::string::npos) {
            info.displayProtocol = "Wayland";
        } else if (sessionLower.find("x11") != std::string::npos || 
                   sessionLower.find("xorg") != std::string::npos) {
            info.displayProtocol = "X11";
        }
    } else {
        // Fallback: check DISPLAY
        const char* display = std::getenv("DISPLAY");
        if (display) {
            info.displayProtocol = "X11";
        } else {
            info.displayProtocol = "Unknown";
        }
    }
    
    // Get DISPLAY
    const char* display = std::getenv("DISPLAY");
    if (display) info.display = display;
    
    // Detect window manager using WindowManagerDetector
    info.windowManager = WindowManagerDetector::GetWMName();
    info.desktopEnv = info.windowManager;
    
    // Get desktop environment from XDG
    const char* desktopEnv = std::getenv("XDG_CURRENT_DESKTOP");
    if (desktopEnv) {
        info.desktopEnv = desktopEnv;
    }
    
    // Get session name
    const char* sessionName = std::getenv("XDG_SESSION_DESKTOP");
    if (sessionName) {
        info.osVersion = sessionName;
    }
#endif
    
    return info;
}

HardwareDetector::HardwareInfo HardwareDetector::detectHardware() noexcept {
    HardwareInfo info;
    
#ifdef __linux__
    // CPU info
    std::ifstream cpuinfo("/proc/cpuinfo");
    std::string line;
    int coreCount = 0;
    int threadCount = 0;
    
    while (std::getline(cpuinfo, line)) {
        if (line.find("model name") != std::string::npos) {
            size_t pos = line.find(":");
            if (pos != std::string::npos && info.cpu.empty()) {
                info.cpu = trim(line.substr(pos + 1));
            }
            coreCount++;
        }
        if (line.find("siblings") != std::string::npos) {
            size_t pos = line.find(":");
            if (pos != std::string::npos) {
                try {
                    threadCount = std::stoi(trim(line.substr(pos + 1)));
                } catch (...) {}
            }
        }
        if (line.find("cpu cores") != std::string::npos) {
            size_t pos = line.find(":");
            if (pos != std::string::npos) {
                try {
                    coreCount = std::stoi(trim(line.substr(pos + 1)));
                } catch (...) {}
            }
        }
    }
    cpuinfo.close();
    
    if (info.cpu.empty()) info.cpu = "Unknown";
    info.cpuCores = coreCount > 0 ? coreCount : 1;
    info.cpuThreads = threadCount > 0 ? threadCount : info.cpuCores;
    
    // GPU info using helper
    info.gpu = runCommand("lspci 2>/dev/null | grep -i 'vga\\|3d\\|display' | head -1 | cut -d':' -f3-");
    
    // RAM info in bytes
    std::ifstream meminfo("/proc/meminfo");
    while (std::getline(meminfo, line)) {
        if (line.find("MemTotal") != std::string::npos) {
            size_t pos = line.find(":");
            if (pos != std::string::npos) {
                std::string value = trim(line.substr(pos + 1));
                // Value is in KB, convert to bytes
                try {
                    info.ram = std::stoll(value) * 1024;  // KB -> bytes
                } catch (...) {}
            }
            break;
        }
    }
    meminfo.close();
    
    // Storage info in bytes
    FILE* pipe = popen("lsblk -nd -o NAME,MODEL,SIZE,TYPE,MOUNTPOINT -b 2>/dev/null", "r");
    if (pipe) {
        char buffer[1024];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            std::string line = trim(buffer);
            if (line.empty()) continue;
            
            std::istringstream iss(line);
            std::string name, model, size, type, mount;
            iss >> name >> model >> size >> type >> mount;
            
            // Only include disk types
            if (type == "disk") {
                HardwareInfo::StorageDevice device;
                device.name = name;
                device.model = model;
                
                // Parse size (in bytes from lsblk -b)
                try {
                    int64_t bytes = std::stoll(size);
                    device.size = bytes / (1024 * 1024 * 1024);  // Convert to GB
                } catch (...) {
                    device.size = 0;
                }
                
                // Determine type
                if (name.find("nvme") != std::string::npos) {
                    device.type = "NVMe";
                } else if (name.find("sd") != std::string::npos) {
                    device.type = "SSD";  // Could be HDD too
                } else {
                    device.type = "Unknown";
                }
                
                device.mountPoint = mount;
                info.storage.push_back(device);
            }
        }
        pclose(pipe);
    }
    
    // Motherboard info
    std::ifstream boardVendor("/sys/class/dmi/id/board_vendor");
    std::ifstream boardName("/sys/class/dmi/id/board_name");
    if (boardVendor && boardName) {
        std::string vendor, name;
        std::getline(boardVendor, vendor);
        std::getline(boardName, name);
        if (!vendor.empty() && !name.empty()) {
            info.motherboard = trim(vendor) + " " + trim(name);
        }
    }
    if (info.motherboard.empty()) info.motherboard = "Unknown";
    
    // BIOS info
    std::ifstream biosVersion("/sys/class/dmi/id/bios_version");
    if (biosVersion) {
        std::getline(biosVersion, info.bios);
        info.bios = trim(info.bios);
    }
    if (info.bios.empty()) info.bios = "Unknown";
    
#else
    // Non-Linux fallbacks
    info.cpu = "Unknown";
    info.cpuCores = 1;
    info.cpuThreads = 1;
    info.gpu = "Unknown";
    info.ram = 0;
    info.motherboard = "Unknown";
    info.bios = "Unknown";
#endif
    
    return info;
}

std::string HardwareDetector::getOS() noexcept {
    return detectSystem().os;
}

bool HardwareDetector::isLinux() noexcept {
#ifdef __linux__
    return true;
#else
    return false;
#endif
}

bool HardwareDetector::isWindows() noexcept {
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}

bool HardwareDetector::isMacOS() noexcept {
#ifdef __APPLE__
    return true;
#else
    return false;
#endif
}

bool HardwareDetector::isBSD() noexcept {
#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    return true;
#else
    return false;
#endif
}

bool HardwareDetector::isWayland() noexcept {
#ifdef __linux__
    const char* xdgSession = std::getenv("XDG_SESSION_TYPE");
    if (xdgSession) {
        std::string session = toLower(xdgSession);
        return session.find("wayland") != std::string::npos;
    }
#endif
    return false;
}

bool HardwareDetector::isX11() noexcept {
    return !isWayland();
}

std::string HardwareDetector::getWindowManager() noexcept {
#ifdef __linux__
    return WindowManagerDetector::GetWMName();
#else
    return "Unknown";
#endif
}

int64_t HardwareDetector::getTotalRAM() noexcept {
    return detectHardware().ram;
}

std::string HardwareDetector::getCPU() noexcept {
    return detectHardware().cpu;
}

std::string HardwareDetector::getGPU() noexcept {
    return detectHardware().gpu;
}

int HardwareDetector::getCPUCores() noexcept {
    return detectHardware().cpuCores;
}

std::vector<HardwareDetector::HardwareInfo::StorageDevice> HardwareDetector::getStorage() noexcept {
    return detectHardware().storage;
}

} // namespace havel
