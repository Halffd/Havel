#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <linux/input-event-codes.h>
#include <X11/keysym.h>
#include <X11/X.h>

#ifdef WINDOWS
#include <windows.h>
#endif

namespace havel {

// Universal key code that works across all platforms
using UniversalKey = int;

// Key mapping class - converts between different key code systems
class KeyMap {
public:
    // Initialize the key mappings
    static void Initialize();
    
    // Convert from string name to evdev code
    static int FromString(const std::string& name);
    
    // Convert from string name to X11 KeySym
    static unsigned long ToX11(const std::string& name);
    
    // Convert from string name to Windows VK code
    static int ToWindows(const std::string& name);
    
    // Convert evdev code to string name
    static std::string EvdevToString(int code);
    
    // Convert X11 KeySym to string name
    static std::string X11ToString(unsigned long keysym);
    
    // Convert Windows VK to string name
    static std::string WindowsToString(int vk);
    
    // Convert evdev to X11
    static unsigned long EvdevToX11(int evdev);
    
    // Convert X11 to evdev
    static int X11ToEvdev(unsigned long keysym);
    
    // Convert evdev to Windows
    static int EvdevToWindows(int evdev);
    
    // Convert Windows to evdev
    static int WindowsToEvdev(int vk);
    
    // Check if a key is a modifier
    static bool IsModifier(int evdev);
    
    // Check if a key is a mouse button
    static bool IsMouseButton(int evdev);
    
    // Check if a key is a joystick button
    static bool IsJoystickButton(int evdev);
    
    // Get all alternative names for a key
    static std::vector<std::string> GetAliases(const std::string& name);
    
private:
    struct KeyEntry {
        std::string primaryName;
        std::vector<std::string> aliases;
        int evdevCode;
        unsigned long x11KeySym;
        int windowsVK;
    };
    
    static void AddKey(const std::string& name, int evdev, unsigned long x11, int windows);
    static void AddAlias(const std::string& alias, const std::string& primaryName);
    
    static bool initialized;
    static std::unordered_map<std::string, KeyEntry> nameToKey;
    static std::unordered_map<int, std::string> evdevToName;
    static std::unordered_map<unsigned long, std::string> x11ToName;
    static std::unordered_map<int, std::string> windowsToName;
};

} // namespace havel
