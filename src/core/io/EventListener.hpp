#pragma once
#include <shared_mutex>
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
    // Side-aware modifier keys bitmask
    enum Modifier {
        Null    = 0,
        LCtrl   = 1 << 0,
        RCtrl   = 1 << 1,
        LShift  = 1 << 2,
        RShift  = 1 << 3,
        LAlt    = 1 << 4,
        RAlt    = 1 << 5,
        LMeta   = 1 << 6,
        RMeta   = 1 << 7
    };
    EventListener();
    ~EventListener();
    
    // Start listening on specified devices
    bool Start(const std::vector<std::string>& devicePaths, bool grabDevices = false);
    std::map<int, bool> evdevKeyState;
    
    // Stop listening
    void Stop();
    
    // Enable/disable device grabbing
    void SetGrabDevices(bool grab) { grabDevices = grab; }
    
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
        
        // Convert to side-aware bitmask for precise modifier matching
        int ToBitmask() const {
            int mask = 0;
            if (leftCtrl) mask |= Modifier::LCtrl;
            if (rightCtrl) mask |= Modifier::RCtrl;
            if (leftShift) mask |= Modifier::LShift;
            if (rightShift) mask |= Modifier::RShift;
            if (leftAlt) mask |= Modifier::LAlt;
            if (rightAlt) mask |= Modifier::RAlt;
            if (leftMeta) mask |= Modifier::LMeta;
            if (rightMeta) mask |= Modifier::RMeta;
            return mask;
        }

        // Convert to logical bitmask for compatibility with legacy code
        int ToLogicalBitmask() const {
            int mask = 0;
            if (IsCtrlPressed()) mask |= (Modifier::LCtrl | Modifier::RCtrl);   // Use both bits to represent "any Ctrl"
            if (IsShiftPressed()) mask |= (Modifier::LShift | Modifier::RShift); // Use both bits to represent "any Shift"
            if (IsAltPressed()) mask |= (Modifier::LAlt | Modifier::RAlt);     // Use both bits to represent "any Alt"
            if (IsMetaPressed()) mask |= (Modifier::LMeta | Modifier::RMeta);   // Use both bits to represent "any Meta"
            return mask;
        }
    };
    
    const ModifierState& GetModifierState() const;
    
    // Debugging
    std::string GetModifiersString() const;
    std::string GetActiveInputsString() const;
    
    int GetCurrentModifiersMask() const;
    // Hotkey optimization
    struct ActiveInput {
        std::chrono::steady_clock::time_point timestamp;
        int modifiers;  // Modifiers that were held when this key was pressed
        
        ActiveInput() : modifiers(0) {}
        explicit ActiveInput(int mods) : 
            timestamp(std::chrono::steady_clock::now()),
            modifiers(mods) {}
        ActiveInput(int mods, std::chrono::steady_clock::time_point time) :
            timestamp(time),
            modifiers(mods) {}
    };
    
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

    // Callback for any key press
    using AnyKeyPressCallback = std::function<void(const std::string& key)>;
    void SetAnyKeyPressCallback(AnyKeyPressCallback callback);
    
    int uinputFd = -1;

    // Callback for any key press
    std::function<void(const std::string& key)> anyKeyPressCallback = nullptr;

    // Callback for input notification (for watchdog)
    std::function<void()> inputNotificationCallback = nullptr;

private:
    // Device info
    struct DeviceInfo {
        std::string path;
        int fd = -1;
        std::string name;
    };
    

    void ExecuteHotkeyCallback(const HotKey& hotkey);
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
    bool CheckModifierMatch(const HotKey& hotkey) const;
    
    // Update modifier state
    void UpdateModifierState(int evdevCode, bool down);
    
    // Check if should block event
    bool ShouldBlockEvent(int evdevCode, bool down);
    
    // Handle key remap
    int RemapKey(int evdevCode, bool down);
    bool EvaluateWheelCombo(const HotKey& hotkey, int wheelDirection);

    std::atomic<bool> running{false};
    std::atomic<bool> shutdown{false};
    std::atomic<bool> blockInput{false};
    std::atomic<int> pendingCallbacks{0};
    std::thread eventThread;
    
    std::vector<DeviceInfo> devices;
    int shutdownFd = -1; // eventfd for clean shutdown
    int emergencyShutdownKey = 0;
    
    // State tracking (exact from IO.cpp)
    mutable std::shared_mutex stateMutex;
    std::map<int, std::chrono::steady_clock::time_point> keyDownTime;
    std::unordered_map<int, ActiveInput> activeInputs;  // Maps key code to ActiveInput
    ModifierState modifierState;
    
    // Hotkey management (exact from IO.cpp)
    mutable std::shared_mutex hotkeyMutex;
    
    // Hotkey optimization data structures
    std::unordered_map<int, std::vector<int>> combosByKey;  // keyCode -> hotkey IDs
    std::unordered_map<int, int> comboPressedCount;  // hotkey ID -> count of pressed keys
    
    // Key remapping (exact from IO.cpp)
    std::mutex remapMutex;
    std::map<int, int> keyRemaps;
    std::map<int, int> activeRemaps;
    
    // Combo tracking - 0 means infinite time window (hold-based combos)
    int comboTimeWindow = 0; // milliseconds (0 = infinite)
    
    // Mouse and scroll sensitivity
    double mouseSensitivity = 1.0;
    double scrollSpeed = 1.0;
    
    // Mouse button state tracking
    std::map<int, bool> mouseButtonState;

    // Wheel event tracking for wheel combos
    std::chrono::steady_clock::time_point lastWheelUpTime{std::chrono::steady_clock::time_point::min()};
    std::chrono::steady_clock::time_point lastWheelDownTime{std::chrono::steady_clock::time_point::min()};

    // Device grabbing
    bool grabDevices = false;
    
    // X11 hotkey monitor (separate component)
    #ifdef __linux__
    std::unique_ptr<X11HotkeyMonitor> x11Monitor;
    #endif
};

} // namespace havel
