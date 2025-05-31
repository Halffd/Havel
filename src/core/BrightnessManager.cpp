#include "BrightnessManager.hpp"
#include "../utils/Logger.hpp"
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <memory>
#include <array>
#include <cmath>
#include <regex>

namespace havel {

BrightnessManager::BrightnessManager() {
    // Initialize default settings
    settings.dayBrightness = 1.0;
    settings.nightBrightness = 1.0;
    settings.dayTemperature = 6500;
    settings.nightTemperature = 6500;
    settings.latitude = "22";
    settings.longitude = "44";
    settings.verbose = false;
    
    // Initialize current state
    settings.currentBrightness = 1.0;
    settings.currentGamma = 6500;

    // Determine display method
    displayMethod = isX11() ? "randr" : "wayland";
    if (settings.verbose) {
        lo.info("Using display method: " + displayMethod);
    }
}

void BrightnessManager::setLocation(const std::string& lat, const std::string& lon) {
    settings.latitude = lat;
    settings.longitude = lon;
}

bool BrightnessManager::resetToDefaults() {
    std::string cmd = "gammastep -m " + displayMethod + " -o -x";
    if (settings.verbose) {
        lo.info("Resetting to defaults with command: " + cmd);
    }
    return system(cmd.c_str()) == 0;
}

bool BrightnessManager::validateBrightness(const std::string& brightness) {
    std::regex pattern("^[+-]?[0-9]*\\.?[0-9]+(?::[+-]?[0-9]*\\.?[0-9]+)?$");
    return std::regex_match(brightness, pattern);
}

bool BrightnessManager::validateGamma(const std::string& gamma) {
    std::regex pattern("^\\d+(?::\\d+)?$");
    if (!std::regex_match(gamma, pattern)) {
        return false;
    }

    auto [dayGamma, nightGamma] = parseGammaValue(gamma);
    return dayGamma >= 0 && dayGamma <= MAX_TEMPERATURE &&
           nightGamma >= 0 && nightGamma <= MAX_TEMPERATURE;
}

int BrightnessManager::convertGammaToTemperature(int gamma) {
    if (gamma <= 100) {
        return 1000 + (gamma * (6500 - 1000) / 100);
    } else if (gamma <= 999) {
        return 6501 + ((gamma - 101) * (MAX_TEMPERATURE - 6501) / (999 - 101));
    }
    return gamma;
}

std::tuple<double, double> BrightnessManager::parseBrightnessValue(const std::string& brightness) {
    double day, night;
    size_t colonPos = brightness.find(':');
    
    if (colonPos != std::string::npos) {
        day = std::stod(brightness.substr(0, colonPos));
        night = std::stod(brightness.substr(colonPos + 1));
    } else {
        day = night = std::stod(brightness);
    }

    // Convert percentages to decimal
    if (std::abs(day) > 10) day /= 100;
    else if (std::abs(day) > 1) day /= 10;
    
    if (std::abs(night) > 10) night /= 100;
    else if (std::abs(night) > 1) night /= 10;

    return {day, night};
}

std::tuple<int, int> BrightnessManager::parseGammaValue(const std::string& gamma) {
    int day, night;
    size_t colonPos = gamma.find(':');
    
    if (colonPos != std::string::npos) {
        day = std::stoi(gamma.substr(0, colonPos));
        night = std::stoi(gamma.substr(colonPos + 1));
    } else {
        day = night = std::stoi(gamma);
    }

    return {day, night};
}

bool BrightnessManager::isX11() {
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(
        popen("pgrep -x \"Xorg\"", "r"), pclose);
    
    if (!pipe) {
        lo.error("Failed to execute pgrep command");
        return false;
    }
    
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    
    return !result.empty();
}

std::optional<double> BrightnessManager::getCurrentBrightness() {
    if (displayMethod != "randr") return std::nullopt;

    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(
        popen("xrandr --verbose | grep \"Brightness\" | awk '{print $2}'", "r"), 
        pclose);
    
    if (!pipe) {
        lo.error("Failed to get current brightness");
        return std::nullopt;
    }
    
    if (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        try {
            return std::stod(buffer.data());
        } catch (const std::exception& e) {
            lo.error("Failed to parse current brightness: " + std::string(e.what()));
        }
    }
    
    return std::nullopt;
}

bool BrightnessManager::adjustBrightnessRandr(double& dayBrightness, double& nightBrightness) {
    auto currentBrightness = getCurrentBrightness();
    if (!currentBrightness) return false;

    // Handle relative brightness changes
    if (dayBrightness < 0 || dayBrightness > 0) {
        dayBrightness = *currentBrightness + dayBrightness;
    }
    if (nightBrightness < 0 || nightBrightness > 0) {
        nightBrightness = *currentBrightness + nightBrightness;
    }

    // Clamp values between 0 and 1
    dayBrightness = std::max(0.0, std::min(1.0, dayBrightness));
    nightBrightness = std::max(0.0, std::min(1.0, nightBrightness));

    return true;
}

bool BrightnessManager::adjustBrightnessWayland(double& dayBrightness, double& nightBrightness) {
    // Convert percentages to decimal if needed
    if (dayBrightness > 10) dayBrightness /= 100;
    else if (dayBrightness > 1) dayBrightness /= 10;
    
    if (nightBrightness > 10) nightBrightness /= 100;
    else if (nightBrightness > 1) nightBrightness /= 10;

    return true;
}

bool BrightnessManager::executeGammastep() {
    std::ostringstream cmd;
    cmd << "gammastep -P -m " << displayMethod
        << " -l " << settings.latitude << ":" << settings.longitude
        << " -t " << settings.dayTemperature << ":" << settings.nightTemperature
        << " -b " << settings.dayBrightness << ":" << settings.nightBrightness
        << " -O " << settings.currentGamma;

    if (settings.verbose) {
        lo.info("Executing command: " + cmd.str());
    }

    if (system(nullptr)) {
        int ret = system(cmd.str().c_str());
        if (ret != 0) {
            lo.error("Command failed with exit code: " + std::to_string(ret));
        }
        return ret == 0;
    } else {
        lo.error("System shell not available");
        return false;
    }
}

bool BrightnessManager::setBrightnessAndTemperature(const std::string& brightness, const std::string& gamma) {
    if (!validateBrightness(brightness)) {
        lo.error("Invalid brightness value: " + brightness);
        return false;
    }

    if (!validateGamma(gamma)) {
        lo.error("Invalid gamma value: " + gamma);
        return false;
    }

    // Parse brightness values
    auto [dayBrightness, nightBrightness] = parseBrightnessValue(brightness);

    // Adjust brightness based on display method
    if (displayMethod == "randr") {
        if (!adjustBrightnessRandr(dayBrightness, nightBrightness)) {
            return false;
        }
    } else {
        if (!adjustBrightnessWayland(dayBrightness, nightBrightness)) {
            return false;
        }
    }

    // Parse and convert gamma values
    auto [dayGamma, nightGamma] = parseGammaValue(gamma);
    settings.dayTemperature = convertGammaToTemperature(dayGamma);
    settings.nightTemperature = convertGammaToTemperature(nightGamma);

    if (settings.dayTemperature < MIN_TEMPERATURE || settings.nightTemperature < MIN_TEMPERATURE ||
        settings.dayTemperature > MAX_TEMPERATURE || settings.nightTemperature > MAX_TEMPERATURE) {
        lo.error("Temperature values out of range (1000K-25000K)");
        return false;
    }

    // Update settings
    settings.dayBrightness = dayBrightness;
    settings.nightBrightness = nightBrightness;

    // Update current state
    settings.currentBrightness = dayBrightness;
    settings.currentGamma = settings.dayTemperature;

    // Execute gammastep
    if (!executeGammastep()) {
        lo.error("Failed to set brightness and temperature");
        return false;
    }

    if (settings.verbose) {
        lo.info("Successfully set brightness to " + std::to_string(settings.dayBrightness) + 
                " (day) and " + std::to_string(settings.nightBrightness) + " (night)");
        lo.info("Color temperature set to " + std::to_string(settings.dayTemperature) + 
                "K (day) and " + std::to_string(settings.nightTemperature) + "K (night)");
    }

    return true;
}

bool BrightnessManager::setStartupValues() {
    settings.currentBrightness = STARTUP_BRIGHTNESS;
    settings.currentGamma = STARTUP_GAMMA;
    return setBrightnessAndTemperature(
        std::to_string(STARTUP_BRIGHTNESS),
        std::to_string(STARTUP_GAMMA)
    );
}

bool BrightnessManager::setDefaultBrightness() {
    settings.currentBrightness = DEFAULT_BRIGHTNESS;
    settings.currentGamma = 6500; // Standard daylight temperature
    return setBrightnessAndTemperature(
        std::to_string(DEFAULT_BRIGHTNESS),
        "6500"
    );
}

bool BrightnessManager::increaseBrightness(double amount) {
    double newBrightness = std::min(1.0, settings.currentBrightness + amount);
    if (newBrightness != settings.currentBrightness) {
        lo.info("Increasing brightness from " + std::to_string(settings.currentBrightness) + 
                " to " + std::to_string(newBrightness));
        settings.currentBrightness = newBrightness;
        settings.dayBrightness = newBrightness;
        settings.nightBrightness = newBrightness;
        return executeGammastep();
    }
    return false;
}

bool BrightnessManager::decreaseBrightness(double amount) {
    double newBrightness = std::max(0.0, settings.currentBrightness - amount);
    if (newBrightness != settings.currentBrightness) {
        lo.info("Decreasing brightness from " + std::to_string(settings.currentBrightness) + 
                " to " + std::to_string(newBrightness));
        settings.currentBrightness = newBrightness;
        settings.dayBrightness = newBrightness;
        settings.nightBrightness = newBrightness;
        return executeGammastep();
    }
    return false;
}

bool BrightnessManager::increaseGamma(int amount) {
    int newGamma = std::min(MAX_TEMPERATURE, settings.currentGamma + amount);
    if (newGamma != settings.currentGamma) {
        settings.currentGamma = newGamma;
        return setBrightnessAndTemperature(
            std::to_string(settings.currentBrightness),
            std::to_string(settings.currentGamma)
        );
    }
    return false;
}

bool BrightnessManager::decreaseGamma(int amount) {
    int newGamma = std::max(MIN_TEMPERATURE, settings.currentGamma - amount);
    if (newGamma != settings.currentGamma) {
        settings.currentGamma = newGamma;
        return setBrightnessAndTemperature(
            std::to_string(settings.currentBrightness),
            std::to_string(settings.currentGamma)
        );
    }
    return false;
}

} // namespace H 