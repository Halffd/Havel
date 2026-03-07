/*
 * Temperature.cpp
 * 
 * Temperature information utilities for Havel language.
 * Implementation using /sys/class/thermal and /sys/class/hwmon (Linux).
 */
#include "Temperature.hpp"
#include <fstream>
#include <sstream>
#include <dirent.h>
#include <cstring>
#include <regex>

namespace havel {

static double readThermalZone(const std::string& path) {
    std::ifstream tempFile(path + "/temp");
    int temp;
    
    if (tempFile >> temp) {
        // Temperature is in millidegrees Celsius
        return temp / 1000.0;
    }
    
    return -1.0;  // Invalid reading
}

static std::string readSensorName(const std::string& path) {
    std::ifstream nameFile(path + "/name");
    std::string name;
    
    if (std::getline(nameFile, name)) {
        // Trim whitespace
        size_t start = name.find_first_not_of(" \t\n\r");
        size_t end = name.find_last_not_of(" \t\n\r");
        if (start != std::string::npos) {
            return name.substr(start, end - start + 1);
        }
    }
    
    return "Unknown";
}

double Temperature::cpu() {
    // Try thermal zones first
    DIR* dir = opendir("/sys/class/thermal");
    if (dir) {
        struct dirent* entry;
        
        while ((entry = readdir(dir)) != nullptr) {
            std::string name = entry->d_name;
            if (name.find("thermal_zone") == 0) {
                std::string path = std::string("/sys/class/thermal/") + name;
                std::string type = readSensorName(path);
                
                // Look for CPU-related sensors
                if (type.find("cpu") != std::string::npos || 
                    type.find("x86_pkg") != std::string::npos ||
                    type.find("core") != std::string::npos) {
                    double temp = readThermalZone(path);
                    if (temp >= 0) {
                        closedir(dir);
                        return temp;
                    }
                }
            }
        }
        
        closedir(dir);
    }
    
    // Try hwmon
    dir = opendir("/sys/class/hwmon");
    if (dir) {
        struct dirent* entry;
        
        while ((entry = readdir(dir)) != nullptr) {
            std::string name = entry->d_name;
            if (name[0] == 'h' && name[1] == 'w' && name[2] == 'm' && name[3] == 'o' && name[4] == 'n') {
                std::string path = std::string("/sys/class/hwmon/") + name;
                std::string sensorName = readSensorName(path);
                
                if (sensorName.find("coretemp") != std::string::npos ||
                    sensorName.find("k10temp") != std::string::npos ||
                    sensorName.find("cpu") != std::string::npos) {
                    // Try temp1_input
                    std::ifstream tempFile(path + "/temp1_input");
                    int temp;
                    if (tempFile >> temp) {
                        closedir(dir);
                        return temp / 1000.0;
                    }
                }
            }
        }
        
        closedir(dir);
    }
    
    return -1.0;  // No sensor found
}

double Temperature::gpu() {
    // Try NVIDIA GPU
    std::ifstream nvidiaGpu("/sys/class/drm/card0/device/hwmon/hwmon*/temp1_input");
    if (nvidiaGpu.is_open()) {
        int temp;
        if (nvidiaGpu >> temp) {
            return temp / 1000.0;
        }
    }
    
    // Try AMD GPU
    DIR* dir = opendir("/sys/class/drm");
    if (dir) {
        struct dirent* entry;
        
        while ((entry = readdir(dir)) != nullptr) {
            std::string name = entry->d_name;
            if (name.find("card") == 0 && name != "card0") {
                std::string path = std::string("/sys/class/drm/") + name + "/device/hwmon";
                DIR* hwmonDir = opendir(path.c_str());
                if (hwmonDir) {
                    struct dirent* hwmonEntry;
                    while ((hwmonEntry = readdir(hwmonDir)) != nullptr) {
                        std::string hwmonName = hwmonEntry->d_name;
                        if (hwmonName.find("hwmon") == 0) {
                            std::string tempPath = path + "/" + hwmonName + "/temp1_input";
                            std::ifstream tempFile(tempPath);
                            int temp;
                            if (tempFile >> temp) {
                                closedir(hwmonDir);
                                closedir(dir);
                                return temp / 1000.0;
                            }
                        }
                    }
                    closedir(hwmonDir);
                }
            }
        }
        
        closedir(dir);
    }
    
    return -1.0;  // No GPU sensor found
}

std::vector<TemperatureSensor> Temperature::all() {
    std::vector<TemperatureSensor> sensors;
    
    // Scan thermal zones
    DIR* dir = opendir("/sys/class/thermal");
    if (dir) {
        struct dirent* entry;
        
        while ((entry = readdir(dir)) != nullptr) {
            std::string name = entry->d_name;
            if (name.find("thermal_zone") == 0) {
                std::string path = std::string("/sys/class/thermal/") + name;
                double temp = readThermalZone(path);
                
                if (temp >= 0) {
                    TemperatureSensor sensor;
                    sensor.name = readSensorName(path);
                    sensor.temperature = temp;
                    sensors.push_back(sensor);
                }
            }
        }
        
        closedir(dir);
    }
    
    // Scan hwmon
    dir = opendir("/sys/class/hwmon");
    if (dir) {
        struct dirent* entry;
        
        while ((entry = readdir(dir)) != nullptr) {
            std::string name = entry->d_name;
            if (name[0] == 'h' && name[1] == 'w' && name[2] == 'm' && name[3] == 'o' && name[4] == 'n') {
                std::string path = std::string("/sys/class/hwmon/") + name;
                
                // Try multiple temperature inputs
                for (int i = 1; i <= 10; i++) {
                    std::string tempPath = path + "/temp" + std::to_string(i) + "_input";
                    std::ifstream tempFile(tempPath);
                    int temp;
                    
                    if (tempFile >> temp) {
                        TemperatureSensor sensor;
                        sensor.name = readSensorName(path) + " " + std::to_string(i);
                        sensor.temperature = temp / 1000.0;
                        sensors.push_back(sensor);
                    } else {
                        break;  // No more temp inputs
                    }
                }
            }
        }
        
        closedir(dir);
    }
    
    return sensors;
}

} // namespace havel
