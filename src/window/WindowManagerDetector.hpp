#pragma once
#include <string>
#include <memory>
#include <stdexcept>

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
    static std::string sessionType;
    static std::string wmName;
    static std::string sessionName;
private:
    static bool CheckProcess(const std::string& processName) noexcept;
    static bool CheckEnvironmentVar(const std::string& varName, const std::string& value) noexcept;
    static bool CheckXProperty(const std::string& property) noexcept;
}; 