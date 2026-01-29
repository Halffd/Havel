#include "Device.hpp"
#include <fstream>
#include <algorithm>
#include <iostream>
#include <regex>

Device Device::parseDeviceBlock(const std::vector<std::string>& lines) {
    Device device;
    
    for (const std::string& line : lines) {
        if (line.empty()) continue;
        
        char prefix = line[0];
        if (line.length() < 3 || line[1] != ':' || line[2] != ' ') continue;
        
        std::string content = line.substr(3);
        
        switch (prefix) {
            case 'I':
                device.parseInfoLine(content);
                break;
            case 'N':
                if (content.starts_with("Name=")) {
                    device.name = content.substr(5);
                    if (device.name.length() >= 2 && device.name[0] == '"' && device.name.back() == '"') {
                        device.name = device.name.substr(1, device.name.length() - 2);
                    }
                }
                break;
            case 'P':
                if (content.starts_with("Phys=")) {
                    device.phys = content.substr(5);
                }
                break;
            case 'S':
                if (content.starts_with("Sysfs=")) {
                    device.sysfs = content.substr(6);
                }
                break;
            case 'U':
                if (content.starts_with("Uniq=")) {
                    device.uniq = content.substr(5);
                }
                break;
            case 'H':
                if (content.starts_with("Handlers=")) {
                    device.handlers = content.substr(9);
                    device.parseHandlers(device.handlers);
                }
                break;
            case 'B':
                device.capabilities.push_back(content);
                device.parseCapabilities(content);
                break;
        }
    }
    
    device.caps = device.analyzeCapabilities();
    device.type = device.detectType();
    return device;
}

void Device::parseInfoLine(const std::string& content) {
    std::regex infoRegex(R"(Bus=([0-9a-f]+)\s+Vendor=([0-9a-f]+)\s+Product=([0-9a-f]+)\s+Version=([0-9a-f]+))");
    std::smatch matches;
    
    if (std::regex_search(content, matches, infoRegex)) {
        busType = std::stoi(matches[1].str(), nullptr, 16);
        vendor = std::stoi(matches[2].str(), nullptr, 16);
        product = std::stoi(matches[3].str(), nullptr, 16);
        version = std::stoi(matches[4].str(), nullptr, 16);
    }
}

void Device::parseHandlers(const std::string& handlersStr) {
    eventPath = extractEventPath();
}

std::string Device::extractEventPath() const {
    std::regex eventRegex(R"(event(\d+))");
    std::smatch matches;
    
    if (std::regex_search(handlers, matches, eventRegex)) {
        return "/dev/input/event" + matches[1].str();
    }
    return "";
}

void Device::parseCapabilities(const std::string& capLine) {
    if (capLine.starts_with("EV=")) {
        eventCapabilities = parseHexBitmask(capLine.substr(3));
    } else if (capLine.starts_with("KEY=")) {
        keyCapabilities = parseHexBitmask(capLine.substr(4));
    } else if (capLine.starts_with("REL=")) {
        relCapabilities = parseHexBitmask(capLine.substr(4));
    } else if (capLine.starts_with("ABS=")) {
        absCapabilities = parseHexBitmask(capLine.substr(4));
    }
}

std::vector<uint64_t> Device::parseHexBitmask(const std::string& hex) const {
    std::vector<uint64_t> result;
    std::istringstream iss(hex);
    std::string chunk;
    
    while (iss >> chunk) {
        if (!chunk.empty()) {
            result.push_back(std::stoull(chunk, nullptr, 16));
        }
    }
    return result;
}

bool Device::hasKey(int keycode) const {
    if (keyCapabilities.empty()) return false;
    
    size_t wordIndex = keycode / 64;
    int bitIndex = keycode % 64;
    
    if (wordIndex >= keyCapabilities.size()) return false;
    
    return (keyCapabilities[wordIndex] & (1ULL << bitIndex)) != 0;
}

bool Device::hasEventType(int eventType) const {
    if (eventCapabilities.empty()) return false;
    return (eventCapabilities[0] & (1ULL << eventType)) != 0;
}

