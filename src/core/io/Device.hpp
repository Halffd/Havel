#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <sstream>

// Event types from linux/input.h
#define EV_SYN 0x00
#define EV_KEY 0x01
#define EV_REL 0x02
#define EV_ABS 0x03
#define EV_MSC 0x04
#define EV_SW 0x05
#define EV_LED 0x11
#define EV_SND 0x12
#define EV_FF 0x15

// Key codes from linux/input-event-codes.h
#define KEY_A 30
#define KEY_Z 44
#define KEY_Q 16
#define KEY_1 2
#define KEY_9 10
#define KEY_0 11
#define KEY_SPACE 57
#define KEY_ENTER 28
#define KEY_LEFTSHIFT 42
#define KEY_RIGHTSHIFT 54
#define KEY_LEFTCTRL 29
#define KEY_RIGHTCTRL 97
#define KEY_LEFTALT 56
#define KEY_RIGHTALT 100

// Mouse buttons
#define BTN_LEFT 0x110
#define BTN_RIGHT 0x111
#define BTN_MIDDLE 0x112
#define BTN_SIDE 0x113
#define BTN_EXTRA 0x114

// Gamepad buttons
#define BTN_GAMEPAD 0x130
#define BTN_A 0x130
#define BTN_B 0x131
#define BTN_X 0x133
#define BTN_Y 0x134
#define BTN_TL 0x136
#define BTN_TR 0x137
#define BTN_SELECT 0x13a
#define BTN_START 0x13b

// Joystick buttons
#define BTN_JOYSTICK 0x120
#define BTN_TRIGGER 0x120
#define BTN_THUMB 0x121

// Relative axes
#define REL_X 0x00
#define REL_Y 0x01
#define REL_WHEEL 0x08

// Absolute axes
#define ABS_X 0x00
#define ABS_Y 0x01
#define ABS_RX 0x03
#define ABS_RY 0x04
#define ABS_HAT0X 0x10
#define ABS_HAT0Y 0x11

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

struct DeviceCapabilities {
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
    DeviceCapabilities capabilities;
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
    DeviceCapabilities caps;
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
    DeviceCapabilities analyzeCapabilities();
    bool isRealKeyboard() const;
    bool isRealMouse() const;
    bool isGamepad() const;
    bool isJoystick() const;
    
    // String representation
    std::string toString() const;
    DeviceInfo toDeviceInfo() const;
    
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