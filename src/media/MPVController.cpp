#include "MPVController.hpp"
#include <chrono>
#include <cstring>
#include <iostream>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

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

MPVController::~MPVController() { Shutdown(); }

bool MPVController::Initialize() {
  if (initialized)
    return true;
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
    socket_fd = -1;
  }

  socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (socket_fd < 0)
    return false;

  struct sockaddr_un addr{};
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

  int retries = 0;
  while (connect(socket_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    if (errno == ECONNREFUSED) {
      // Stale socket – try to remove it
      unlink(socket_path.c_str());
      // Optional: small delay before retry
    }
    if (++retries >= max_retries) {
      close(socket_fd);
      socket_fd = -1;
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(retry_delay_ms));
  }
  return true;
}
void MPVController::SendCommand(const std::vector<std::string> &cmd) {
  if (!EnsureInitialized())
    return;

  if (!IsSocketAlive() && !ConnectSocket()) {
    std::cerr << "MPV socket not available\n";
    return;
  }

  std::string json = "{\"command\": [";
  for (size_t i = 0; i < cmd.size(); ++i) {
    json += "\"" + cmd[i] + "\"";
    if (i + 1 < cmd.size())
      json += ", ";
  }
  json += "]}\n";

  for (int attempt = 0; attempt < max_retries; ++attempt) {
    if (!IsSocketAlive() && !ConnectSocket())
      continue;

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
  if (std::chrono::duration_cast<std::chrono::seconds>(now - last_error_time)
          .count() > 60) {
    std::cerr << "Failed to send MPV command after retries." << std::endl;
    last_error_time = now;
  }
}

bool MPVController::IsSocketAlive() {
  if (socket_fd == -1) {
    return ConnectSocket();
  }
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
void MPVController::ToggleSubtitleVisibility() {
  SendCommand({"cycle", "sub-visibility"});
}
void MPVController::ToggleSecondarySubtitleVisibility() {
  SendCommand({"cycle", "secondary-sub-visibility"});
}
void MPVController::IncreaseSubtitleFontSize() {
  SendCommand({"add", "sub-font-size", "2"});
}
void MPVController::DecreaseSubtitleFontSize() {
  SendCommand({"add", "sub-font-size", "-2"});
}
void MPVController::SubtitleDelayForward() {
  SendCommand({"add", "sub-delay", "0.1"});
}
void MPVController::SubtitleDelayBackward() {
  SendCommand({"add", "sub-delay", "-0.1"});
}
void MPVController::SubtitleScaleUp() {
  SendCommand({"add", "sub-scale", "0.1"});
}
void MPVController::SubtitleScaleDown() {
  SendCommand({"add", "sub-scale", "-0.1"});
}

void MPVController::SendRaw(const std::string &data) {
  SendCommand({"script-message", data});
}

void MPVController::SeekForward() {
  SendCommand({"seek", std::to_string(seek_s)});
}
void MPVController::SeekBackward() {
  SendCommand({"seek", "-" + std::to_string(seek_s)});
}
void MPVController::SeekForward2() {
  SendCommand({"seek", std::to_string(seek2_s)});
}
void MPVController::SeekBackward2() {
  SendCommand({"seek", "-" + std::to_string(seek2_s)});
}
void MPVController::SeekForward3() {
  SendCommand({"seek", std::to_string(seek3_s)});
}
void MPVController::SeekBackward3() {
  SendCommand({"seek", "-" + std::to_string(seek3_s)});
}
void MPVController::SpeedUp() { SendCommand({"multiply", "speed", "1.1"}); }
void MPVController::SlowDown() { SendCommand({"multiply", "speed", "0.9"}); }
void MPVController::SetLoop(bool enable) {
  SendCommand({"set", "loop-playlist", enable ? "inf" : "no"});
}

// Additional methods for script API
void MPVController::Seek(int seconds) {
  SendCommand({"seek", std::to_string(seconds)});
}

void MPVController::AddSpeed(double delta) {
  SendCommand({"add", "speed", std::to_string(delta)});
}

void MPVController::AddSubScale(double delta) {
  SendCommand({"add", "sub-scale", std::to_string(delta)});
}

void MPVController::AddSubDelay(double delta) {
  SendCommand({"add", "sub-delay", std::to_string(delta)});
}

void MPVController::SubSeek(int index) {
  SendCommand({"sub-seek", std::to_string(index)});
}

void MPVController::Cycle(const std::string &property) {
  SendCommand({"cycle", property});
}

std::string MPVController::CopyCurrentSubtitle() {
  // TODO: Implement subtitle text retrieval
  // Requires JSON parsing of mpv's response
  return "";
}

void MPVController::SetIPC(const std::string &socketPath) {
  SetSocketPath(socketPath);
  Reconnect();
}

void MPVController::IPCRestart() { Reconnect(); }

std::string MPVController::GetProperty(const std::string &prop) {
  if (!EnsureInitialized())
    return "";

  if (!IsSocketAlive() && !ConnectSocket()) {
    return "";
  }

  // Send get_property command
  std::string json = "{\"command\": [\"get_property\", \"" + prop + "\"]}\n";

  ssize_t sent = send(socket_fd, json.c_str(), json.size(), 0);
  if (sent < 0) {
    close(socket_fd);
    socket_fd = -1;
    return "";
  }

  // Receive response
  char buffer[1024] = {0};
  ssize_t len = recv(socket_fd, buffer, sizeof(buffer) - 1, 0);
  if (len > 0) {
    buffer[len] = '\0';
    std::string response(buffer);

    // Simple JSON parsing for property value
    // MPV returns: {"data": "value"} or {"data": 123} or {"data": null}
    size_t dataStart = response.find("\"data\":");
    if (dataStart != std::string::npos) {
      dataStart += 7; // Skip "data":

      // Skip whitespace
      while (dataStart < response.length() && isspace(response[dataStart])) {
        dataStart++;
      }

      // Handle null
      if (dataStart + 4 < response.length() &&
          response.substr(dataStart, 4) == "null") {
        return "";
      }

      // Handle string values (in quotes)
      if (dataStart < response.length() && response[dataStart] == '"') {
        dataStart++; // Skip opening quote
        size_t endQuote = response.find('"', dataStart);
        if (endQuote != std::string::npos) {
          return response.substr(dataStart, endQuote - dataStart);
        }
      }

      // Handle numeric values
      size_t endPos = response.find_first_of(",}", dataStart);
      if (endPos != std::string::npos) {
        return response.substr(dataStart, endPos - dataStart);
      }

      // Return everything after "data:" if we can't parse it
      return response.substr(dataStart);
    }
  }

  return "";
}

void MPVController::SetProperty(const std::string &prop,
                                const std::string &value) {
  SendCommand({"set", prop, value});
}

void MPVController::SetSocketPath(const std::string &path) {
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
