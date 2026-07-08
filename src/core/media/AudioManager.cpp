#include "AudioManager.hpp"
#include "backends/AudioBackendImpl.hpp"
#include <algorithm>
#include <chrono>

namespace havel {

static std::unique_ptr<AudioManager> g_instance;

AudioManager &AudioManager::get() {
  if (!g_instance) {
    g_instance = std::make_unique<AudioManager>(AudioBackend::AUTO);
    g_instance->ensureBackend();  // A1: backend always ready after get()
  }
  return *g_instance;
}

AudioManager::AudioManager(AudioBackend) {
  // Backend is initialized lazily on first use via ensureBackend()
}

AudioManager::~AudioManager() {
  stopMonitoring();
}

void AudioManager::ensureBackend() {
  std::call_once(backendInitFlag_, [this]() {
    backend_.reset(CreateAudioBackend(AudioBackend::AUTO));
    if (backend_) {
      info("Audio backend initialized: {}", backend_->getName());
      {
        std::lock_guard<std::mutex> lk(deviceMutex);
        backend_->updateDeviceCache(cachedDevices);
      }
      startMonitoring();
    } else {
      warning("No audio backend available");
    }
  });
}

bool AudioManager::setVolume(double v) { return setVolume(getDefaultOutput(), v); }
bool AudioManager::setVolume(const std::string &d, double v) {
  ensureBackend();
  v = std::clamp(v, MIN_VOLUME, MAX_VOLUME);
  bool ok = backend_ && backend_->setVolume(d, v);
  if (ok) {
    std::lock_guard<std::mutex> lk(callbackMutex);
    if (volumeCallback) volumeCallback(d, v);
  }
  return ok;
}
double AudioManager::getVolume() { return getVolume(getDefaultOutput()); }
double AudioManager::getVolume(const std::string &d) { ensureBackend(); return backend_ ? backend_->getVolume(d) : 1.0; }
bool AudioManager::increaseVolume(double a) { return setVolume(getVolume() + a); }
bool AudioManager::increaseVolume(const std::string &d, double a) { return setVolume(d, getVolume(d) + a); }
bool AudioManager::decreaseVolume(double a) { return setVolume(getVolume() - a); }
bool AudioManager::decreaseVolume(const std::string &d, double a) { return setVolume(d, getVolume(d) - a); }

bool AudioManager::toggleMute() { return setMute(!isMuted()); }
bool AudioManager::toggleMute(const std::string &d) { return setMute(d, !isMuted(d)); }
bool AudioManager::setMute(bool m) { return setMute(getDefaultOutput(), m); }
bool AudioManager::setMute(const std::string &d, bool m) {
  ensureBackend();
  bool ok = backend_ && backend_->setMute(d, m);
  if (ok) {
    std::lock_guard<std::mutex> lk(callbackMutex);
    if (muteCallback) muteCallback(d, m);
  }
  return ok;
}
bool AudioManager::isMuted() { return isMuted(getDefaultOutput()); }
bool AudioManager::isMuted(const std::string &d) { ensureBackend(); return backend_ && backend_->isMuted(d); }

const std::vector<AudioDevice> &AudioManager::getDevices() const {
  // Caller must NOT hold the reference across an interrupt / yield —
  // monitor thread rewrites the vector under deviceMutex.
  std::lock_guard<std::mutex> lk(deviceMutex);
  if (backend_) backend_->updateDeviceCache(cachedDevices);
  return cachedDevices;
}
std::vector<AudioDevice> AudioManager::getDevicesSnapshot() const {
  std::lock_guard<std::mutex> lk(deviceMutex);
  if (backend_ && cachedDevices.empty()) backend_->updateDeviceCache(cachedDevices);
  return cachedDevices;
}
std::vector<AudioDevice> AudioManager::getOutputDevices() const {
  std::vector<AudioDevice> out;
  for (const auto &d : getDevicesSnapshot()) if (!d.isInput) out.push_back(d);
  return out;
}
std::vector<AudioDevice> AudioManager::getInputDevices() const {
  std::vector<AudioDevice> in;
  for (const auto &d : getDevicesSnapshot()) if (d.isInput) in.push_back(d);
  return in;
}
std::string AudioManager::getDefaultOutput() const {
  return defaultOutputDevice.empty() ? "default" : defaultOutputDevice;
}
std::string AudioManager::getDefaultInput() const { return "default"; }
bool AudioManager::setDefaultOutput(const std::string &d) { defaultOutputDevice = d; return true; }

AudioDevice *AudioManager::findDeviceByName(const std::string &name) {
  // Returned pointer is a heap copy of the snapshot — caller owns it.
  auto snap = getDevicesSnapshot();
  auto it = std::find_if(snap.begin(), snap.end(),
    [&](const AudioDevice &d) { return d.name == name; });
  return it != snap.end() ? new AudioDevice(*it) : nullptr;
}

AudioDevice *AudioManager::findDeviceByIndex(uint32_t index) {
  auto snap = getDevicesSnapshot();
  auto it = std::find_if(snap.begin(), snap.end(),
    [index](const AudioDevice &d) { return d.index == index; });
  return it != snap.end() ? new AudioDevice(*it) : nullptr;
}

std::optional<AudioDevice> AudioManager::findDeviceByNameValue(const std::string &name) const {
  auto snap = getDevicesSnapshot();
  auto it = std::find_if(snap.begin(), snap.end(),
    [&](const AudioDevice &d) { return d.name == name; });
  return it != snap.end() ? std::optional<AudioDevice>(*it) : std::nullopt;
}

std::optional<AudioDevice> AudioManager::findDeviceByIndexValue(uint32_t index) const {
  auto snap = getDevicesSnapshot();
  auto it = std::find_if(snap.begin(), snap.end(),
    [index](const AudioDevice &d) { return d.index == index; });
  return it != snap.end() ? std::optional<AudioDevice>(*it) : std::nullopt;
}

bool AudioManager::playTestSound() { ensureBackend(); return backend_ && backend_->playTestSound(); }
bool AudioManager::playSound(const std::string &f) { ensureBackend(); return backend_ && backend_->playSound(f); }

void AudioManager::setVolumeCallback(VolumeCallback cb) {
  std::lock_guard<std::mutex> lk(callbackMutex);
  volumeCallback = std::move(cb);
}
void AudioManager::setMuteCallback(MuteCallback cb) {
  std::lock_guard<std::mutex> lk(callbackMutex);
  muteCallback = std::move(cb);
}
void AudioManager::setDeviceCallback(DeviceCallback cb) {
  std::lock_guard<std::mutex> lk(callbackMutex);
  deviceCallback = std::move(cb);
}

bool AudioManager::setApplicationVolume(const std::string &n, double v) { ensureBackend(); return backend_ && backend_->setApplicationVolume(n, v); }
double AudioManager::getApplicationVolume(const std::string &n) const { return backend_ ? backend_->getApplicationVolume(n) : 1.0; }
std::vector<AudioManager::ApplicationInfo> AudioManager::getApplications() const { return backend_ ? backend_->getApplications() : std::vector<ApplicationInfo>(); }
std::string AudioManager::getActiveApplicationName() const { return ""; }

AudioBackend AudioManager::getBackend() const {
  // A9: do not fabricate ALSA when no backend is initialized;
  // callers wanting meaningful value should use ensureBackend() first.
  if (!backend_) return AudioBackend::AUTO;
  return backend_->getType();
}

void AudioManager::startMonitoring() {
  monitoring = true;
  monitorThread = std::make_unique<std::thread>([this] {
    while (monitoring.load(std::memory_order_acquire)) {
      std::this_thread::sleep_for(std::chrono::seconds(2));
      if (!backend_) continue;
      DeviceCallback cb;
      {
        std::lock_guard<std::mutex> lk(callbackMutex);
        cb = deviceCallback;
      }
      std::vector<AudioDevice> old, next;
      {
        std::lock_guard<std::mutex> lk(deviceMutex);
        old = cachedDevices;
        backend_->updateDeviceCache(cachedDevices);
        next = cachedDevices;
      }
      if (cb && old != next) {
        for (const auto &d : next) {
          if (std::find(old.begin(), old.end(), d) == old.end()) cb(d, true);
        }
        for (const auto &o : old) {
          if (std::find(next.begin(), next.end(), o) == next.end()) cb(o, false);
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
