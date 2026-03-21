/*
 * BrightnessService.cpp
 *
 * Pure C++ brightness service implementation.
 */
#include "BrightnessService.hpp"
#include "core/BrightnessManager.hpp"

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
    return monitorIndex >= 0 ? m_manager->getTemperature(monitorIndex) : static_cast<int>(m_manager->getTemperature());
}

void BrightnessService::setTemperature(int temperature, int monitorIndex) {
    if (!m_manager) return;
    if (monitorIndex >= 0) m_manager->setTemperature(monitorIndex, static_cast<uint32_t>(temperature));
    else m_manager->setTemperature(static_cast<uint32_t>(temperature));
}

} // namespace havel::host
