#pragma once

#include <string>
#include <cstdint>
#include <unordered_set>
#include <memory>
#include <functional>

struct zwp_virtual_keyboard_v1;
struct xkb_context;
struct xkb_keymap;
struct xkb_state;
struct xkb_compose_table;
struct xkb_compose_state;

namespace havel {

class WaylandProtocolClient;
class UinputDevice;

class __attribute__((visibility("default"))) VirtualKeyboard {
public:
    explicit VirtualKeyboard(WaylandProtocolClient &client);
    ~VirtualKeyboard();

    bool initialize();
    void cleanup();
    bool isAvailable() const { return vkb_ != nullptr; }

    void pressKey(uint32_t keycode);
    void releaseKey(uint32_t keycode);
    void tapKey(uint32_t keycode, uint32_t delayMs = 50);
    void typeText(const std::string &text);

    void pressKeyByName(const std::string &keyName);
    void releaseKeyByName(const std::string &keyName);
    void tapKeyByName(const std::string &keyName, uint32_t delayMs = 50);

    void setModifiers(uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group);
    void releaseAllModifiers();

    uint32_t keysymToKeycode(uint32_t keysym);
    uint32_t keyNameToKeycode(const std::string &name);
    uint32_t keyNameToKeysym(const std::string &name);

    void setUinputFallback(UinputDevice *uinput) { uinput_ = uinput; }

    bool isKeyDown(uint32_t keycode) const;

private:
    bool setupKeymap();
    bool compileDefaultKeymap();
    void sendKey(uint32_t keycode, uint32_t state);
    void updateModifiers();

    WaylandProtocolClient &client_;
    zwp_virtual_keyboard_v1 *vkb_ = nullptr;
    UinputDevice *uinput_ = nullptr;

    xkb_context *xkbContext_ = nullptr;
    xkb_keymap *xkbKeymap_ = nullptr;
    xkb_state *xkbState_ = nullptr;
    xkb_compose_table *composeTable_ = nullptr;
    xkb_compose_state *composeState_ = nullptr;

    [[maybe_unused]] uint32_t serial_ = 0;
    std::unordered_set<uint32_t> pressedKeys_;
    uint32_t modsDepressed_ = 0;
    uint32_t modsLatched_ = 0;
    uint32_t modsLocked_ = 0;
    uint32_t layoutGroup_ = 0;
};

} // namespace havel
