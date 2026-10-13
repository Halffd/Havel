#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <sstream>
#include "havel_platform.h"
#if !HAVEL_PLATFORM_WINDOWS
#include <linux/input.h>
#endif
// EV_/KEY_/BTN_/REL_/ABS_ codes now live in input_event_codes.hpp, which is
// one copy shared with KeyMap.hpp instead of a second hand-written list.
#include "input_event_codes.hpp"

enum class DeviceType {
    Unknown,
    Keyboard,
    Mouse,
    Gamepad,
    Joystick,
    Audio,
    Button,
    Other
};

// Renamed from DeviceCapabilities: the Windows SDK ships a TCHAR-generic
// alias `#define DeviceCapabilities DeviceCapabilitiesA` (the real ANSI entry
// point of the Win32 DeviceCapabilitiesA API), so on a Windows build every
// use of the old name was silently rewritten to DeviceCapabilitiesA - the
// function, not this struct - and the members stopped parsing.
struct InputDeviceCapabilities {
    int totalKeys = 0;
    int letterKeys = 0;
    int numberKeys = 0;
    int modifierKeys = 0;
    int mouseButtons = 0;
    int gamepadButtons = 0;
    int joystickButtons = 0;
    bool hasMovement = false;
    bool hasAbsoluteAxes = false;
    bool hasRelativeAxes = false;
    bool hasAnalogSticks = false;
    bool hasDPad = false;
};

struct DeviceInfo {
    std::string name;
    std::string eventPath;
    DeviceType type;
    InputDeviceCapabilities capabilities;
    double confidence = 0.0; // 0.0 = not confident, 1.0 = very confident
    std::string reason; // Why this classification was chosen
};

class Device {
public:
    int busType = 0;
    int vendor = 0;
    int product = 0;
    int version = 0;
    std::string name;
    std::string phys;
    std::string sysfs;
    std::string uniq;
    std::string handlers;
    std::string eventPath;
    std::vector<std::string> capabilities;
    DeviceType type = DeviceType::Unknown;
    InputDeviceCapabilities caps;
    double confidence = 0.0;
    std::string classificationReason;
    
    // Parse a device block from /proc/bus/input/devices
    static Device parseDeviceBlock(const std::vector<std::string>& lines);
    
    // Get all input devices from /proc/bus/input/devices
    static std::vector<Device> getAllDevices();
    
    // Find best devices by type
    static std::vector<DeviceInfo> findKeyboards();
    static std::vector<DeviceInfo> findMice();
    static std::vector<DeviceInfo> findGamepads();
    
    // Capability checking
    bool hasKey(int keycode) const;
    bool hasEventType(int eventType) const;
    bool hasRelativeAxis(int axis) const;
    bool hasAbsoluteAxis(int axis) const;
    
    // Device type detection
    DeviceType detectType();
    InputDeviceCapabilities analyzeCapabilities();
    bool isRealKeyboard() const;
    bool isRealMouse() const;
    bool isGamepad() const;
    bool isJoystick() const;
    
    // String representation
    std::string toString() const;
    DeviceInfo toDeviceInfo() const;

    // Device merging functionality
    static std::vector<Device> mergeDevicesByVendorProduct(const std::vector<Device>& devices);

private:
    std::vector<uint64_t> keyCapabilities;
    std::vector<uint64_t> eventCapabilities;
    std::vector<uint64_t> relCapabilities;
    std::vector<uint64_t> absCapabilities;
    
    void parseInfoLine(const std::string& line);
    void parseHandlers(const std::string& handlersStr);
    void parseCapabilities(const std::string& capLine);
    std::string extractEventPath() const;
    std::vector<uint64_t> parseHexBitmask(const std::string& hex) const;
    int countKeysInRange(int start, int end) const;
};
