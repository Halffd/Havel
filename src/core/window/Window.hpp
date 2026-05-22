#pragma once
#include <string>
#include <chrono>
#include <memory>
#include "types.hpp"
#include "WindowManager.hpp"

namespace havel {

class Window {
public:
    Window(cstr title, wID id = 0);
    Window(wID id);
    Window() = default;
    ~Window() = default;

    wID ID() const { return m_id; }
    std::string Title() const { return m_title; }
    static std::string Title(wID win);
    std::string Class() const { return m_class; }
    static std::string Class(wID win);
    pID PID() const { return m_pid; }
    static pID PID(wID win);

    Rect Pos() const;
    static Rect Pos(wID win);

    void Activate();
    void Close();
    void Min();
    void Max();
    void Hide();
    void Show();
    void Minimize();
    void Transparency(int alpha = 255);
    void Transparency(wID win, int alpha);
    void AlwaysOnTop(bool top = true);
    void AlwaysOnTop(wID win, bool top);
    void ToggleFullscreen();

    bool Move(int x, int y, bool centerOnScreen = false);
    bool Resize(int width, int height, bool fullscreen = false);
    bool MoveResize(int x, int y, int width, int height);
    bool Center();
    bool MoveToCorner(const std::string& corner);
    bool MoveToMonitor(int monitorIndex);
    void Snap(int position);

    bool Active();
    bool Active(wID win);
    bool Exists();
    bool Exists(wID win);

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
            return WindowManager::GetwIDByPID(identifier);
        } else {
            return 0;
        }
    }

private:
    std::string m_title;
    std::string m_class;
    pID m_pid {0};
    wID m_id {0};

    bool operator==(const Window& other) const noexcept {
        return m_id == other.m_id;
    }
};

} // namespace havel

namespace std {
template<>
struct hash<havel::Window> {
    size_t operator()(const havel::Window& w) const noexcept {
        return std::hash<::wID>{}(w.ID());
    }
};
}
