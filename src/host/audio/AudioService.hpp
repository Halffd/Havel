/*
 * AudioService.hpp
 *
 * Pure C++ audio service - no VM, no interpreter, no HavelValue.
 */
#pragma once

#include "core/media/AudioManager.hpp"
#include <string>
#include <vector>
#include <cstdint>

namespace havel {
}

namespace havel::host {

class __attribute__((visibility("default"))) AudioService {
public:
    explicit AudioService(havel::AudioManager* manager);
    ~AudioService() = default;

    double getVolume() const;
    void setVolume(double volume);
    void increaseVolume(double amount);
    void decreaseVolume(double amount);
    
    void toggleMute();
    void setMute(bool muted);
    bool isMuted() const;

    // Application-level volume
    std::string getActiveApplicationName() const;
    double getActiveAppVolume() const;
    void increaseActiveAppVolume(double amount);
    void decreaseActiveAppVolume(double amount);

    // Device queries
    std::vector<havel::AudioDevice> getAllDevices() const;
    havel::AudioDevice findDeviceByName(const std::string &name) const;
    havel::AudioDevice findDeviceByIndex(uint32_t index) const;

  // Default device control
  bool setDefaultOutput(const std::string &device);
  std::string getDefaultOutput() const;
  std::string getDefaultInput() const;

  // Backend info
  std::string getBackendName() const;

  // Sound
  bool playTestSound();
  bool playSound(const std::string &soundFile);

  // Per-application volume
  bool setApplicationVolume(const std::string &name, double volume);
  std::vector<havel::AudioManager::ApplicationInfo> getApplications() const;

private:
    havel::AudioManager* m_manager;
};

} // namespace havel::host
