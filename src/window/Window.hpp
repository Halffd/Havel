#pragma once
#include <string>
#include <chrono>
#include <memory>
#include "utils/Rect.hpp"
#include "types.hpp"

namespace havel {

class Window {
public:
    Window(cstr title, wID id = 0);
    Window(wID id);
    Window() = default;
    ~Window() = default;

    // Static display pointer
    #ifdef __linux__
    static std::shared_ptr<Display> display;
    static DisplayServer displayServer;
    #endif

    // Window properties
    wID ID() const { return m_id; }
    std::string Title() const { return m_title; }
    std::string Title(wID win);
    
    // Position methods
    Rect Pos() const;
    static Rect Pos(wID win);
    
    // Window operations
    void Activate();
    void Activate(wID win);
    void Close();
    void Close(wID win);
    void Min();
    void Min(wID win);
    void Max();
    void Max(wID win);
    void Hide();
    void Show();
    void Minimize();
    void Transparency(int alpha = 255);
    void Transparency(wID win, int alpha);
    void AlwaysOnTop(bool top = true);
    void AlwaysOnTop(wID win, bool top);
    void ToggleFullscreen();

    // Movement and Resizing
    bool Move(int x, int y, bool centerOnScreen = false);
    bool Resize(int width, int height, bool fullscreen = false);
    bool MoveResize(int x, int y, int width, int height);
    bool Center();
    bool MoveToCorner(const std::string& corner);
    bool MoveToMonitor(int monitorIndex);
    void Snap(int position);

    // Window info
    bool Active();
    bool Active(wID win);
    bool Exists();
    bool Exists(wID win);

    // Window finding methods
    static wID Find(cstr identifier);
    static wID Find2(cstr identifier, cstr type = "title");
    
    template<typename T>
    static wID FindT(T identifier) {
        if constexpr (std::is_same_v<T, std::string>) {
            return Find(identifier);
        } else if constexpr (std::is_same_v<T, const char*>) {
            return Find(std::string(identifier));
        } else if constexpr (std::is_same_v<T, wID>) {
            return identifier;
        } else if constexpr (std::is_same_v<T, pID>) {
            return GetwIDByPID(identifier);
        } else {
            return 0;
        }
    }

private:
    // Platform-specific implementations
    static Rect GetPositionX11(wID win);
    static Rect GetPositionWayland(wID win);
    #ifdef _WIN32
    static Rect GetPositionWindows(wID win);
    #endif

    // Helper methods
    static wID FindByTitle(cstr title);
    static wID FindByClass(cstr className);
    static wID GetwIDByPID(pID pid);
    static wID GetwIDByProcessName(cstr processName);
    
    // X11 helper methods
    static void SetAlwaysOnTopX11(wID win, bool top);

    std::string m_title;
    wID m_id {0};
};

} // namespace havel