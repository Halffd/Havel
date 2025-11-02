#include "EventListener.hpp"
#include "../IO.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <linux/uinput.h>
#include <cstring>
#include <algorithm>
#include <spdlog/spdlog.h>

using namespace spdlog;

namespace havel {

/**
 * EventListener - Unified input event listener for keyboard, mouse, and joystick
 * 
 * This class consolidates all input device handling into a single event loop.
 * It replaces the old separate evdev listeners (keyboard, mouse, gamepad) with
 * a unified approach that:
 * 
 * 1. Monitors multiple input devices simultaneously using select()
 * 2. Processes keyboard, mouse button, mouse movement, wheel, and joystick events
 * 3. Evaluates hotkeys with full support for:
 *    - Single keys with modifiers (Ctrl+W, Alt+Tab, etc.)
 *    - Mouse buttons (LButton, RButton, MButton, XButton1/2)
 *    - Mouse wheel (WheelUp, WheelDown)
 *    - Combos with & operator (LButton & RButton, CapsLock & W, etc.)
 *    - Joystick buttons (JoyA, JoyB, JoyX, JoyY, etc.)
 *    - Repeat intervals (@LAlt:850 for custom repeat timing)
 * 4. Applies mouse and scroll sensitivity scaling
 * 5. Supports key remapping (e.g., CapsLock -> Ctrl)
 * 6. Forwards events through uinput (with optional blocking)
 * 
 * The implementation uses exact logic from IO.cpp's evdev listeners to ensure
 * identical behavior while providing better performance and maintainability.
 */

EventListener::EventListener() {
    shutdownFd = eventfd(0, EFD_NONBLOCK);
}

EventListener::~EventListener() {
    Stop();
    if (shutdownFd >= 0) {
        close(shutdownFd);
    }
    if (uinputFd >= 0) {
        ioctl(uinputFd, UI_DEV_DESTROY);
        close(uinputFd);
    }
}

bool EventListener::Start(const std::vector<std::string>& devicePaths) {
    if (running.load()) {
        return false;
    }
    
    // Open all devices
    for (const auto& path : devicePaths) {
        int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            error("Failed to open device: {}", path);
            continue;
        }
        
        char name[256] = "Unknown";
        ioctl(fd, EVIOCGNAME(sizeof(name)), name);
        
        DeviceInfo device;
        device.path = path;
        device.fd = fd;
        device.name = name;
        devices.push_back(device);
        
        info("Opened input device: {} ({})", name, path);
    }
    
    if (devices.empty()) {
        error("No input devices opened");
        return false;
    }
    
    running.store(true);
    shutdown.store(false);
    eventThread = std::thread(&EventListener::EventLoop, this);
    
    return true;
}

void EventListener::Stop() {
    if (!running.load()) {
        return;
    }
    
    running.store(false);
    shutdown.store(true);
    
    // Signal shutdown
    if (shutdownFd >= 0) {
        uint64_t val = 1;
        write(shutdownFd, &val, sizeof(val));
    }
    
    if (eventThread.joinable()) {
        eventThread.join();
    }
    
    // Close all devices
    for (auto& device : devices) {
        if (device.fd >= 0) {
            close(device.fd);
        }
    }
    devices.clear();
}

bool EventListener::SetupUinput() {
    uinputFd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (uinputFd < 0) {
        error("Failed to open /dev/uinput");
        return false;
    }
    
    // Enable all key events
    ioctl(uinputFd, UI_SET_EVBIT, EV_KEY);
    ioctl(uinputFd, UI_SET_EVBIT, EV_SYN);
    ioctl(uinputFd, UI_SET_EVBIT, EV_REL);
    ioctl(uinputFd, UI_SET_EVBIT, EV_ABS);
    
    // Enable all keys
    for (int i = 0; i < KEY_MAX; i++) {
        ioctl(uinputFd, UI_SET_KEYBIT, i);
    }
    
    // Enable mouse buttons
    for (int i = BTN_MOUSE; i < BTN_JOYSTICK; i++) {
        ioctl(uinputFd, UI_SET_KEYBIT, i);
    }
    
    // Enable relative axes (mouse)
    ioctl(uinputFd, UI_SET_RELBIT, REL_X);
    ioctl(uinputFd, UI_SET_RELBIT, REL_Y);
    ioctl(uinputFd, UI_SET_RELBIT, REL_WHEEL);
    ioctl(uinputFd, UI_SET_RELBIT, REL_HWHEEL);
    
    // Setup device
    struct uinput_setup usetup = {};
    strcpy(usetup.name, "Havel Virtual Input");
    usetup.id.bustype = BUS_USB;
    usetup.id.vendor = 0x1234;
    usetup.id.product = 0x5678;
    usetup.id.version = 1;
    
    if (ioctl(uinputFd, UI_DEV_SETUP, &usetup) < 0) {
        error("Failed to setup uinput device");
        close(uinputFd);
        uinputFd = -1;
        return false;
    }
    
    if (ioctl(uinputFd, UI_DEV_CREATE) < 0) {
        error("Failed to create uinput device");
        close(uinputFd);
        uinputFd = -1;
        return false;
    }
    
    info("Uinput device created successfully");
    return true;
}

