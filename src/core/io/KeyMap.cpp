#include "KeyMap.hpp"
#include "utils/Logger.hpp"
#include "utils/DebugFlags.hpp"
#include "havel_platform.h"
#include <algorithm>

// The key data itself lives in modules/app/kymap.hv, which is the single
// source of truth for evdev / X11 / Windows codes and their aliases. This file
// only owns the process-local lookup maps, which are populated once from that
// module via HydrateFromModule() and then read without locking.
//
// Snapshotting (rather than querying the VM per lookup) is deliberate:
// KeyMap::EvdevToString runs on the IO event thread, concurrently with user
// bytecode on the VM thread, and the VM call stack is not reentrant.

namespace havel {

// Static member definitions
bool KeyMap::initialized = false;
std::unordered_map<std::string, KeyMap::KeyEntry> KeyMap::nameToKey;
std::unordered_map<int, std::string> KeyMap::evdevToName;
std::unordered_map<unsigned long, std::string> KeyMap::x11ToName;
std::unordered_map<int, std::string> KeyMap::windowsToName;

int KeyMap::FromString(const std::string& name) {
    auto it = nameToKey.find(name);
    if (it != nameToKey.end()) {
        return it->second.evdevCode;
    }
    return 0;
}

unsigned long KeyMap::ToX11(const std::string& name) {
    auto it = nameToKey.find(name);
    if (it != nameToKey.end()) {
        return it->second.x11KeySym;
    }
    return 0;
}

int KeyMap::ToWindows(const std::string& name) {
    auto it = nameToKey.find(name);
    if (it != nameToKey.end()) {
        return it->second.windowsVK;
    }
    return 0;
}

std::string KeyMap::EvdevToString(int code) {
    auto it = evdevToName.find(code);
    if (it != evdevToName.end()) {
        return it->second;
    }
    return "";
}

std::string KeyMap::X11ToString(unsigned long keysym) {
    auto it = x11ToName.find(keysym);
    if (it != x11ToName.end()) {
        return it->second;
    }
    return "";
}

std::string KeyMap::WindowsToString(int vk) {
    auto it = windowsToName.find(vk);
    if (it != windowsToName.end()) {
        return it->second;
    }
    return "";
}

unsigned long KeyMap::EvdevToX11(int evdev) {
    std::string name = EvdevToString(evdev);
    if (!name.empty()) {
        return ToX11(name);
    }
    return 0;
}

int KeyMap::X11ToEvdev(unsigned long keysym) {
    std::string name = X11ToString(keysym);
    if (!name.empty()) {
        return FromString(name);
    }
    return 0;
}

int KeyMap::EvdevToWindows(int evdev) {
    std::string name = EvdevToString(evdev);
    if (!name.empty()) {
        return ToWindows(name);
    }
    return 0;
}

int KeyMap::WindowsToEvdev(int vk) {
    std::string name = WindowsToString(vk);
    if (!name.empty()) {
        return FromString(name);
    }
    return 0;
}

bool KeyMap::IsModifier(int evdev) {
    return evdev == KEY_LEFTCTRL || evdev == KEY_RIGHTCTRL ||
           evdev == KEY_LEFTSHIFT || evdev == KEY_RIGHTSHIFT ||
           evdev == KEY_LEFTALT || evdev == KEY_RIGHTALT ||
           evdev == KEY_LEFTMETA || evdev == KEY_RIGHTMETA;
}

bool KeyMap::IsMouseButton(int evdev) {
    return evdev >= BTN_LEFT && evdev <= BTN_TASK;
}

bool KeyMap::IsJoystickButton(int evdev) {
    return evdev >= BTN_JOYSTICK && evdev <= BTN_THUMBR;
}

std::vector<std::string> KeyMap::GetAliases(const std::string& name) {
    auto it = nameToKey.find(name);
    if (it != nameToKey.end()) {
        return it->second.aliases;
    }
    return {};
}

// Install the rows exported by modules/app/kymap.hv.
//
// The module has already resolved aliases to their primary codes, so a row
// carries its codes directly. Rows flagged isAlias populate only nameToKey;
// keys additionally populate the reverse maps. That reproduces the original
// AddKey/AddAlias split exactly.
void KeyMap::HydrateFromModule(const std::vector<KeyRow> &rows) {
    if (initialized) {
        if (debugging::debug_io)
            ::havel::debug("KeyMap::HydrateFromModule(): Already initialized, skipping.");
        return;
    }

    if (debugging::debug_io)
        ::havel::debug("KeyMap::HydrateFromModule(): hydrating {} rows from kymap.hv", rows.size());

    for (const auto &row : rows) {
        KeyEntry entry = {row.name, row.aliases, row.evdevCode, row.x11KeySym, row.windowsVK};
        nameToKey[row.name] = std::move(entry);

        // Only real keys populate the reverse maps. An alias shares its
        // primary's codes, so letting it in here would displace the primary:
        // evdev 12 would come back as "-" rather than "minus", 125 as "super"
        // rather than "win". This is what AddAlias did not do.
        if (row.isAlias) continue;

        if (row.evdevCode != 0) evdevToName[row.evdevCode] = row.name;
        if (row.x11KeySym != 0) x11ToName[row.x11KeySym] = row.name;
        if (row.windowsVK != 0) windowsToName[row.windowsVK] = row.name;
    }

    if (debugging::debug_io)
        ::havel::debug("KeyMap::HydrateFromModule(): completed, total names: {}", nameToKey.size());

    initialized = true;
}

void KeyMap::Initialize() {
    // Retained for the existing call sites (IO::ensureBackend runs this while
    // constructing the IO backend). It no longer builds the tables itself: the
    // data lives in modules/app/kymap.hv and is installed by
    // HydrateFromModule(), which Havel::initialize() calls once the VM exists.
    //
    // The IO backend starts before the VM does, so at this point the maps are
    // legitimately still empty; key name resolution happens later, on input
    // events, by which time hydration has run. Doing the work here instead
    // would need the VM, which does not exist yet.
    if (debugging::debug_io)
        ::havel::debug("KeyMap::Initialize(): tables supplied later by kymap.hv");
}

} // namespace havel
