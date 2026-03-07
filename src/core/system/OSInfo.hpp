/*
 * OSInfo.hpp
 * 
 * Operating system information utilities for Havel language.
 * Provides OS name, distro, kernel, hostname, architecture, uptime.
 */
#pragma once

#include <string>

namespace havel {

struct OSInfo {
    // Get OS name (e.g., "Linux")
    static std::string name();
    
    // Get distribution name (e.g., "Ubuntu 22.04")
    static std::string distro();
    
    // Get kernel version
    static std::string kernel();
    
    // Get hostname
    static std::string hostname();
    
    // Get architecture (e.g., "x86_64")
    static std::string arch();
    
    // Get system uptime in seconds
    static double uptime();
};

} // namespace havel
