#pragma once

#include <wayland-client.h>
#include <string>
#include <vector>
#include <mutex>
#include <memory>
#include <functional>
#include <atomic>

struct wl_seat;
struct wl_output;
struct zwp_virtual_keyboard_manager_v1;
struct zwlr_virtual_pointer_manager_v1;
struct zwlr_foreign_toplevel_manager_v1;
struct zwlr_data_control_manager_v1;
struct ext_data_control_manager_v1;

namespace havel {

struct WaylandProtocolInfo {
    std::string name;
    uint32_t version;
};

struct WaylandOutputInfo {
    struct wl_output *output = nullptr;
    std::string name;
    int32_t width = 0;
    int32_t height = 0;
    int32_t scale = 1;
    int32_t x = 0;
    int32_t y = 0;
};

class __attribute__((visibility("default"))) WaylandProtocolClient {
public:
    static WaylandProtocolClient& instance();

    bool connect();
    void disconnect();
    bool isConnected() const { return connected_; }

    wl_display* display() const { return display_; }
    wl_seat* seat() const { return seat_; }

    bool hasVirtualKeyboard() const { return virtualKeyboardManager_ != nullptr; }
    bool hasVirtualPointer() const { return virtualPointerManager_ != nullptr; }
    bool hasForeignToplevel() const { return foreignToplevelManager_ != nullptr; }
    bool hasDataControl() const { return dataControlManager_ != nullptr || extDataControlManager_ != nullptr; }
    bool hasExtDataControl() const { return extDataControlManager_ != nullptr; }
    bool hasWlrDataControl() const { return dataControlManager_ != nullptr; }
    bool hasSeat() const { return seat_ != nullptr; }

    zwp_virtual_keyboard_manager_v1* virtualKeyboardManager() const { return virtualKeyboardManager_; }
    zwlr_virtual_pointer_manager_v1* virtualPointerManager() const { return virtualPointerManager_; }
    zwlr_foreign_toplevel_manager_v1* foreignToplevelManager() const { return foreignToplevelManager_; }
    zwlr_data_control_manager_v1* wlrDataControlManager() const { return dataControlManager_; }
    ext_data_control_manager_v1* extDataControlManager() const { return extDataControlManager_; }

    const std::vector<WaylandOutputInfo>& outputs() const { return outputs_; }
    const std::vector<WaylandProtocolInfo>& protocols() const { return protocolList_; }

    std::string compositorName() const { return compositorName_; }

    bool roundtrip();
    void dispatch(int timeoutMs = -1);
    int pollFd() const;

    void setCompositorOverride(const std::string& name) { compositorName_ = name; }

private:
    WaylandProtocolClient() = default;
    ~WaylandProtocolClient();

    WaylandProtocolClient(const WaylandProtocolClient&) = delete;
    WaylandProtocolClient& operator=(const WaylandProtocolClient&) = delete;

    void handleGlobal(uint32_t name, const char *interface, uint32_t version);
    void handleGlobalRemove(uint32_t name);

    static void registryHandleGlobal(void *data, struct wl_registry *registry,
                                      uint32_t name, const char *interface, uint32_t version);
    static void registryHandleGlobalRemove(void *data, struct wl_registry *registry,
                                            uint32_t name);

    static void seatHandleCapabilities(void *data, struct wl_seat *seat, uint32_t caps);
    static void seatHandleName(void *data, struct wl_seat *seat, const char *name);

    static void outputHandleGeometry(void *data, struct wl_output *output,
                                      int32_t x, int32_t y, int32_t physW, int32_t physH,
                                      int32_t subpixel, const char *make, const char *model,
                                      int32_t transform);
    static void outputHandleMode(void *data, struct wl_output *output,
                                  uint32_t flags, int32_t width, int32_t height,
                                  int32_t refresh);
    static void outputHandleScale(void *data, struct wl_output *output, int32_t scale);
    static void outputHandleDone(void *data, struct wl_output *output);
    static void outputHandleName(void *data, struct wl_output *output, const char *name);
    static void outputHandleDescription(void *data, struct wl_output *output, const char *desc);

    wl_display *display_ = nullptr;
    wl_registry *registry_ = nullptr;
    wl_seat *seat_ = nullptr;

    zwp_virtual_keyboard_manager_v1 *virtualKeyboardManager_ = nullptr;
    zwlr_virtual_pointer_manager_v1 *virtualPointerManager_ = nullptr;
    zwlr_foreign_toplevel_manager_v1 *foreignToplevelManager_ = nullptr;
    zwlr_data_control_manager_v1 *dataControlManager_ = nullptr;
    ext_data_control_manager_v1 *extDataControlManager_ = nullptr;

    std::vector<WaylandOutputInfo> outputs_;
    std::vector<WaylandProtocolInfo> protocolList_;
    std::string compositorName_;

    uint32_t seatGlobalId_ = 0;
    std::mutex mutex_;
    std::atomic<bool> connected_{false};
    bool initialized_ = false;

    static const struct wl_registry_listener registryListener_;
    static const struct wl_seat_listener seatListener_;
    static const struct wl_output_listener outputListener_;
};

} // namespace havel
