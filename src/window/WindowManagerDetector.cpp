#include "WindowManagerDetector.hpp"
#include <cstdlib>
#include <array>
#include <memory>
#include <iostream>

#ifdef __linux__
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include "x11.h"
#endif

WindowManagerDetector::WMType WindowManagerDetector::Detect() noexcept {
    try {
        const char* wmName = std::getenv("XDG_CURRENT_DESKTOP");
        const char* sessionType = std::getenv("XDG_SESSION_TYPE");
        const char* wmSession = std::getenv("DESKTOP_SESSION");
        
        // Check Desktop Environments first
        if (CheckEnvironmentVar("XDG_CURRENT_DESKTOP", "GNOME") || CheckProcess("gnome-shell"))
            return WMType::GNOME;
        if (CheckEnvironmentVar("XDG_CURRENT_DESKTOP", "KDE") || CheckProcess("plasmashell"))
            return WMType::KDE;
        if (CheckEnvironmentVar("XDG_CURRENT_DESKTOP", "XFCE") || CheckProcess("xfce4-session"))
            return WMType::XFCE;
        if (CheckEnvironmentVar("XDG_CURRENT_DESKTOP", "MATE") || CheckProcess("mate-session"))
            return WMType::MATE;
        if (CheckEnvironmentVar("XDG_CURRENT_DESKTOP", "X-Cinnamon") || CheckProcess("cinnamon-session"))
            return WMType::CINNAMON;
        if (CheckEnvironmentVar("XDG_CURRENT_DESKTOP", "LXDE") || CheckProcess("lxsession"))
            return WMType::LXDE;
        if (CheckEnvironmentVar("XDG_CURRENT_DESKTOP", "LXQt") || CheckProcess("lxqt-session"))
            return WMType::LXQT;
        if (CheckEnvironmentVar("XDG_CURRENT_DESKTOP", "Budgie") || CheckProcess("budgie-wm"))
            return WMType::BUDGIE;
        if (CheckEnvironmentVar("XDG_CURRENT_DESKTOP", "Deepin") || CheckProcess("deepin-wm"))
            return WMType::DEEPIN;
        if (CheckEnvironmentVar("XDG_CURRENT_DESKTOP", "Pantheon") || CheckProcess("gala"))
            return WMType::PANTHEON;

        // Check Window Managers
        if (CheckProcess("i3") || CheckXProperty("I3_SOCKET_PATH"))
            return WMType::I3;
        if (CheckProcess("sway"))
            return WMType::SWAY;
        if (CheckProcess("bspwm"))
            return WMType::BSPWM;
        if (CheckProcess("dwm"))
            return WMType::DWM;
        if (CheckProcess("awesome"))
            return WMType::AWESOME;
        if (CheckProcess("xmonad"))
            return WMType::XMONAD;
        if (CheckProcess("openbox"))
            return WMType::OPENBOX;
        if (CheckProcess("fluxbox"))
            return WMType::FLUXBOX;
        if (CheckProcess("icewm"))
            return WMType::ICEWM;

        // Check Compositors
        if (CheckProcess("compiz"))
            return WMType::COMPIZ;
        if (CheckProcess("xfwm4"))
            return WMType::XFWM;
        if (CheckProcess("mutter"))
            return WMType::MUTTER;
        if (CheckProcess("kwin_x11") || CheckProcess("kwin_wayland"))
            return WMType::KWIN;
        if (CheckProcess("Hyprland") || CheckEnvironmentVar("XDG_CURRENT_DESKTOP", "Hyprland"))
            return WMType::HYPRLAND;
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
        case WMType::I3: return "i3";
        case WMType::SWAY: return "Sway";
        case WMType::BSPWM: return "BSPWM";
        case WMType::DWM: return "DWM";
        case WMType::AWESOME: return "Awesome";
        case WMType::XMONAD: return "XMonad";
        case WMType::OPENBOX: return "Openbox";
        case WMType::FLUXBOX: return "Fluxbox";
        case WMType::ICEWM: return "IceWM";
        
        // Compositors
        case WMType::COMPIZ: return "Compiz";
        case WMType::XFWM: return "XFWM";
        case WMType::MUTTER: return "Mutter";
        case WMType::KWIN: return "KWin";
        case WMType::HYPRLAND: return "Hyprland";
        case WMType::WAYFIRE: return "Wayfire";
        case WMType::RIVER: return "River";
        case WMType::PICOM: return "Picom";
        case WMType::COMPTON: return "Compton";
        
        // Desktop Environments
        case WMType::GNOME: return "GNOME";
        case WMType::KDE: return "KDE Plasma";
        case WMType::XFCE: return "XFCE";
        case WMType::MATE: return "MATE";
        case WMType::CINNAMON: return "Cinnamon";
        case WMType::LXDE: return "LXDE";
        case WMType::LXQT: return "LXQt";
        case WMType::BUDGIE: return "Budgie";
        case WMType::DEEPIN: return "Deepin";
        case WMType::PANTHEON: return "Pantheon";
        
        default: return "Unknown";
    }
}

bool WindowManagerDetector::IsWayland() noexcept {
    try {
        const char* sessionType = std::getenv("XDG_SESSION_TYPE");
        return sessionType && std::string(sessionType) == "wayland";
    } catch (...) {
        return false;
    }
}

bool WindowManagerDetector::IsX11() noexcept {
    try {
        const char* sessionType = std::getenv("XDG_SESSION_TYPE");
        return !sessionType || std::string(sessionType) == "x11";
    } catch (...) {
        return true; // Default to X11 if we can't determine
    }
}

bool WindowManagerDetector::CheckProcess(const std::string& processName) noexcept {
#ifdef __linux__
    try {
        DIR* dir = opendir("/proc");
        if (!dir) return false;
        
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (entry->d_type == DT_DIR) {
                // Check if directory name is a number (PID)
                std::string pid = entry->d_name;
                if (pid.find_first_not_of("0123456789") != std::string::npos)
                    continue;
                
                std::string cmdlinePath = "/proc/" + pid + "/cmdline";
                FILE* cmdline = fopen(cmdlinePath.c_str(), "r");
                if (cmdline) {
                    std::array<char, 1024> buffer;
                    size_t bytes = fread(buffer.data(), 1, buffer.size() - 1, cmdline);
                    fclose(cmdline);
                    
                    if (bytes > 0) {
                        buffer[bytes] = '\0';
                        std::string cmd(buffer.data());
                        if (cmd.find(processName) != std::string::npos) {
                            closedir(dir);
                            return true;
                        }
                    }
                }
            }
        }
        closedir(dir);
    } catch (...) {
        return false;
    }
#endif
    return false;
}

bool WindowManagerDetector::CheckEnvironmentVar(const std::string& varName, 
                                              const std::string& value) noexcept {
    try {
        const char* env = std::getenv(varName.c_str());
        return env && std::string(env) == value;
    } catch (...) {
        return false;
    }
}

bool WindowManagerDetector::CheckXProperty(const std::string& property) noexcept {
    try {
        Display* display = XOpenDisplay(nullptr);
        if (!display) return false;
        
        int screen = DefaultScreen(display);
        Window root = RootWindow(display, screen);
        XWindowAttributes attributes;
        if (XGetWindowAttributes(display, root, &attributes) == 0) {
            XCloseDisplay(display);
            return false;
        }
        
        Atom atom = XInternAtom(display, property.c_str(), x11::XFalse);
        if (atom == x11::XNone) {
            XCloseDisplay(display);
            return false;
        }
        
        XCloseDisplay(display);
        return true;
    } catch (...) {
        return false;
    }
} 