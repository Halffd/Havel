#include "AudioManager.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>

#ifdef __linux__
#include <pulse/error.h>
#include <pulse/volume.h>
#endif

namespace havel {

// Helper userdata structs for threaded mainloop signaling (must be visible to
// all callbacks)
#ifdef __linux__
struct PAResultDouble {
  double *out;
  pa_threaded_mainloop *ml;
};
struct PAResultBool {
  bool *out;
  pa_threaded_mainloop *ml;
};
struct PAResultString {
  std::string *out;
  pa_threaded_mainloop *ml;
};
struct PAResultDevices {
  std::vector<AudioDevice> *out;
  pa_threaded_mainloop *ml;
};
struct PAResultApps {
  std::vector<AudioManager::ApplicationInfo> *out;
  pa_threaded_mainloop *ml;
};
#endif

AudioManager::AudioManager(AudioBackend backend) : currentBackend(backend) {
  debug("Initializing AudioManager with backend: {}",
        backend == AudioBackend::AUTO    ? "AUTO"
        : backend == AudioBackend::PULSE ? "PULSE"
                                         : "ALSA");

  if (backend == AudioBackend::AUTO) {
    // Try PulseAudio first, fallback to ALSA
    if (initializePulse()) {
      currentBackend = AudioBackend::PULSE;
      info("Using PulseAudio backend");
    } else if (initializeAlsa()) {
      currentBackend = AudioBackend::ALSA;
      info("Using ALSA backend");
    } else {
      error("Failed to initialize any audio backend");
      return;
    }
  } else if (backend == AudioBackend::PULSE) {
    if (!initializePulse()) {
      error("Failed to initialize PulseAudio");
      return;
    }
  } else if (backend == AudioBackend::ALSA) {
    if (!initializeAlsa()) {
      error("Failed to initialize ALSA");
      return;
    }
  }

  startMonitoring();
}

AudioManager::~AudioManager() {
  stopMonitoring();
  cleanup();
}

// === VOLUME CONTROL ===
bool AudioManager::setVolume(double volume) {
  return setVolume(getDefaultOutput(), volume);
}

bool AudioManager::setVolume(const std::string &device, double volume) {
  volume = std::clamp(volume, MIN_VOLUME, MAX_VOLUME);

  bool success = false;
  if (currentBackend == AudioBackend::PULSE) {
    success = setPulseVolume(device, volume);
  } else if (currentBackend == AudioBackend::ALSA) {
    success = setAlsaVolume(volume);
  }

  if (success && volumeCallback) {
    volumeCallback(device, volume);
  }

  debug("Set volume for {}: {:.2f} - {}", device, volume,
        success ? "SUCCESS" : "FAILED");
  return success;
}

double AudioManager::getVolume() { return getVolume(getDefaultOutput()); }

double AudioManager::getVolume(const std::string &device) {
  if (currentBackend == AudioBackend::PULSE) {
    return getPulseVolume(device);
  } else if (currentBackend == AudioBackend::ALSA) {
    return getAlsaVolume();
  }
  return 0.0;
}

bool AudioManager::increaseVolume(double amount) {
  double current = getVolume();
  return setVolume(std::min(MAX_VOLUME, current + amount));
}

bool AudioManager::increaseVolume(const std::string &device, double amount) {
  double current = getVolume(device);
  return setVolume(device, std::min(MAX_VOLUME, current + amount));
}

bool AudioManager::decreaseVolume(double amount) {
  double current = getVolume();
  return setVolume(std::max(MIN_VOLUME, current - amount));
}

bool AudioManager::decreaseVolume(const std::string &device, double amount) {
  double current = getVolume(device);
  return setVolume(device, std::max(MIN_VOLUME, current - amount));
}

// === MUTE CONTROL ===
bool AudioManager::toggleMute() { return setMute(!isMuted()); }

bool AudioManager::toggleMute(const std::string &device) {
  return setMute(device, !isMuted(device));
}

bool AudioManager::setMute(bool muted) {
  return setMute(getDefaultOutput(), muted);
}

bool AudioManager::setMute(const std::string &device, bool muted) {
  bool success = false;
  if (currentBackend == AudioBackend::PULSE) {
    success = setPulseMute(device, muted);
  } else if (currentBackend == AudioBackend::ALSA) {
    success = setAlsaMute(muted);
  }

  if (success && muteCallback) {
    muteCallback(device, muted);
  }

  debug("Set mute for {}: {} - {}", device, muted,
        success ? "SUCCESS" : "FAILED");
  return success;
}

bool AudioManager::isMuted() { return isMuted(getDefaultOutput()); }

bool AudioManager::isMuted(const std::string &device) {
  if (currentBackend == AudioBackend::PULSE) {
    return isPulseMuted(device);
  } else if (currentBackend == AudioBackend::ALSA) {
    return isAlsaMuted();
  }
  return false;
}

// === DEVICE MANAGEMENT ===
// Forward declare the callback function
static void set_default_sink_callback(pa_context *c, int success,
                                      void *userdata) {
  auto *data =
      static_cast<std::pair<bool *, pa_threaded_mainloop *> *>(userdata);
  *(data->first) = (success >= 0);
  pa_threaded_mainloop_signal(data->second, 0);
  delete data;
}

bool AudioManager::setDefaultOutput(const std::string &device) {
  if (currentBackend == AudioBackend::PULSE) {
    if (!pa_context)
      return false;

    pa_threaded_mainloop_lock(pa_mainloop);

    // First, find the device to ensure it exists
    const auto *dev = findDeviceByName(device);
    if (!dev) {
      error("Device not found: {}", device);
      pa_threaded_mainloop_unlock(pa_mainloop);
      return false;
    }

    // Create data to pass to the callback
    auto *callback_data =
        new std::pair<bool *, pa_threaded_mainloop *>(nullptr, pa_mainloop);
    bool success = false;
    callback_data->first = &success;

    // Set the default sink
    pa_operation *op =
        pa_context_set_default_sink(pa_context, dev->name.c_str(),
                                    set_default_sink_callback, callback_data);

    if (op) {
      while (pa_operation_get_state(op) == PA_OPERATION_RUNNING) {
        pa_threaded_mainloop_wait(pa_mainloop);
      }
      pa_operation_unref(op);
    } else {
      delete callback_data;
      pa_threaded_mainloop_unlock(pa_mainloop);
      error("Failed to create operation for setting default sink");
      return false;
    }

    pa_threaded_mainloop_unlock(pa_mainloop);

    if (success) {
      info("Set default output device to: {}", device);
      updateDeviceCache(); // Refresh device cache
    } else {
      error("Failed to set default output device to: {}", device);
    }

    return success;
  }

  // ALSA implementation would go here
  error("Setting default output device is not supported with the current "
        "backend");
  return false;
}

void AudioManager::updateDeviceCache() const {
  std::lock_guard<std::mutex> lock(deviceMutex);

  // Clear existing cache
  cachedDevices.clear();

  // Get devices based on backend
  std::vector<AudioDevice> devices;
  if (currentBackend == AudioBackend::PULSE) {
    auto outputs = getPulseDevices(false);
    auto inputs = getPulseDevices(true);
    devices.reserve(outputs.size() + inputs.size());
    devices.insert(devices.end(), outputs.begin(), outputs.end());
    devices.insert(devices.end(), inputs.begin(), inputs.end());
  } else if (currentBackend == AudioBackend::ALSA) {
    // ALSA implementation would go here
  }

  // Update cache
  cachedDevices = std::move(devices);
}

const std::vector<AudioDevice> &AudioManager::getDevices() const {
  if (cachedDevices.empty()) {
    updateDeviceCache();
  }
  return cachedDevices;
}

std::vector<AudioDevice> AudioManager::getOutputDevices() const {
  std::vector<AudioDevice> result;
  const auto &devices = getDevices();

  // Filter for output devices (non-input)
  std::copy_if(devices.begin(), devices.end(), std::back_inserter(result),
               [](const AudioDevice &dev) {
                 return dev.name.find("input") == std::string::npos;
               });

  return result;
}

std::vector<AudioDevice> AudioManager::getInputDevices() const {
  std::vector<AudioDevice> result;
  const auto &devices = getDevices();

  // Filter for input devices
  std::copy_if(devices.begin(), devices.end(), std::back_inserter(result),
               [](const AudioDevice &dev) {
                 return dev.name.find("input") != std::string::npos;
               });

  return result;
}

AudioDevice *AudioManager::findDeviceByName(const std::string &name) {
  updateDeviceCache();
  auto it = std::find_if(
      cachedDevices.begin(), cachedDevices.end(),
      [&name](const AudioDevice &dev) { return dev.name == name; });
  return it != cachedDevices.end() ? &(*it) : nullptr;
}

const AudioDevice *
AudioManager::findDeviceByName(const std::string &name) const {
  const auto &devices = getDevices();
  auto it = std::find_if(devices.begin(), devices.end(),
                         [&name](const AudioDevice &dev) {
                           return dev.name == name || dev.description == name;
                         });
  return it != devices.end() ? &(*it) : nullptr;
}

AudioDevice *AudioManager::findDeviceByIndex(uint32_t index) {
  updateDeviceCache();
  if (index < cachedDevices.size()) {
    return &cachedDevices[index];
  }
  return nullptr;
}

const AudioDevice *AudioManager::findDeviceByIndex(uint32_t index) const {
  const auto &devices = getDevices();
  if (index < devices.size()) {
    return &devices[index];
  }
  return nullptr;
}

void AudioManager::printDeviceInfo(const AudioDevice &device) const {
  info("Device: {}", device.name);
  info("  Description: {}", device.description);
  info("  Index: {}", device.index);
  info("  Default: {}", device.isDefault ? "Yes" : "No");
  info("  Muted: {}", device.isMuted ? "Yes" : "No");
  info("  Volume: {:.0f}%", device.volume * 100.0);
  info("  Channels: {}", device.channels);
}

void AudioManager::printDevices() const {
  const auto &devices = getDevices();
  if (devices.empty()) {
    info("No audio devices found");
    return;
  }

  info("=== Audio Devices ({} found) ===", devices.size());
  for (size_t i = 0; i < devices.size(); ++i) {
    const auto &device = devices[i];
    info("[{}] {} ({})", i, device.name, device.isDefault ? "Default" : "");
  }
}

std::string AudioManager::getDefaultOutput() const {
  if (currentBackend == AudioBackend::PULSE) {
    // For PulseAudio, we can get the default sink
    if (!pa_context)
      return "";

    std::string defaultSink;
    auto *ctx = const_cast<struct pa_context *>(pa_context);

    pa_threaded_mainloop_lock(pa_mainloop);

    PAResultString data{&defaultSink, pa_mainloop};
    pa_operation *op = pa_context_get_server_info(
        ctx,
        [](struct pa_context *c, const pa_server_info *i, void *userdata) {
          auto *d = static_cast<PAResultString *>(userdata);
          if (!d)
            return;
          if (i && d->out) {
            *(d->out) = i->default_sink_name ? i->default_sink_name : "";
          }
          if (d->ml)
            pa_threaded_mainloop_signal(d->ml, 0);
        },
        &data);

    if (op) {
      while (pa_operation_get_state(op) == PA_OPERATION_RUNNING) {
        pa_threaded_mainloop_wait(pa_mainloop);
      }
      pa_operation_unref(op);
    }

    pa_threaded_mainloop_unlock(pa_mainloop);

    // If we couldn't get the default sink, try to get the first output device
    if (defaultSink.empty()) {
      auto outputs = getOutputDevices();
      if (!outputs.empty()) {
        defaultSink = outputs[0].name;
      }
    }

    return defaultSink;
  }

  // ALSA implementation would go here
  return "";
}

std::string AudioManager::getDefaultInput() const {
  // For PulseAudio, get the default source
  if (currentBackend == AudioBackend::PULSE) {
    if (!pa_context)
      return "";

    std::string defaultSource;
    auto *ctx = const_cast<struct pa_context *>(pa_context);

    pa_threaded_mainloop_lock(pa_mainloop);

    PAResultString data{&defaultSource, pa_mainloop};
    pa_operation *op = pa_context_get_server_info(
        ctx,
        [](struct pa_context *c, const pa_server_info *i, void *userdata) {
          auto *d = static_cast<PAResultString *>(userdata);
          if (!d)
            return;
          if (i && d->out) {
            *(d->out) = i->default_source_name ? i->default_source_name : "";
          }
          if (d->ml)
            pa_threaded_mainloop_signal(d->ml, 0);
        },
        &data);

    if (op) {
      while (pa_operation_get_state(op) == PA_OPERATION_RUNNING) {
        pa_threaded_mainloop_wait(pa_mainloop);
      }
      pa_operation_unref(op);
    }

    pa_threaded_mainloop_unlock(pa_mainloop);

    // If we couldn't get the default source, try to get the first input device
    if (defaultSource.empty()) {
      auto inputs = getInputDevices();
      if (!inputs.empty()) {
        defaultSource = inputs[0].name;
      }
    }

    return defaultSource;
  }

  // ALSA implementation would go here
  return "";
}

bool AudioManager::setDefaultInput(const std::string &device) {
  if (currentBackend == AudioBackend::PULSE) {
    if (!pa_context)
      return false;

    pa_threaded_mainloop_lock(pa_mainloop);

    pa_operation *op = pa_context_set_default_source(pa_context, device.c_str(),
                                                     nullptr, nullptr);

    bool success = false;
    if (op) {
      pa_operation_unref(op);
      success = true;
    }

    pa_threaded_mainloop_unlock(pa_mainloop);
    return success;
  }

  // ALSA implementation would go here
  return false;
}

// === PLAYBACK CONTROL ===
bool AudioManager::playTestSound() {
  // Play a simple sine wave test tone
  return system(
             "speaker-test -t sine -f 440 -l 1 -D default >/dev/null 2>&1") ==
         0;
}

bool AudioManager::playSound(const std::string &soundFile) {
  std::string command;
  if (currentBackend == AudioBackend::PULSE) {
    command = "paplay \"" + soundFile + "\" >/dev/null 2>&1";
  } else {
    command = "aplay \"" + soundFile + "\" >/dev/null 2>&1";
  }
  return system(command.c_str()) == 0;
}

bool AudioManager::playNotificationSound() {
  // Try common notification sound paths
  std::vector<std::string> soundPaths = {
      "/usr/share/sounds/Oxygen-Sys-App-Message.ogg",
      "/usr/share/sounds/KDE-Sys-App-Message.ogg",
      "/usr/share/sounds/ubuntu/stereo/message.ogg",
      "/usr/share/sounds/generic.wav"};

  for (const auto &path : soundPaths) {
    if (playSound(path)) {
      return true;
    }
  }

  // Fallback to system beep
  return system("printf '\\007'") == 0;
}

// === BACKEND IMPLEMENTATIONS ===
#ifdef __linux__

void AudioManager::cleanup() {
  // Clean up PulseAudio resources
  if (pa_context) {
    // Only try to disconnect if we have a valid context state
    pa_context_state_t state = pa_context_get_state(pa_context);
    if (state != PA_CONTEXT_UNCONNECTED && state != PA_CONTEXT_TERMINATED) {
      pa_context_disconnect(pa_context);
      // Give some time for the disconnect to complete
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Remove any callbacks to prevent callbacks during cleanup
    pa_context_set_state_callback(pa_context, nullptr, nullptr);

    // Unref the context
    pa_context_unref(pa_context);
    pa_context = nullptr;
  }

  // Clean up the mainloop if it exists
  if (pa_mainloop) {
    // Stop the mainloop if it's running
    if (pa_threaded_mainloop_in_thread(pa_mainloop)) {
      pa_threaded_mainloop_stop(pa_mainloop);
    }

    // Free the mainloop
    pa_threaded_mainloop_free(pa_mainloop);
    pa_mainloop = nullptr;
  }

  // Clean up ALSA resources
  if (alsa_mixer) {
    if (alsa_elem) {
      // No need to free alsa_elem as it's owned by alsa_mixer
      alsa_elem = nullptr;
    }

    if (snd_mixer_close(alsa_mixer) < 0) {
      error("Failed to close ALSA mixer");
    }
    alsa_mixer = nullptr;
  }
}

bool AudioManager::initializePulse() {
  // Initialize mainloop
  pa_mainloop = pa_threaded_mainloop_new();
  if (!pa_mainloop) {
    error("Failed to create PulseAudio mainloop");
    return false;
  }

  // Lock the mainloop before starting it
  pa_threaded_mainloop_lock(pa_mainloop);

  // Start the mainloop
  if (pa_threaded_mainloop_start(pa_mainloop) < 0) {
    pa_threaded_mainloop_unlock(pa_mainloop);
    pa_threaded_mainloop_free(pa_mainloop);
    pa_mainloop = nullptr;
    error("Failed to start PulseAudio mainloop");
    return false;
  }

  // Create context
  pa_context =
      pa_context_new(pa_threaded_mainloop_get_api(pa_mainloop), "Havel");
  if (!pa_context) {
    pa_threaded_mainloop_unlock(pa_mainloop);
    pa_threaded_mainloop_stop(pa_mainloop);
    pa_threaded_mainloop_free(pa_mainloop);
    pa_mainloop = nullptr;
    error("Failed to create PulseAudio context");
    return false;
  }

  // Set up state callback
  pa_context_set_state_callback(
      pa_context,
      [](struct pa_context *c, void *userdata) {
        pa_threaded_mainloop *m = static_cast<pa_threaded_mainloop *>(userdata);
        pa_threaded_mainloop_signal(m, 0);
      },
      pa_mainloop);

  // Connect to the server
  if (pa_context_connect(pa_context, nullptr, PA_CONTEXT_NOFLAGS, nullptr) <
      0) {
    pa_threaded_mainloop_unlock(pa_mainloop);
    cleanup();
    error("Failed to connect to PulseAudio: {}",
          pa_strerror(pa_context_errno(pa_context)));
    return false;
  }

  // Wait for connection with timeout (5 seconds)
  auto start = std::chrono::steady_clock::now();
  pa_context_state_t state;
  while (true) {
    state = pa_context_get_state(pa_context);
    if (state == PA_CONTEXT_READY) {
      break;
    }
    if (state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED) {
      pa_threaded_mainloop_unlock(pa_mainloop);
      cleanup();
      error("PulseAudio connection failed or was terminated");
      return false;
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed =
        std::chrono::duration_cast<std::chrono::seconds>(now - start).count();
    if (elapsed >= 5) { // 5 second timeout
      pa_threaded_mainloop_unlock(pa_mainloop);
      cleanup();
      error("PulseAudio connection timed out");
      return false;
    }

    // Wait for state change with a small delay
    pa_threaded_mainloop_wait(pa_mainloop);
  }

  pa_threaded_mainloop_unlock(pa_mainloop);
  debug("Successfully connected to PulseAudio");
  return true;
}

bool AudioManager::initializeAlsa() {
  int err;

  // 1. Open the mixer
  err = snd_mixer_open(&alsa_mixer, 0);
  if (err < 0) {
    error("Failed to open ALSA mixer: {}", snd_strerror(err));
    return false;
  }

  // 2. Attach to the default sound card
  const char *card = "default";
  err = snd_mixer_attach(alsa_mixer, card);
  if (err < 0) {
    error("Failed to attach to ALSA card '{}': {}", card, snd_strerror(err));
    snd_mixer_close(alsa_mixer);
    alsa_mixer = nullptr;
    return false;
  }

  // 3. Register the mixer
  err = snd_mixer_selem_register(alsa_mixer, nullptr, nullptr);
  if (err < 0) {
    error("Failed to register ALSA mixer: {}", snd_strerror(err));
    snd_mixer_close(alsa_mixer);
    alsa_mixer = nullptr;
    return false;
  }

  // 4. Load the mixer elements
  err = snd_mixer_load(alsa_mixer);
  if (err < 0) {
    error("Failed to load ALSA mixer elements: {}", snd_strerror(err));
    snd_mixer_close(alsa_mixer);
    alsa_mixer = nullptr;
    return false;
  }

  // 5. Find a suitable playback element
  snd_mixer_selem_id_t *sid = nullptr;
  snd_mixer_selem_id_alloca(&sid);
  if (!sid) {
    error("Failed to allocate ALSA mixer element ID");
    snd_mixer_close(alsa_mixer);
    alsa_mixer = nullptr;
    return false;
  }

  // Try common element names in order of preference
  const char *elem_names[] = {"Master",  "PCM",      "Headphone",
                              "Speaker", "Line Out", nullptr};
  bool found = false;

  for (int i = 0; elem_names[i] != nullptr; i++) {
    snd_mixer_selem_id_set_index(sid, 0);
    snd_mixer_selem_id_set_name(sid, elem_names[i]);

    alsa_elem = snd_mixer_find_selem(alsa_mixer, sid);
    if (alsa_elem) {
      // Verify this element has volume control
      if (snd_mixer_selem_has_playback_volume(alsa_elem)) {
        debug("Using ALSA element: {}", elem_names[i]);
        found = true;
        break;
      }
      alsa_elem = nullptr; // Keep looking if no volume control
    }
  }

  if (!found) {
    // If no named element found, try the first one with volume control
    snd_mixer_elem_t *elem = nullptr;
    for (elem = snd_mixer_first_elem(alsa_mixer); elem;
         elem = snd_mixer_elem_next(elem)) {
      if (snd_mixer_selem_has_playback_volume(elem)) {
        alsa_elem = elem;
        debug("Using first available ALSA element with volume control");
        found = true;
        break;
      }
    }
  }

  if (!found) {
    error("No suitable ALSA mixer element with volume control found");
    snd_mixer_close(alsa_mixer);
    alsa_mixer = nullptr;
    return false;
  }

  // 6. Set volume range (0-100)
  long min, max;
  if (snd_mixer_selem_get_playback_volume_range(alsa_elem, &min, &max) < 0) {
    warning("Could not get ALSA volume range, using defaults");
  } else {
    debug("ALSA volume range: {} to {}", min, max);
  }

  return true;
}

// === PULSEAUDIO IMPLEMENTATIONS ===
bool AudioManager::setPulseVolume(const std::string &device, double volume) {
  if (!pa_context)
    return false;

  pa_volume_t pa_volume = pa_sw_volume_from_linear(volume);
  pa_cvolume cv;
  pa_cvolume_set(&cv, 2, pa_volume); // Stereo

  pa_threaded_mainloop_lock(pa_mainloop);

  pa_operation *op = pa_context_set_sink_volume_by_name(
      pa_context, device.c_str(), &cv, nullptr, nullptr);

  if (op) {
    pa_operation_unref(op);
    pa_threaded_mainloop_unlock(pa_mainloop);
    return true;
  }

  pa_threaded_mainloop_unlock(pa_mainloop);
  return false;
}

// Callback for getting volume
static void pulse_volume_callback(struct pa_context *c, const pa_sink_info *i,
                                  int eol, void *userdata) {
  if (eol || !i || !userdata) {
    if (userdata) {
      auto *data = static_cast<PAResultDouble *>(userdata);
      if (data && data->ml)
        pa_threaded_mainloop_signal(data->ml, 0);
    }
    return;
  }
  auto *data = static_cast<PAResultDouble *>(userdata);
  if (data && data->out) {
    *(data->out) = pa_sw_volume_to_linear(pa_cvolume_avg(&i->volume));
  }
  if (data && data->ml)
    pa_threaded_mainloop_signal(data->ml, 0);
}

double AudioManager::getPulseVolume(const std::string &device) const {
  if (!pa_context)
    return 0.0;

  double volume = 0.0;

  // Cast away const for C API compatibility
  auto *ctx = const_cast<struct pa_context *>(pa_context);

  pa_threaded_mainloop_lock(pa_mainloop);

  PAResultDouble data{&volume, pa_mainloop};
  pa_operation *op = pa_context_get_sink_info_by_name(
      ctx, device.c_str(), pulse_volume_callback, &data);

  if (op) {
    while (pa_operation_get_state(op) == PA_OPERATION_RUNNING) {
      pa_threaded_mainloop_wait(pa_mainloop);
    }
    pa_operation_unref(op);
  }

  pa_threaded_mainloop_unlock(pa_mainloop);
  return volume;
}

bool AudioManager::setPulseMute(const std::string &device, bool muted) {
  if (!pa_context)
    return false;

  pa_threaded_mainloop_lock(pa_mainloop);

  pa_operation *op = pa_context_set_sink_mute_by_name(
      pa_context, device.c_str(), muted ? 1 : 0, nullptr, nullptr);

  if (op) {
    pa_operation_unref(op);
    pa_threaded_mainloop_unlock(pa_mainloop);
    return true;
  }

  pa_threaded_mainloop_unlock(pa_mainloop);
  return false;
}

// Callback for getting mute status
static void pulse_mute_callback(struct pa_context *c, const pa_sink_info *i,
                                int eol, void *userdata) {
  if (eol || !i || !userdata) {
    if (userdata) {
      auto *data = static_cast<PAResultBool *>(userdata);
      if (data && data->ml)
        pa_threaded_mainloop_signal(data->ml, 0);
    }
    return;
  }
  auto *data = static_cast<PAResultBool *>(userdata);
  if (data && data->out) {
    *(data->out) = i->mute;
  }
  if (data && data->ml)
    pa_threaded_mainloop_signal(data->ml, 0);
}

bool AudioManager::isPulseMuted(const std::string &device) const {
  if (!pa_context)
    return false;

  bool muted = false;

  // Cast away const for C API compatibility
  auto *ctx = const_cast<struct pa_context *>(pa_context);

  pa_threaded_mainloop_lock(pa_mainloop);

  PAResultBool data{&muted, pa_mainloop};
  pa_operation *op = pa_context_get_sink_info_by_name(
      ctx, device.c_str(), pulse_mute_callback, &data);

  if (op) {
    while (pa_operation_get_state(op) == PA_OPERATION_RUNNING) {
      pa_threaded_mainloop_wait(pa_mainloop);
    }
    pa_operation_unref(op);
  }

  pa_threaded_mainloop_unlock(pa_mainloop);
  return muted;
}

// Callback for device enumeration (sinks)
static void pulse_sink_device_callback(struct pa_context *c,
                                       const pa_sink_info *i, int eol,
                                       void *userdata) {
  auto *data = static_cast<PAResultDevices *>(userdata);
  if (!data)
    return;
  if (eol) {
    if (data->ml)
      pa_threaded_mainloop_signal(data->ml, 0);
    return;
  }
  if (!i || !data->out)
    return;
  AudioDevice device;
  device.name = i->name ? i->name : "";
  device.description = i->description ? i->description : "";
  device.index = i->index;
  device.channels = i->sample_spec.channels;
  device.volume = pa_sw_volume_to_linear(pa_cvolume_avg(&i->volume));
  device.isMuted = i->mute;
  data->out->push_back(device);
}

// Callback for device enumeration (sources)
static void pulse_source_device_callback(struct pa_context *c,
                                         const pa_source_info *i, int eol,
                                         void *userdata) {
  auto *data = static_cast<PAResultDevices *>(userdata);
  if (!data)
    return;
  if (eol) {
    if (data->ml)
      pa_threaded_mainloop_signal(data->ml, 0);
    return;
  }
  if (!i || !data->out)
    return;
  AudioDevice device;
  device.name = i->name ? i->name : "";
  device.description = i->description ? i->description : "";
  device.index = i->index;
  device.channels = i->sample_spec.channels;
  device.volume = pa_sw_volume_to_linear(pa_cvolume_avg(&i->volume));
  device.isMuted = i->mute;
  data->out->push_back(device);
}

std::vector<AudioDevice> AudioManager::getPulseDevices(bool input) const {
  std::vector<AudioDevice> devices;

  // Use a const_cast to work around the PulseAudio API's lack of
  // const-correctness
  auto *ctx = const_cast<struct pa_context *>(pa_context);
  if (!ctx)
    return devices;

  pa_threaded_mainloop_lock(pa_mainloop);

  PAResultDevices data{&devices, pa_mainloop};
  pa_operation *op = nullptr;
  if (input) {
    op = pa_context_get_source_info_list(ctx, pulse_source_device_callback,
                                         &data);
  } else {
    op = pa_context_get_sink_info_list(ctx, pulse_sink_device_callback, &data);
  }

  if (op) {
    while (pa_operation_get_state(op) == PA_OPERATION_RUNNING) {
      pa_threaded_mainloop_wait(pa_mainloop);
    }
    pa_operation_unref(op);
  }

  pa_threaded_mainloop_unlock(pa_mainloop);
  return devices;
}

// === ALSA IMPLEMENTATIONS ===
bool AudioManager::setAlsaVolume(double volume) {
  if (!alsa_elem)
    return false;

  long min, max;
  snd_mixer_selem_get_playback_volume_range(alsa_elem, &min, &max);

  long alsa_volume = min + (long)((max - min) * volume);

  int err = snd_mixer_selem_set_playback_volume_all(alsa_elem, alsa_volume);
  if (err < 0) {
    error("Failed to set ALSA volume: {}", snd_strerror(err));
    return false;
  }

  return true;
}

double AudioManager::getAlsaVolume() {
  if (!alsa_elem)
    return 0.0;

  long min, max, volume;
  snd_mixer_selem_get_playback_volume_range(alsa_elem, &min, &max);
  snd_mixer_selem_get_playback_volume(alsa_elem, SND_MIXER_SCHN_MONO, &volume);

  return (double)(volume - min) / (max - min);
}

bool AudioManager::setAlsaMute(bool muted) {
  if (!alsa_elem)
    return false;

  int err = snd_mixer_selem_set_playback_switch_all(alsa_elem, muted ? 0 : 1);
  if (err < 0) {
    error("Failed to set ALSA mute: {}", snd_strerror(err));
    return false;
  }

  return true;
}

bool AudioManager::isAlsaMuted() {
  if (!alsa_elem)
    return false;

  int value;
  snd_mixer_selem_get_playback_switch(alsa_elem, SND_MIXER_SCHN_MONO, &value);
  return value == 0;
}

// === MONITORING ===
void AudioManager::startMonitoring() {
  if (monitoring)
    return;

  monitoring = true;
  monitorThread =
      std::make_unique<std::thread>(&AudioManager::monitorDevices, this);
  debug("Started audio monitoring thread");
}

void AudioManager::stopMonitoring() {
  if (!monitoring)
    return;

  monitoring = false;
  if (monitorThread && monitorThread->joinable()) {
    monitorThread->join();
  }
  debug("Stopped audio monitoring thread");
}

void AudioManager::monitorDevices() {
  auto lastDeviceCheck = std::chrono::steady_clock::now();
  std::vector<AudioDevice> previousDevices;

  while (monitoring) {
    auto now = std::chrono::steady_clock::now();

    // Check for device changes every 2 seconds
    if (now - lastDeviceCheck >= std::chrono::seconds(2)) {
      std::lock_guard<std::mutex> lock(deviceMutex);

      auto currentDevices = getOutputDevices();

      // Compare with previous devices to detect changes
      for (const auto &device : currentDevices) {
        bool found = false;
        for (const auto &prev : previousDevices) {
          if (device.name == prev.name) {
            found = true;
            // Check for volume/mute changes
            if (std::abs(device.volume - prev.volume) > 0.01 &&
                volumeCallback) {
              volumeCallback(device.name, device.volume);
            }
            if (device.isMuted != prev.isMuted && muteCallback) {
              muteCallback(device.name, device.isMuted);
            }
            break;
          }
        }
        if (!found && deviceCallback) {
          deviceCallback(device, true); // Device added
        }
      }

      // Check for removed devices
      for (const auto &prev : previousDevices) {
        bool found = false;
        for (const auto &device : currentDevices) {
          if (device.name == prev.name) {
            found = true;
            break;
          }
        }
        if (!found && deviceCallback) {
          deviceCallback(prev, false); // Device removed
        }
      }

      previousDevices = currentDevices;
      lastDeviceCheck = now;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

// === UTILITY ===
bool AudioManager::isBackendAvailable(AudioBackend backend) {
  if (backend == AudioBackend::PULSE) {
    return system("pulseaudio --check >/dev/null 2>&1") == 0;
  } else if (backend == AudioBackend::ALSA) {
    return system("aplay -l >/dev/null 2>&1") == 0;
  }
  return false;
}

std::vector<std::string> AudioManager::getSupportedFormats() {
  return {"wav", "ogg", "mp3", "flac", "aac"}; // Common formats
}

#endif // __linux__

// === APPLICATION VOLUME CONTROL IMPLEMENTATION ===
#ifdef __linux__
// Callback for sink input volume
static void pulse_sink_input_volume_callback(struct pa_context *c,
                                             const pa_sink_input_info *i,
                                             int eol, void *userdata) {
  if (eol || !userdata) {
    if (userdata) {
      auto *data = static_cast<PAResultDouble *>(userdata);
      if (data->ml)
        pa_threaded_mainloop_signal(data->ml, 0);
    }
    return;
  }
  if (!i) {
    auto *data = static_cast<PAResultDouble *>(userdata);
    if (data && data->ml)
      pa_threaded_mainloop_signal(data->ml, 0);
    return;
  }
  auto *data = static_cast<PAResultDouble *>(userdata);
  if (data && data->out) {
    *(data->out) = pa_sw_volume_to_linear(pa_cvolume_avg(&i->volume));
  }
  if (data && data->ml)
    pa_threaded_mainloop_signal(data->ml, 0);
}

// Callback for sink input enumeration
static void pulse_sink_input_callback(struct pa_context *c,
                                      const pa_sink_input_info *i, int eol,
                                      void *userdata) {
  auto *data = static_cast<PAResultApps *>(userdata);
  if (!data)
    return;
  if (eol) {
    if (data->ml)
      pa_threaded_mainloop_signal(data->ml, 0);
    return;
  }
  if (!i || !data->out)
    return;
  AudioManager::ApplicationInfo app;
  app.index = i->index;
  app.name = i->name ? i->name : "Unknown";
  // Safely get icon name, checking for NULL
  const char *icon_name = nullptr;
  if (i->proplist) {
    icon_name = pa_proplist_gets(i->proplist, PA_PROP_APPLICATION_ICON_NAME);
  }
  app.icon = icon_name ? icon_name : "";
  app.volume = pa_sw_volume_to_linear(pa_cvolume_avg(&i->volume));
  app.isMuted = i->mute;
  app.sinkInputIndex = i->index;
  data->out->push_back(app);
}

// Per-application volume control methods
bool AudioManager::setApplicationVolume(const std::string &applicationName,
                                        double volume) {
  if (currentBackend != AudioBackend::PULSE || !pa_context)
    return false;

  volume = std::clamp(volume, MIN_VOLUME, MAX_VOLUME);

  // First, get all applications to find the one with matching name
  auto applications = getApplications();
  for (const auto &app : applications) {
    if (app.name == applicationName) {
      return setApplicationVolume(app.sinkInputIndex, volume);
    }
  }

  return false;
}

bool AudioManager::setApplicationVolume(uint32_t applicationIndex,
                                        double volume) {
  if (currentBackend != AudioBackend::PULSE || !pa_context || !pa_mainloop)
    return false;

  volume = std::clamp(volume, MIN_VOLUME, MAX_VOLUME);

  pa_volume_t pa_volume = pa_sw_volume_from_linear(volume);
  pa_cvolume cv;
  pa_cvolume_set(&cv, 2, pa_volume); // Stereo

  pa_threaded_mainloop_lock(pa_mainloop);

  pa_operation *op = pa_context_set_sink_input_volume(
      pa_context, applicationIndex, &cv, nullptr, nullptr);

  bool success = false;
  if (op) {
    // Wait for operation to complete
    while (pa_operation_get_state(op) == PA_OPERATION_RUNNING) {
      pa_threaded_mainloop_wait(pa_mainloop);
    }
    pa_operation_unref(op);
    success = true;
  }

  pa_threaded_mainloop_unlock(pa_mainloop);
  return success;
}

double
AudioManager::getApplicationVolume(const std::string &applicationName) const {
  if (currentBackend != AudioBackend::PULSE || !pa_context)
    return 0.0;

  // First, get all applications to find the one with matching name
  auto applications = getApplications();
  for (const auto &app : applications) {
    if (app.name == applicationName) {
      return getApplicationVolume(app.sinkInputIndex);
    }
  }

  return 0.0;
}

double AudioManager::getApplicationVolume(uint32_t applicationIndex) const {
  if (currentBackend != AudioBackend::PULSE || !pa_context || !pa_mainloop)
    return 0.0;

  double volume = 0.0;

  // Cast away const for C API compatibility
  auto *ctx = const_cast<struct pa_context *>(pa_context);
  auto *ml = const_cast<pa_threaded_mainloop *>(pa_mainloop);

  pa_threaded_mainloop_lock(ml);

  PAResultDouble data{&volume, ml};
  pa_operation *op = pa_context_get_sink_input_info(
      ctx, applicationIndex, pulse_sink_input_volume_callback, &data);

  if (op) {
    while (pa_operation_get_state(op) == PA_OPERATION_RUNNING) {
      pa_threaded_mainloop_wait(ml);
    }
    pa_operation_unref(op);
  }

  pa_threaded_mainloop_unlock(ml);
  return volume;
}

bool AudioManager::increaseApplicationVolume(const std::string &applicationName,
                                             double amount) {
  double current = getApplicationVolume(applicationName);
  return setApplicationVolume(applicationName,
                              std::min(MAX_VOLUME, current + amount));
}

bool AudioManager::increaseApplicationVolume(uint32_t applicationIndex,
                                             double amount) {
  double current = getApplicationVolume(applicationIndex);
  return setApplicationVolume(applicationIndex,
                              std::min(MAX_VOLUME, current + amount));
}

bool AudioManager::decreaseApplicationVolume(const std::string &applicationName,
                                             double amount) {
  double current = getApplicationVolume(applicationName);
  return setApplicationVolume(applicationName,
                              std::max(MIN_VOLUME, current - amount));
}

bool AudioManager::decreaseApplicationVolume(uint32_t applicationIndex,
                                             double amount) {
  double current = getApplicationVolume(applicationIndex);
  return setApplicationVolume(applicationIndex,
                              std::max(MIN_VOLUME, current - amount));
}

// Active window application volume control
bool AudioManager::setActiveApplicationVolume(double volume) {
  std::string activeAppName = getActiveApplicationName();
  if (activeAppName.empty())
    return false;

  return setApplicationVolume(activeAppName, volume);
}

bool AudioManager::increaseActiveApplicationVolume(double amount) {
  try {
    std::string activeAppName = getActiveApplicationName();
    if (activeAppName.empty()) {
      debug("No active application found for volume control");
      return false;
    }

    debug("Attempting to increase volume for application: {}", activeAppName);
    bool result = increaseApplicationVolume(activeAppName, amount);
    debug("Volume increase result: {}", result ? "SUCCESS" : "FAILED");
    return result;
  } catch (const std::exception &e) {
    error("Exception in increaseActiveApplicationVolume: {}", e.what());
    return false;
  }
}

bool AudioManager::decreaseActiveApplicationVolume(double amount) {
  try {
    std::string activeAppName = getActiveApplicationName();
    if (activeAppName.empty()) {
      debug("No active application found for volume control");
      return false;
    }

    debug("Attempting to decrease volume for application: {}", activeAppName);
    bool result = decreaseApplicationVolume(activeAppName, amount);
    debug("Volume decrease result: {}", result ? "SUCCESS" : "FAILED");
    return result;
  } catch (const std::exception &e) {
    error("Exception in decreaseActiveApplicationVolume: {}", e.what());
    return false;
  }
}

double AudioManager::getActiveApplicationVolume() const {
  std::string activeAppName = getActiveApplicationName();
  if (activeAppName.empty())
    return 0.0;

  return getApplicationVolume(activeAppName);
}

std::vector<AudioManager::ApplicationInfo>
AudioManager::getApplications() const {
  std::vector<ApplicationInfo> applications;

  if (currentBackend != AudioBackend::PULSE || !pa_context || !pa_mainloop) {
    debug("PulseAudio not available for application enumeration");
    return applications;
  }

  try {
    // Use a const_cast to work around the PulseAudio API's lack of
    // const-correctness
    auto *ctx = const_cast<struct pa_context *>(pa_context);
    if (!ctx) {
      debug("Invalid PulseAudio context");
      return applications;
    }

    // Check context state before proceeding
    pa_context_state_t state = pa_context_get_state(ctx);
    if (state != PA_CONTEXT_READY) {
      debug("PulseAudio context not ready (state: {})",
            static_cast<int>(state));
      return applications;
    }

    pa_threaded_mainloop_lock(pa_mainloop);

    PAResultApps data{&applications, pa_mainloop};
    pa_operation *op = pa_context_get_sink_input_info_list(
        ctx, pulse_sink_input_callback, &data);

    if (op) {
      while (pa_operation_get_state(op) == PA_OPERATION_RUNNING) {
        pa_threaded_mainloop_wait(pa_mainloop);
      }
      pa_operation_unref(op);
    } else {
      debug("Failed to create operation for sink input list");
    }

    pa_threaded_mainloop_unlock(pa_mainloop);
  } catch (const std::exception &e) {
    error("Exception in getApplications: {}", e.what());
    if (pa_mainloop) {
      pa_threaded_mainloop_unlock(pa_mainloop);
    }
  }

  debug("Found {} audio applications", applications.size());
  return applications;
}

std::string AudioManager::getActiveApplicationName() const {
  // Check if PulseAudio backend is available
  if (currentBackend != AudioBackend::PULSE || !pa_context || !pa_mainloop) {
    debug("PulseAudio not available for active application detection");
    return "";
  }

  // Get the active window's PID and use it to find the corresponding audio
  // application
  auto pid = havel::WindowManager::GetActiveWindowPID();
  if (pid == 0) {
    debug("No active window PID found");
    return "";
  }

  // Get all applications and find the one with matching PID
  auto applications = getApplications();
  if (applications.empty()) {
    debug("No audio applications found");
    return "";
  }

  // First try to match by process name/class
  std::string windowClass;
  try {
    windowClass = havel::WindowManager::GetActiveWindowClass();
  } catch (const std::exception &e) {
    debug("Failed to get active window class: {}", e.what());
  }

  if (!windowClass.empty()) {
    for (const auto &app : applications) {
      if (!app.name.empty() &&
          app.name.find(windowClass) != std::string::npos) {
        debug("Found application by class match: {}", app.name);
        return app.name;
      }
    }
  }

  // If no match by class, try using the window title
  std::string windowTitle;
  try {
    windowTitle = havel::WindowManager::GetActiveWindowTitle();
  } catch (const std::exception &e) {
    debug("Failed to get active window title: {}", e.what());
  }

  if (!windowTitle.empty()) {
    for (const auto &app : applications) {
      if (!app.name.empty() &&
          app.name.find(windowTitle) != std::string::npos) {
        debug("Found application by title match: {}", app.name);
        return app.name;
      }
    }
  }

  debug("No matching audio application found for active window");
  return "";
}

#endif // __linux__

} // namespace havel