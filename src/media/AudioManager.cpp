#include "AudioManager.hpp"
#include "backends/AudioBackendImpl.hpp"
#include <algorithm>
#include <chrono>

namespace havel {

static std::unique_ptr<AudioManager> g_instance;

AudioManager &AudioManager::get() {
  if (!g_instance) g_instance = std::make_unique<AudioManager>(AudioBackend::AUTO);
  return *g_instance;
}

AudioManager::AudioManager(AudioBackend) {
  backend_.reset(CreateAudioBackend(AudioBackend::AUTO));
  info("AudioManager proxy created, backend: {}", backend_ ? backend_->getName() : "none");
  startMonitoring();
}

AudioManager::~AudioManager() {
  stopMonitoring();
}

bool AudioManager::setVolume(double v) { return setVolume(getDefaultOutput(), v); }
bool AudioManager::setVolume(const std::string &d, double v) {
  v = std::clamp(v, MIN_VOLUME, MAX_VOLUME);
  bool ok = backend_ && backend_->setVolume(d, v);
  if (ok && volumeCallback) volumeCallback(d, v);
  return ok;
}
double AudioManager::getVolume() { return getVolume(getDefaultOutput()); }
double AudioManager::getVolume(const std::string &d) { return backend_ ? backend_->getVolume(d) : 1.0; }
bool AudioManager::increaseVolume(double a) { return setVolume(getVolume() + a); }
bool AudioManager::increaseVolume(const std::string &d, double a) { return setVolume(d, getVolume(d) + a); }
bool AudioManager::decreaseVolume(double a) { return setVolume(getVolume() - a); }
bool AudioManager::decreaseVolume(const std::string &d, double a) { return setVolume(d, getVolume(d) - a); }

bool AudioManager::toggleMute() { return setMute(!isMuted()); }
bool AudioManager::toggleMute(const std::string &d) { return setMute(d, !isMuted(d)); }
bool AudioManager::setMute(bool m) { return setMute(getDefaultOutput(), m); }
bool AudioManager::setMute(const std::string &d, bool m) {
  bool ok = backend_ && backend_->setMute(d, m);
  if (ok && muteCallback) muteCallback(d, m);
  return ok;
}
bool AudioManager::isMuted() { return isMuted(getDefaultOutput()); }
bool AudioManager::isMuted(const std::string &d) { return backend_ && backend_->isMuted(d); }

const std::vector<AudioDevice> &AudioManager::getDevices() const {
  if (backend_) backend_->updateDeviceCache(cachedDevices);
  return cachedDevices;
}
std::vector<AudioDevice> AudioManager::getOutputDevices() const { return getDevices(); }
std::string AudioManager::getDefaultOutput() const { return "default"; }
std::string AudioManager::getDefaultInput() const { return "default"; }
bool AudioManager::setDefaultOutput(const std::string &d) { defaultOutputDevice = d; return true; }

AudioDevice *AudioManager::findDeviceByName(const std::string &name) {
  auto &devs = getDevices();
  auto it = std::find_if(devs.begin(), devs.end(),
    [&](const AudioDevice &d) { return d.name == name; });
  return it != devs.end() ? const_cast<AudioDevice *>(&*it) : nullptr;
}

AudioDevice *AudioManager::findDeviceByIndex(uint32_t index) {
  auto &devs = getDevices();
  auto it = std::find_if(devs.begin(), devs.end(),
    [index](const AudioDevice &d) { return d.index == index; });
  return it != devs.end() ? const_cast<AudioDevice *>(&*it) : nullptr;
}

bool AudioManager::playTestSound() { return backend_ && backend_->playTestSound(); }
bool AudioManager::playSound(const std::string &f) { return backend_ && backend_->playSound(f); }

void AudioManager::setVolumeCallback(VolumeCallback cb) { volumeCallback = cb; }
void AudioManager::setMuteCallback(MuteCallback cb) { muteCallback = cb; }
void AudioManager::setDeviceCallback(DeviceCallback cb) { deviceCallback = cb; }

bool AudioManager::setApplicationVolume(const std::string &n, double v) { return backend_ && backend_->setApplicationVolume(n, v); }
double AudioManager::getApplicationVolume(const std::string &n) const { return backend_ ? backend_->getApplicationVolume(n) : 1.0; }
std::vector<AudioManager::ApplicationInfo> AudioManager::getApplications() const { return backend_ ? backend_->getApplications() : std::vector<ApplicationInfo>(); }
std::string AudioManager::getActiveApplicationName() const { return ""; }

AudioBackend AudioManager::getBackend() const { return backend_ ? backend_->getType() : AudioBackend::ALSA; }

void AudioManager::startMonitoring() {
  monitoring = true;
  monitorThread = std::make_unique<std::thread>([this] {
    while (monitoring) {
      std::this_thread::sleep_for(std::chrono::seconds(2));
      if (!backend_) continue;
      std::vector<AudioDevice> old = cachedDevices;
      backend_->updateDeviceCache(cachedDevices);
      if (deviceCallback && old != cachedDevices) {
        for (const auto &d : cachedDevices) {
          if (std::find(old.begin(), old.end(), d) == old.end()) deviceCallback(d, true);
        }
        for (const auto &o : old) {
          if (std::find(cachedDevices.begin(), cachedDevices.end(), o) == cachedDevices.end()) deviceCallback(o, false);
        }
      }
    }
  });
}

void AudioManager::stopMonitoring() {
  monitoring = false;
  if (monitorThread && monitorThread->joinable()) monitorThread->join();
}

} // namespace havel
