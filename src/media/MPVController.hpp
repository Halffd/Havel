#pragma once
#include <string>
#include <vector>
#include <chrono>

namespace havel {

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
        void SendRaw(const std::string& data);

        void SetSocketPath(const std::string& path);
        bool Reconnect();
        void SendCommand(const std::vector<std::string>& cmd);
        bool IsSocketAlive();

    private:
        bool EnsureInitialized();
        bool ConnectSocket();

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
    };

} // namespace H
