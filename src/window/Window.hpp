#pragma once
#include "types.hpp"
#include <string>
#include <chrono>
#include <memory>

#ifdef __linux__
#include <X11/Xlib.h>
struct _XDisplay;
typedef struct _XDisplay Display;
#endif

namespace havel {

class Window {
public:
    Window(cstr title, wID id = 0);
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
    void Activate(wID win = 0);
    void Close(wID win = 0);
    void Min(wID win = 0);
    void Max(wID win = 0);
    void Hide(wID win = 0);
    void Show(wID win = 0);
    void Minimize(wID win = 0);
    void Transparency(wID win = 0, int alpha = 255);
    void AlwaysOnTop(wID win = 0, bool top = true);
    
    // Window info
    bool Active(wID win = 0);
    bool Exists(wID win = 0);

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
    wID m_id;
};

} // namespace havel
