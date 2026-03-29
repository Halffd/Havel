/*
 * HardwareDetector.hpp - System and hardware detection
 * 
 * Provides system information:
 * - OS detection (Windows, macOS, Linux, BSD)
 * - Window manager / desktop environment detection
 * - Display server (X11/Wayland)
 * - Hardware info (CPU, GPU, RAM, storage)
 */
#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace havel {

class HardwareDetector {
public:
    // System information structure
    struct SystemInfo {
        std::string os;                  // Windows, MacOS, Linux, BSD, Unknown
        std::string osVersion;           // OS version string
        std::string kernel;              // Kernel version
        std::string hostname;            // System hostname
        std::string shell;               // User's shell
        std::string user;                // Username
        std::string home;                // Home directory
        
        // Linux-specific
        std::string displayProtocol;     // X11 or Wayland
        std::string display;             // DISPLAY env var
        std::string windowManager;       // Window manager name
        std::string desktopEnv;          // Desktop environment
    };
    
    // Hardware information structure
    struct HardwareInfo {
        std::string cpu;                 // CPU model name
        int cpuCores;                    // Number of CPU cores
        int cpuThreads;                  // Number of CPU threads
        std::string gpu;                 // GPU model name
        int64_t ram;                     // RAM in MB
        std::string motherboard;         // Motherboard info
        std::string bios;                // BIOS version
        
        // Storage devices
        struct StorageDevice {
            std::string name;            // Device name
            std::string model;           // Model identifier
            int64_t size;                // Size in GB
            std::string type;            // HDD, SSD, NVMe
            std::string mountPoint;      // Mount point
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
     * Get OS name
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
     * Get total RAM in MB
     * @return RAM size in MB
     */
    static int64_t getTotalRAM() noexcept;
    
    /**
     * Get CPU model name
     * @return CPU model string
     */
    static std::string getCPU() noexcept;
    
    /**
     * Get GPU model name
     * @return GPU model string
     */
    static std::string getGPU() noexcept;
    
    /**
     * Get number of CPU cores
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
