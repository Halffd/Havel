#include "VirtualKeyboard.hpp"
#include "WaylandProtocolClient.hpp"
#include "core/io/UinputDevice.hpp"
#include "utils/Logger.hpp"

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-compose.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <chrono>
#include <thread>

extern "C" {
#include "virtual-keyboard-unstable-v1-client-protocol.h"
}

namespace havel {

VirtualKeyboard::VirtualKeyboard(WaylandProtocolClient &client)
    : client_(client) {}

VirtualKeyboard::~VirtualKeyboard() {
    cleanup();
}

bool VirtualKeyboard::initialize() {
    if (vkb_) return true;

    if (!client_.hasVirtualKeyboard()) {
        debug("VirtualKeyboard: compositor does not support virtual-keyboard protocol");
        return false;
    }

    if (!client_.seat()) {
        error("VirtualKeyboard: no seat available");
        return false;
    }

    vkb_ = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(
        client_.virtualKeyboardManager(), client_.seat());

    if (!vkb_) {
        error("VirtualKeyboard: failed to create virtual keyboard");
        return false;
    }

    if (!setupKeymap()) {
        error("VirtualKeyboard: failed to setup keymap");
        zwp_virtual_keyboard_v1_destroy(vkb_);
        vkb_ = nullptr;
        return false;
    }

    client_.roundtrip();

    info("VirtualKeyboard: initialized successfully");
    return true;
}

void VirtualKeyboard::cleanup() {
    if (vkb_) {
        releaseAllModifiers();
        for (auto kc : pressedKeys_) {
            sendKey(kc, WL_KEYBOARD_KEY_STATE_RELEASED);
        }
        pressedKeys_.clear();
        zwp_virtual_keyboard_v1_destroy(vkb_);
        vkb_ = nullptr;
    }
    if (composeState_) {
        xkb_compose_state_unref(composeState_);
        composeState_ = nullptr;
    }
    if (composeTable_) {
        xkb_compose_table_unref(composeTable_);
        composeTable_ = nullptr;
    }
    if (xkbState_) {
        xkb_state_unref(xkbState_);
        xkbState_ = nullptr;
    }
    if (xkbKeymap_) {
        xkb_keymap_unref(xkbKeymap_);
        xkbKeymap_ = nullptr;
    }
    if (xkbContext_) {
        xkb_context_unref(xkbContext_);
        xkbContext_ = nullptr;
    }
}

bool VirtualKeyboard::setupKeymap() {
    xkbContext_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!xkbContext_) {
        error("VirtualKeyboard: failed to create XKB context");
        return false;
    }

    if (!compileDefaultKeymap()) {
        return false;
    }

    composeTable_ = xkb_compose_table_new_from_locale(
        xkbContext_, setlocale(LC_CTYPE, nullptr), XKB_COMPOSE_COMPILE_NO_FLAGS);
    if (composeTable_) {
        composeState_ = xkb_compose_state_new(composeTable_, XKB_COMPOSE_STATE_NO_FLAGS);
    }

    return true;
}

bool VirtualKeyboard::compileDefaultKeymap() {
    struct xkb_rule_names rules = {};
    rules.rules = nullptr;
    rules.model = "pc105";
    rules.layout = nullptr;
    rules.variant = nullptr;
    rules.options = nullptr;

    const char *layoutEnv = getenv("XKB_DEFAULT_LAYOUT");
    const char *variantEnv = getenv("XKB_DEFAULT_VARIANT");
    const char *optionsEnv = getenv("XKB_DEFAULT_OPTIONS");
    const char *modelEnv = getenv("XKB_DEFAULT_MODEL");
    const char *rulesEnv = getenv("XKB_DEFAULT_RULES");

    if (layoutEnv) rules.layout = layoutEnv;
    if (variantEnv) rules.variant = variantEnv;
    if (optionsEnv) rules.options = optionsEnv;
    if (modelEnv) rules.model = modelEnv;
    if (rulesEnv) rules.rules = rulesEnv;

    xkbKeymap_ = xkb_keymap_new_from_names(xkbContext_, &rules, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!xkbKeymap_) {
        error("VirtualKeyboard: failed to compile keymap");
        return false;
    }

    xkbState_ = xkb_state_new(xkbKeymap_);
    if (!xkbState_) {
        error("VirtualKeyboard: failed to create XKB state");
        return false;
    }

    char *keymapStr = xkb_keymap_get_as_string(xkbKeymap_, XKB_KEYMAP_FORMAT_TEXT_V1);
    if (!keymapStr) {
        error("VirtualKeyboard: failed to get keymap as string");
        return false;
    }

    size_t keymapLen = strlen(keymapStr) + 1;

    int fd = memfd_create("havel-keymap", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd < 0) {
        error("VirtualKeyboard: memfd_create failed: {}", strerror(errno));
        free(keymapStr);
        return false;
    }

    if (ftruncate(fd, keymapLen) < 0) {
        error("VirtualKeyboard: ftruncate failed: {}", strerror(errno));
        close(fd);
        free(keymapStr);
        return false;
    }

    void *map = mmap(nullptr, keymapLen, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        error("VirtualKeyboard: mmap failed: {}", strerror(errno));
        close(fd);
        free(keymapStr);
        return false;
    }

    memcpy(map, keymapStr, keymapLen);
    munmap(map, keymapLen);
    free(keymapStr);

    zwp_virtual_keyboard_v1_keymap(vkb_, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd, keymapLen);

    close(fd);
    return true;
}

