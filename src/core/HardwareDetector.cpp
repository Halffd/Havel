/*
 * HardwareDetector.cpp - System and hardware detection
 * 
 * Uses existing system info classes (CpuInfo, MemoryInfo, OSInfo, Temperature)
 * and adds additional detection (GPU, storage, window manager, display server).
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
    
    // Use OSInfo class for basic OS information
    info.os = OSInfo::name();
    info.kernel = OSInfo::kernel();
    info.hostname = OSInfo::hostname();
    info.arch = OSInfo::arch();
    info.osVersion = OSInfo::distro();
    info.uptime = OSInfo::uptime();
    
    // Get environment variables
    const char* shell = std::getenv("SHELL");
    if (shell) info.shell = shell;
    
    const char* user = std::getenv("USER");
    if (!user) user = std::getenv("LOGNAME");
    if (user) info.user = user;
    
    const char* home = std::getenv("HOME");
    if (home) info.home = home;
    
#ifdef __linux__
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
#else
    info.displayProtocol = "Unknown";
#endif
    
    return info;
}

HardwareDetector::HardwareInfo HardwareDetector::detectHardware() noexcept {
    HardwareInfo info;
    
    // Use CpuInfo class for CPU information
    info.cpu = CpuInfo::name();
    info.cpuCores = CpuInfo::cores();
    info.cpuThreads = CpuInfo::threads();
    info.cpuFrequency = CpuInfo::frequency();
    info.cpuUsage = CpuInfo::usage();
    
    // Use MemoryInfo class for memory information (already in bytes)
    info.ramTotal = MemoryInfo::total();
    info.ramUsed = MemoryInfo::used();
    info.ramFree = MemoryInfo::free();
    
    // Swap information (already in bytes)
    info.swapTotal = MemoryInfo::swapTotal();
    info.swapUsed = MemoryInfo::swapUsed();
    info.swapFree = MemoryInfo::swapFree();
    
    // Use Temperature class for temperature readings
    info.cpuTemperature = Temperature::cpu();
    
    // GPU info using helper
    info.gpu = runCommand("lspci 2>/dev/null | grep -i 'vga\\|3d\\|display' | head -1 | cut -d':' -f3-");
    info.gpuTemperature = Temperature::gpu();
    
#ifdef __linux__
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
    info.motherboard = "Unknown";
    info.bios = "Unknown";
#endif
    
    // Storage info
    info.storage = getStorage();
    
    return info;
}

std::string HardwareDetector::getOS() noexcept {
    return OSInfo::name();
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

uint64_t HardwareDetector::getTotalRAM() noexcept {
    return MemoryInfo::total();
}

std::string HardwareDetector::getCPU() noexcept {
    return CpuInfo::name();
}

std::string HardwareDetector::getGPU() noexcept {
    return runCommand("lspci 2>/dev/null | grep -i 'vga\\|3d\\|display' | head -1 | cut -d':' -f3-");
}

int HardwareDetector::getCPUCores() noexcept {
    return CpuInfo::cores();
}

std::vector<HardwareDetector::HardwareInfo::StorageDevice> HardwareDetector::getStorage() noexcept {
    std::vector<HardwareInfo::StorageDevice> storage;
    
#ifdef __linux__
    // Use lsblk for storage information
    FILE* pipe = popen("lsblk -nd -o NAME,MODEL,SIZE,TYPE,MOUNTPOINT,FSTYPE,AVAIL -b 2>/dev/null", "r");
    if (pipe) {
        char buffer[1024];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            std::string line = trim(buffer);
            if (line.empty()) continue;
            
            std::istringstream iss(line);
            std::string name, model, size, type, mount, fstype, avail;
            iss >> name >> model >> size >> type >> mount >> fstype >> avail;
            
            // Only include disk types
            if (type == "disk") {
                HardwareInfo::StorageDevice device;
                device.name = name;
                device.model = model.empty() ? "Unknown" : model;
                
                // Parse size (in bytes from lsblk -b)
                try {
                    device.size = std::stoull(size);
                } catch (...) {
                    device.size = 0;
                }
                
                // Get used/free from df for the mount point
                device.used = 0;
                device.free = 0;
                if (!mount.empty() && mount != "none" && mount != "[SWAP]") {
                    // Parse available space and calculate used
                    try {
                        uint64_t availBytes = std::stoull(avail);
                        device.free = availBytes;
                        device.used = device.size - device.free;
                    } catch (...) {
                        // Keep used/free as 0
                    }
                }
                
                // Determine type
                if (name.find("nvme") != std::string::npos) {
                    device.type = "NVMe";
                } else if (name.find("sd") != std::string::npos) {
                    device.type = "SSD";  // Could be HDD too
                } else {
                    device.type = "Unknown";
                }
                
                device.mountPoint = mount == "none" ? "" : mount;
                device.filesystem = fstype;
                storage.push_back(device);
            }
        }
        pclose(pipe);
    }
#endif
    
    return storage;
}

} // namespace havel