bool Device::hasRelativeAxis(int axis) const {
    if (relCapabilities.empty()) return false;
    return (relCapabilities[0] & (1ULL << axis)) != 0;
}

bool Device::hasAbsoluteAxis(int axis) const {
    if (absCapabilities.empty()) return false;
    
    size_t wordIndex = axis / 64;
    int bitIndex = axis % 64;
    
    if (wordIndex >= absCapabilities.size()) return false;
    
    return (absCapabilities[wordIndex] & (1ULL << bitIndex)) != 0;
}

int Device::countKeysInRange(int start, int end) const {
    int count = 0;
    for (int i = start; i <= end; i++) {
        if (hasKey(i)) count++;
    }
    return count;
}

DeviceCapabilities Device::analyzeCapabilities() {
    DeviceCapabilities caps;
    
    if (!hasEventType(EV_KEY)) return caps;
    
    // Count different types of keys
    caps.letterKeys = countKeysInRange(KEY_A, KEY_Z);  // A-Z
    caps.numberKeys = countKeysInRange(KEY_1, KEY_0);  // 1-9,0
    caps.totalKeys = countKeysInRange(0, 255);
    
    // Count modifiers
    if (hasKey(KEY_LEFTSHIFT)) caps.modifierKeys++;
    if (hasKey(KEY_RIGHTSHIFT)) caps.modifierKeys++;
    if (hasKey(KEY_LEFTCTRL)) caps.modifierKeys++;
    if (hasKey(KEY_RIGHTCTRL)) caps.modifierKeys++;
    if (hasKey(KEY_LEFTALT)) caps.modifierKeys++;
    if (hasKey(KEY_RIGHTALT)) caps.modifierKeys++;
    
    // Count mouse buttons
    if (hasKey(BTN_LEFT)) caps.mouseButtons++;
    if (hasKey(BTN_RIGHT)) caps.mouseButtons++;
    if (hasKey(BTN_MIDDLE)) caps.mouseButtons++;
    if (hasKey(BTN_SIDE)) caps.mouseButtons++;
    if (hasKey(BTN_EXTRA)) caps.mouseButtons++;
    if (hasKey(BTN_FORWARD)) caps.mouseButtons++;
    if (hasKey(BTN_BACK)) caps.mouseButtons++;
    
    // Count gamepad buttons
    caps.gamepadButtons = countKeysInRange(BTN_GAMEPAD, BTN_GAMEPAD + 16);
    caps.joystickButtons = countKeysInRange(BTN_JOYSTICK, BTN_JOYSTICK + 16);
    
    // Check movement capabilities
    caps.hasRelativeAxes = hasEventType(EV_REL) && (hasRelativeAxis(REL_X) || hasRelativeAxis(REL_Y));
    caps.hasAbsoluteAxes = hasEventType(EV_ABS) && (hasAbsoluteAxis(ABS_X) || hasAbsoluteAxis(ABS_Y));
    caps.hasMovement = caps.hasRelativeAxes || caps.hasAbsoluteAxes;
    
    // Check for analog sticks
    caps.hasAnalogSticks = hasAbsoluteAxis(ABS_X) && hasAbsoluteAxis(ABS_Y) && 
                          hasAbsoluteAxis(ABS_RX) && hasAbsoluteAxis(ABS_RY);
    
    // Check for D-pad
    caps.hasDPad = hasAbsoluteAxis(ABS_HAT0X) && hasAbsoluteAxis(ABS_HAT0Y);
    
    return caps;
}

bool Device::isRealKeyboard() const {
    if (!hasEventType(EV_KEY)) return false;
    
    // Must have essential keyboard components
    bool hasLetters = caps.letterKeys >= 20;  // Most letters
    bool hasNumbers = caps.numberKeys >= 8;   // Most numbers
    bool hasModifiers = caps.modifierKeys >= 2; // At least 2 modifiers
    bool hasSpace = hasKey(KEY_SPACE);
    bool hasEnter = hasKey(KEY_ENTER);
    bool hasEnoughKeys = caps.totalKeys >= 50;
    
    // Exclude devices that are clearly not keyboards
    bool isNotMouse = caps.mouseButtons == 0;
    bool isNotGamepad = caps.gamepadButtons == 0 && caps.joystickButtons == 0;
    bool hasNoMovement = !caps.hasMovement;
    
    return hasLetters && hasNumbers && hasModifiers && 
           hasSpace && hasEnter && hasEnoughKeys &&
           isNotMouse && isNotGamepad && hasNoMovement;
}

