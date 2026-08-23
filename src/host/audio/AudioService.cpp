/*
 * AudioService.cpp
 *
 * Pure C++ audio service implementation.
 */
#include "AudioService.hpp"
#include "core/media/AudioManager.hpp"

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

std::string AudioService::getActiveApplicationName() const {
    return m_manager ? m_manager->getActiveApplicationName() : "";
}

double AudioService::getActiveAppVolume() const {
    if (!m_manager) return 0.0;
    auto name = m_manager->getActiveApplicationName();
    if (name.empty()) {
        // No active app — return master volume (NOT arbitrary apps[0])
        return m_manager->getVolume();
    }
    return m_manager->getApplicationVolume(name);
}

void AudioService::increaseActiveAppVolume(double amount) {
    if (!m_manager) return;
    auto name = m_manager->getActiveApplicationName();
    if (name.empty()) {
        auto apps = m_manager->getApplications();
        if (!apps.empty()) {
            name = apps[0].name;
        }
    }
    if (!name.empty()) {
        double vol = m_manager->getApplicationVolume(name);
        m_manager->setApplicationVolume(name, std::clamp(vol + amount, 0.0, 1.5));
    }
}

void AudioService::decreaseActiveAppVolume(double amount) {
    if (!m_manager) return;
    auto name = m_manager->getActiveApplicationName();
    if (name.empty()) {
        auto apps = m_manager->getApplications();
        if (!apps.empty()) {
            name = apps[0].name;
        }
    }
    if (!name.empty()) {
        double vol = m_manager->getApplicationVolume(name);
        m_manager->setApplicationVolume(name, std::clamp(vol - amount, 0.0, 1.5));
    }
}

std::vector<havel::AudioDevice> AudioService::getAllDevices() const {
    if (!m_manager) return {};
    return m_manager->getDevices();
}

havel::AudioDevice AudioService::findDeviceByName(const std::string &name) const {
    if (!m_manager) return {};
    auto *dev = m_manager->findDeviceByName(name);
    if (!dev) return {};
    havel::AudioDevice copy = *dev;
    delete dev;
    return copy;
}

havel::AudioDevice AudioService::findDeviceByIndex(uint32_t index) const {
    if (!m_manager) return {};
    auto *dev = m_manager->findDeviceByIndex(index);
    if (!dev) return {};
    havel::AudioDevice copy = *dev;
    delete dev;
    return copy;
}

bool AudioService::setDefaultOutput(const std::string &device) {
    return m_manager && m_manager->setDefaultOutput(device);
}

std::string AudioService::getDefaultOutput() const {
  return m_manager ? m_manager->getDefaultOutput() : "";
}

std::string AudioService::getDefaultInput() const {
  return m_manager ? m_manager->getDefaultInput() : "";
}

std::string AudioService::getBackendName() const {
  return m_manager ? std::string(m_manager->getBackend() == AudioBackend::PIPEWIRE ? "PipeWire" :
    m_manager->getBackend() == AudioBackend::PULSE ? "PulseAudio" :
    m_manager->getBackend() == AudioBackend::ALSA ? "ALSA" :
    m_manager->getBackend() == AudioBackend::WINDOWS ? "Windows" : "unknown") : "";
}

bool AudioService::playTestSound() {
  return m_manager && m_manager->playTestSound();
}

bool AudioService::playSound(const std::string &soundFile) {
  return m_manager && m_manager->playSound(soundFile);
}

bool AudioService::setApplicationVolume(const std::string &name, double volume) {
  return m_manager && m_manager->setApplicationVolume(name, volume);
}

std::vector<havel::AudioManager::ApplicationInfo> AudioService::getApplications() const {
  if (!m_manager) return {};
  return m_manager->getApplications();
}

} // namespace havel::host
