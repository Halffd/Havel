/*
 * Temperature.hpp
 * 
 * Temperature information utilities for Havel language.
 * Provides CPU and GPU temperature readings.
 */
#pragma once

#include <string>
#include <vector>

namespace havel {

struct TemperatureSensor {
    std::string name;
    double temperature;  // in Celsius
};

struct Temperature {
    // Get CPU temperature in Celsius
    static double cpu();
    
    // Get GPU temperature in Celsius
    static double gpu();
    
    // Get all temperature sensors
    static std::vector<TemperatureSensor> all();
};

} // namespace havel
