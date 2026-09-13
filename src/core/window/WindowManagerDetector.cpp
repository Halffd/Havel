#include "WindowManagerDetector.hpp"
#include "core/display/DisplayManager.hpp"
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_set>

#ifdef __linux__
#include "x11.h"
#include <dirent.h>
#include <sys/types.h>
#include <unistd.h>
#endif
std::string WindowManagerDetector::wmName;
std::string WindowManagerDetector::sessionType;
std::string WindowManagerDetector::sessionName;
WindowManagerDetector::WMType WindowManagerDetector::Detect() noexcept {
  // Memoized: Detect() runs a ~30-probe cascade and multiple subsystems
  // call it independently during startup (WindowManager ctor, both
  // backends, factory, Havel init). The WM cannot change mid-process,
  // so compute once. Magic static = thread-safe init.
  static const WMType detected = DetectOnce();
  return detected;
}

WindowManagerDetector::WMType WindowManagerDetector::DetectOnce() noexcept {
  try {
    wmName = std::string(std::getenv("XDG_CURRENT_DESKTOP"));
    sessionType = std::string(std::getenv("XDG_SESSION_TYPE"));
    sessionName = std::string(std::getenv("DESKTOP_SESSION"));

    // Check Desktop Environments first
    if (CheckEnvironmentVar("XDG_CURRENT_DESKTOP", "GNOME") ||
        CheckProcess("gnome-shell"))
      return WMType::GNOME;
    if (CheckEnvironmentVar("XDG_CURRENT_DESKTOP", "KDE") ||
        CheckProcess("plasmashell"))
      return WMType::KDE;
    if (CheckEnvironmentVar("XDG_CURRENT_DESKTOP", "XFCE") ||
        CheckProcess("xfce4-session"))
      return WMType::XFCE;
    if (CheckEnvironmentVar("XDG_CURRENT_DESKTOP", "MATE") ||
        CheckProcess("mate-session"))
      return WMType::MATE;
    if (CheckEnvironmentVar("XDG_CURRENT_DESKTOP", "X-Cinnamon") ||
        CheckProcess("cinnamon-session"))
      return WMType::CINNAMON;
    if (CheckEnvironmentVar("XDG_CURRENT_DESKTOP", "LXDE") ||
        CheckProcess("lxsession"))
      return WMType::LXDE;
    if (CheckEnvironmentVar("XDG_CURRENT_DESKTOP", "LXQt") ||
        CheckProcess("lxqt-session"))
      return WMType::LXQT;
    if (CheckEnvironmentVar("XDG_CURRENT_DESKTOP", "Budgie") ||
        CheckProcess("budgie-wm"))
      return WMType::BUDGIE;
    if (CheckEnvironmentVar("XDG_CURRENT_DESKTOP", "Deepin") ||
        CheckProcess("deepin-wm"))
      return WMType::DEEPIN;
    if (CheckEnvironmentVar("XDG_CURRENT_DESKTOP", "Pantheon") ||
        CheckProcess("gala"))
      return WMType::PANTHEON;

    // Check Window Managers
    // Prioritize Wayland compositors first to avoid false positives
    if (CheckProcess("Hyprland") ||
        CheckEnvironmentVar("XDG_CURRENT_DESKTOP", "Hyprland"))
      return WMType::HYPRLAND;
    if (CheckProcess("sway"))
      return WMType::SWAY;
    if (CheckProcess("river"))
      return WMType::RIVER;
    if (CheckProcess("wayfire"))
      return WMType::WAYFIRE;

    // Check X11 window managers
    if (CheckProcess("i3") || CheckXProperty("I3_SOCKET_PATH"))
      return WMType::I3;
    if (CheckProcess("bspwm"))
      return WMType::BSPWM;
    if (CheckProcess("dwm"))
      return WMType::DWM;
    if (CheckProcess("herbstluft"))
      return WMType::HERBSTLUFT;
    if (CheckProcess("awesome"))
      return WMType::AWESOME;
    if (CheckProcess("xmonad"))
      return WMType::XMONAD;
    if (CheckProcess("qtile"))
      return WMType::QTILE;
    if (CheckProcess("xfwm4"))
      return WMType::XFWM;
    if (CheckProcess("openbox"))
      return WMType::OPENBOX;
    if (CheckProcess("fluxbox"))
      return WMType::FLUXBOX;
    if (CheckProcess("jwm"))
      return WMType::JWM;
    if (CheckProcess("mutter") || CheckProcess("gnome-shell"))
      return WMType::MUTTER;
    if (CheckProcess("kwin_x11") || CheckProcess("kwin_wayland"))
      return WMType::KWIN;
    if (CheckProcess("wayfire"))
      return WMType::WAYFIRE;
    if (CheckProcess("river"))
      return WMType::RIVER;
    if (CheckProcess("picom"))
      return WMType::PICOM;
    if (CheckProcess("compton"))
      return WMType::COMPTON;

    return WMType::UNKNOWN;
  } catch (...) {
    return WMType::UNKNOWN;
  }
}

