/*
 * CpuInfo.cpp
 * 
 * CPU information utilities for Havel language.
 * Implementation using /proc filesystem (Linux).
 */
#include "CpuInfo.hpp"
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <regex>

namespace havel {

int CpuInfo::cores() {
    return static_cast<int>(sysconf(_SC_NPROCESSORS_CONF));
}

int CpuInfo::threads() {
    return static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN));
}

std::string CpuInfo::name() {
    std::ifstream cpuinfo("/proc/cpuinfo");
    std::string line;
    
    while (std::getline(cpuinfo, line)) {
        if (line.find("model name") == 0 || line.find("model name") == std::string::npos) {
            size_t pos = line.find(':');
            if (pos != std::string::npos) {
                std::string name = line.substr(pos + 1);
                // Trim leading whitespace
                size_t start = name.find_first_not_of(" \t");
                if (start != std::string::npos) {
                    return name.substr(start);
                }
            }
        }
    }
    
    return "Unknown";
}

double CpuInfo::frequency() {
    std::ifstream cpuinfo("/proc/cpuinfo");
    std::string line;
    
    while (std::getline(cpuinfo, line)) {
        if (line.find("cpu MHz") == 0) {
            size_t pos = line.find(':');
            if (pos != std::string::npos) {
                try {
                    return std::stod(line.substr(pos + 1));
                } catch (...) {
                    // Continue to next line
                }
            }
        }
    }
    
    return 0.0;
}

double CpuInfo::usage() {
    // Read /proc/stat for CPU usage
    std::ifstream stat("/proc/stat");
    std::string line;
    
    if (std::getline(stat, line) && line.find("cpu ") == 0) {
        std::istringstream iss(line);
        std::string cpu;
        unsigned long long user, nice, system, idle, iowait, irq, softirq;
        
        iss >> cpu >> user >> nice >> system >> idle >> iowait >> irq >> softirq;
        
        unsigned long long total = user + nice + system + idle + iowait + irq + softirq;
        unsigned long long idleTotal = idle + iowait;
        
        // For a more accurate reading, we'd need to compare two samples
        // This is a simplified version
        if (total > 0) {
            return 100.0 * (1.0 - (double)idleTotal / total);
        }
    }
    
    return 0.0;
}

std::vector<double> CpuInfo::usagePerCore() {
    std::vector<double> usage;
    std::ifstream stat("/proc/stat");
    std::string line;
    
    // Skip the aggregate "cpu" line
    std::getline(stat, line);
    
    while (std::getline(stat, line)) {
        if (line.find("cpu") == 0 && std::isdigit(line[3])) {
            std::istringstream iss(line);
            std::string cpu;
            unsigned long long user, nice, system, idle, iowait, irq, softirq;
            
            iss >> cpu >> user >> nice >> system >> idle >> iowait >> irq >> softirq;
            
            unsigned long long total = user + nice + system + idle + iowait + irq + softirq;
            unsigned long long idleTotal = idle + iowait;
            
            if (total > 0) {
                usage.push_back(100.0 * (1.0 - (double)idleTotal / total));
            } else {
                usage.push_back(0.0);
            }
        } else {
            break;  // No more cpu lines
        }
    }
    
    return usage;
}

} // namespace havel