void EventListener::SendUinputEvent(int type, int code, int value) {
    if (uinputFd < 0) {
        return;
    }
    
    struct input_event ev = {};
    ev.type = type;
    ev.code = code;
    ev.value = value;
    gettimeofday(&ev.time, nullptr);
    
    write(uinputFd, &ev, sizeof(ev));
    
    // Send SYN event
    ev.type = EV_SYN;
    ev.code = SYN_REPORT;
    ev.value = 0;
    write(uinputFd, &ev, sizeof(ev));
}

void EventListener::RegisterHotkey(int id, const HotKey& hotkey) {
    std::lock_guard<std::mutex> lock(hotkeyMutex);
    hotkeys[id] = hotkey;
}

void EventListener::UnregisterHotkey(int id) {
    std::lock_guard<std::mutex> lock(hotkeyMutex);
    hotkeys.erase(id);
}

bool EventListener::GetKeyState(int evdevCode) const {
    std::lock_guard<std::mutex> lock(stateMutex);
    auto it = evdevKeyState.find(evdevCode);
    return it != evdevKeyState.end() && it->second;
}

EventListener::ModifierState EventListener::GetModifierState() const {
    std::lock_guard<std::mutex> lock(stateMutex);
    return modifierState;
}

void EventListener::SetBlockInput(bool block) {
    blockInput.store(block);
}

void EventListener::AddKeyRemap(int fromCode, int toCode) {
    std::lock_guard<std::mutex> lock(remapMutex);
    keyRemaps[fromCode] = toCode;
}

void EventListener::RemoveKeyRemap(int fromCode) {
    std::lock_guard<std::mutex> lock(remapMutex);
    keyRemaps.erase(fromCode);
}

void EventListener::SetEmergencyShutdownKey(int evdevCode) {
    emergencyShutdownKey = evdevCode;
}

void EventListener::SetMouseSensitivity(double sensitivity) {
    mouseSensitivity = sensitivity;
}

void EventListener::SetScrollSpeed(double speed) {
    scrollSpeed = speed;
}

#ifdef __linux__
bool EventListener::StartX11Monitor(Display* display) {
    if (!display) {
        error("Cannot start X11 monitor: display is null");
        return false;
    }
    
    if (!x11Monitor) {
        x11Monitor = std::make_unique<X11HotkeyMonitor>();
    }
    
    // Register all current hotkeys with X11 monitor
    {
        std::lock_guard<std::mutex> lock(hotkeyMutex);
        for (const auto& [id, hotkey] : hotkeys) {
            if (!hotkey.evdev) {  // Only X11 hotkeys
                x11Monitor->RegisterHotkey(id, hotkey);
            }
        }
    }
    
    return x11Monitor->Start(display);
}

void EventListener::StopX11Monitor() {
    if (x11Monitor) {
        x11Monitor->Stop();
    }
}

bool EventListener::IsX11MonitorRunning() const {
    return x11Monitor && x11Monitor->IsRunning();
}
#endif

