/*
 * MemoryInfo.cpp
 * 
 * Memory information utilities for Havel language.
 * Implementation using /proc/meminfo (Linux).
 */
#include "MemoryInfo.hpp"
#include <fstream>
#include <string>
#include <regex>

namespace havel {

static uint64_t parseMeminfoValue(const std::string& label) {
    std::ifstream meminfo("/proc/meminfo");
    std::string line;
    
    while (std::getline(meminfo, line)) {
        if (line.find(label) == 0) {
            size_t pos = line.find(':');
            if (pos != std::string::npos) {
                try {
                    std::string value = line.substr(pos + 1);
                    // Remove "kB" suffix and convert to bytes
                    size_t kbPos = value.find("kB");
                    if (kbPos != std::string::npos) {
                        value = value.substr(0, kbPos);
                    }
                    // Trim whitespace
                    size_t start = value.find_first_not_of(" \t");
                    if (start != std::string::npos) {
                        uint64_t kb = std::stoull(value.substr(start));
                        return kb * 1024;  // Convert kB to bytes
                    }
                } catch (...) {
                    // Return 0 on error
                }
            }
        }
    }
    
    return 0;
}

uint64_t MemoryInfo::total() {
    return parseMeminfoValue("MemTotal");
}

uint64_t MemoryInfo::used() {
    uint64_t total = parseMeminfoValue("MemTotal");
    uint64_t free = parseMeminfoValue("MemFree");
    uint64_t buffers = parseMeminfoValue("Buffers");
    uint64_t cached = parseMeminfoValue("Cached");
    
    // Used = Total - Free - Buffers - Cached
    return total - free - buffers - cached;
}

uint64_t MemoryInfo::free() {
    return parseMeminfoValue("MemFree");
}

uint64_t MemoryInfo::swapTotal() {
    return parseMeminfoValue("SwapTotal");
}

uint64_t MemoryInfo::swapUsed() {
    uint64_t total = parseMeminfoValue("SwapTotal");
    uint64_t free = parseMeminfoValue("SwapFree");
    return total - free;
}

uint64_t MemoryInfo::swapFree() {
    return parseMeminfoValue("SwapFree");
}

} // namespace havel