bool Device::isRealMouse() const {
    if (!hasEventType(EV_KEY) || !hasEventType(EV_REL)) return false;
    
    // Must have mouse buttons and movement
    bool hasMouseButtons = caps.mouseButtons >= 2; // At least left+right
    bool hasMovement = hasRelativeAxis(REL_X) && hasRelativeAxis(REL_Y);
    
    // Should not have keyboard characteristics
    bool notKeyboard = caps.letterKeys < 5 && caps.numberKeys < 5;
    bool notGamepad = caps.gamepadButtons == 0 && caps.joystickButtons == 0;
    
    return hasMouseButtons && hasMovement && notKeyboard && notGamepad;
}

bool Device::isGamepad() const {
    if (!hasEventType(EV_KEY)) return false;
    
    // Must have gamepad buttons
    bool hasGamepadButtons = caps.gamepadButtons >= 4; // A, B, X, Y etc
    
    // Usually has analog sticks or D-pad
    bool hasControllerInputs = caps.hasAnalogSticks || caps.hasDPad;
    
    // Should not have keyboard characteristics
    bool notKeyboard = caps.letterKeys < 5;
    bool notMouse = caps.mouseButtons == 0;
    
    return hasGamepadButtons && hasControllerInputs && notKeyboard && notMouse;
}

bool Device::isJoystick() const {
    if (!hasEventType(EV_KEY)) return false;
    
    // Has joystick buttons or fewer gamepad buttons
    bool hasJoystickButtons = caps.joystickButtons > 0;
    bool hasLimitedGamepadButtons = caps.gamepadButtons > 0 && caps.gamepadButtons < 4;
    
    // Has movement but not mouse-like
    bool hasMovement = caps.hasAbsoluteAxes;
    bool notMouse = !hasRelativeAxis(REL_X) || !hasRelativeAxis(REL_Y);
    
    // Should not have keyboard characteristics  
    bool notKeyboard = caps.letterKeys < 5;
    
    return (hasJoystickButtons || hasLimitedGamepadButtons) && 
           hasMovement && notMouse && notKeyboard;
}

DeviceType Device::detectType() {
    confidence = 0.0;
    classificationReason = "";
    
    // Try keyboard detection first
    if (isRealKeyboard()) {
        confidence = 0.95;
        classificationReason = "Has letters, numbers, modifiers, and essential keys";
        return DeviceType::Keyboard;
    }

    // Try auxiliary keyboard detection (function keys only, macro keys, etc.)
    bool isAuxKeyboard =
        hasEventType(EV_KEY) &&
        caps.totalKeys >= 3 &&
        caps.totalKeys <= 40 &&
        caps.letterKeys == 0 &&
        caps.numberKeys == 0 &&
        !caps.hasMovement &&
        caps.mouseButtons == 0;

    if (isAuxKeyboard) {
        confidence = 0.85;
        classificationReason = "Key-only auxiliary keyboard (F13+ / macro keys)";
        return DeviceType::Keyboard; // Use Keyboard type to ensure it gets opened
    }

    // Try mouse detection
    if (isRealMouse()) {
        confidence = 0.9;
        classificationReason = "Has mouse buttons and relative movement";
        return DeviceType::Mouse;
    }
    
    // Try gamepad detection
    if (isGamepad()) {
        confidence = 0.85;
        classificationReason = "Has gamepad buttons and controller inputs";
        return DeviceType::Gamepad;
    }
    
    // Try joystick detection
    if (isJoystick()) {
        confidence = 0.8;
        classificationReason = "Has joystick buttons and absolute movement";
        return DeviceType::Joystick;
    }
    
    // Fallback classifications based on name
    std::string lowerName = name;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
    
    if (lowerName.find("keyboard") != std::string::npos) {
        confidence = 0.3;
        classificationReason = "Name contains 'keyboard' but lacks full keyboard capabilities";
        return DeviceType::Keyboard;
    }
    
    if (lowerName.find("mouse") != std::string::npos) {
        confidence = 0.3;
        classificationReason = "Name contains 'mouse' but lacks full mouse capabilities";
        return DeviceType::Mouse;
    }
    
    if (lowerName.find("button") != std::string::npos) {
        confidence = 0.7;
        classificationReason = "Name indicates button device";
        return DeviceType::Button;
    }
    
    if (lowerName.find("audio") != std::string::npos || 
        lowerName.find("hdmi") != std::string::npos ||
        lowerName.find("speaker") != std::string::npos) {
        confidence = 0.8;
        classificationReason = "Name indicates audio device";
        return DeviceType::Audio;
    }
    
    confidence = 0.1;
    classificationReason = "Unknown device type";
    return DeviceType::Unknown;
}

