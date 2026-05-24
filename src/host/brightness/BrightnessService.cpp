#include "BrightnessService.hpp"
#include "core/brightness/BrightnessManager.hpp"

namespace havel::host {

BrightnessService::BrightnessService(havel::BrightnessManager* manager)
    : m_manager(manager) {}

double BrightnessService::getBrightness(int monitorIndex) const {
    if (!m_manager) return 1.0;
    return monitorIndex >= 0 ? m_manager->getBrightness(monitorIndex) : m_manager->getBrightness();
}

void BrightnessService::setBrightness(double brightness, int monitorIndex) {
    if (!m_manager) return;
    if (monitorIndex >= 0) m_manager->setBrightness(monitorIndex, brightness);
    else m_manager->setBrightness(brightness);
}

void BrightnessService::increaseBrightness(double step, int monitorIndex) {
    if (!m_manager) return;
    if (monitorIndex >= 0) m_manager->increaseBrightness(monitorIndex, step);
    else m_manager->increaseBrightness(step);
}

void BrightnessService::decreaseBrightness(double step, int monitorIndex) {
    if (!m_manager) return;
    if (monitorIndex >= 0) m_manager->decreaseBrightness(monitorIndex, step);
    else m_manager->decreaseBrightness(step);
}

int BrightnessService::getTemperature(int monitorIndex) const {
    if (!m_manager) return 6500;
    return monitorIndex >= 0 ? m_manager->getTemperature(monitorIndex) : m_manager->getTemperature();
}

void BrightnessService::setTemperature(int temperature, int monitorIndex) {
    if (!m_manager) return;
    if (monitorIndex >= 0) m_manager->setTemperature(monitorIndex, temperature);
    else m_manager->setTemperature(temperature);
}

void BrightnessService::increaseTemperature(int amount, int monitorIndex) {
    if (!m_manager) return;
    if (monitorIndex >= 0) m_manager->increaseTemperature(monitorIndex, amount);
    else m_manager->increaseTemperature(amount);
}

void BrightnessService::decreaseTemperature(int amount, int monitorIndex) {
    if (!m_manager) return;
    if (monitorIndex >= 0) m_manager->decreaseTemperature(monitorIndex, amount);
    else m_manager->decreaseTemperature(amount);
}

void BrightnessService::setGammaRGB(double red, double green, double blue, int monitorIndex) {
    if (!m_manager) return;
    if (monitorIndex >= 0) m_manager->setGammaRGB(monitorIndex, red, green, blue);
    else m_manager->setGammaRGB(red, green, blue);
}

void BrightnessService::getGammaRGB(double& red, double& green, double& blue, int monitorIndex) const {
    if (!m_manager) return;
    BrightnessManager::RGBColor c;
    if (monitorIndex >= 0) {
        auto name = m_manager->getMonitor(monitorIndex);
        if (!name.empty()) c = m_manager->getGammaRGB(name);
        else return;
    } else {
        c = m_manager->getGammaRGB();
    }
    red = c.red; green = c.green; blue = c.blue;
}

void BrightnessService::increaseGamma(int amount, int monitorIndex) {
    if (!m_manager) return;
    if (monitorIndex >= 0) m_manager->increaseGamma(monitorIndex, amount);
    else m_manager->increaseGamma(amount);
}

void BrightnessService::decreaseGamma(int amount, int monitorIndex) {
    if (!m_manager) return;
    if (monitorIndex >= 0) m_manager->decreaseGamma(monitorIndex, amount);
    else m_manager->decreaseGamma(amount);
}

double BrightnessService::getShadowLift(int monitorIndex) const {
    if (!m_manager) return 0.0;
    return monitorIndex >= 0 ? m_manager->getShadowLift(monitorIndex) : m_manager->getShadowLift();
}

void BrightnessService::setShadowLift(double lift, int monitorIndex) {
    if (!m_manager) return;
    if (monitorIndex >= 0) m_manager->setShadowLift(monitorIndex, lift);
    else m_manager->setShadowLift(lift);
}

void BrightnessService::increaseShadowLift(int amount, int monitorIndex) {
    if (!m_manager) return;
    if (monitorIndex >= 0) m_manager->increaseShadowLift(monitorIndex, amount);
    else m_manager->increaseShadowLift(amount);
}

void BrightnessService::decreaseShadowLift(int amount, int monitorIndex) {
    if (!m_manager) return;
    if (monitorIndex >= 0) m_manager->decreaseShadowLift(monitorIndex, amount);
    else m_manager->decreaseShadowLift(amount);
}

void BrightnessService::setBrightnessAndRGB(double brightness, double red, double green, double blue, int monitorIndex) {
    if (!m_manager) return;
    if (monitorIndex >= 0) m_manager->setBrightnessAndRGB(monitorIndex, brightness, red, green, blue);
    else m_manager->setBrightnessAndRGB(brightness, red, green, blue);
}

void BrightnessService::setBrightnessAndTemperature(double brightness, int kelvin, int monitorIndex) {
    if (!m_manager) return;
    if (monitorIndex >= 0) m_manager->setBrightnessAndTemperature(monitorIndex, brightness, kelvin);
    else m_manager->setBrightnessAndTemperature(brightness, kelvin);
}

void BrightnessService::setBrightnessAndShadowLift(double brightness, double shadowLift, int monitorIndex) {
    if (!m_manager) return;
    if (monitorIndex >= 0) m_manager->setBrightnessAndShadowLift(monitorIndex, brightness, shadowLift);
    else m_manager->setBrightnessAndShadowLift(brightness, shadowLift);
}

void BrightnessService::enableDayNightMode(double dayBrightness, double nightBrightness, int dayTemperature, int nightTemperature, int dayStartHour, int nightStartHour, int checkIntervalMinutes) {
    if (!m_manager) return;
    BrightnessManager::DayNightSettings s;
    s.dayBrightness = dayBrightness;
    s.nightBrightness = nightBrightness;
    s.dayTemperature = dayTemperature;
    s.nightTemperature = nightTemperature;
    s.dayStartHour = dayStartHour;
    s.nightStartHour = nightStartHour;
    s.checkInterval = std::chrono::minutes(checkIntervalMinutes);
    s.autoAdjust = true;
    m_manager->enableDayNightMode(s);
}

void BrightnessService::disableDayNightMode() {
    if (!m_manager) return;
    m_manager->disableDayNightMode();
}

bool BrightnessService::isDayNightModeEnabled() const {
    if (!m_manager) return false;
    return m_manager->isDayNightModeEnabled();
}

void BrightnessService::setDaySettings(double brightness, int temperature) {
    if (!m_manager) return;
    m_manager->setDaySettings(brightness, temperature);
}

void BrightnessService::setNightSettings(double brightness, int temperature) {
    if (!m_manager) return;
    m_manager->setNightSettings(brightness, temperature);
}

void BrightnessService::setDayNightTiming(int dayStart, int nightStart) {
    if (!m_manager) return;
    m_manager->setDayNightTiming(dayStart, nightStart);
}

bool BrightnessService::switchToDay(int monitorIndex) {
    if (!m_manager) return false;
    if (monitorIndex >= 0) return m_manager->switchToDay(monitorIndex);
    return m_manager->switchToDay();
}

bool BrightnessService::switchToNight(int monitorIndex) {
    if (!m_manager) return false;
    if (monitorIndex >= 0) return m_manager->switchToNight(monitorIndex);
    return m_manager->switchToNight();
}

bool BrightnessService::isDay() const {
    if (!m_manager) return false;
    return m_manager->isDay();
}

std::vector<std::string> BrightnessService::getConnectedMonitors() const {
    if (!m_manager) return {};
    return m_manager->getConnectedMonitors();
}

std::string BrightnessService::getMonitor(int index) const {
    if (!m_manager) return {};
    return m_manager->getMonitor(index);
}

} // namespace havel::host
