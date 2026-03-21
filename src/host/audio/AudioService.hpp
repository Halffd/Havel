/*
 * AudioService.hpp
 *
 * Pure C++ audio service - no VM, no interpreter, no HavelValue.
 */
#pragma once

#include <string>

namespace havel { class AudioManager; }

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

private:
    havel::AudioManager* m_manager;
};

} // namespace havel::host
