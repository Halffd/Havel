#pragma once
#include "ConfigManager.hpp"
#include "../utils/Logger.hpp"
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <memory>
#include <array>
#include <cmath>
#include <regex>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>
#include <functional>

using namespace std;
namespace havel {
class BrightnessManager {
public:
    struct RGBColor {
        double red, green, blue;
    };
    struct DayNightSettings {
        double dayBrightness = 1.0;
        double nightBrightness = 0.3;
        int dayTemperature = 6500;    // Kelvin
        int nightTemperature = 3000;  // Kelvin
        
        // Timing (24h format)
        int dayStartHour = 7;    // 7:00 AM
        int nightStartHour = 20; // 8:00 PM
        
        bool autoAdjust = false;
        chrono::minutes checkInterval{5}; // Check every 5 minutes
    };

    BrightnessManager();
    ~BrightnessManager();
    string getMonitor(int index);
    // === BRIGHTNESS OVERLOADS ===
    bool setBrightness(double brightness);  // All monitors
    bool setBrightness(const string& monitor, double brightness);
    double getBrightness();
    double getBrightness(const string& monitor);
    
    // === RGB GAMMA OVERLOADS ===
    bool setGammaRGB(double red, double green, double blue);  // All monitors
    bool setGammaRGB(const string& monitor, double red, double green, double blue);
    RGBColor getGammaRGB();
    RGBColor getGammaRGB(const string& monitor);

    // === KELVIN TEMPERATURE OVERLOADS ===
    bool setTemperature(int kelvin);  // All monitors
    bool setTemperature(const string& monitor, int kelvin);
    int getTemperature();
    int getTemperature(const string& monitor);
    void decreaseGamma(int amount = DEFAULT_TEMP_AMOUNT);
    void increaseGamma(int amount = DEFAULT_TEMP_AMOUNT);
    void decreaseGamma(string monitor, int amount = DEFAULT_TEMP_AMOUNT);
    void increaseGamma(string monitor, int amount = DEFAULT_TEMP_AMOUNT);
    
    // === COMBINED OPERATIONS ===
    bool setBrightnessAndRGB(double brightness, double red, double green, double blue);
    bool setBrightnessAndRGB(const string& monitor, double brightness, double red, double green, double blue);
    
    bool setBrightnessAndTemperature(double brightness, int kelvin);
    bool setBrightnessAndTemperature(const string& monitor, double brightness, int kelvin);

    // === INCREMENT OPERATIONS ===
    bool increaseBrightness(double amount = DEFAULT_BRIGHTNESS_AMOUNT);
    bool increaseBrightness(const string& monitor, double amount = DEFAULT_BRIGHTNESS_AMOUNT);
    bool decreaseBrightness(double amount = DEFAULT_BRIGHTNESS_AMOUNT);
    bool decreaseBrightness(const string& monitor, double amount = DEFAULT_BRIGHTNESS_AMOUNT);

    bool increaseTemperature(int amount = DEFAULT_TEMP_AMOUNT);
    bool increaseTemperature(const string& monitor, int amount = DEFAULT_TEMP_AMOUNT);
    bool decreaseTemperature(int amount = DEFAULT_TEMP_AMOUNT);
    bool decreaseTemperature(const string& monitor, int amount = DEFAULT_TEMP_AMOUNT);

    // === DAY/NIGHT AUTOMATION ===
    void enableDayNightMode(const DayNightSettings& settings);
    void disableDayNightMode();
    bool isDayNightModeEnabled() const { return dayNightSettings.autoAdjust; }
    
    void setDaySettings(double brightness, int temperature);
    void setNightSettings(double brightness, int temperature);
    void setDayNightTiming(int dayStart, int nightStart);
    
    // Manual day/night switching
    bool switchToDay();   // All monitors
    bool switchToNight(); // All monitors
    bool switchToDay(const string& monitor);
    bool switchToNight(const string& monitor);

    // === UTILITY ===
    vector<string> getConnectedMonitors();
    bool isDay();  // Based on current time and settings
    string primaryMonitor;
    // Constants
    static constexpr double DEFAULT_BRIGHTNESS_AMOUNT = 0.02;
    static constexpr int DEFAULT_TEMP_AMOUNT = 200;
    static constexpr int MIN_TEMPERATURE = 0;
    static constexpr int MAX_TEMPERATURE = 25000;

private:
    Display* x11_display;
    Window x11_root;
    // === KELVIN TO RGB CONVERSION ===

    RGBColor kelvinToRGB(int kelvin) const;
    int rgbToKelvin(const RGBColor& rgb) const;
    
    // === BACKEND IMPLEMENTATIONS ===
    bool setBrightnessXrandr(const string& monitor, double brightness);
    bool setBrightnessXrandr(double brightness);
    double getBrightnessXrandr(const string& monitor);
    double getBrightnessSysfs(const string& monitor);
    
    bool setGammaXrandrRGB(const string& monitor, double red, double green, double blue);
    bool setGammaXrandrRGB(double red, double green, double blue);
    double getGammaXrandr(const string& monitor);
    RGBColor getGammaXrandrRGB(const string& monitor);
    
    bool setBrightnessWayland(const string& monitor, double brightness);
    bool setBrightnessWayland(double brightness);
    
    bool setGammaWaylandRGB(const string& monitor, double red, double green, double blue);
    bool setGammaWaylandRGB(double red, double green, double blue);

    // === DAY/NIGHT AUTOMATION ===
    void dayNightWorkerThread();
    void applyCurrentTimeSettings();
    
    // === STATE ===
    DayNightSettings dayNightSettings;
    
    vector<string> cached_monitors;
    string displayMethod;
    
    // Threading for day/night
    atomic<bool> stopDayNightThread{false};
    unique_ptr<thread> dayNightThread;
    mutex settingsMutex;
    
    // Current state tracking
    map<string, double> brightness;
    map<string, int> temperature;
    vector<string> monitors;
};

} // namespace havel