std::vector<Device> Device::getAllDevices() {
    std::vector<Device> devices;
    std::ifstream proc("/proc/bus/input/devices");
    
    if (!proc.is_open()) {
        std::cerr << "Cannot open /proc/bus/input/devices" << std::endl;
        return devices;
    }
    
    std::vector<std::string> currentBlock;
    std::string line;
    
    while (std::getline(proc, line)) {
        if (line.empty()) {
            if (!currentBlock.empty()) {
                Device device = parseDeviceBlock(currentBlock);
                if (!device.name.empty() && !device.eventPath.empty()) {
                    devices.push_back(device);
                }
                currentBlock.clear();
            }
        } else {
            currentBlock.push_back(line);
        }
    }
    
    // Handle last block
    if (!currentBlock.empty()) {
        Device device = parseDeviceBlock(currentBlock);
        if (!device.name.empty() && !device.eventPath.empty()) {
            devices.push_back(device);
        }
    }
    
    return devices;
}

std::vector<DeviceInfo> Device::findKeyboards() {
    std::vector<Device> allDevices = getAllDevices();
    std::vector<Device> mergedDevices = mergeDevicesByVendorProduct(allDevices);
    std::vector<DeviceInfo> keyboards;

    for (const auto& device : mergedDevices) {
        if (device.type == DeviceType::Keyboard) {
            keyboards.push_back(device.toDeviceInfo());
        }
    }

    // Sort by confidence (highest first)
    std::sort(keyboards.begin(), keyboards.end(),
              [](const DeviceInfo& a, const DeviceInfo& b) {
                  return a.confidence > b.confidence;
              });

    return keyboards;
}

std::vector<DeviceInfo> Device::findMice() {
    std::vector<Device> allDevices = getAllDevices();
    std::vector<Device> mergedDevices = mergeDevicesByVendorProduct(allDevices);
    std::vector<DeviceInfo> mice;

    for (const auto& device : mergedDevices) {
        if (device.type == DeviceType::Mouse) {
            mice.push_back(device.toDeviceInfo());
        }
    }

    std::sort(mice.begin(), mice.end(),
              [](const DeviceInfo& a, const DeviceInfo& b) {
                  return a.confidence > b.confidence;
              });

    return mice;
}

std::vector<DeviceInfo> Device::findGamepads() {
    std::vector<Device> allDevices = getAllDevices();
    std::vector<Device> mergedDevices = mergeDevicesByVendorProduct(allDevices);
    std::vector<DeviceInfo> gamepads;

    for (const auto& device : mergedDevices) {
        if (device.type == DeviceType::Gamepad || device.type == DeviceType::Joystick) {
            gamepads.push_back(device.toDeviceInfo());
        }
    }

    std::sort(gamepads.begin(), gamepads.end(),
              [](const DeviceInfo& a, const DeviceInfo& b) {
                  return a.confidence > b.confidence;
              });

    return gamepads;
}

DeviceInfo Device::toDeviceInfo() const {
    DeviceInfo info;
    info.name = name;
    info.eventPath = eventPath;
    info.type = type;
    info.capabilities = caps;
    info.confidence = confidence;
    info.reason = classificationReason;
    return info;
}

