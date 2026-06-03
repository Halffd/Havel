/*
 * AudioService.hpp
 *
 * Pure C++ audio service - no VM, no interpreter, no HavelValue.
 */
#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace havel { 
class AudioManager;
struct AudioDevice;
}

namespace havel::host {

class AudioService {
public:
    explicit AudioService(havel::AudioManager* manager);
    ~AudioService() = default;

    double getVolume() const;
    void setVolume(double volume);
    void increaseVolume(double amount);
    void decreaseVolume(double amount);
    
    void toggleMute();
    void setMute(bool muted);
    bool isMuted() const;

    // Application-level volume
    std::string getActiveApplicationName() const;
    double getActiveAppVolume() const;
    void increaseActiveAppVolume(double amount);
    void decreaseActiveAppVolume(double amount);

    // Device queries
    std::vector<havel::AudioDevice> getAllDevices() const;
    havel::AudioDevice findDeviceByName(const std::string &name) const;
    havel::AudioDevice findDeviceByIndex(uint32_t index) const;

    // Default device control
    bool setDefaultOutput(const std::string &device);
    std::string getDefaultOutput() const;

    // Sound
    bool playTestSound();

private:
    havel::AudioManager* m_manager;
};

} // namespace havel::host
