/*
 * BrightnessService.hpp
 *
 * Pure C++ brightness service - no VM, no interpreter, no HavelValue.
 */
#pragma once

#include <string>

namespace havel { class BrightnessManager; }

namespace havel::host {

class BrightnessService {
public:
    explicit BrightnessService(havel::BrightnessManager* manager);
    ~BrightnessService() = default;

    double getBrightness(int monitorIndex = -1) const;
    void setBrightness(double brightness, int monitorIndex = -1);
    void increaseBrightness(double step, int monitorIndex = -1);
    void decreaseBrightness(double step, int monitorIndex = -1);
    
    int getTemperature(int monitorIndex = -1) const;
    void setTemperature(int temperature, int monitorIndex = -1);

private:
    havel::BrightnessManager* m_manager;
};

} // namespace havel::host
