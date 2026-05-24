#pragma once

#include <string>
#include <vector>

namespace havel { class BrightnessManager; }

namespace havel::host {

class BrightnessService {
public:
    explicit BrightnessService(havel::BrightnessManager* manager);
    ~BrightnessService() = default;

    // Brightness
    double getBrightness(int monitorIndex = -1) const;
    void setBrightness(double brightness, int monitorIndex = -1);
    void increaseBrightness(double step, int monitorIndex = -1);
    void decreaseBrightness(double step, int monitorIndex = -1);

    // Temperature
    int getTemperature(int monitorIndex = -1) const;
    void setTemperature(int temperature, int monitorIndex = -1);
    void increaseTemperature(int amount, int monitorIndex = -1);
    void decreaseTemperature(int amount, int monitorIndex = -1);

    // Gamma RGB
    void setGammaRGB(double red, double green, double blue, int monitorIndex = -1);
    void getGammaRGB(double& red, double& green, double& blue, int monitorIndex = -1) const;

    // Gamma adjustment
    void increaseGamma(int amount, int monitorIndex = -1);
    void decreaseGamma(int amount, int monitorIndex = -1);

    // Shadow lift
    double getShadowLift(int monitorIndex = -1) const;
    void setShadowLift(double lift, int monitorIndex = -1);
    void increaseShadowLift(int amount, int monitorIndex = -1);
    void decreaseShadowLift(int amount, int monitorIndex = -1);

    // Combined operations
    void setBrightnessAndRGB(double brightness, double red, double green, double blue, int monitorIndex = -1);
    void setBrightnessAndTemperature(double brightness, int kelvin, int monitorIndex = -1);
    void setBrightnessAndShadowLift(double brightness, double shadowLift, int monitorIndex = -1);

    // Day/night automation
    void enableDayNightMode(double dayBrightness, double nightBrightness, int dayTemperature, int nightTemperature, int dayStartHour, int nightStartHour, int checkIntervalMinutes);
    void disableDayNightMode();
    bool isDayNightModeEnabled() const;
    void setDaySettings(double brightness, int temperature);
    void setNightSettings(double brightness, int temperature);
    void setDayNightTiming(int dayStart, int nightStart);
    bool switchToDay(int monitorIndex = -1);
    bool switchToNight(int monitorIndex = -1);
    bool isDay() const;

    // Utility
    std::vector<std::string> getConnectedMonitors() const;
    std::string getMonitor(int index) const;

private:
    havel::BrightnessManager* m_manager;
};

} // namespace havel::host