std::string WindowManagerDetector::GetWMName() noexcept {
  switch (Detect()) {
  // Window Managers
  case WMType::I3:
    return "i3";
  case WMType::SWAY:
    return "Sway";
  case WMType::BSPWM:
    return "BSPWM";
  case WMType::DWM:
    return "DWM";
  case WMType::AWESOME:
    return "Awesome";
  case WMType::XMONAD:
    return "XMonad";
  case WMType::OPENBOX:
    return "Openbox";
  case WMType::FLUXBOX:
    return "Fluxbox";
  case WMType::ICEWM:
    return "IceWM";

  // Compositors
  case WMType::COMPIZ:
    return "Compiz";
  case WMType::XFWM:
    return "XFWM";
  case WMType::MUTTER:
    return "Mutter";
  case WMType::KWIN:
    return "KWin";
  case WMType::HYPRLAND:
    return "Hyprland";
  case WMType::WAYFIRE:
    return "Wayfire";
  case WMType::RIVER:
    return "River";
  case WMType::PICOM:
    return "Picom";
  case WMType::COMPTON:
    return "Compton";

  // Desktop Environments
  case WMType::GNOME:
    return "GNOME";
  case WMType::KDE:
    return "KDE Plasma";
  case WMType::XFCE:
    return "XFCE";
  case WMType::MATE:
    return "MATE";
  case WMType::CINNAMON:
    return "Cinnamon";
  case WMType::LXDE:
    return "LXDE";
  case WMType::LXQT:
    return "LXQt";
  case WMType::BUDGIE:
    return "Budgie";
  case WMType::DEEPIN:
    return "Deepin";
  case WMType::PANTHEON:
    return "Pantheon";

  default:
    return "Unknown";
  }
}

bool WindowManagerDetector::IsWayland() noexcept {
  try {
    // Primary check: WAYLAND_DISPLAY environment variable
    const char *waylandDisplay = std::getenv("WAYLAND_DISPLAY");
    if (waylandDisplay && std::strlen(waylandDisplay) > 0) {
      return true; // Any WAYLAND_DISPLAY set indicates Wayland session
    }

    // Secondary: Check for running Wayland compositors
    if (CheckProcess("Hyprland") || CheckProcess("sway") ||
        CheckProcess("wayfire") || CheckProcess("river") ||
        CheckProcess("weston") || CheckProcess("kwin_wayland")) {
      return true;
    }

    // Tertiary: XDG_SESSION_TYPE (only reliable with display managers)
    const char *sessionType = std::getenv("XDG_SESSION_TYPE");
    if (sessionType && std::string(sessionType) == "wayland") {
      return true;
    }

    return false;
  } catch (...) {
    return false;
  }
}

