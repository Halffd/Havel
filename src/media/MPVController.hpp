#pragma once
#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace havel {

struct MPVInstance {
  std::string pid;
  std::string socketPath;
  std::string command;
  bool isActive;
};

class MPVController {
public:
  MPVController();
  ~MPVController();

  bool Initialize();
  void Shutdown();

  void PlayPause();
  void Stop();
  void Next();
  void Previous();
  void VolumeUp();
  void VolumeDown();
  void ToggleMute();
  void ToggleSubtitleVisibility();
  void ToggleSecondarySubtitleVisibility();
  void IncreaseSubtitleFontSize();
  void DecreaseSubtitleFontSize();
  void SubtitleDelayForward();
  void SubtitleDelayBackward();
  void SubtitleScaleUp();
  void SubtitleScaleDown();
  void SeekForward();
  void SeekBackward();
  void SeekForward2();
  void SeekBackward2();
  void SeekForward3();
  void SeekBackward3();
  void SpeedUp();
  void SlowDown();
  void SetLoop(bool enable);
  void SendRaw(const std::string &data);

  // New functions for enhanced MPV control
  std::vector<MPVInstance> FindMPVInstances();
  bool SetActiveInstance(const std::string &pid);
  bool SetActiveInstanceBySocket(const std::string &socketPath);
  std::optional<MPVInstance> GetActiveInstance() const;
  bool IsMPVRunning();
  void ChangeSocket(const std::string &socketPath);
  void ControlMultiple(const std::vector<std::string> &pids,
                       const std::vector<std::string> &commands);

  // Existing functions with enhanced names
  void SetSocketPath(const std::string &path);
  bool Reconnect();
  void SendCommand(const std::vector<std::string> &cmd);
  bool IsSocketAlive();

private:
  bool EnsureInitialized();
  bool ConnectSocket();
  std::string FindMPVSocket(const std::string &pid = "");

  bool initialized;
  std::string socket_path;
  int socket_fd;
  int socket_timeout_sec;
  int retry_delay_ms;
  int max_retries;
  int seek_s = 1;
  int seek2_s = 5;
  int seek3_s = 30;
  std::chrono::steady_clock::time_point last_error_time;

  // New member variables for multiple instance support
  std::vector<MPVInstance> mpvInstances;
  std::optional<MPVInstance> activeInstance;
};

} // namespace havel
