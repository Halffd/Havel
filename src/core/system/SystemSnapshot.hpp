/*
 * SystemSnapshot.hpp
 * 
 * System snapshot structure for Havel language.
 * Provides a unified view of system information.
 */
#pragma once

#include "CpuInfo.hpp"
#include "MemoryInfo.hpp"
#include "OSInfo.hpp"
#include "Temperature.hpp"

namespace havel {

struct SystemSnapshot {
    CpuInfo cpu;
    MemoryInfo memory;
    OSInfo os;
    Temperature temperature;
    
    // Refresh all system information
    void refresh() {
        // Information is fetched on-demand, no caching needed
    }
};

} // namespace havel