std::string Device::toString() const {
    std::ostringstream oss;
    oss << "Device: '" << name << "'\n";
    oss << "  Type: ";

    switch (type) {
        case DeviceType::Keyboard: oss << "Keyboard"; break;
        case DeviceType::Mouse: oss << "Mouse"; break;
        case DeviceType::Gamepad: oss << "Gamepad"; break;
        case DeviceType::Joystick: oss << "Joystick"; break;
        case DeviceType::Audio: oss << "Audio"; break;
        case DeviceType::Button: oss << "Button"; break;
        default: oss << "Unknown"; break;
    }

    oss << " (confidence: " << (confidence * 100) << "%)\n";
    oss << "  Event: " << eventPath << "\n";
    oss << "  Bus: 0x" << std::hex << busType << ", Vendor: 0x" << vendor
        << ", Product: 0x" << product << std::dec << "\n";
    oss << "  Capabilities: " << caps.totalKeys << " keys, "
        << caps.mouseButtons << " mouse buttons, "
        << caps.gamepadButtons << " gamepad buttons\n";
    oss << "  Reason: " << classificationReason << "\n";

    return oss.str();
}

// Function to merge capabilities of devices with the same vendor+product
std::vector<Device> Device::mergeDevicesByVendorProduct(const std::vector<Device>& devices) {
    std::vector<Device> mergedDevices;
    std::map<std::pair<int, int>, Device> deviceMap; // Map of (vendor, product) to merged device

    for (const auto& device : devices) {
        std::pair<int, int> key = std::make_pair(device.vendor, device.product);

        if (deviceMap.find(key) == deviceMap.end()) {
            // First device with this vendor+product combination
            deviceMap[key] = device;
            // Initialize the combined capabilities
            deviceMap[key].capabilities.clear(); // Will be rebuilt from all devices
            deviceMap[key].handlers = device.handlers; // Will be combined later
        } else {
            // Merge with existing device
            Device& existing = deviceMap[key];

            // Combine capabilities by OR-ing the bitmasks
            // Extend vectors if needed
            size_t maxSize = std::max(existing.keyCapabilities.size(), device.keyCapabilities.size());
            existing.keyCapabilities.resize(maxSize, 0);
            for (size_t i = 0; i < device.keyCapabilities.size(); i++) {
                existing.keyCapabilities[i] |= device.keyCapabilities[i];
            }

            maxSize = std::max(existing.eventCapabilities.size(), device.eventCapabilities.size());
            existing.eventCapabilities.resize(maxSize, 0);
            for (size_t i = 0; i < device.eventCapabilities.size(); i++) {
                existing.eventCapabilities[i] |= device.eventCapabilities[i];
            }

            maxSize = std::max(existing.relCapabilities.size(), device.relCapabilities.size());
            existing.relCapabilities.resize(maxSize, 0);
            for (size_t i = 0; i < device.relCapabilities.size(); i++) {
                existing.relCapabilities[i] |= device.relCapabilities[i];
            }

            maxSize = std::max(existing.absCapabilities.size(), device.absCapabilities.size());
            existing.absCapabilities.resize(maxSize, 0);
            for (size_t i = 0; i < device.absCapabilities.size(); i++) {
                existing.absCapabilities[i] |= device.absCapabilities[i];
            }

            // Combine handlers string
            if (!existing.handlers.empty() && existing.handlers.find(device.handlers) == std::string::npos) {
                existing.handlers += " " + device.handlers;
            }

            // Combine capability strings
            existing.capabilities.insert(existing.capabilities.end(),
                                       device.capabilities.begin(),
                                       device.capabilities.end());

            // Re-analyze capabilities after merging
            existing.caps = existing.analyzeCapabilities();

            // Update name to indicate it's a merged device
            if (existing.name.find("(merged)") == std::string::npos) {
                existing.name += " (merged)";
            }
        }
    }

    // Convert map back to vector
    for (auto& pair : deviceMap) {
        // Update the device type based on merged capabilities
        pair.second.type = pair.second.detectType();
        mergedDevices.push_back(pair.second);
    }

    return mergedDevices;
}