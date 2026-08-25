#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <vector>
#include <cstdint>
#include "../../core/BrightnessManager.hpp"

namespace havel::host {

class BrightnessService {
public:
    BrightnessService() = default;
    ~BrightnessService() = default;

    // Initialize the brightness manager
    bool initialize() {
        if (initialized_) return true;
        
        
        initialized_ = true;
        return true;
    }

    // Brightness control
    bool setBrightness(double brightness) {
        return manager_.setBrightness(brightness);
    }
    
    bool setBrightness(const std::string& monitor, double brightness) {
        return manager_.setBrightness(monitor, brightness);
    }
    
    double getBrightness() const {
        return manager_.getBrightness();
    }
    
    double getBrightness(const std::string& monitor) const {
        return manager_.getBrightness(monitor);
    }
    
    bool increaseBrightness(double amount = 0.02) {
        return manager_.increaseBrightness(amount);
    }
    
    bool decreaseBrightness(double amount = 0.02) {
        return manager_.decreaseBrightness(amount);
    }
    
    bool increaseBrightness(const std::string& monitor, double amount = 0.02) {
        return manager_.increaseBrightness(monitor, amount);
    }
    
    bool decreaseBrightness(const std::string& monitor, double amount = 0.02) {
        return manager_.decreaseBrightness(monitor, amount);
    }

    // Gamma/Color temperature control
    bool setGammaRGB(double red, double green, double blue) {
        return manager_.setGammaRGB(red, green, blue);
    }
    
    bool setGammaRGB(const std::string& monitor, double red, double green, double blue) {
        return manager_.setGammaRGB(monitor, red, green, blue);
    }
    
    bool setTemperature(int kelvin) {
        return manager_.setTemperature(kelvin);
    }
    
    bool setTemperature(const std::string& monitor, int kelvin) {
        return manager_.setTemperature(monitor, kelvin);
    }
    
    int getTemperature() const {
        return manager_.getTemperature();
    }
    
    int getTemperature(const std::string& monitor) const {
        return manager_.getTemperature(monitor);
    }
    
    bool increaseTemperature(int amount = 200) {
        return manager_.increaseTemperature(amount);
    }
    
    bool decreaseTemperature(int amount = 200) {
        return manager_.decreaseTemperature(amount);
    }
    
    bool increaseTemperature(const std::string& monitor, int amount = 200) {
        return manager_.increaseTemperature(monitor, amount);
    }
    
    bool decreaseTemperature(const std::string& monitor, int amount = 200) {
        return manager_.decreaseTemperature(monitor, amount);
    }

    // Gamma RGB
    struct RGBColor {
        double red = 1.0;
        double green = 1.0;
        double blue = 1.0;
    };
    
    RGBColor getGammaRGB() const {
        auto rgb = manager_.getGammaRGB();
        return {rgb.red, rgb.green, rgb.blue};
    }
    
    RGBColor getGammaRGB(const std::string& monitor) const {
        auto rgb = manager_.getGammaRGB(monitor);
        return {rgb.red, rgb.green, rgb.blue};
    }

    // Shadow lift
    bool setShadowLift(double lift) {
        return manager_.setShadowLift(lift);
    }
    
    bool setShadowLift(const std::string& monitor, double lift) {
        return manager_.setShadowLift(monitor, lift);
    }
    
    double getShadowLift() const {
        return manager_.getShadowLift();
    }
    
    double getShadowLift(const std::string& monitor) const {
        return manager_.getShadowLift(monitor);
    }

    // Monitor management
    struct MonitorInfo {
        std::string name;
        uint64_t outputId = 0;
        uint64_t crtc = 0;
        bool isPrimary = false;
        double brightness = 1.0;
        int temperature = 6500;
        RGBColor gamma = {1.0, 1.0, 1.0};
        double shadowLift = 0.0;
    };
    
    std::vector<MonitorInfo> getMonitors() const {
        std::vector<MonitorInfo> result;
        auto monitors = manager_.getConnectedMonitors();
        for (const auto& name : monitors) {
            MonitorInfo mi;
            mi.name = name;
            mi.outputId = 0;
            mi.crtc = 0;
            mi.isPrimary = (name == manager_.primaryMonitor);
            mi.brightness = manager_.getBrightness(name);
            mi.temperature = manager_.getTemperature(name);
            auto rgb = manager_.getGammaRGB(name);
            mi.gamma = {rgb.red, rgb.green, rgb.blue};
            mi.shadowLift = manager_.getShadowLift(name);
            result.push_back(mi);
        }
        return result;
    }
    
    bool isAvailable() const {
        return initialized_;
    }

private:
    BrightnessManager manager_;
    bool initialized_ = false;
};

} // namespace havel::host
