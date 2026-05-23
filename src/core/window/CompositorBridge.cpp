#include "CompositorBridge.hpp"
#include "strategies/CompositorStrategy.hpp"
#include "utils/Logger.hpp"
#include "utils/Util.hpp"
#include <cstdlib>
#include <thread>

namespace havel {

CompositorBridge::CompositorBridge() {
  strategy_ = CreateCompositorStrategy();
  if (strategy_) {
    compositorType = strategy_->GetType();
    info("Compositor bridge initialized for: {}", strategy_->GetName());
  } else {
    debug("Compositor bridge not available");
  }
}

CompositorBridge::~CompositorBridge() { Stop(); }

void CompositorBridge::Start() {
  if (running.load()) {
    warn("Compositor bridge already running");
    return;
  }
  running = true;
  monitorThread = std::thread(&CompositorBridge::MonitoringLoop, this);
  info("Compositor bridge started");
}

void CompositorBridge::Stop() {
  if (!running.load()) return;
  running = false;
  if (monitorThread.joinable()) monitorThread.join();
  info("Compositor bridge stopped");
}

CompositorBridge::WindowInfo CompositorBridge::GetActiveWindow() const {
  std::lock_guard<std::mutex> lock(cacheMutex);
  return cachedWindowInfo;
}

CompositorBridge::CompositorType CompositorBridge::GetCompositorType() const {
  return compositorType;
}

bool CompositorBridge::IsAvailable() const {
  return strategy_ != nullptr;
}

void CompositorBridge::MonitoringLoop() {
  while (running.load()) {
    if (strategy_) {
      try {
        auto info = strategy_->QueryActiveWindow();
        if (info.valid) {
          std::lock_guard<std::mutex> lock(cacheMutex);
          cachedWindowInfo = info;
        }
      } catch (const std::exception &e) {
        error("Error querying compositor: {}", e.what());
      }
    }
    std::this_thread::sleep_for(pollInterval);
  }
}

bool CompositorBridge::IsKDERunning() {
  const char *kdeSession = std::getenv("KDE_SESSION_VERSION");
  if (kdeSession && std::string(kdeSession) == "5") return true;
  const char *desktopSession = std::getenv("DESKTOP_SESSION");
  if (desktopSession &&
      std::string(desktopSession).find("plasma") != std::string::npos)
    return true;
  return system("pgrep -x kwin_wayland >/dev/null 2>&1") == 0 ||
         system("pgrep -x kwin_x11 >/dev/null 2>&1") == 0;
}

bool CompositorBridge::SendKWinZoomCommand(const std::string &command) {
  return system(("qdbus " + command + " 2>/dev/null").c_str()) == 0;
}

std::string
CompositorBridge::SendKWinZoomCommandWithOutput(const std::string &command) {
  std::array<char, 128> buffer;
  std::string result;
  std::string fullCommand = "qdbus " + command + " 2>/dev/null";
  auto pipe = popen(fullCommand.c_str(), "r");
  if (!pipe) {
    error("Failed to execute qdbus command: {}", command);
    return "";
  }
  while (fgets(buffer.data(), buffer.size(), pipe)) result += buffer.data();
  pclose(pipe);
  if (!result.empty() && result.back() == '\n') result.pop_back();
  return result;
}

} // namespace havel
