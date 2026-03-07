/*
 * CpuInfo.hpp
 * 
 * CPU information utilities for Havel language.
 * Provides CPU cores, threads, usage, name, frequency.
 */
#pragma once

#include <string>
#include <vector>

namespace havel {

struct CpuInfo {
    // Get number of CPU cores
    static int cores();
    
    // Get number of CPU threads (logical cores)
    static int threads();
    
    // Get CPU name/model
    static std::string name();
    
    // Get CPU frequency in MHz
    static double frequency();
    
    // Get overall CPU usage percentage (0-100)
    static double usage();
    
    // Get per-core usage percentages
    static std::vector<double> usagePerCore();
};

} // namespace havel
