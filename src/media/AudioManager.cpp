#include "AudioManager.hpp"
#include <cmath>
#include <algorithm>
#include <chrono>

#ifdef __linux__
#include <pulse/error.h>
#include <pulse/volume.h>
#endif

namespace havel {

AudioManager::AudioManager(AudioBackend backend) : currentBackend(backend) {
    debug("Initializing AudioManager with backend: {}", 
          backend == AudioBackend::AUTO ? "AUTO" : 
          backend == AudioBackend::PULSE ? "PULSE" : "ALSA");
    
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

bool AudioManager::setVolume(const std::string& device, double volume) {
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
    
    debug("Set volume for {}: {:.2f} - {}", device, volume, success ? "SUCCESS" : "FAILED");
    return success;
}

double AudioManager::getVolume() {
    return getVolume(getDefaultOutput());
}

double AudioManager::getVolume(const std::string& device) {
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

bool AudioManager::increaseVolume(const std::string& device, double amount) {
    double current = getVolume(device);
    return setVolume(device, std::min(MAX_VOLUME, current + amount));
}

bool AudioManager::decreaseVolume(double amount) {
    double current = getVolume();
    return setVolume(std::max(MIN_VOLUME, current - amount));
}

bool AudioManager::decreaseVolume(const std::string& device, double amount) {
    double current = getVolume(device);
    return setVolume(device, std::max(MIN_VOLUME, current - amount));
}

// === MUTE CONTROL ===
bool AudioManager::toggleMute() {
    return setMute(!isMuted());
}

bool AudioManager::toggleMute(const std::string& device) {
    return setMute(device, !isMuted(device));
}

bool AudioManager::setMute(bool muted) {
    return setMute(getDefaultOutput(), muted);
}

bool AudioManager::setMute(const std::string& device, bool muted) {
    bool success = false;
    if (currentBackend == AudioBackend::PULSE) {
        success = setPulseMute(device, muted);
    } else if (currentBackend == AudioBackend::ALSA) {
        success = setAlsaMute(muted);
    }
    
    if (success && muteCallback) {
        muteCallback(device, muted);
    }
    
    debug("Set mute for {}: {} - {}", device, muted, success ? "SUCCESS" : "FAILED");
    return success;
}

bool AudioManager::isMuted() {
    return isMuted(getDefaultOutput());
}

bool AudioManager::isMuted(const std::string& device) {
    if (currentBackend == AudioBackend::PULSE) {
        return isPulseMuted(device);
    } else if (currentBackend == AudioBackend::ALSA) {
        return isAlsaMuted();
    }
    return false;
}

// === DEVICE MANAGEMENT ===
std::vector<AudioDevice> AudioManager::getOutputDevices() {
    if (currentBackend == AudioBackend::PULSE) {
        return getPulseDevices(false);
    }
    return {}; // ALSA device enumeration is more complex
}

std::vector<AudioDevice> AudioManager::getInputDevices() {
    if (currentBackend == AudioBackend::PULSE) {
        return getPulseDevices(true);
    }
    return {};
}

std::string AudioManager::getDefaultOutput() {
    if (defaultOutputDevice.empty()) {
        // Try to detect default device
        auto devices = getOutputDevices();
        for (const auto& device : devices) {
            if (device.isDefault) {
                defaultOutputDevice = device.name;
                break;
            }
        }
        if (defaultOutputDevice.empty() && !devices.empty()) {
            defaultOutputDevice = devices[0].name;
        }
    }
    return defaultOutputDevice;
}

std::string AudioManager::getDefaultInput() {
    if (defaultInputDevice.empty()) {
        auto devices = getInputDevices();
        for (const auto& device : devices) {
            if (device.isDefault) {
                defaultInputDevice = device.name;
                break;
            }
        }
        if (defaultInputDevice.empty() && !devices.empty()) {
            defaultInputDevice = devices[0].name;
        }
    }
    return defaultInputDevice;
}

// === PLAYBACK CONTROL ===
bool AudioManager::playTestSound() {
    // Play a simple sine wave test tone
    return system("speaker-test -t sine -f 440 -l 1 -D default >/dev/null 2>&1") == 0;
}

bool AudioManager::playSound(const std::string& soundFile) {
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
        "/usr/share/sounds/generic.wav"
    };
    
    for (const auto& path : soundPaths) {
        if (playSound(path)) {
            return true;
        }
    }
    
    // Fallback to system beep
    return system("printf '\\007'") == 0;
}

// === BACKEND IMPLEMENTATIONS ===
#ifdef __linux__
bool AudioManager::initializePulse() {
    pa_mainloop = pa_threaded_mainloop_new();
    if (!pa_mainloop) {
        error("Failed to create PulseAudio mainloop");
        return false;
    }
    
    pa_threaded_mainloop_start(pa_mainloop);
    
    pa_context = pa_context_new(pa_threaded_mainloop_get_api(pa_mainloop), "Havel");
    if (!pa_context) {
        error("Failed to create PulseAudio context");
        return false;
    }
    
    if (pa_context_connect(pa_context, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0) {
        error("Failed to connect to PulseAudio: {}", pa_strerror(pa_context_errno(pa_context)));
        return false;
    }
    
    // Wait for connection
    pa_context_state_t state;
    while ((state = pa_context_get_state(pa_context)) != PA_CONTEXT_READY) {
        if (state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED) {
            error("PulseAudio connection failed");
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    return true;
}

bool AudioManager::initializeAlsa() {
        int err = snd_mixer_open(&alsa_mixer, 0);
        if (err < 0) {
            error("Failed to open ALSA mixer: {}", snd_strerror(err));
            return false;
        }
        
        err = snd_mixer_attach(alsa_mixer, "default");
        if (err < 0) {
            error("Failed to attach ALSA mixer: {}", snd_strerror(err));
            snd_mixer_close(alsa_mixer);
            alsa_mixer = nullptr;
            return false;
        }
        
        err = snd_mixer_selem_register(alsa_mixer, nullptr, nullptr);
        if (err < 0) {
            error("Failed to register ALSA mixer: {}", snd_strerror(err));
            snd_mixer_close(alsa_mixer);
            alsa_mixer = nullptr;
            return false;
        }
        
        err = snd_mixer_load(alsa_mixer);
        if (err < 0) {
            error("Failed to load ALSA mixer: {}", snd_strerror(err));
            snd_mixer_close(alsa_mixer);
            alsa_mixer = nullptr;
            return false;
        }
        
        // Find Master or PCM element
        snd_mixer_selem_id_t *sid;
        snd_mixer_selem_id_alloca(&sid);
        
        const char* elem_names[] = {"Master", "PCM", "Speaker", "Headphone"};
        for (const char* name : elem_names) {
            snd_mixer_selem_id_set_index(sid, 0);
            snd_mixer_selem_id_set_name(sid, name);
            alsa_elem = snd_mixer_find_selem(alsa_mixer, sid);
            if (alsa_elem) {
                debug("Using ALSA element: {}", name);
                break;
            }
        }
        
        if (!alsa_elem) {
            error("No suitable ALSA mixer element found");
            snd_mixer_close(alsa_mixer);
            alsa_mixer = nullptr;
            return false;
        }
        
        return true;
    }
    
    void AudioManager::cleanup() {
        if (pa_context) {
            pa_context_disconnect(pa_context);
            pa_context_unref(pa_context);
            pa_context = nullptr;
        }
        
        if (pa_mainloop) {
            pa_threaded_mainloop_stop(pa_mainloop);
            pa_threaded_mainloop_free(pa_mainloop);
            pa_mainloop = nullptr;
        }
        
        if (alsa_mixer) {
            snd_mixer_close(alsa_mixer);
            alsa_mixer = nullptr;
        }
    }
    
    // === PULSEAUDIO IMPLEMENTATIONS ===
    bool AudioManager::setPulseVolume(const std::string& device, double volume) {
        if (!pa_context) return false;
        
        pa_volume_t pa_volume = pa_sw_volume_from_linear(volume);
        pa_cvolume cv;
        pa_cvolume_set(&cv, 2, pa_volume); // Stereo
        
        pa_threaded_mainloop_lock(pa_mainloop);
        
        pa_operation* op = pa_context_set_sink_volume_by_name(
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
    static void pulse_volume_callback(pa_context *c, const pa_sink_info *i, int eol, void *userdata) {
        if (eol || !i) return;
        
        double* volume_ptr = static_cast<double*>(userdata);
        *volume_ptr = pa_sw_volume_to_linear(pa_cvolume_avg(&i->volume));
    }
    
    double AudioManager::getPulseVolume(const std::string& device) {
        if (!pa_context) return 0.0;
        
        double volume = 0.0;
        
        pa_threaded_mainloop_lock(pa_mainloop);
        
        pa_operation* op = pa_context_get_sink_info_by_name(
            pa_context, device.c_str(), pulse_volume_callback, &volume);
        
        if (op) {
            while (pa_operation_get_state(op) == PA_OPERATION_RUNNING) {
                pa_threaded_mainloop_wait(pa_mainloop);
            }
            pa_operation_unref(op);
        }
        
        pa_threaded_mainloop_unlock(pa_mainloop);
        return volume;
    }
    
    bool AudioManager::setPulseMute(const std::string& device, bool muted) {
        if (!pa_context) return false;
        
        pa_threaded_mainloop_lock(pa_mainloop);
        
        pa_operation* op = pa_context_set_sink_mute_by_name(
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
    static void pulse_mute_callback(pa_context *c, const pa_sink_info *i, int eol, void *userdata) {
        if (eol || !i) return;
        
        bool* mute_ptr = static_cast<bool*>(userdata);
        *mute_ptr = i->mute;
    }
    
    bool AudioManager::isPulseMuted(const std::string& device) {
        if (!pa_context) return false;
        
        bool muted = false;
        
        pa_threaded_mainloop_lock(pa_mainloop);
        
        pa_operation* op = pa_context_get_sink_info_by_name(
            pa_context, device.c_str(), pulse_mute_callback, &muted);
        
        if (op) {
            while (pa_operation_get_state(op) == PA_OPERATION_RUNNING) {
                pa_threaded_mainloop_wait(pa_mainloop);
            }
            pa_operation_unref(op);
        }
        
        pa_threaded_mainloop_unlock(pa_mainloop);
        return muted;
    }
    
    // Callback for device enumeration
    static void pulse_device_callback(pa_context *c, const pa_sink_info *i, int eol, void *userdata) {
        if (eol) return;
        
        auto* devices = static_cast<std::vector<AudioDevice>*>(userdata);
        
        AudioDevice device;
        device.name = i->name;
        device.description = i->description;
        device.index = i->index;
        device.channels = i->sample_spec.channels;
        device.volume = pa_sw_volume_to_linear(pa_cvolume_avg(&i->volume));
        device.isMuted = i->mute;
        
        devices->push_back(device);
    }
    
    std::vector<AudioDevice> AudioManager::getPulseDevices(bool input) {
        std::vector<AudioDevice> devices;
        if (!pa_context) return devices;
        
        pa_threaded_mainloop_lock(pa_mainloop);
        
        pa_operation* op;
        if (input) {
            op = pa_context_get_source_info_list(pa_context, 
                reinterpret_cast<pa_source_info_cb_t>(pulse_device_callback), &devices);
        } else {
            op = pa_context_get_sink_info_list(pa_context, pulse_device_callback, &devices);
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
        if (!alsa_elem) return false;
        
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
        if (!alsa_elem) return 0.0;
        
        long min, max, volume;
        snd_mixer_selem_get_playback_volume_range(alsa_elem, &min, &max);
        snd_mixer_selem_get_playback_volume(alsa_elem, SND_MIXER_SCHN_MONO, &volume);
        
        return (double)(volume - min) / (max - min);
    }
    
    bool AudioManager::setAlsaMute(bool muted) {
        if (!alsa_elem) return false;
        
        int err = snd_mixer_selem_set_playback_switch_all(alsa_elem, muted ? 0 : 1);
        if (err < 0) {
            error("Failed to set ALSA mute: {}", snd_strerror(err));
            return false;
        }
        
        return true;
    }
    
    bool AudioManager::isAlsaMuted() {
        if (!alsa_elem) return false;
        
        int value;
        snd_mixer_selem_get_playback_switch(alsa_elem, SND_MIXER_SCHN_MONO, &value);
        return value == 0;
    }
    
    // === MONITORING ===
    void AudioManager::startMonitoring() {
        if (monitoring) return;
        
        monitoring = true;
        monitorThread = std::make_unique<std::thread>(&AudioManager::monitorDevices, this);
        debug("Started audio monitoring thread");
    }
    
    void AudioManager::stopMonitoring() {
        if (!monitoring) return;
        
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
                for (const auto& device : currentDevices) {
                    bool found = false;
                    for (const auto& prev : previousDevices) {
                        if (device.name == prev.name) {
                            found = true;
                            // Check for volume/mute changes
                            if (std::abs(device.volume - prev.volume) > 0.01 && volumeCallback) {
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
                for (const auto& prev : previousDevices) {
                    bool found = false;
                    for (const auto& device : currentDevices) {
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
    
    } // namespace havel