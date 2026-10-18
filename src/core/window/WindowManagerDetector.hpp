#pragma once
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

class WindowManagerDetector {
public:
  enum class WMType {
    UNKNOWN = 0,

    // Window Managers
    I3,
    SWAY,
    BSPWM,
    DWM,
    AWESOME,
    XMONAD,
    OPENBOX,
    FLUXBOX,
    ICEWM,
    HERBSTLUFT,
    QTILE,
    JWM,

    // Compositors
    COMPIZ,
    XFWM,
    MUTTER,
    KWIN,
    HYPRLAND,
    WAYFIRE,
    RIVER,
    PICOM,
    COMPTON,

    // Desktop Environments
    GNOME,
    KDE,
    XFCE,
    MATE,
    CINNAMON,
    LXDE,
    LXQT,
    BUDGIE,
    DEEPIN,
    PANTHEON
  };

  static WMType Detect() noexcept;
  static std::string GetWMName() noexcept;
  static bool IsWayland() noexcept;
  static bool IsX11() noexcept;
  // Pure predicate over the environment: no state, no X11 connection.
  // Public so the colon-list matching it performs can be tested directly
  // - Detect() is memoized in a magic static and cannot be re-driven.
  static std::string sessionType;
  static std::string wmName;
  static std::string sessionName;

  // XDG_CURRENT_DESKTOP is spec'd as a colon-separated list, not a single
  // value: "ubuntu:GNOME", "X-Cinnamon:GNOME", "sway:sway-plasma". Exact
  // equality missed every multi-entry value, so all desktop-environment
  // probes fell through to the bare process checks. Pure env-var predicate:
  // inline in the header so the unit test is self-contained (just this
  // header + gtest, no CORE_SOURCES dependency chain).
  static bool CheckEnvironmentVar(
      const std::string &varName, const std::string &value) noexcept {
    try {
      const char *env = std::getenv(varName.c_str());
      if (!env)
        return false;
      std::string list(env);
      size_t start = 0;
      while (start <= list.size()) {
        size_t end = list.find(':', start);
        if (end == std::string::npos)
          end = list.size();
        if (end > start && list.compare(start, end - start, value) == 0)
          return true;
        start = end + 1;
      }
      return false;
    } catch (...) {
      return false;
    }
  }

private:
  static WMType DetectOnce() noexcept;
  static bool CheckProcess(const std::string &processName) noexcept;
  static bool CheckXProperty(const std::string &property) noexcept;
};