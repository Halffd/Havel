/*
 * AudioService.cpp
 *
 * Pure C++ audio service implementation.
 */
#include "AudioService.hpp"
#include "media/AudioManager.hpp"

namespace havel::host {

AudioService::AudioService(havel::AudioManager* manager)
    : m_manager(manager) {}

double AudioService::getVolume() const {
    return m_manager ? m_manager->getVolume() : 0.5;
}

void AudioService::setVolume(double volume) {
    if (m_manager) m_manager->setVolume(volume);
}

void AudioService::increaseVolume(double amount) {
    if (m_manager) m_manager->increaseVolume(amount);
}

void AudioService::decreaseVolume(double amount) {
    if (m_manager) m_manager->decreaseVolume(amount);
}

void AudioService::toggleMute() {
    if (m_manager) m_manager->toggleMute();
}

void AudioService::setMute(bool muted) {
    if (m_manager) m_manager->setMute(muted);
}

bool AudioService::isMuted() const {
    return m_manager && m_manager->isMuted();
}

} // namespace havel::host
