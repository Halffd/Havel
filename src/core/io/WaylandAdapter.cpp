#include "InputBackend.hpp"
#include "utils/Logger.hpp"
#include <shared_mutex>

#ifdef __linux__
#include <wayland-client.h>
#include <poll.h>
#endif

namespace havel {

class WaylandAdapter : public InputBackend {
public:
    WaylandAdapter() = default;
    ~WaylandAdapter() override { Shutdown(); }

    InputBackendType GetType() const override { return InputBackendType::Wayland; }
    std::string GetName() const override { return "wayland"; }

    bool Init() override {
#ifdef __linux__
        if (initialized_) return true;

        display_ = wl_display_connect(nullptr);
        if (!display_) {
            error("WaylandAdapter: Failed to connect to Wayland display");
            return false;
        }

        debug("WaylandAdapter: Connected to Wayland display");
        initialized_ = true;
        return true;
#else
        return false;
#endif
    }

    void Shutdown() override {
#ifdef __linux__
        if (display_) {
            wl_display_disconnect(display_);
            display_ = nullptr;
        }
#endif
        initialized_ = false;
    }

    bool IsInitialized() const override { return initialized_; }

    std::vector<DeviceInfo> EnumerateDevices() override {
        std::vector<DeviceInfo> result;
#ifdef __linux__
        if (display_) {
            DeviceInfo info;
            info.path = "wayland";
            info.name = "Wayland Compositor";
            info.fd = wl_display_get_fd(display_);
            info.backend = InputBackendType::Wayland;
            result.push_back(info);
        }
#endif
        return result;
    }

    bool OpenDevice(const std::string &path) override {
        return Init();
    }

    void CloseDevice(const std::string &path) override {
        Shutdown();
    }

    bool GrabDevice(const std::string &path) override {
        warn("WaylandAdapter: Direct grab not supported by Wayland protocol");
        return false;
    }

    void UngrabDevice(const std::string &path) override {}

    void UngrabAllDevices() override {}

    int GetPollFd() const override {
#ifdef __linux__
        return display_ ? wl_display_get_fd(display_) : -1;
#else
        return -1;
#endif
    }

    bool PollEvents(int timeoutMs) override {
#ifdef __linux__
        if (!display_) return false;

        struct pollfd pfd;
        pfd.fd = wl_display_get_fd(display_);
        pfd.events = POLLIN;

        if (poll(&pfd, 1, timeoutMs) <= 0) return false;

        wl_display_dispatch_pending(display_);
        wl_display_flush(display_);
        return true;
#else
        return false;
#endif
    }

    std::pair<int, int> GetMousePosition() const override {
        std::shared_lock<std::shared_mutex> lock(stateMutex_);
        return {mouseX_, mouseY_};
    }

    bool GetKeyState(uint32_t code) const override {
        std::shared_lock<std::shared_mutex> lock(stateMutex_);
        auto it = keyStates_.find(code);
        return it != keyStates_.end() && it->second;
    }

    uint32_t GetModifiers() const override {
        std::shared_lock<std::shared_mutex> lock(stateMutex_);
        return modifiers_;
    }

    bool SupportsGrab() const override { return false; }
    bool SupportsSynthesis() const override { return false; }

private:
#ifdef __linux__
    wl_display *display_ = nullptr;
#else
    void *display_ = nullptr;
#endif

    bool initialized_ = false;
    mutable std::shared_mutex stateMutex_;
    std::unordered_map<uint32_t, bool> keyStates_;
    uint32_t modifiers_ = 0;
    int32_t mouseX_ = 0;
    int32_t mouseY_ = 0;
};

std::unique_ptr<InputBackend> havel::CreateWaylandAdapter() {
    return std::make_unique<WaylandAdapter>();
}

}
