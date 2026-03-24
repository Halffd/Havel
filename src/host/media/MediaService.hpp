/*
 * MediaService.hpp - Media control via MPRIS/DBus
 *
 * Provides media playback control for any MPRIS-compatible player
 * (Spotify, VLC, Firefox, Chrome, etc.)
 */
#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace havel::host {

/**
 * MediaService - Media playback control
 *
 * Uses MPRIS D-Bus interface to control media players.
 */
class MediaService {
public:
  MediaService();
  ~MediaService();

  // ==========================================================================
  // Playback control
  // ==========================================================================

  /// Toggle play/pause
  void playPause();

  /// Start/resume playback
  void play();

  /// Pause playback
  void pause();

  /// Stop playback
  void stop();

  /// Skip to next track
  void next();

  /// Skip to previous track
  void previous();

  // ==========================================================================
  // Volume control
  // ==========================================================================

  /// Get volume (0.0 to 1.0)
  double getVolume() const;

  /// Set volume (0.0 to 1.0)
  void setVolume(double volume);

  // ==========================================================================
  // Position control
  // ==========================================================================

  /// Get current position in microseconds
  int64_t getPosition() const;

  /// Set position in microseconds
  void setPosition(int64_t position);

  // ==========================================================================
  // Player management
  // ==========================================================================

  /// Get list of available MPRIS players
  std::vector<std::string> getAvailablePlayers() const;

  /// Get current active player
  std::string getActivePlayer() const;

  /// Set active player (e.g., "spotify", "vlc", "firefox")
  void setActivePlayer(const std::string &player);

  /// Check if a player is available
  bool hasPlayer() const;

private:
  struct Impl;
  Impl *impl_;
};

} // namespace havel::host
