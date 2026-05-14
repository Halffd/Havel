#pragma once

#include "InputBackend.hpp"
#include "core/CallbackTypes.hpp"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace havel {

class HotkeyExecutor;
struct HotKey;

class InputManager {
public:
    using HotkeyCallback = std::function<void()>;

    InputManager();
    ~InputManager();

    bool Start(InputBackendType backend = InputBackendType::Unknown);
    void Stop();

    bool IsRunning() const { return running_.load(); }

    void SetBackend(InputBackendType type);
    InputBackendType GetBackendType() const;

    void SetHotkeyExecutor(HotkeyExecutor *executor) { hotkeyExecutor_ = executor; }

    bool GrabInput();
    void UngrabInput();

    bool GetKeyState(uint32_t code) const;
    uint32_t GetModifiers() const;
    std::pair<int, int> GetMousePosition() const;

    void SetKeyCallback(InputBackend::KeyCallback cb);
    void SetMouseCallback(InputBackend::MouseCallback cb);

    void RegisterHotkey(int id, uint32_t code, uint32_t modifiers, HotkeyCallback cb);
    void UnregisterHotkey(int id);

    void SetInputBlockCallback(std::function<bool(const InputEvent &)> cb) {
        blockCallback_ = std::move(cb);
    }

private:
    void EventLoop();
    void ProcessKeyEvent(const KeyEvent &event);
    void ProcessMouseEvent(const MouseEvent &event);
    bool EvaluateHotkeys(uint32_t code, bool down, uint32_t modifiers);

    std::unique_ptr<InputBackend> backend_;
    std::atomic<bool> running_{false};
    std::atomic<bool> grabbed_{false};
    std::thread eventThread_;

    HotkeyExecutor *hotkeyExecutor_ = nullptr;
    std::function<bool(const InputEvent &)> blockCallback_;

    struct HotkeyInfo {
        uint32_t code = 0;
        uint32_t modifiers = 0;
        bool wildcard = false;
        HotkeyCallback callback;
    };

    std::unordered_map<int, HotkeyInfo> hotkeys_;
    mutable std::mutex hotkeyMutex_;

    std::unordered_map<uint32_t, bool> keyStates_;
    uint32_t modifiers_ = 0;
    mutable std::mutex stateMutex_;
};

}
