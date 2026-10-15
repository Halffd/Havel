#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include "havel_platform.h"

#if HAVEL_PLATFORM_LINUX
#  include <linux/input-event-codes.h>
#endif

#if HAVEL_PLATFORM_WINDOWS
#include <windows.h>
#endif

namespace havel {

// Universal key code that works across all platforms
using UniversalKey = int;

// One resolved key entry as produced by modules/app/kymap.hv. Alias rows
// carry the same codes as their primary and repeat its alias list, matching
// the snapshot semantics the module applies.
//
// isAlias records which rows came from AddAlias rather than AddKey. An alias
// shares its primary's codes, so only real keys may populate the reverse
// (code -> name) maps; otherwise evdev 12 would resolve to "-" not "minus".
struct KeyRow {
    std::string name;
    int evdevCode = 0;
    unsigned long x11KeySym = 0;
    int windowsVK = 0;
    std::vector<std::string> aliases;
    bool isAlias = false;
};

// Key mapping class - converts between different key code systems.
// The tables are declared in modules/app/kymap.hv and installed at runtime.
class KeyMap {
public:
    // Initialize the key mappings
    static void Initialize();

    // Populate the maps from the kymap Havel module (modules/app/kymap.hv).
    // Called once by the kymap host module while the VM is alive; the rows are
    // snapshotted into plain C++ maps so later lookups are lock-free reads and
    // safe to call from the IO event thread.
    static void HydrateFromModule(const std::vector<KeyRow> &rows);
    
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

    // Has the table been installed from modules/app/kymap.hv yet? The lexer
    // runs during early parse passes that happen before the VM exists, so
    // callers must handle a not-yet-hydrated table.
    static bool IsHydrated();
    
private:
    struct KeyEntry {
        std::string primaryName;
        std::vector<std::string> aliases;
        int evdevCode;
        unsigned long x11KeySym;
        int windowsVK;
    };
    
    static bool initialized;
    static std::unordered_map<std::string, KeyEntry> nameToKey;
    static std::unordered_map<int, std::string> evdevToName;
    static std::unordered_map<unsigned long, std::string> x11ToName;
    static std::unordered_map<int, std::string> windowsToName;
};

} // namespace havel
