#include "CompositorBridge.hpp"
#include "strategies/CompositorStrategy.hpp"
#include "utils/Logger.hpp"
#include "utils/Util.hpp"
#include "utils/SafeExec.hpp"
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
    return havel::utils::processExistsByName("kwin_wayland") ||
        havel::utils::processExistsByName("kwin_x11");
}

bool CompositorBridge::SendKWinZoomCommand(const std::string &command) {
    auto result = havel::utils::execSync({"qdbus", command});
    return result && result->exitCode == 0;
}

std::string
CompositorBridge::SendKWinZoomCommandWithOutput(const std::string &command) {
    auto output = havel::utils::execCapture({"qdbus", command});
    if (!output) return "";
    std::string result = *output;
    if (!result.empty() && result.back() == '\n') result.pop_back();
    return result;
}

} // namespace havel
