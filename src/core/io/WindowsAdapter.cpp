#include "InputBackend.hpp"
#include "utils/Logger.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

namespace havel {

class WindowsAdapter : public InputBackend {
public:
    WindowsAdapter() = default;
    ~WindowsAdapter() override { Shutdown(); }

    InputBackendType GetType() const override { return InputBackendType::Windows; }
    std::string GetName() const override { return "windows"; }

    bool Init() override {
#ifdef _WIN32
        if (initialized_) return true;

        instance_ = GetModuleHandle(nullptr);
        initialized_ = true;
        debug("WindowsAdapter: Initialized");
        return true;
#else
        return false;
#endif
    }

    void Shutdown() override {
#ifdef _WIN32
        if (hook_) {
            UnhookWindowsHookEx(hook_);
            hook_ = nullptr;
        }
#endif
        initialized_ = false;
    }

    bool IsInitialized() const override { return initialized_; }

    std::vector<DeviceInfo> EnumerateDevices() override {
        std::vector<DeviceInfo> result;
#ifdef _WIN32
        DeviceInfo info;
        info.path = "windows";
        info.name = "Windows Input System";
        info.backend = InputBackendType::Windows;
        result.push_back(info);
#endif
        return result;
    }

    bool OpenDevice(const std::string &path) override { return Init(); }
    void CloseDevice(const std::string &path) override { Shutdown(); }

    bool GrabDevice(const std::string &path) override {
#ifdef _WIN32
        if (!initialized_) return false;

        if (!hook_) {
            hook_ = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc,
                instance_, 0);
            if (!hook_) {
                error("WindowsAdapter: Failed to install keyboard hook");
                return false;
            }
        }
        grabbed_ = true;
        debug("WindowsAdapter: Grabbed input");
        return true;
#else
        return false;
#endif
    }

    void UngrabDevice(const std::string &path) override { UngrabAllDevices(); }

    void UngrabAllDevices() override {
#ifdef _WIN32
        if (hook_) {
            UnhookWindowsHookEx(hook_);
            hook_ = nullptr;
        }
        grabbed_ = false;
#endif
    }

    int GetPollFd() const override { return -1; }

    bool PollEvents(int timeoutMs) override {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        return true;
    }

    std::pair<int, int> GetMousePosition() const override {
#ifdef _WIN32
        POINT pt;
        GetCursorPos(&pt);
        return {pt.x, pt.y};
#else
        return {0, 0};
#endif
    }

    bool GetKeyState(uint32_t code) const override {
#ifdef _WIN32
        return (GetAsyncKeyState(code) & 0x8000) != 0;
#else
        return false;
#endif
    }

    uint32_t GetModifiers() const override {
        uint32_t mods = 0;
#ifdef _WIN32
        if (GetAsyncKeyState(VK_CONTROL) & 0x8000) mods |= (1 << 0);
        if (GetAsyncKeyState(VK_SHIFT) & 0x8000) mods |= (1 << 1);
        if (GetAsyncKeyState(VK_MENU) & 0x8000) mods |= (1 << 2);
        if (GetAsyncKeyState(VK_LWIN) || GetAsyncKeyState(VK_RWIN)) mods |= (1 << 3);
#endif
        return mods;
    }

    bool SupportsGrab() const override { return true; }
    bool SupportsSynthesis() const override { return true; }

private:
#ifdef _WIN32
    static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
        if (nCode == HC_ACTION && instance_) {
            KBDLLHOOKSTRUCT *kb = (KBDLLHOOKSTRUCT *)lParam;
            bool down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
            bool repeat = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) ? false :
                (kb->flags & LLKHF_EXTENDED) != 0;

            if (instance_->keyCallback_) {
                KeyEvent event;
                event.code = kb->vkCode;
                event.down = down;
                event.repeat = repeat;
                event.modifiers = instance_->GetModifiers();
                event.timestamp = std::chrono::steady_clock::now();
                instance_->keyCallback_(event);
            }

            if (instance_->grabbed_) {
                return 1;
            }
        }
        return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }

    static WindowsAdapter *instance_;
    HHOOK hook_ = nullptr;
    HMODULE instance_ = nullptr;
#else
    void *hook_ = nullptr;
    void *instance_ = nullptr;
#endif

    bool initialized_ = false;
    bool grabbed_ = false;
};

#ifdef _WIN32
WindowsAdapter *WindowsAdapter::instance_ = nullptr;
#endif

}
