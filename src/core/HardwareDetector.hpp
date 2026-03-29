/*
 * HardwareDetector.hpp - System and hardware detection
 * 
 * Uses existing system info classes (CpuInfo, MemoryInfo, OSInfo, Temperature)
 * and adds additional detection (GPU, storage, window manager, display server).
 */
#pragma once

#include "system/CpuInfo.hpp"
#include "system/MemoryInfo.hpp"
#include "system/OSInfo.hpp"
#include "system/Temperature.hpp"
#include <string>
#include <vector>
#include <cstdint>

namespace havel {

class HardwareDetector {
public:
    // System information structure
    struct SystemInfo {
        std::string os;                  // Windows, MacOS, Linux, BSD, Unknown
        std::string osVersion;           // OS version string (e.g., "Ubuntu 22.04")
        std::string kernel;              // Kernel version
        std::string hostname;            // System hostname
        std::string arch;                // Architecture (e.g., "x86_64")
        std::string shell;               // User's shell
        std::string user;                // Username
        std::string home;                // Home directory
        
        // Linux-specific
        std::string displayProtocol;     // X11 or Wayland
        std::string display;             // DISPLAY env var
        std::string windowManager;       // Window manager name
        std::string desktopEnv;          // Desktop environment
        
        double uptime;                   // System uptime in seconds
    };
    
    // Hardware information structure
    struct HardwareInfo {
        // CPU info (from CpuInfo class)
        std::string cpu;                 // CPU model name
        int cpuCores;                    // Number of CPU cores
        int cpuThreads;                  // Number of CPU threads
        double cpuFrequency;             // CPU frequency in MHz
        double cpuUsage;                 // CPU usage percentage (0-100)
        
        // GPU info
        std::string gpu;                 // GPU model name
        double gpuTemperature;           // GPU temperature in Celsius
        
        // Memory info (from MemoryInfo class, all in bytes)
        uint64_t ramTotal;               // Total RAM in bytes
        uint64_t ramUsed;                // Used RAM in bytes
        uint64_t ramFree;                // Free RAM in bytes
        
        // Swap info (in bytes)
        uint64_t swapTotal;              // Total swap in bytes
        uint64_t swapUsed;               // Used swap in bytes
        uint64_t swapFree;               // Free swap in bytes
        
        // System info
        std::string motherboard;         // Motherboard info
        std::string bios;                // BIOS version
        
        // Temperature (from Temperature class)
        double cpuTemperature;           // CPU temperature in Celsius
        
        // Storage devices
        struct StorageDevice {
            std::string name;            // Device name
            std::string model;           // Model identifier
            uint64_t size;               // Size in bytes
            uint64_t used;               // Used space in bytes
            uint64_t free;               // Free space in bytes
            std::string type;            // HDD, SSD, NVMe
            std::string mountPoint;      // Mount point
            std::string filesystem;      // Filesystem type
        };
        std::vector<StorageDevice> storage;
    };
    
    /**
     * Detect system information
     * @return SystemInfo structure with detected information
     */
    static SystemInfo detectSystem() noexcept;
    
    /**
     * Detect hardware information
     * @return HardwareInfo structure with detected hardware
     */
    static HardwareInfo detectHardware() noexcept;
    
    /**
     * Get OS name (convenience wrapper)
     * @return OS name string
     */
    static std::string getOS() noexcept;
    
    /**
     * Check if running on Linux
     * @return true if Linux
     */
    static bool isLinux() noexcept;
    
    /**
     * Check if running on Windows
     * @return true if Windows
     */
    static bool isWindows() noexcept;
    
    /**
     * Check if running on macOS
     * @return true if macOS
     */
    static bool isMacOS() noexcept;
    
    /**
     * Check if running on BSD
     * @return true if BSD
     */
    static bool isBSD() noexcept;
    
    /**
     * Check if using Wayland
     * @return true if Wayland session
     */
    static bool isWayland() noexcept;
    
    /**
     * Check if using X11
     * @return true if X11 session
     */
    static bool isX11() noexcept;
    
    /**
     * Get window manager name
     * @return Window manager name
     */
    static std::string getWindowManager() noexcept;
    
    /**
     * Get total RAM in bytes (convenience wrapper)
     * @return RAM size in bytes
     */
    static uint64_t getTotalRAM() noexcept;
    
    /**
     * Get CPU model name (convenience wrapper)
     * @return CPU model string
     */
    static std::string getCPU() noexcept;
    
    /**
     * Get GPU model name
     * @return GPU model string
     */
    static std::string getGPU() noexcept;
    
    /**
     * Get number of CPU cores (convenience wrapper)
     * @return Number of physical cores
     */
    static int getCPUCores() noexcept;
    
    /**
     * Get storage information
     * @return Vector of storage devices
     */
    static std::vector<HardwareInfo::StorageDevice> getStorage() noexcept;
};

} // namespace havel