// Main event loop - EXACT logic from IO.cpp StartEvdevHotkeyListener
void EventListener::EventLoop() {
    info("EventListener: Starting event loop");
    
    while (running.load() && !shutdown.load()) {
        fd_set readfds;
        FD_ZERO(&readfds);
        
        int maxFd = shutdownFd;
        FD_SET(shutdownFd, &readfds);
        
        for (const auto& device : devices) {
            FD_SET(device.fd, &readfds);
            if (device.fd > maxFd) {
                maxFd = device.fd;
            }
        }
        
        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        
        int ret = select(maxFd + 1, &readfds, nullptr, nullptr, &timeout);
        
        if (ret < 0) {
            if (errno == EINTR) continue;
            error("select() failed: {}", strerror(errno));
            break;
        }
        
        if (ret == 0) {
            // Timeout
            continue;
        }
        
        // Check shutdown signal
        if (FD_ISSET(shutdownFd, &readfds)) {
            break;
        }
        
        // Process events from all devices
        for (const auto& device : devices) {
            if (!FD_ISSET(device.fd, &readfds)) {
                continue;
            }
            
            struct input_event ev;
            ssize_t n = read(device.fd, &ev, sizeof(ev));
            
            if (n != sizeof(ev)) {
                continue;
            }
            
            if (ev.type == EV_KEY) {
                ProcessKeyboardEvent(ev);
            } else if (ev.type == EV_REL || ev.type == EV_ABS) {
                ProcessMouseEvent(ev);
            }
        }
    }
    
    // Wait for pending callbacks
    info("EventListener: Waiting for {} pending callbacks", pendingCallbacks.load());
    while (pendingCallbacks.load() > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    info("EventListener: Event loop stopped");
}

// Process keyboard event - EXACT logic from IO.cpp
void EventListener::ProcessKeyboardEvent(const input_event& ev) {
    int originalCode = ev.code;
    int mappedCode = originalCode;
    bool down = (ev.value == 1);
    bool repeat = (ev.value == 2);
    
    // Handle key remapping
    {
        std::lock_guard<std::mutex> lock(remapMutex);
        if (down && !repeat) {
            // On press: apply remapping
            auto remapIt = keyRemaps.find(originalCode);
            if (remapIt != keyRemaps.end()) {
                mappedCode = remapIt->second;
            }
            activeRemaps[originalCode] = mappedCode;
        } else {
            // On release: use the same mapping we stored on press
            auto it = activeRemaps.find(originalCode);
            if (it != activeRemaps.end()) {
                mappedCode = it->second;
                if (!down) {
                    activeRemaps.erase(it);
                }
            } else {
                // Fallback
                auto remapIt = keyRemaps.find(originalCode);
                if (remapIt != keyRemaps.end()) {
                    mappedCode = remapIt->second;
                }
            }
        }
    }
    
    // Update key state
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        evdevKeyState[originalCode] = down;
        
        // Track active inputs for combos
        if (down) {
            activeInputs[mappedCode] = std::chrono::steady_clock::now();
        } else {
            activeInputs.erase(mappedCode);
        }
        
        // Update modifier state
        UpdateModifierState(originalCode, down);
    }
    
    // Evaluate hotkeys
    bool shouldBlock = EvaluateHotkeys(originalCode, down, repeat);
    
    // Forward event to uinput if not blocked
    if (!shouldBlock && !blockInput.load()) {
        SendUinputEvent(EV_KEY, mappedCode, ev.value);
    } else if (!down) {
        // Always release keys so modifiers don't stick
        SendUinputEvent(EV_KEY, mappedCode, 0);
    }
}

/**
 * Process mouse event - Handles mouse buttons, movement, and wheel
 * 
 * This function processes all mouse-related events:
 * - EV_KEY: Mouse button presses/releases (BTN_LEFT, BTN_RIGHT, etc.)
 * - EV_REL: Relative movement (REL_X, REL_Y) and wheel (REL_WHEEL, REL_HWHEEL)
 * - EV_ABS: Absolute positioning (touchpads, joystick axes)
 * 
 * For each event type, it:
 * 1. Updates internal state (button states, active inputs for combos)
 * 2. Evaluates registered hotkeys (button hotkeys, wheel hotkeys, combos)
 * 3. Applies sensitivity scaling (mouse movement, scroll speed)
 * 4. Forwards events to uinput (unless blocked by a grabbed hotkey)
 * 
 * EXACT logic from IO.cpp handleMouseButton/handleMouseRelative
 */
