#pragma once
#include "core/ConfigManager.hpp"
#include "../utils/Logger.hpp"
#include "../window/WindowManager.hpp"
#include <cstdlib>
#include <format>
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>

// Platform-specific headers
#ifdef __linux__

// PipeWire headers
#ifdef HAVE_PIPEWIRE
#include <pipewire/pipewire.h>
#include <pipewire/core.h>
#include <spa/param/audio/raw.h>
#include <spa/param/props.h>
#include <spa/pod/builder.h>
#endif

// PulseAudio headers
#ifdef HAVE_PULSEAUDIO
#include <pulse/pulseaudio.h>
#include <pulse/thread-mainloop.h>
#include <pulse/context.h>
#include <pulse/introspect.h>
#include <pulse/volume.h>
#include <pulse/error.h>
#include <pulse/stream.h>
#include <pulse/subscribe.h>
#include <pulse/version.h>
#endif

// ALSA headers
#ifdef HAVE_ALSA
#include <alsa/asoundlib.h>
#include <alsa/mixer.h>
#include <alsa/control.h>
#include <alsa/error.h>
#include <alsa/version.h>
#endif

#endif // __linux__


namespace havel {

enum class AudioBackend {
    PIPEWIRE,
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

#ifdef HAVE_PIPEWIRE
struct PipeWireNode {
    uint32_t id;
    std::string name;
    std::string mediaClass;
    std::string description;
    double volume = 1.0;
    bool isMuted = false;
    pw_proxy* proxy = nullptr;
    int pending_params = 0;
    spa_hook node_listener;
};
#endif

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
    const std::vector<AudioDevice>& getDevices() const;
    std::vector<AudioDevice> getOutputDevices() const;
    std::vector<AudioDevice> getInputDevices() const;
    
    AudioDevice* findDeviceByName(const std::string& name);
    const AudioDevice* findDeviceByName(const std::string& name) const;
    AudioDevice* findDeviceByIndex(uint32_t index);
    const AudioDevice* findDeviceByIndex(uint32_t index) const;
    
    std::string getDefaultOutput() const;
    std::string getDefaultInput() const;
    
    void printDevices() const;
    void printDeviceInfo(const AudioDevice& device) const;
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

    // === APPLICATION VOLUME CONTROL ===
    struct ApplicationInfo {
        uint32_t index;
        std::string name;
        std::string icon;
        double volume;
        bool isMuted;
        uint32_t sinkInputIndex; // Used by PulseAudio
    };
    
    bool setApplicationVolume(const std::string& applicationName, double volume);
    bool setApplicationVolume(uint32_t applicationIndex, double volume);
    double getApplicationVolume(const std::string& applicationName) const;
    double getApplicationVolume(uint32_t applicationIndex) const;
    
    bool increaseApplicationVolume(const std::string& applicationName, double amount = 0.05);
    bool increaseApplicationVolume(uint32_t applicationIndex, double amount = 0.05);
    bool decreaseApplicationVolume(const std::string& applicationName, double amount = 0.05);
    bool decreaseApplicationVolume(uint32_t applicationIndex, double amount = 0.05);
    
    bool setActiveApplicationVolume(double volume);
    bool increaseActiveApplicationVolume(double amount = 0.05);
    bool decreaseActiveApplicationVolume(double amount = 0.05);
    double getActiveApplicationVolume() const;
    
    std::vector<ApplicationInfo> getApplications() const;
    std::string getActiveApplicationName() const;
    
    // === UTILITY ===
    AudioBackend getBackend() const { return currentBackend; }
    bool isBackendAvailable(AudioBackend backend);
    std::vector<std::string> getSupportedFormats();
    
    static constexpr double DEFAULT_VOLUME_STEP = 0.05;
    static constexpr double MIN_VOLUME = 0.0;
    double MAX_VOLUME = 1.5; // Allow boosting
private:
    AudioBackend currentBackend;
    
    mutable std::vector<AudioDevice> cachedDevices;
    mutable std::mutex deviceMutex;

    void updateDeviceCache() const;
    void internalUpdateDeviceCache() const;
    
    std::string defaultOutputDevice;
    std::string defaultInputDevice;
    
#ifdef __linux__
    // Friends for PipeWire callbacks - make variables accessible to them
    friend void on_node_info(void *data, const struct pw_node_info *info);
    friend void on_registry_global(void *data, uint32_t id, uint32_t permissions, const char *type, uint32_t version, const struct spa_dict *props);
    friend void on_registry_global_remove(void *data, uint32_t id);
    friend void on_core_sync(void *data, uint32_t id, int seq);

    // Public API required for callbacks
public:
    void parse_pw_node_info(PipeWireNode& node, const pw_node_info* info);

    // PipeWire specific
#ifdef HAVE_PIPEWIRE
    pw_thread_loop* pw_loop = nullptr;
    pw_context* pw_context = nullptr;
    pw_core* pw_core = nullptr;
    pw_registry* pw_registry = nullptr;
    spa_hook core_listener;
    spa_hook registry_listener;
    std::map<uint32_t, PipeWireNode> pw_nodes;
    mutable std::mutex pw_mutex;
    bool pw_ready = false;
    int pw_sync_seq = -1;
#endif // HAVE_PIPEWIRE

    // PulseAudio specific
#ifdef HAVE_PULSEAUDIO
    pa_threaded_mainloop* pa_mainloop = nullptr;
    struct pa_context* pa_ctxt = nullptr;
#endif
    
    // ALSA specific
#ifdef HAVE_ALSA
    snd_mixer_t* alsa_mixer = nullptr;
    snd_mixer_elem_t* alsa_elem = nullptr;
#endif // HAVE_ALSA
#endif // __linux__

    VolumeCallback volumeCallback;
    MuteCallback muteCallback; 
    DeviceCallback deviceCallback;
    
    std::atomic<bool> monitoring{false};
    std::unique_ptr<std::thread> monitorThread;
    
    bool initializePipeWire();
    bool initializePulse();
    bool initializeAlsa();
    void cleanup();
    
#ifdef __linux__
#ifdef HAVE_PIPEWIRE
    void setup_pipewire_listeners();
#endif

#ifdef HAVE_PULSEAUDIO
    bool setPulseVolume(const std::string& device, double volume);
    double getPulseVolume(const std::string& device) const;
    bool setPulseMute(const std::string& device, bool muted);
    bool isPulseMuted(const std::string& device) const;
    std::vector<AudioDevice> getPulseDevices(bool input = false) const;
#endif

#ifdef HAVE_ALSA
    bool setAlsaVolume(double volume);
    double getAlsaVolume();
    bool setAlsaMute(bool muted);
    bool isAlsaMuted();
    std::vector<AudioDevice> getAlsaDevices(bool input) const;
#endif
#endif // __linux__
    
    void startMonitoring();
    void stopMonitoring();
    void monitorDevices();
    
    template<typename... Args> static void debug(const std::string& format, Args&&... args) { Logger::getInstance().debug("[AudioManager] " + format, std::forward<Args>(args)...); }
    template<typename... Args> static void info(const std::string& format, Args&&... args) { Logger::getInstance().info("[AudioManager] " + format, std::forward<Args>(args)...); }
    template<typename... Args> static void warning(const std::string& format, Args&&... args) { Logger::getInstance().warning("[AudioManager] " + format, std::forward<Args>(args)...); }
    template<typename... Args> static void error(const std::string& format, Args&&... args) { Logger::getInstance().error("[AudioManager] " + format, std::forward<Args>(args)...); }
};

} // namespace havel