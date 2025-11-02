#pragma once
#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <linux/input.h>
#include <sys/eventfd.h>
#include "KeyMap.hpp"

#ifdef __linux__
#include "X11HotkeyMonitor.hpp"
#endif

namespace havel {

// Forward declaration
struct HotKey;

// Event listener that handles all input devices with unified evdev logic
class EventListener {
public:
    EventListener();
    ~EventListener();
    
    // Start listening on specified devices
    bool Start(const std::vector<std::string>& devicePaths);
    
    // Stop listening
    void Stop();
    
    // Check if running
    bool IsRunning() const { return running.load(); }
    
    // Register hotkey
    void RegisterHotkey(int id, const HotKey& hotkey);
    
    // Unregister hotkey
    void UnregisterHotkey(int id);
    
    // Get key state
    bool GetKeyState(int evdevCode) const;
    
    // Get modifier state
    struct ModifierState {
        bool leftCtrl = false;
        bool rightCtrl = false;
        bool leftShift = false;
        bool rightShift = false;
        bool leftAlt = false;
        bool rightAlt = false;
        bool leftMeta = false;
        bool rightMeta = false;
        
        bool IsCtrlPressed() const { return leftCtrl || rightCtrl; }
        bool IsShiftPressed() const { return leftShift || rightShift; }
        bool IsAltPressed() const { return leftAlt || rightAlt; }
        bool IsMetaPressed() const { return leftMeta || rightMeta; }
    };
    
    ModifierState GetModifierState() const;
    
    // Setup uinput for event forwarding
    bool SetupUinput();
    
    // Send event through uinput
    void SendUinputEvent(int type, int code, int value);
    
    // Set blocking mode for specific keys
    void SetBlockInput(bool block);
    
    // Add key remap
    void AddKeyRemap(int fromCode, int toCode);
    
    // Remove key remap
    void RemoveKeyRemap(int fromCode);
    
    // Set emergency shutdown key
    void SetEmergencyShutdownKey(int evdevCode);
    
    // Set mouse sensitivity (1.0 = default)
    void SetMouseSensitivity(double sensitivity);
    double GetMouseSensitivity() const { return mouseSensitivity; }
    
    // Set scroll speed (1.0 = default)
    void SetScrollSpeed(double speed);
    double GetScrollSpeed() const { return scrollSpeed; }
    
    // X11 hotkey monitoring (separate from evdev)
    #ifdef __linux__
    bool StartX11Monitor(Display* display);
    void StopX11Monitor();
    bool IsX11MonitorRunning() const;
    #endif
    
private:
    // Device info
    struct DeviceInfo {
        std::string path;
        int fd = -1;
        std::string name;
    };
    
    // Main event loop (exact logic from IO.cpp)
    void EventLoop();
    
    // Process keyboard event (exact logic from IO.cpp)
    void ProcessKeyboardEvent(const input_event& ev);
    
    // Process mouse event
    void ProcessMouseEvent(const input_event& ev);
    
    // Evaluate hotkeys (exact logic from IO.cpp)
    bool EvaluateHotkeys(int evdevCode, bool down, bool repeat);
    
    // Evaluate combo hotkeys (exact logic from IO.cpp)
    bool EvaluateCombo(const HotKey& hotkey);
    
    // Check modifier match (exact logic from IO.cpp)
    bool CheckModifierMatch(int requiredModifiers, bool wildcard) const;
    
    // Update modifier state
    void UpdateModifierState(int evdevCode, bool down);
    
    // Check if should block event
    bool ShouldBlockEvent(int evdevCode, bool down);
    
    // Handle key remap
    int RemapKey(int evdevCode, bool down);
    
    std::atomic<bool> running{false};
    std::atomic<bool> shutdown{false};
    std::atomic<bool> blockInput{false};
    std::atomic<int> pendingCallbacks{0};
    std::thread eventThread;
    
    std::vector<DeviceInfo> devices;
    int shutdownFd = -1; // eventfd for clean shutdown
    int uinputFd = -1;
    int emergencyShutdownKey = 0;
    
    // State tracking (exact from IO.cpp)
    mutable std::mutex stateMutex;
    std::map<int, bool> evdevKeyState;
    std::map<int, std::chrono::steady_clock::time_point> keyDownTime;
    std::unordered_map<int, std::chrono::steady_clock::time_point> activeInputs;
    ModifierState modifierState;
    
    // Hotkey management (exact from IO.cpp)
    mutable std::mutex hotkeyMutex;
    std::map<int, HotKey> hotkeys;
    
    // Key remapping (exact from IO.cpp)
    std::mutex remapMutex;
    std::map<int, int> keyRemaps;
    std::map<int, int> activeRemaps;
    
    // Combo tracking
    int comboTimeWindow = 500; // milliseconds
    
    // Mouse and scroll sensitivity
    double mouseSensitivity = 1.0;
    double scrollSpeed = 1.0;
    
    // Mouse button state tracking
    std::map<int, bool> mouseButtonState;
    
    // X11 hotkey monitor (separate component)
    #ifdef __linux__
    std::unique_ptr<X11HotkeyMonitor> x11Monitor;
    #endif
};

} // namespace havel