void EventListener::ProcessMouseEvent(const input_event& ev) {
    bool shouldBlock = false;
    auto now = std::chrono::steady_clock::now();
    
    // Handle different event types based on evdev event type
    if (ev.type == EV_KEY) {
        // Mouse button event (BTN_LEFT, BTN_RIGHT, BTN_MIDDLE, BTN_SIDE, BTN_EXTRA)
        bool down = (ev.value == 1);
        
        // Update button state and active inputs
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            mouseButtonState[ev.code] = down;
            
            if (down) {
                activeInputs[ev.code] = now;
            } else {
                activeInputs.erase(ev.code);
            }
        }
        
        // Check for mouse button hotkeys
        std::lock_guard<std::mutex> hotkeyLock(hotkeyMutex);
        std::lock_guard<std::mutex> stateLock(stateMutex);
        
        for (auto& [id, hotkey] : hotkeys) {
            if (!hotkey.enabled) continue;
            
            // Check for combo hotkeys first
            if (hotkey.type == HotkeyType::Combo) {
                if (EvaluateCombo(hotkey)) {
                    // Combo matched
                    pendingCallbacks++;
                    std::thread([callback = hotkey.callback, alias = hotkey.alias, this]() {
                        try {
                            if (running.load() && !shutdown.load()) {
                                callback();
                            }
                        } catch (const std::exception& e) {
                            error("Hotkey '{}' threw: {}", alias, e.what());
                        }
                        pendingCallbacks--;
                    }).detach();
                    
                    if (hotkey.grab) {
                        shouldBlock = true;
                    }
                }
                continue;
            }
            
            // Check for mouse button hotkeys
            if (hotkey.type != HotkeyType::MouseButton) continue;
            
            // Match button code
            if (hotkey.mouseButton != ev.code) continue;
            
            // Event type check
            if (hotkey.eventType == HotkeyEventType::Down && !down) continue;
            if (hotkey.eventType == HotkeyEventType::Up && down) continue;
            
            // Modifier matching
            bool modifierMatch = CheckModifierMatch(hotkey.modifiers, hotkey.wildcard);
            if (!modifierMatch) continue;
            
            // Context checks
            if (!hotkey.contexts.empty()) {
                if (!std::all_of(hotkey.contexts.begin(), hotkey.contexts.end(),
                               [](auto& ctx) { return ctx(); })) {
                    continue;
                }
            }
            
            // Hotkey matched! Execute callback
            info("Mouse button hotkey triggered: {} button: {}", hotkey.alias, ev.code);
            
            pendingCallbacks++;
            std::thread([callback = hotkey.callback, alias = hotkey.alias, this]() {
                try {
                    if (running.load() && !shutdown.load()) {
                        callback();
                    }
                } catch (const std::exception& e) {
                    error("Hotkey '{}' threw: {}", alias, e.what());
                }
                pendingCallbacks--;
            }).detach();
            
            if (hotkey.grab) {
                shouldBlock = true;
            }
        }
        
        // Forward event if not blocked
        if (!shouldBlock && !blockInput.load()) {
            SendUinputEvent(EV_KEY, ev.code, ev.value);
        }
        
    } else if (ev.type == EV_REL) {
        // Relative mouse movement or wheel
        
        if (ev.code == REL_X || ev.code == REL_Y) {
            // Mouse movement - apply sensitivity
            double scaledValue = ev.value * mouseSensitivity;
            int32_t scaledInt = static_cast<int32_t>(std::round(scaledValue));
            
            // Preserve direction for small movements
            if (scaledInt == 0 && ev.value != 0) {
                scaledInt = (ev.value > 0) ? 1 : -1;
            }
            
            if (!blockInput.load()) {
                SendUinputEvent(EV_REL, ev.code, scaledInt);
            }
            
        } else if (ev.code == REL_WHEEL || ev.code == REL_HWHEEL) {
            // Mouse wheel - check for wheel hotkeys
            int wheelDirection = (ev.value > 0) ? 1 : -1;
            
            std::lock_guard<std::mutex> hotkeyLock(hotkeyMutex);
            std::lock_guard<std::mutex> stateLock(stateMutex);
            
            for (auto& [id, hotkey] : hotkeys) {
                if (!hotkey.enabled || hotkey.type != HotkeyType::MouseWheel) continue;
                
                // Match wheel direction
                if (hotkey.wheelDirection != wheelDirection) continue;
                
                // Modifier matching
                bool modifierMatch = CheckModifierMatch(hotkey.modifiers, hotkey.wildcard);
                if (!modifierMatch) continue;
                
                // Context checks
                if (!hotkey.contexts.empty()) {
                    if (!std::all_of(hotkey.contexts.begin(), hotkey.contexts.end(),
                                   [](auto& ctx) { return ctx(); })) {
                        continue;
                    }
                }
                
                // Hotkey matched! Execute callback
                info("Wheel hotkey triggered: {} direction: {}", hotkey.alias, wheelDirection);
                
                pendingCallbacks++;
                std::thread([callback = hotkey.callback, alias = hotkey.alias, this]() {
                    try {
                        if (running.load() && !shutdown.load()) {
                            callback();
                        }
                    } catch (const std::exception& e) {
                        error("Hotkey '{}' threw: {}", alias, e.what());
                    }
                    pendingCallbacks--;
                }).detach();
                
                if (hotkey.grab) {
                    shouldBlock = true;
                }
            }
            
            // Apply scroll speed and forward if not blocked
            if (!shouldBlock && !blockInput.load()) {
                double scaledValue = ev.value * scrollSpeed;
                int32_t scaledInt = static_cast<int32_t>(std::round(scaledValue));
                
                // Preserve direction
                if (scaledInt == 0 && ev.value != 0) {
                    scaledInt = (ev.value > 0) ? 1 : -1;
                }
                
                SendUinputEvent(EV_REL, ev.code, scaledInt);
            }
        } else {
            // Other relative events (e.g., REL_DIAL)
            if (!blockInput.load()) {
                SendUinputEvent(ev.type, ev.code, ev.value);
            }
        }
        
    } else if (ev.type == EV_ABS) {
        // Absolute positioning or joystick axes
        // For joystick, we might want to convert axis movements to button presses
        // For now, just forward
        if (!blockInput.load()) {
            SendUinputEvent(ev.type, ev.code, ev.value);
        }
    }
}

