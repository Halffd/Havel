/*
 * MediaService.hpp
 *
 * Media playback service.
 * Provides audio/video playback control.
 * 
 * Uses MPV internally for playback.
 */
#pragma once

#include <string>
#include <cstdint>

namespace havel { class MPVController; class AudioManager; }

namespace havel::host {

/**
 * MediaService - Media playback
 * 
 * Provides:
 * - play(path): Play media file
 * - pause()/resume(): Control playback
 * - stop(): Stop playback
 * - volume: Get/set volume
 * - position: Get/set playback position
 * - duration: Get media duration
 */
class MediaService {
public:
    MediaService();
    ~MediaService();

    // =========================================================================
    // Playback control
    // =========================================================================

    /// Play media file
    /// @param path File path or URL
    /// @return true if started
    bool play(const std::string& path);

    /// Pause playback
    void pause();

    /// Resume playback
    void resume();

    /// Toggle pause/resume
    void togglePause();

    /// Stop playback
    void stop();

    /// Check if playing
    bool isPlaying() const;

    /// Check if paused
    bool isPaused() const;

    // =========================================================================
    // Volume control
    // =========================================================================

    /// Get volume (0-100)
    int getVolume() const;

    /// Set volume (0-100)
    void setVolume(int volume);

    /// Mute audio
    void setMute(bool mute);

    /// Check if muted
    bool isMuted() const;

    // =========================================================================
    // Position/duration
    // =========================================================================

    /// Get current position in seconds
    double getPosition() const;

    /// Set position in seconds
    void setPosition(double seconds);

    /// Get media duration in seconds
    double getDuration() const;

    /// Get progress (0.0-1.0)
    double getProgress() const;

    // =========================================================================
    // Playlist
    // =========================================================================

    /// Play next item in playlist
    void next();

    /// Play previous item
    void previous();

    /// Get current media title
    std::string getTitle() const;

private:
    std::shared_ptr<havel::MPVController> m_mpv;
    std::shared_ptr<havel::AudioManager> m_audio;
};

} // namespace havel::host
