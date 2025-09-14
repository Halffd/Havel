#pragma once
#include "core/ConfigManager.hpp"
#include "../utils/Logger.hpp"
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>

#ifdef __linux__
#include <pulse/pulseaudio.h>
#include <alsa/asoundlib.h>
#endif

namespace havel {

enum class AudioBackend {
    PULSE,
    ALSA,
    AUTO
};

struct AudioDevice {
    std::string name;
    std::string description;
    uint32_t index;
    bool isDefault = false;
    bool isMuted = false;
    double volume = 1.0;  // 0.0 - 1.0
    int channels = 2;
};

class AudioManager {
public:
    AudioManager(AudioBackend backend = AudioBackend::AUTO);
    ~AudioManager();

    // === VOLUME CONTROL ===
    bool setVolume(double volume);  // Default device
    bool setVolume(const std::string& device, double volume);
    double getVolume();
    double getVolume(const std::string& device);
    
    bool increaseVolume(double amount = 0.05);
    bool increaseVolume(const std::string& device, double amount = 0.05);
    bool decreaseVolume(double amount = 0.05);  
    bool decreaseVolume(const std::string& device, double amount = 0.05);

    // === MUTE CONTROL ===
    bool toggleMute();  // Default device
    bool toggleMute(const std::string& device);
    bool setMute(bool muted);
    bool setMute(const std::string& device, bool muted);
    bool isMuted();
    bool isMuted(const std::string& device);

    // === DEVICE MANAGEMENT ===
    std::vector<AudioDevice> getOutputDevices();
    std::vector<AudioDevice> getInputDevices();
    std::string getDefaultOutput();
    std::string getDefaultInput();
    bool setDefaultOutput(const std::string& device);
    bool setDefaultInput(const std::string& device);

    // === PLAYBACK CONTROL ===
    bool playTestSound();
    bool playSound(const std::string& soundFile);
    bool playNotificationSound();
    
    // === CALLBACKS ===
    using VolumeCallback = std::function<void(const std::string&, double)>;
    using MuteCallback = std::function<void(const std::string&, bool)>;
    using DeviceCallback = std::function<void(const AudioDevice&, bool)>; // added/removed
    
    void setVolumeCallback(VolumeCallback callback) { volumeCallback = callback; }
    void setMuteCallback(MuteCallback callback) { muteCallback = callback; }
    void setDeviceCallback(DeviceCallback callback) { deviceCallback = callback; }

    // === UTILITY ===
    AudioBackend getBackend() const { return currentBackend; }
    bool isBackendAvailable(AudioBackend backend);
    std::vector<std::string> getSupportedFormats();
    
    // Constants
    static constexpr double DEFAULT_VOLUME_STEP = 0.05;
    static constexpr double MIN_VOLUME = 0.0;
    static constexpr double MAX_VOLUME = 1.0;

private:
    AudioBackend currentBackend;
    std::string defaultOutputDevice;
    std::string defaultInputDevice;
    
    // PulseAudio specific
#ifdef __linux__
    pa_threaded_mainloop* pa_mainloop = nullptr;
    pa_context* pa_context = nullptr;
    pa_context_state_t pa_state;
    
    // ALSA specific  
    snd_mixer_t* alsa_mixer = nullptr;
    snd_mixer_elem_t* alsa_elem = nullptr;
#endif

    // Callbacks
    VolumeCallback volumeCallback;
    MuteCallback muteCallback; 
    DeviceCallback deviceCallback;
    
    // Threading
    std::atomic<bool> monitoring{false};
    std::unique_ptr<std::thread> monitorThread;
    std::mutex deviceMutex;
    
    // Backend implementations
    bool initializePulse();
    bool initializeAlsa();
    void cleanup();
    
    // PulseAudio methods
    bool setPulseVolume(const std::string& device, double volume);
    double getPulseVolume(const std::string& device);
    bool setPulseMute(const std::string& device, bool muted);
    bool isPulseMuted(const std::string& device);
    std::vector<AudioDevice> getPulseDevices(bool input = false);
    
    // ALSA methods  
    bool setAlsaVolume(double volume);
    double getAlsaVolume();
    bool setAlsaMute(bool muted);
    bool isAlsaMuted();
    
    // Monitoring thread
    void startMonitoring();
    void stopMonitoring();
    void monitorDevices();
    
    // Logging helpers
    template<typename... Args>
    static void debug(const std::string& format, Args&&... args) {
        havel::Logger::getInstance().debug("[AudioManager] " + format, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    static void info(const std::string& format, Args&&... args) {
        havel::Logger::getInstance().info("[AudioManager] " + format, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    static void warning(const std::string& format, Args&&... args) {
        havel::Logger::getInstance().warning("[AudioManager] " + format, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    static void error(const std::string& format, Args&&... args) {
        havel::Logger::getInstance().error("[AudioManager] " + format, std::forward<Args>(args)...);
    }
};

} // namespace havel