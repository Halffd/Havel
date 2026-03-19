/*
 * DetectorModule.cpp
 *
 * System detection module for Havel language.
 * Host binding - connects language to DisplayManager and WindowManagerDetector.
 */
#include "DetectorModule.hpp"
#include "../../havel-lang/runtime/Environment.hpp"
#include "core/DisplayManager.hpp"
#include "window/WindowManagerDetector.hpp"

namespace havel::modules {

void registerDetectorModule(Environment &env, std::shared_ptr<IHostAPI>) {
  // =========================================================================
  // Display detector
  // =========================================================================

  env.Define(
      "detectDisplay",
      HavelValue(makeBuiltinFunction([](const std::vector<HavelValue> &)
                                         -> HavelResult {
        auto monitors = DisplayManager::GetMonitors();
        auto result =
            std::make_shared<std::unordered_map<std::string, HavelValue>>();

        (*result)["count"] = HavelValue(static_cast<double>(monitors.size()));
        (*result)["type"] =
            HavelValue(WindowManagerDetector::IsWayland() ? "Wayland" : "X11");

        auto monitorsArray = std::make_shared<std::vector<HavelValue>>();
        for (const auto &monitor : monitors) {
          auto monitorObj =
              std::make_shared<std::unordered_map<std::string, HavelValue>>();
          (*monitorObj)["name"] = HavelValue(monitor.name);
          (*monitorObj)["x"] = HavelValue(static_cast<double>(monitor.x));
          (*monitorObj)["y"] = HavelValue(static_cast<double>(monitor.y));
          (*monitorObj)["width"] =
              HavelValue(static_cast<double>(monitor.width));
          (*monitorObj)["height"] =
              HavelValue(static_cast<double>(monitor.height));
          (*monitorObj)["isPrimary"] = HavelValue(monitor.isPrimary);
          monitorsArray->push_back(HavelValue(monitorObj));
        }
        (*result)["monitors"] = HavelValue(monitorsArray);

        return HavelValue(result);
      })));

  // =========================================================================
  // Monitor config detector
  // =========================================================================

  env.Define(
      "detectMonitorConfig",
      HavelValue(makeBuiltinFunction([](const std::vector<HavelValue> &)
                                         -> HavelResult {
        auto monitors = DisplayManager::GetMonitors();
        auto result =
            std::make_shared<std::unordered_map<std::string, HavelValue>>();

        (*result)["totalMonitors"] =
            HavelValue(static_cast<double>(monitors.size()));

        int primaryCount = 0;
        int totalWidth = 0, totalHeight = 0;

        for (const auto &monitor : monitors) {
          if (monitor.isPrimary)
            primaryCount++;
          totalWidth += monitor.width;
          totalHeight += monitor.height;
        }

        (*result)["primaryMonitors"] =
            HavelValue(static_cast<double>(primaryCount));
        (*result)["totalWidth"] = HavelValue(static_cast<double>(totalWidth));
        (*result)["totalHeight"] = HavelValue(static_cast<double>(totalHeight));
        (*result)["sessionType"] =
            HavelValue(WindowManagerDetector::IsWayland() ? "Wayland" : "X11");

        return HavelValue(result);
      })));

  // =========================================================================
  // Window manager detector
  // =========================================================================

  env.Define(
      "detectWindowManager",
      HavelValue(makeBuiltinFunction(
          [](const std::vector<HavelValue> &) -> HavelResult {
            auto result =
                std::make_shared<std::unordered_map<std::string, HavelValue>>();

            (*result)["name"] = HavelValue(WindowManagerDetector::GetWMName());
            (*result)["isWayland"] =
                HavelValue(WindowManagerDetector::IsWayland());
            (*result)["isX11"] = HavelValue(WindowManagerDetector::IsX11());
            (*result)["sessionType"] = HavelValue(
                WindowManagerDetector::IsWayland() ? "Wayland" : "X11");

            return HavelValue(result);
          })));

  // =========================================================================
  // System detector (OS, desktop environment, etc.)
  // =========================================================================

  env.Define(
      "detectSystem",
      HavelValue(makeBuiltinFunction(
          [](const std::vector<HavelValue> &) -> HavelResult {
            auto result =
                std::make_shared<std::unordered_map<std::string, HavelValue>>();

#ifdef __linux__
            (*result)["os"] = HavelValue("Linux");
#elif _WIN32
            (*result)["os"] = HavelValue("Windows");
#elif __APPLE__
            (*result)["os"] = HavelValue("macOS");
#else
            (*result)["os"] = HavelValue("Unknown");
#endif

            (*result)["arch"] = HavelValue(
#ifdef __x86_64__
                "x86_64"
#elif __aarch64__
                "arm64"
#else
                "unknown"
#endif
            );

            // Add window manager and desktop environment detection
            (*result)["windowManager"] =
                HavelValue(WindowManagerDetector::GetWMName());
            (*result)["displayServer"] = HavelValue(
                WindowManagerDetector::IsWayland() ? "Wayland" : "X11");
            (*result)["isWayland"] =
                HavelValue(WindowManagerDetector::IsWayland());
            (*result)["isX11"] = HavelValue(WindowManagerDetector::IsX11());

            // Desktop environment detection
            const char *de = std::getenv("XDG_CURRENT_DESKTOP");
            (*result)["desktopEnvironment"] = HavelValue(de ? de : "Unknown");

            return HavelValue(result);
          })));
}

} // namespace havel::modules
