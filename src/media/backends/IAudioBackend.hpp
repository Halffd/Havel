#pragma once
#include "../AudioManager.hpp"
#include <string>
#include <vector>
#include <functional>

namespace havel {

class IAudioBackend {
public:
  virtual ~IAudioBackend() = default;

  virtual bool initialize() = 0;
  virtual void cleanup() = 0;
  virtual AudioBackend getType() const = 0;
  virtual std::string getName() const = 0;

  virtual bool setVolume(const std::string &device, double volume) = 0;
  virtual double getVolume(const std::string &device) = 0;
  virtual bool setMute(const std::string &device, bool muted) = 0;
  virtual bool isMuted(const std::string &device) = 0;

  virtual std::vector<AudioDevice> getDevices(bool input) const = 0;
  virtual void updateDeviceCache(std::vector<AudioDevice> &cache) const = 0;
  virtual std::string getDefaultOutput() const = 0;
  virtual std::string getDefaultInput() const = 0;

  virtual bool setApplicationVolume(const std::string &appName, double volume) = 0;
  virtual bool setApplicationVolume(uint32_t appIndex, double volume) = 0;
  virtual double getApplicationVolume(const std::string &appName) const = 0;
  virtual double getApplicationVolume(uint32_t appIndex) const = 0;
  virtual std::vector<AudioManager::ApplicationInfo> getApplications() const = 0;
  virtual std::string getActiveApplicationName() const = 0;

  virtual bool playTestSound() = 0;
  virtual bool playSound(const std::string &soundFile) = 0;

  using VolumeCallback = std::function<void(const std::string &, double)>;
  using MuteCallback = std::function<void(const std::string &, bool)>;
  using DeviceCallback = std::function<void(const AudioDevice &, bool)>;

  virtual void setVolumeCallback(VolumeCallback) {}
  virtual void setMuteCallback(MuteCallback) {}
  virtual void setDeviceCallback(DeviceCallback) {}
};

} // namespace havel