bool WindowManagerDetector::IsX11() noexcept {
  try {
    // Check for Wayland first - if present, we're NOT on pure X11
    const char *waylandDisplay = std::getenv("WAYLAND_DISPLAY");
    if (waylandDisplay && std::strlen(waylandDisplay) > 0) {
      return false; // Wayland session (may have Xwayland, but not native X11)
    }

    // Check for X11 DISPLAY
    const char *display = std::getenv("DISPLAY");
    if (display && std::strlen(display) > 0) {
      return true; // X11 session (including startx)
    }

    // Fallback: XDG_SESSION_TYPE (unreliable with startx)
    const char *sessionType = std::getenv("XDG_SESSION_TYPE");
    if (sessionType) {
      std::string type(sessionType);
      if (type == "x11")
        return true;
      if (type == "wayland")
        return false;
    }

    // Unknown: default to non-X11 (safer than assuming X11)
    return false;
  } catch (...) {
    // On error, default to non-X11 (safer)
    return false;
  }
}

bool WindowManagerDetector::CheckProcess(
    const std::string &processName) noexcept {
#ifdef __linux__
  // One /proc cmdline snapshot per process, shared by every CheckProcess
  // call. Detect() cascades through up to ~30 CheckProcess probes and the
  // callers (WindowManager ctor, both backends, factory, Havel init)
  // re-run Detect() several times per launch; the old per-call /proc walk
  // did 13k+ openat/read/close triplets per havel start (measured with
  // strace: 14,490 cmdline reads, 18 opendir("/proc") scans). The WM
  // cannot change during our lifetime, so a snapshot is equivalent.
  // Guarded C++11 magic static: thread-safe one-time build.
  static const auto processBasenames = []() noexcept {
    std::unordered_set<std::string> names;
    try {
      DIR *dir = opendir("/proc");
      if (!dir)
        return names;
      struct dirent *entry;
      while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_type != DT_DIR)
          continue;
        // Directory name must be a PID (all digits, non-empty)
        const char *pid = entry->d_name;
        if (!*pid)
          continue;
        for (const char *p = pid; *p; ++p) {
          if (*p < '0' || *p > '9') {
            pid = nullptr;
            break;
          }
        }
        if (!pid)
          continue;

        char path[64];
        snprintf(path, sizeof(path), "/proc/%s/cmdline", entry->d_name);
        FILE *cmdline = fopen(path, "r");
        if (!cmdline)
          continue;
        std::array<char, 1024> buffer;
        size_t bytes = fread(buffer.data(), 1, buffer.size() - 1, cmdline);
        fclose(cmdline);
        if (bytes == 0)
          continue;

        // First null-separated token = executable path; store its
        // basename so probes match "cinnamon" for "/usr/bin/cinnamon".
        const char *exe = buffer.data();
        const char *end = static_cast<const char *>(
            memchr(exe, '\0', bytes));
        size_t exeLen = end ? static_cast<size_t>(end - exe) : bytes;
        if (exeLen == 0)
          continue;
        const char *slash = static_cast<const char *>(
            memrchr(exe, '/', exeLen));
        const char *base = slash ? slash + 1 : exe;
        size_t baseLen = slash ? exeLen - static_cast<size_t>(base - exe) : exeLen;
        if (baseLen > 0) {
          names.emplace(base, baseLen);
        }
      }
      closedir(dir);
    } catch (...) {
    }
    return names;
  }();

  try {
    return processBasenames.find(processName) != processBasenames.end();
  } catch (...) {
    return false;
  }
#else
  (void)processName;
  return false;
#endif
}

bool WindowManagerDetector::CheckEnvironmentVar(
    const std::string &varName, const std::string &value) noexcept {
  try {
    const char *env = std::getenv(varName.c_str());
    return env && std::string(env) == value;
  } catch (...) {
    return false;
  }
}

bool WindowManagerDetector::CheckXProperty(
    const std::string &property) noexcept {
  try {
    Display *display = havel::DisplayManager::GetDisplay();
    if (!display)
      return false;

    int screen = DefaultScreen(display);
    Window root = RootWindow(display, screen);
    XWindowAttributes attributes;
    if (XGetWindowAttributes(display, root, &attributes) == 0) {
      return false;
    }

    Atom atom = XInternAtom(display, property.c_str(), x11::XFalse);
    if (atom == x11::XNone) {
      return false;
    }

    // Check if property exists on root window
    return true;
  } catch (...) {
    return false;
  }
}