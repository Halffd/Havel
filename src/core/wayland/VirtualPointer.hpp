#pragma once

#include <string>
#include <cstdint>
#include <memory>

struct zwlr_virtual_pointer_v1;
struct wl_output;

namespace havel {

class WaylandProtocolClient;
class UinputDevice;

class VirtualPointer {
public:
    explicit VirtualPointer(WaylandProtocolClient &client);
    ~VirtualPointer();

    bool initialize();
    void cleanup();
    bool isAvailable() const { return vptr_ != nullptr; }

    bool moveRelative(double dx, double dy);
    bool moveAbsolute(uint32_t x, uint32_t y, uint32_t xExtent, uint32_t yExtent);
    bool clickButton(uint32_t button, bool down);
    bool tapButton(uint32_t button, uint32_t delayMs = 50);
    bool scrollAxis(uint32_t axis, double value, int32_t discrete = 0);
    bool scrollDiscrete(int32_t dy, int32_t dx = 0);
    bool scrollSmooth(double dy, double dx = 0.0);
    void frame();

    void setUinputFallback(UinputDevice *uinput) { uinput_ = uinput; }

    static uint32_t buttonNameToLinux(const std::string &name);

private:
    uint32_t timestamp();

    WaylandProtocolClient &client_;
    zwlr_virtual_pointer_v1 *vptr_ = nullptr;
    UinputDevice *uinput_ = nullptr;
    uint32_t serial_ = 0;
};

} // namespace havel