// Update modifier state - EXACT logic from IO.cpp
void EventListener::UpdateModifierState(int evdevCode, bool down) {
    // This is called with stateMutex already locked
    
    if (evdevCode == KEY_LEFTCTRL) {
        modifierState.leftCtrl = down;
    } else if (evdevCode == KEY_RIGHTCTRL) {
        modifierState.rightCtrl = down;
    } else if (evdevCode == KEY_LEFTSHIFT) {
        modifierState.leftShift = down;
    } else if (evdevCode == KEY_RIGHTSHIFT) {
        modifierState.rightShift = down;
    } else if (evdevCode == KEY_LEFTALT) {
        modifierState.leftAlt = down;
    } else if (evdevCode == KEY_RIGHTALT) {
        modifierState.rightAlt = down;
    } else if (evdevCode == KEY_LEFTMETA) {
        modifierState.leftMeta = down;
    } else if (evdevCode == KEY_RIGHTMETA) {
        modifierState.rightMeta = down;
    }
}

// Check modifier match - EXACT logic from IO.cpp
bool EventListener::CheckModifierMatch(int requiredModifiers, bool wildcard) const {
    // This is called with stateMutex already locked
    
    bool ctrlRequired = (requiredModifiers & (1 << 0)) != 0;
    bool shiftRequired = (requiredModifiers & (1 << 1)) != 0;
    bool altRequired = (requiredModifiers & (1 << 2)) != 0;
    bool metaRequired = (requiredModifiers & (1 << 3)) != 0;
    
    bool ctrlPressed = modifierState.IsCtrlPressed();
    bool shiftPressed = modifierState.IsShiftPressed();
    bool altPressed = modifierState.IsAltPressed();
    bool metaPressed = modifierState.IsMetaPressed();
    
    if (wildcard) {
        // Wildcard: only check that REQUIRED modifiers are pressed
        return (!ctrlRequired || ctrlPressed) &&
               (!shiftRequired || shiftPressed) &&
               (!altRequired || altPressed) &&
               (!metaRequired || metaPressed);
    } else {
        // Normal: exact modifier match
        return (ctrlRequired == ctrlPressed) &&
               (shiftRequired == shiftPressed) &&
               (altRequired == altPressed) &&
               (metaRequired == metaPressed);
    }
}

