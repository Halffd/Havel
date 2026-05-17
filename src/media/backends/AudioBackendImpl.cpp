#include "AudioBackendImpl.hpp"
#include <cmath>
#include <cstdlib>
#include <unistd.h>

namespace havel {

AudioBackendImpl::AudioBackendImpl() {
  // Try backends in order
#ifdef HAVE_PIPEWIRE
  if (access("/run/pipewire-0", F_OK) == 0) { activeBackend = AudioBackend::PIPEWIRE; return; }
#endif
#ifdef HAVE_PULSEAUDIO
  if (access("/run/user", F_OK) == 0 && system("pactl info >/dev/null 2>&1") == 0) { activeBackend = AudioBackend::PULSE; return; }
#endif
#ifdef HAVE_ALSA
  if (access("/dev/snd", F_OK) == 0) { activeBackend = AudioBackend::ALSA; return; }
#endif
}
AudioBackendImpl::~AudioBackendImpl() = default;
bool AudioBackendImpl::initialize() { return true; }
void AudioBackendImpl::cleanup() {}

AudioBackend AudioBackendImpl::getType() const { return activeBackend; }
std::string AudioBackendImpl::getName() const {
  switch (activeBackend) {
    case AudioBackend::PIPEWIRE: return "PipeWire";
    case AudioBackend::PULSE: return "PulseAudio";
    case AudioBackend::ALSA: return "ALSA";
    default: return "null";
  }
}

bool AudioBackendImpl::setVolume(const std::string &d, double v) {
  v = std::clamp(v, 0.0, 1.5);
#ifdef HAVE_ALSA
  if (activeBackend == AudioBackend::ALSA) {
    std::string cmd = "amixer set Master " + std::to_string(static_cast<int>(v * 100)) + "% 2>/dev/null";
    return system(cmd.c_str()) == 0;
  }
#endif
#ifdef HAVE_PULSEAUDIO
  if (activeBackend == AudioBackend::PULSE) {
    std::string cmd = "pactl set-sink-volume @DEFAULT_SINK@ " +
      std::to_string(static_cast<int>(v * 100)) + "% 2>/dev/null";
    return system(cmd.c_str()) == 0;
  }
#endif
  return false;
}

double AudioBackendImpl::getVolume(const std::string &) {
#ifdef HAVE_ALSA
  if (activeBackend == AudioBackend::ALSA) {
    auto fp = popen("amixer get Master 2>/dev/null | grep -oP '\\d+%' | head -1", "r");
    if (!fp) return 1.0;
    char buf[16] = {};
    if (fgets(buf, sizeof(buf), fp)) { pclose(fp); return std::atoi(buf) / 100.0; }
    pclose(fp);
  }
#endif
#ifdef HAVE_PULSEAUDIO
  if (activeBackend == AudioBackend::PULSE) {
    auto fp = popen("pactl get-sink-volume @DEFAULT_SINK@ 2>/dev/null | head -1 | grep -oP '\\d+%' | head -1", "r");
    if (!fp) return 1.0;
    char buf[16] = {};
    if (fgets(buf, sizeof(buf), fp)) { pclose(fp); return std::atoi(buf) / 100.0; }
    pclose(fp);
  }
#endif
  return 1.0;
}

bool AudioBackendImpl::setMute(const std::string &, bool m) {
#ifdef HAVE_ALSA
  if (activeBackend == AudioBackend::ALSA) {
    std::string cmd = "amixer set Master " + std::string(m ? "mute" : "unmute") + " 2>/dev/null";
    return system(cmd.c_str()) == 0;
  }
#endif
#ifdef HAVE_PULSEAUDIO
  if (activeBackend == AudioBackend::PULSE) {
    std::string cmd = "pactl set-sink-mute @DEFAULT_SINK@ " + std::string(m ? "1" : "0") + " 2>/dev/null";
    return system(cmd.c_str()) == 0;
  }
#endif
  return false;
}

bool AudioBackendImpl::isMuted(const std::string &) {
  return false;
}

void AudioBackendImpl::updateDeviceCache(std::vector<AudioDevice> &c) const {
  std::lock_guard<std::mutex> lock(deviceMutex);
  c.clear();
  AudioDevice d;
  d.name = "default"; d.description = "Default audio device";
  d.index = 0; d.volume = const_cast<AudioBackendImpl *>(this)->getVolume("");
  c.push_back(d);
}

std::vector<AudioDevice> AudioBackendImpl::getDevices(bool) const {
  std::lock_guard<std::mutex> lock(deviceMutex);
  return cachedDevices;
}

std::string AudioBackendImpl::getDefaultOutput() const { return "default"; }
std::string AudioBackendImpl::getDefaultInput() const { return "default"; }

bool AudioBackendImpl::setApplicationVolume(const std::string &, double) { return false; }
bool AudioBackendImpl::setApplicationVolume(uint32_t, double) { return false; }
double AudioBackendImpl::getApplicationVolume(const std::string &) const { return 1.0; }
double AudioBackendImpl::getApplicationVolume(uint32_t) const { return 1.0; }
std::vector<AudioManager::ApplicationInfo> AudioBackendImpl::getApplications() const { return {}; }
std::string AudioBackendImpl::getActiveApplicationName() const { return ""; }

bool AudioBackendImpl::playTestSound() {
  return system("speaker-test -c 2 -l 1 -t sine -f 440 2>/dev/null") == 0;
}

bool AudioBackendImpl::playSound(const std::string &f) {
  return system(("paplay " + f + " 2>/dev/null").c_str()) == 0 ||
         system(("aplay " + f + " 2>/dev/null").c_str()) == 0;
}

IAudioBackend *CreateAudioBackend(AudioBackend) {
  return new AudioBackendImpl();
}

} // namespace havel