void VirtualKeyboard::pressKey(uint32_t keycode) {
    sendKey(keycode, WL_KEYBOARD_KEY_STATE_PRESSED);
    pressedKeys_.insert(keycode);
}

void VirtualKeyboard::releaseKey(uint32_t keycode) {
    sendKey(keycode, WL_KEYBOARD_KEY_STATE_RELEASED);
    pressedKeys_.erase(keycode);
}

void VirtualKeyboard::tapKey(uint32_t keycode, uint32_t delayMs) {
    pressKey(keycode);
    if (delayMs > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
    releaseKey(keycode);
}

void VirtualKeyboard::typeText(const std::string &text) {
    if (!vkb_ && !uinput_) return;

    if (!vkb_) {
        if (uinput_) {
            uinput_->BeginBatch();
            for (char c : text) {
                uint32_t keysym = xkb_keysym_from_name(&c, XKB_KEYSYM_CASE_INSENSITIVE);
                if (keysym == XKB_KEY_NoSymbol) continue;
                uint32_t kc = keysymToKeycode(keysym);
                if (kc == 0) continue;
                uinput_->SendEvent(EV_KEY, kc, 1);
                uinput_->SendEvent(EV_SYN, SYN_REPORT, 0);
                uinput_->SendEvent(EV_KEY, kc, 0);
                uinput_->SendEvent(EV_SYN, SYN_REPORT, 0);
            }
            uinput_->EndBatch();
        }
        return;
    }

    if (!xkbState_ || !xkbKeymap_) return;

    for (size_t i = 0; i < text.size(); ) {
        uint32_t keysym = 0;
        uint32_t keycode = 0;
        uint32_t modsNeeded = 0;

        unsigned char first = static_cast<unsigned char>(text[i]);
        uint32_t cp = 0;
        int bytes = 0;

        if (first < 0x80) { cp = first; bytes = 1; }
        else if ((first & 0xE0) == 0xC0) { cp = first & 0x1F; bytes = 2; }
        else if ((first & 0xF0) == 0xE0) { cp = first & 0x0F; bytes = 3; }
        else if ((first & 0xF8) == 0xF0) { cp = first & 0x07; bytes = 4; }
        else { i++; continue; }

        for (int j = 1; j < bytes && i + j < text.size(); j++) {
            cp = (cp << 6) | (static_cast<unsigned char>(text[i + j]) & 0x3F);
        }
        i += bytes;

        keysym = xkb_keysym_from_name(
            reinterpret_cast<const char *>(reinterpret_cast<unsigned char *>(&cp)),
            XKB_KEYSYM_CASE_INSENSITIVE);

        if (keysym == XKB_KEY_NoSymbol) {
            if (cp < 0x100) keysym = cp;
            else keysym = xkb_utf32_to_keysym(cp);
        }

        if (keysym == XKB_KEY_NoSymbol) continue;

        keycode = keysymToKeycode(keysym);
        if (keycode == 0) continue;

        xkb_layout_index_t numLayouts = xkb_keymap_num_layouts(xkbKeymap_);
        for (xkb_layout_index_t layout = 0; layout < numLayouts; ++layout) {
            if (xkb_keymap_key_repeats(xkbKeymap_, keycode)) continue;
            xkb_level_index_t numLevels = xkb_keymap_num_levels_for_key(xkbKeymap_, keycode, layout);
            for (xkb_level_index_t level = 0; level < numLevels; ++level) {
                const xkb_keysym_t *syms;
                int numSyms = xkb_keymap_key_get_syms_by_level(xkbKeymap_, keycode, layout, level, &syms);
                for (int s = 0; s < numSyms; s++) {
                    if (syms[s] == keysym) {
                        if (level == 1) modsNeeded |= 1 << 0;
                        if (level == 2) modsNeeded |= 1 << 1;
                        goto found;
                    }
                }
            }
        }
        found:

        uint32_t oldMods = modsDepressed_;
        if (modsNeeded) {
            modsDepressed_ |= modsNeeded;
            updateModifiers();
        }

        tapKey(keycode, 30);

        if (modsNeeded) {
            modsDepressed_ = oldMods;
            updateModifiers();
        }
    }
}

void VirtualKeyboard::pressKeyByName(const std::string &keyName) {
    uint32_t kc = keyNameToKeycode(keyName);
    if (kc > 0) {
        pressKey(kc);
    } else if (uinput_) {
        uint32_t keysym = keyNameToKeysym(keyName);
        kc = keysymToKeycode(keysym);
        if (kc > 0) pressKey(kc);
    }
}

void VirtualKeyboard::releaseKeyByName(const std::string &keyName) {
    uint32_t kc = keyNameToKeycode(keyName);
    if (kc > 0) {
        releaseKey(kc);
    } else if (uinput_) {
        uint32_t keysym = keyNameToKeysym(keyName);
        kc = keysymToKeycode(keysym);
        if (kc > 0) releaseKey(kc);
    }
}

void VirtualKeyboard::tapKeyByName(const std::string &keyName, uint32_t delayMs) {
    pressKeyByName(keyName);
    if (delayMs > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
    releaseKeyByName(keyName);
}

void VirtualKeyboard::setModifiers(uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group) {
    modsDepressed_ = depressed;
    modsLatched_ = latched;
    modsLocked_ = locked;
    layoutGroup_ = group;
    updateModifiers();
}

void VirtualKeyboard::releaseAllModifiers() {
    modsDepressed_ = 0;
    modsLatched_ = 0;
    modsLocked_ = 0;
    layoutGroup_ = 0;
    updateModifiers();
}

uint32_t VirtualKeyboard::keysymToKeycode(uint32_t keysym) {
    if (!xkbKeymap_) return 0;

    xkb_keycode_t min = xkb_keymap_min_keycode(xkbKeymap_);
    xkb_keycode_t max = xkb_keymap_max_keycode(xkbKeymap_);

    for (xkb_keycode_t kc = min; kc <= max; ++kc) {
    xkb_layout_index_t numLayouts = xkb_keymap_num_layouts(xkbKeymap_);

    for (xkb_layout_index_t layout = 0; layout < numLayouts; ++layout) {
        xkb_level_index_t numLevels = xkb_keymap_num_levels_for_key(xkbKeymap_, kc, layout);
            for (xkb_level_index_t level = 0; level < numLevels; ++level) {
                const xkb_keysym_t *syms;
                int numSyms = xkb_keymap_key_get_syms_by_level(xkbKeymap_, kc, layout, level, &syms);
                for (int s = 0; s < numSyms; s++) {
                    if (syms[s] == keysym) return kc;
                }
            }
        }
    }
    return 0;
}

uint32_t VirtualKeyboard::keyNameToKeycode(const std::string &name) {
    uint32_t keysym = keyNameToKeysym(name);
    if (keysym == XKB_KEY_NoSymbol) return 0;
    return keysymToKeycode(keysym);
}

uint32_t VirtualKeyboard::keyNameToKeysym(const std::string &name) {
    std::string xkbName = name;

    if (xkbName == "ctrl") xkbName = "Control_L";
    else if (xkbName == "alt") xkbName = "Alt_L";
    else if (xkbName == "shift") xkbName = "Shift_L";
    else if (xkbName == "super") xkbName = "Super_L";
    else if (xkbName == "enter" || xkbName == "return") xkbName = "Return";
    else if (xkbName == "esc" || xkbName == "escape") xkbName = "Escape";
    else if (xkbName == "backspace") xkbName = "BackSpace";
    else if (xkbName == "tab") xkbName = "Tab";
    else if (xkbName == "space") xkbName = "space";
    else if (xkbName == "capslock" || xkbName == "caps_lock") xkbName = "Caps_Lock";
    else if (xkbName == "numlock" || xkbName == "num_lock") xkbName = "Num_Lock";
    else if (xkbName == "scrolllock" || xkbName == "scroll_lock") xkbName = "Scroll_Lock";
    else if (xkbName == "delete" || xkbName == "del") xkbName = "Delete";
    else if (xkbName == "insert" || xkbName == "ins") xkbName = "Insert";
    else if (xkbName == "home") xkbName = "Home";
    else if (xkbName == "end") xkbName = "End";
    else if (xkbName == "pageup" || xkbName == "page_up") xkbName = "Prior";
    else if (xkbName == "pagedown" || xkbName == "page_down") xkbName = "Next";
    else if (xkbName == "left") xkbName = "Left";
    else if (xkbName == "right") xkbName = "Right";
    else if (xkbName == "up") xkbName = "Up";
    else if (xkbName == "down") xkbName = "Down";
    else if (xkbName == "print" || xkbName == "printscreen") xkbName = "Print";
    else if (xkbName == "pause" || xkbName == "break") xkbName = "Pause";
    else if (xkbName == "menu" || xkbName == "apps") xkbName = "Menu";

    xkb_keysym_t keysym = xkb_keysym_from_name(xkbName.c_str(), XKB_KEYSYM_CASE_INSENSITIVE);
    return keysym;
}

bool VirtualKeyboard::isKeyDown(uint32_t keycode) const {
    return pressedKeys_.count(keycode) > 0;
}

void VirtualKeyboard::sendKey(uint32_t keycode, uint32_t state) {
    if (vkb_) {
        auto now = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
        uint32_t time = static_cast<uint32_t>(ms & 0xFFFFFFFF);

        zwp_virtual_keyboard_v1_key(vkb_, time, keycode - 8, state);
        client_.roundtrip();
    }
}

void VirtualKeyboard::updateModifiers() {
    if (!vkb_) return;
    zwp_virtual_keyboard_v1_modifiers(vkb_, modsDepressed_, modsLatched_,
                                       modsLocked_, layoutGroup_);
    client_.roundtrip();
}

} // namespace havel
