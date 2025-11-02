#include "KeyMap.hpp"
#include <algorithm>
#include <cctype>

namespace havel {

bool KeyMap::initialized = false;
std::unordered_map<std::string, KeyMap::KeyEntry> KeyMap::nameToKey;
std::unordered_map<int, std::string> KeyMap::evdevToName;
std::unordered_map<unsigned long, std::string> KeyMap::x11ToName;
std::unordered_map<int, std::string> KeyMap::windowsToName;

void KeyMap::AddKey(const std::string& name, int evdev, unsigned long x11, int windows) {
    std::string lowerName = name;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
    
    KeyEntry entry;
    entry.primaryName = lowerName;
    entry.evdevCode = evdev;
    entry.x11KeySym = x11;
    entry.windowsVK = windows;
    
    nameToKey[lowerName] = entry;
    
    if (evdev != 0) {
        evdevToName[evdev] = lowerName;
    }
    if (x11 != 0) {
        x11ToName[x11] = lowerName;
    }
    if (windows != 0) {
        windowsToName[windows] = lowerName;
    }
}

void KeyMap::AddAlias(const std::string& alias, const std::string& primaryName) {
    std::string lowerAlias = alias;
    std::string lowerPrimary = primaryName;
    std::transform(lowerAlias.begin(), lowerAlias.end(), lowerAlias.begin(), ::tolower);
    std::transform(lowerPrimary.begin(), lowerPrimary.end(), lowerPrimary.begin(), ::tolower);
    
    auto it = nameToKey.find(lowerPrimary);
    if (it != nameToKey.end()) {
        it->second.aliases.push_back(lowerAlias);
        nameToKey[lowerAlias] = it->second;
    }
}

void KeyMap::Initialize() {
    if (initialized) return;
    
    // TODO: Add all keys from evdev map - this will be done in a separate file
    // For now, just mark as initialized
    
    initialized = true;
}

int KeyMap::FromString(const std::string& name) {
    Initialize();
    std::string lowerName = name;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
    
    auto it = nameToKey.find(lowerName);
    if (it != nameToKey.end()) {
        return it->second.evdevCode;
    }
    return 0;
}

unsigned long KeyMap::ToX11(const std::string& name) {
    Initialize();
    std::string lowerName = name;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
    
    auto it = nameToKey.find(lowerName);
    if (it != nameToKey.end()) {
        return it->second.x11KeySym;
    }
    return 0;
}

int KeyMap::ToWindows(const std::string& name) {
    Initialize();
    std::string lowerName = name;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
    
    auto it = nameToKey.find(lowerName);
    if (it != nameToKey.end()) {
        return it->second.windowsVK;
    }
    return 0;
}

std::string KeyMap::EvdevToString(int code) {
    Initialize();
    auto it = evdevToName.find(code);
    if (it != evdevToName.end()) {
        return it->second;
    }
    return "unknown";
}

std::string KeyMap::X11ToString(unsigned long keysym) {
    Initialize();
    auto it = x11ToName.find(keysym);
    if (it != x11ToName.end()) {
        return it->second;
    }
    return "unknown";
}

std::string KeyMap::WindowsToString(int vk) {
    Initialize();
    auto it = windowsToName.find(vk);
    if (it != windowsToName.end()) {
        return it->second;
    }
    return "unknown";
}

unsigned long KeyMap::EvdevToX11(int evdev) {
    std::string name = EvdevToString(evdev);
    return ToX11(name);
}

int KeyMap::X11ToEvdev(unsigned long keysym) {
    std::string name = X11ToString(keysym);
    return FromString(name);
}

int KeyMap::EvdevToWindows(int evdev) {
    std::string name = EvdevToString(evdev);
    return ToWindows(name);
}

int KeyMap::WindowsToEvdev(int vk) {
    std::string name = WindowsToString(vk);
    return FromString(name);
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
    return (evdev >= BTN_JOYSTICK && evdev <= BTN_THUMBR) ||
           (evdev >= BTN_DPAD_UP && evdev <= BTN_DPAD_RIGHT);
}

std::vector<std::string> KeyMap::GetAliases(const std::string& name) {
    Initialize();
    std::string lowerName = name;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
    
    auto it = nameToKey.find(lowerName);
    if (it != nameToKey.end()) {
        return it->second.aliases;
    }
    return {};
}

} // namespace havel
