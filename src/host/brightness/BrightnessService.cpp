#include "BrightnessService.hpp"
#include "core/brightness/BrightnessManager.hpp"
#include <cstdio>

#define BR_DEBUG(fmt, ...) fprintf(stderr, "[BR-DEBUG] BrightnessService " fmt "\n", ##__VA_ARGS__)

namespace havel::host {

BrightnessService::BrightnessService(havel::BrightnessManager* manager)
    : m_manager(manager) {
    BR_DEBUG("constructor manager=%p", (void*)manager);
}

double BrightnessService::getBrightness(int monitorIndex) const {
    BR_DEBUG("getBrightness(monitor=%d) m_manager=%p", monitorIndex, (void*)m_manager);
    if (!m_manager) return 1.0;
    return monitorIndex >= 0 ? m_manager->getBrightness(monitorIndex) : m_manager->getBrightness();
}

void BrightnessService::setBrightness(double brightness, int monitorIndex) {
    BR_DEBUG("setBrightness(b=%f monitor=%d)", brightness, monitorIndex);
    if (!m_manager) return;
    if (monitorIndex >= 0) m_manager->setBrightness(monitorIndex, brightness);
    else m_manager->setBrightness(brightness);
}

void BrightnessService::increaseBrightness(double step, int monitorIndex) {
    BR_DEBUG("increaseBrightness(step=%f monitor=%d)", step, monitorIndex);
    if (!m_manager) return;
    if (monitorIndex >= 0) m_manager->increaseBrightness(monitorIndex, step);
    else m_manager->increaseBrightness(step);
}

void BrightnessService::decreaseBrightness(double step, int monitorIndex) {
    BR_DEBUG("decreaseBrightness(step=%f monitor=%d)", step, monitorIndex);
    if (!m_manager) return;
    if (monitorIndex >= 0) m_manager->decreaseBrightness(monitorIndex, step);
    else m_manager->decreaseBrightness(step);
}

int BrightnessService::getTemperature(int monitorIndex) const {
    BR_DEBUG("getTemperature(monitor=%d)", monitorIndex);
    if (!m_manager) return 6500;
    return monitorIndex >= 0 ? m_manager->getTemperature(monitorIndex) : m_manager->getTemperature();
}

void BrightnessService::setTemperature(int temperature, int monitorIndex) {
    BR_DEBUG("setTemperature(t=%d monitor=%d)", temperature, monitorIndex);
    if (!m_manager) return;
    if (monitorIndex >= 0) m_manager->setTemperature(monitorIndex, temperature);
    else m_manager->setTemperature(temperature);
}

void BrightnessService::increaseTemperature(int amount, int monitorIndex) {
    BR_DEBUG("increaseTemperature(amount=%d monitor=%d)", amount, monitorIndex);
    if (!m_manager) return;
    if (monitorIndex >= 0) m_manager->increaseTemperature(monitorIndex, amount);
    else m_manager->increaseTemperature(amount);
}

void BrightnessService::decreaseTemperature(int amount, int monitorIndex) {
    BR_DEBUG("decreaseTemperature(amount=%d monitor=%d)", amount, monitorIndex);
    if (!m_manager) return;
    if (monitorIndex >= 0) m_manager->decreaseTemperature(monitorIndex, amount);
    else m_manager->decreaseTemperature(amount);
}

void BrightnessService::setGammaRGB(double red, double green, double blue, int monitorIndex) {
    BR_DEBUG("setGammaRGB(r=%f g=%f b=%f monitor=%d)", red, green, blue, monitorIndex);
    if (!m_manager) return;
    if (monitorIndex >= 0) m_manager->setGammaRGB(monitorIndex, red, green, blue);
    else m_manager->setGammaRGB(red, green, blue);
}

void BrightnessService::getGammaRGB(double& red, double& green, double& blue, int monitorIndex) const {
    BR_DEBUG("getGammaRGB(monitor=%d)", monitorIndex);
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

double BrightnessService::getGammaR(int monitorIndex) const {
    BR_DEBUG("getGammaR(monitor=%d)", monitorIndex);
    double r = 1.0, g = 1.0, b = 1.0;
    getGammaRGB(r, g, b, monitorIndex);
    return r;
}

double BrightnessService::getGammaG(int monitorIndex) const {
    BR_DEBUG("getGammaG(monitor=%d)", monitorIndex);
    double r = 1.0, g = 1.0, b = 1.0;
    getGammaRGB(r, g, b, monitorIndex);
    return g;
}

double BrightnessService::getGammaB(int monitorIndex) const {
    BR_DEBUG("getGammaB(monitor=%d)", monitorIndex);
    double r = 1.0, g = 1.0, b = 1.0;
    getGammaRGB(r, g, b, monitorIndex);
    return b;
}

void BrightnessService::increaseGamma(int amount, int monitorIndex) {
    BR_DEBUG("increaseGamma(amount=%d monitor=%d)", amount, monitorIndex);
    if (!m_manager) return;
    if (monitorIndex >= 0) m_manager->increaseGamma(monitorIndex, amount);
    else m_manager->increaseGamma(amount);
}

void BrightnessService::decreaseGamma(int amount, int monitorIndex) {
    BR_DEBUG("decreaseGamma(amount=%d monitor=%d)", amount, monitorIndex);
    if (!m_manager) return;
    if (monitorIndex >= 0) m_manager->decreaseGamma(monitorIndex, amount);
    else m_manager->decreaseGamma(amount);
}

double BrightnessService::getShadowLift(int monitorIndex) const {
    BR_DEBUG("getShadowLift(monitor=%d)", monitorIndex);
    if (!m_manager) return 0.0;
    return monitorIndex >= 0 ? m_manager->getShadowLift(monitorIndex) : m_manager->getShadowLift();
}

void BrightnessService::setShadowLift(double lift, int monitorIndex) {
    BR_DEBUG("setShadowLift(lift=%f monitor=%d)", lift, monitorIndex);
    if (!m_manager) return;
    if (monitorIndex >= 0) m_manager->setShadowLift(monitorIndex, lift);
    else m_manager->setShadowLift(lift);
}

void BrightnessService::increaseShadowLift(double amount, int monitorIndex) {
    BR_DEBUG("increaseShadowLift(amount=%f monitor=%d)", amount, monitorIndex);
    if (!m_manager) return;
    if (monitorIndex >= 0) m_manager->increaseShadowLift(monitorIndex, amount);
    else m_manager->increaseShadowLift(amount);
}

void BrightnessService::decreaseShadowLift(double amount, int monitorIndex) {
    BR_DEBUG("decreaseShadowLift(amount=%f monitor=%d)", amount, monitorIndex);
    if (!m_manager) return;
    if (monitorIndex >= 0) m_manager->decreaseShadowLift(monitorIndex, amount);
    else m_manager->decreaseShadowLift(amount);
}

void BrightnessService::setBrightnessAndRGB(double brightness, double red, double green, double blue, int monitorIndex) {
    BR_DEBUG("setBrightnessAndRGB(b=%f r=%f g=%f bl=%f monitor=%d)", brightness, red, green, blue, monitorIndex);
    if (!m_manager) return;
    if (monitorIndex >= 0) m_manager->setBrightnessAndRGB(monitorIndex, brightness, red, green, blue);
    else m_manager->setBrightnessAndRGB(brightness, red, green, blue);
}

void BrightnessService::setBrightnessAndTemperature(double brightness, int kelvin, int monitorIndex) {
    BR_DEBUG("setBrightnessAndTemperature(b=%f k=%d monitor=%d)", brightness, kelvin, monitorIndex);
    if (!m_manager) return;
    if (monitorIndex >= 0) m_manager->setBrightnessAndTemperature(monitorIndex, brightness, kelvin);
    else m_manager->setBrightnessAndTemperature(brightness, kelvin);
}

void BrightnessService::setBrightnessAndShadowLift(double brightness, double shadowLift, int monitorIndex) {
    BR_DEBUG("setBrightnessAndShadowLift(b=%f sl=%f monitor=%d)", brightness, shadowLift, monitorIndex);
    if (!m_manager) return;
    if (monitorIndex >= 0) m_manager->setBrightnessAndShadowLift(monitorIndex, brightness, shadowLift);
    else m_manager->setBrightnessAndShadowLift(brightness, shadowLift);
}

void BrightnessService::enableDayNightMode(double dayBrightness, double nightBrightness, int dayTemperature, int nightTemperature, int dayStartHour, int nightStartHour, int checkIntervalMinutes) {
    BR_DEBUG("enableDayNightMode(db=%f nb=%f dt=%d nt=%d ds=%d ns=%d ci=%d)",
             dayBrightness, nightBrightness, dayTemperature, nightTemperature, dayStartHour, nightStartHour, checkIntervalMinutes);
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
    BR_DEBUG("disableDayNightMode()");
    if (!m_manager) return;
    m_manager->disableDayNightMode();
}

bool BrightnessService::isDayNightModeEnabled() const {
    BR_DEBUG("isDayNightModeEnabled()");
    if (!m_manager) return false;
    return m_manager->isDayNightModeEnabled();
}

void BrightnessService::setDaySettings(double brightness, int temperature) {
    BR_DEBUG("setDaySettings(b=%f t=%d)", brightness, temperature);
    if (!m_manager) return;
    m_manager->setDaySettings(brightness, temperature);
}

void BrightnessService::setNightSettings(double brightness, int temperature) {
    BR_DEBUG("setNightSettings(b=%f t=%d)", brightness, temperature);
    if (!m_manager) return;
    m_manager->setNightSettings(brightness, temperature);
}

void BrightnessService::setDayNightTiming(int dayStart, int nightStart) {
    BR_DEBUG("setDayNightTiming(ds=%d ns=%d)", dayStart, nightStart);
    if (!m_manager) return;
    m_manager->setDayNightTiming(dayStart, nightStart);
}

bool BrightnessService::switchToDay(int monitorIndex) {
    BR_DEBUG("switchToDay(monitor=%d)", monitorIndex);
    if (!m_manager) return false;
    if (monitorIndex >= 0) return m_manager->switchToDay(monitorIndex);
    return m_manager->switchToDay();
}

bool BrightnessService::switchToNight(int monitorIndex) {
    BR_DEBUG("switchToNight(monitor=%d)", monitorIndex);
    if (!m_manager) return false;
    if (monitorIndex >= 0) return m_manager->switchToNight(monitorIndex);
    return m_manager->switchToNight();
}

bool BrightnessService::isDay() const {
    BR_DEBUG("isDay()");
    if (!m_manager) return false;
    return m_manager->isDay();
}

std::vector<std::string> BrightnessService::getConnectedMonitors() const {
    BR_DEBUG("getConnectedMonitors() m_manager=%p", (void*)m_manager);
    if (!m_manager) return {};
    return m_manager->getConnectedMonitors();
}

std::string BrightnessService::getMonitor(int index) const {
    BR_DEBUG("getMonitor(index=%d)", index);
    if (!m_manager) return {};
    return m_manager->getMonitor(index);
}

} // namespace havel::host
