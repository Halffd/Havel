#pragma once
#include "IAudioBackend.hpp"
#include <mutex>

namespace havel {

class AudioBackendImpl : public IAudioBackend {
public:
  AudioBackendImpl();
  ~AudioBackendImpl() override;

  bool initialize() override;
  void cleanup() override;
  AudioBackend getType() const override;
  std::string getName() const override;

  bool setVolume(const std::string &device, double volume) override;
  double getVolume(const std::string &device) override;
  bool setMute(const std::string &device, bool muted) override;
  bool isMuted(const std::string &device) override;

  std::vector<AudioDevice> getDevices(bool input) const override;
  void updateDeviceCache(std::vector<AudioDevice> &cache) const override;
  std::string getDefaultOutput() const override;
  std::string getDefaultInput() const override;

  bool setApplicationVolume(const std::string &appName, double volume) override;
  bool setApplicationVolume(uint32_t appIndex, double volume) override;
  double getApplicationVolume(const std::string &appName) const override;
  double getApplicationVolume(uint32_t appIndex) const override;
  std::vector<AudioManager::ApplicationInfo> getApplications() const override;
  std::string getActiveApplicationName() const override;

  bool playTestSound() override;
  bool playSound(const std::string &soundFile) override;

  AudioBackend activeBackend = AudioBackend::ALSA;
  mutable std::vector<AudioDevice> cachedDevices;
  mutable std::mutex deviceMutex;
};

IAudioBackend *CreateAudioBackend(AudioBackend preferred);

} // namespace havel
