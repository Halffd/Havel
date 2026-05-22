#pragma once
#include "utils/Logger.hpp"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>

namespace havel {

class IAudioBackend;

enum class AudioBackend { PIPEWIRE, PULSE, ALSA, AUTO };

struct AudioDevice {
    std::string name, description;
    uint32_t index = 0;
    bool isDefault = false, isMuted = false;
    double volume = 1.0;
    int channels = 2;
};

inline bool operator==(const AudioDevice &a, const AudioDevice &b) {
  return a.name == b.name && a.index == b.index;
}
inline bool operator!=(const AudioDevice &a, const AudioDevice &b) { return !(a == b); }

class AudioManager {
public:
    AudioManager(AudioBackend backend = AudioBackend::AUTO);
    ~AudioManager();

    AudioManager(const AudioManager &) = delete;
    AudioManager &operator=(const AudioManager &) = delete;

    static AudioManager &get();

    bool setVolume(double volume);
    bool setVolume(const std::string &device, double volume);
    double getVolume();
    double getVolume(const std::string &device);
    bool increaseVolume(double amount = 0.05);
    bool increaseVolume(const std::string &device, double amount = 0.05);
    bool decreaseVolume(double amount = 0.05);
    bool decreaseVolume(const std::string &device, double amount = 0.05);

    bool toggleMute();
    bool toggleMute(const std::string &device);
    bool setMute(bool muted);
    bool setMute(const std::string &device, bool muted);
    bool isMuted();
    bool isMuted(const std::string &device);

    const std::vector<AudioDevice> &getDevices() const;
    std::vector<AudioDevice> getOutputDevices() const;

    std::string getDefaultOutput() const;
    std::string getDefaultInput() const;
    bool setDefaultOutput(const std::string &device);

    AudioDevice *findDeviceByName(const std::string &name);
    AudioDevice *findDeviceByIndex(uint32_t index);

    bool playTestSound();
    bool playSound(const std::string &soundFile);

    using VolumeCallback = std::function<void(const std::string &, double)>;
    using MuteCallback = std::function<void(const std::string &, bool)>;
    using DeviceCallback = std::function<void(const AudioDevice &, bool)>;
    void setVolumeCallback(VolumeCallback cb);
    void setMuteCallback(MuteCallback cb);
    void setDeviceCallback(DeviceCallback cb);

    struct ApplicationInfo { uint32_t index; std::string name, icon; double volume; bool isMuted; uint32_t sinkInputIndex; };
    bool setApplicationVolume(const std::string &name, double volume);
    double getApplicationVolume(const std::string &name) const;
    std::vector<ApplicationInfo> getApplications() const;
    std::string getActiveApplicationName() const;

    AudioBackend getBackend() const;
    static constexpr double DEFAULT_VOLUME_STEP = 0.05;
    static constexpr double MIN_VOLUME = 0.0;
    double MAX_VOLUME = 1.5;

private:
    std::unique_ptr<IAudioBackend> backend_;
    mutable std::vector<AudioDevice> cachedDevices;
    mutable std::mutex deviceMutex;
    std::string defaultOutputDevice;

    std::atomic<bool> monitoring{false};
    std::unique_ptr<std::thread> monitorThread;
    void startMonitoring();
    void stopMonitoring();
    void monitorDevices();

    VolumeCallback volumeCallback;
    MuteCallback muteCallback;
    DeviceCallback deviceCallback;

    template<typename... Args> void debug(const std::string &fmt, Args &&... args) { Logger::getInstance().debug("[AudioManager] " + fmt, std::forward<Args>(args)...); }
    template<typename... Args> void info(const std::string &fmt, Args &&... args) { Logger::getInstance().info("[AudioManager] " + fmt, std::forward<Args>(args)...); }
    template<typename... Args> void warning(const std::string &fmt, Args &&... args) { Logger::getInstance().warning("[AudioManager] " + fmt, std::forward<Args>(args)...); }
    template<typename... Args> void error(const std::string &fmt, Args &&... args) { Logger::getInstance().error("[AudioManager] " + fmt, std::forward<Args>(args)...); }
};

} // namespace havel