// Evaluate hotkeys - EXACT logic from IO.cpp
bool EventListener::EvaluateHotkeys(int evdevCode, bool down, bool repeat) {
    std::lock_guard<std::mutex> hotkeyLock(hotkeyMutex);
    std::lock_guard<std::mutex> stateLock(stateMutex);
    
    bool shouldBlock = false;
    
    // Check emergency shutdown key
    if (down && emergencyShutdownKey != 0 && evdevCode == emergencyShutdownKey) {
        error("🚨 EMERGENCY HOTKEY TRIGGERED! Shutting down...");
        running.store(false);
        shutdown.store(true);
        return true;
    }
    
    for (auto& [id, hotkey] : hotkeys) {
        if (!hotkey.enabled || !hotkey.evdev) {
            continue;
        }
        
        // Check if this is a combo hotkey
        if (hotkey.type == HotkeyType::Combo) {
            if (EvaluateCombo(hotkey)) {
                // Combo matched, execute callback
                pendingCallbacks++;
                std::thread([callback = hotkey.callback, alias = hotkey.alias, this]() {
                    try {
                        if (running.load() && !shutdown.load()) {
                            callback();
                        }
                    } catch (const std::exception& e) {
                        error("Hotkey '{}' threw: {}", alias, e.what());
                    } catch (...) {
                        error("Hotkey '{}' threw unknown exception", alias);
                    }
                    pendingCallbacks--;
                }).detach();
                
                if (hotkey.grab) {
                    shouldBlock = true;
                }
            }
            continue;
        }
        
        // Match against key code
        if (hotkey.key != static_cast<Key>(evdevCode)) {
            continue;
        }
        
        // Event type check
        if (!hotkey.repeat && repeat) {
            continue;
        }
        
        if (hotkey.eventType == HotkeyEventType::Down && !down) {
            continue;
        }
        if (hotkey.eventType == HotkeyEventType::Up && down) {
            continue;
        }
        
        // Modifier matching
        bool isModifierKey = (evdevCode == KEY_LEFTALT || evdevCode == KEY_RIGHTALT ||
                             evdevCode == KEY_LEFTCTRL || evdevCode == KEY_RIGHTCTRL ||
                             evdevCode == KEY_LEFTSHIFT || evdevCode == KEY_RIGHTSHIFT ||
                             evdevCode == KEY_LEFTMETA || evdevCode == KEY_RIGHTMETA);
        
        bool modifierMatch;
        if (isModifierKey && hotkey.modifiers == 0) {
            modifierMatch = true;
        } else {
            modifierMatch = CheckModifierMatch(hotkey.modifiers, hotkey.wildcard);
        }
        
        if (!modifierMatch) {
            continue;
        }
        
        // Context checks
        if (!hotkey.contexts.empty()) {
            if (!std::all_of(hotkey.contexts.begin(), hotkey.contexts.end(),
                           [](auto& ctx) { return ctx(); })) {
                continue;
            }
        }
        
        // Check repeat interval
        if (hotkey.repeatInterval > 0 && repeat) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - hotkey.lastTriggerTime).count();
            
            if (elapsed < hotkey.repeatInterval) {
                continue;
            }
            hotkey.lastTriggerTime = now;
        } else if (down && !repeat) {
            hotkey.lastTriggerTime = std::chrono::steady_clock::now();
        }
        
        // Hotkey matched! Execute callback
        hotkey.success = true;
        debug("Hotkey {} triggered, key: {}, modifiers: {}, down: {}, repeat: {}",
              hotkey.alias, hotkey.key, hotkey.modifiers, down, repeat);
        
        pendingCallbacks++;
        std::thread([callback = hotkey.callback, alias = hotkey.alias, this]() {
            try {
                info("Executing hotkey callback: {}", alias);
                if (running.load() && !shutdown.load()) {
                    callback();
                }
            } catch (const std::exception& e) {
                error("Hotkey '{}' threw: {}", alias, e.what());
            } catch (...) {
                error("Hotkey '{}' threw unknown exception", alias);
            }
            pendingCallbacks--;
        }).detach();
        
        if (hotkey.grab) {
            shouldBlock = true;
        }
    }
    
    return shouldBlock;
}

/**
 * Evaluate combo hotkey - Checks if all keys in a combo are currently pressed
 * 
 * Combos use the & operator to require multiple keys/buttons pressed simultaneously:
 * - "LButton & RButton" - Both mouse buttons pressed together
 * - "CapsLock & W" - CapsLock and W pressed together
 * - "JoyA & JoyB" - Gamepad buttons A and B pressed together
 * 
 * The combo is considered active if:
 * 1. All keys/buttons in the combo are currently pressed
 * 2. All were pressed within the time window (default 500ms)
 * 
 * This allows for natural combo input where keys don't need to be pressed
 * at exactly the same instant, but within a short time window.
 * 
 * EXACT logic from IO.cpp
 */
bool EventListener::EvaluateCombo(const HotKey& hotkey) {
    // This is called with stateMutex already locked
    
    if (hotkey.comboSequence.empty()) {
        return false;
    }
    
    auto now = std::chrono::steady_clock::now();
    
    // Check if all keys in combo are currently pressed within time window
    for (const auto& comboKey : hotkey.comboSequence) {
        int keyCode = static_cast<int>(comboKey.key);
        
        auto it = activeInputs.find(keyCode);
        if (it == activeInputs.end()) {
            return false; // Key not pressed
        }
        
        // Check if key was pressed within time window
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - it->second).count();
        
        if (elapsed > comboTimeWindow) {
            return false; // Key pressed too long ago
        }
    }
    
    // All keys in combo are pressed within time window
    return true;
}

} // namespace havel
