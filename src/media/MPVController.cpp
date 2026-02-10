#include "MPVController.hpp"
#include <iostream>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>
#include <chrono>
#include <thread>

namespace havel {

MPVController::MPVController() {
    initialized = false;
    socket_path = "/tmp/mpvsocket";
    last_error_time = std::chrono::steady_clock::now();
    socket_timeout_sec = 1;
    retry_delay_ms = 100;
    max_retries = 3;
    socket_fd = -1;
}

MPVController::~MPVController() {
    Shutdown();
}

bool MPVController::Initialize() {
    if (initialized) return true;
    std::cout << "Initializing MPV controller" << std::endl;
    initialized = true;
    if (ConnectSocket()) {
        std::cout << "Connected to MPV socket" << std::endl;
    } else {
        std::cout << "MPV socket not open" << std::endl;
    }
    return true;
}

void MPVController::Shutdown() {
    if (socket_fd != -1) {
        close(socket_fd);
        socket_fd = -1;
    }
    initialized = false;
    std::cout << "Shutting down MPV controller" << std::endl;
}

bool MPVController::EnsureInitialized() {
    if (!initialized)
        return Initialize();
    return true;
}

bool MPVController::ConnectSocket() {
    if (socket_fd != -1) {
        close(socket_fd);
    }

    socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socket_fd < 0) return false;

    struct sockaddr_un addr{};
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

    int retries = 0;
    while (connect(socket_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        if (++retries >= max_retries) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(retry_delay_ms));
    }

    return true;
}
    void MPVController::SendCommand(const std::vector<std::string>& cmd) {
    if (!EnsureInitialized()) return;

    if (!IsSocketAlive() && !ConnectSocket()) {
        std::cerr << "MPV socket not available\n";
        return;
    }

    std::string json = "{\"command\": [";
    for (size_t i = 0; i < cmd.size(); ++i) {
        json += "\"" + cmd[i] + "\"";
        if (i + 1 < cmd.size()) json += ", ";
    }
    json += "]}\n";

    for (int attempt = 0; attempt < max_retries; ++attempt) {
        if (!IsSocketAlive() && !ConnectSocket()) continue;

        ssize_t sent = send(socket_fd, json.c_str(), json.size(), 0);
        if (sent < 0) {
            close(socket_fd);
            socket_fd = -1;
            continue;
        }

        char buffer[1024] = {0};
        ssize_t len = recv(socket_fd, buffer, sizeof(buffer) - 1, 0);
        if (len > 0) {
            std::cout << "MPV response: " << buffer << std::endl;
        } else {
            std::cout << "No response or timeout from MPV" << std::endl;
        }
        return;
    }

    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::seconds>(now - last_error_time).count() > 60) {
        std::cerr << "Failed to send MPV command after retries." << std::endl;
        last_error_time = now;
    }
}

bool MPVController::IsSocketAlive() {
    if (socket_fd == -1) return false;
    char buf;
    ssize_t ret = recv(socket_fd, &buf, 1, MSG_PEEK | MSG_DONTWAIT);
    if (ret == 0 || (ret == -1 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        close(socket_fd);
        socket_fd = -1;
        return false;
    }
    return true;
}

void MPVController::PlayPause() { SendCommand({"cycle", "pause"}); }
void MPVController::Stop() { SendCommand({"stop"}); }
void MPVController::Next() { SendCommand({"playlist-next"}); }
void MPVController::Previous() { SendCommand({"playlist-prev"}); }
void MPVController::VolumeUp() { SendCommand({"add", "volume", "5"}); }
void MPVController::VolumeDown() { SendCommand({"add", "volume", "-5"}); }
void MPVController::ToggleMute() { SendCommand({"cycle", "mute"}); }
void MPVController::ToggleSubtitleVisibility() { SendCommand({"cycle", "sub-visibility"}); }
void MPVController::ToggleSecondarySubtitleVisibility() { SendCommand({"cycle", "secondary-sub-visibility"}); }
void MPVController::IncreaseSubtitleFontSize() { SendCommand({"add", "sub-font-size", "2"}); }
void MPVController::DecreaseSubtitleFontSize() { SendCommand({"add", "sub-font-size", "-2"}); }
void MPVController::SubtitleDelayForward() { SendCommand({"add", "sub-delay", "0.1"}); }
void MPVController::SubtitleDelayBackward() { SendCommand({"add", "sub-delay", "-0.1"}); }
void MPVController::SubtitleScaleUp() { SendCommand({"add", "sub-scale", "0.1"}); }
void MPVController::SubtitleScaleDown() { SendCommand({"add", "sub-scale", "-0.1"}); }

void MPVController::SendRaw(const std::string& data) {
    SendCommand({"script-message", data});
}

void MPVController::SeekForward() { SendCommand({"seek", std::to_string(seek_s)});   }
void MPVController::SeekBackward() { SendCommand({"seek", "-" + std::to_string(seek_s)}); }
void MPVController::SeekForward2() { SendCommand({"seek", std::to_string(seek2_s)});   }
void MPVController::SeekBackward2() { SendCommand({"seek", "-" + std::to_string(seek2_s)}); }
void MPVController::SeekForward3() { SendCommand({"seek", std::to_string(seek3_s)});   }
void MPVController::SeekBackward3() { SendCommand({"seek", "-" + std::to_string(seek3_s)}); }
void MPVController::SpeedUp() { SendCommand({"multiply", "speed", "1.1"}); }
void MPVController::SlowDown() { SendCommand({"multiply", "speed", "0.9"}); }
void MPVController::SetLoop(bool enable) {
    SendCommand({"set", "loop-playlist", enable ? "inf" : "no"});
}

void MPVController::SetSocketPath(const std::string& path) {
    socket_path = path;
    if (socket_fd != -1) {
        close(socket_fd);
        socket_fd = -1;
    }
    ConnectSocket();
}

bool MPVController::Reconnect() {
    if (socket_fd != -1) {
        close(socket_fd);
        socket_fd = -1;
    }
    return ConnectSocket();
}

} // namespace havel
