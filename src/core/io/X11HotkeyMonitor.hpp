#pragma once

#ifdef __linux__

#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <vector>
#include <X11/Xlib.h>
#include <X11/keysym.h>

namespace havel {

// Forward declaration
struct HotKey;

/**
 * X11HotkeyMonitor - Separate X11 hotkey monitoring for EventListener
 * 
 * This class provides X11-based hotkey monitoring as a fallback or alternative
 * to evdev-based input handling. It monitors X11 key events and triggers
 * registered hotkeys.
 * 
 * Features:
 * - X11 key event monitoring
 * - Modifier key handling
 * - Hotkey matching and execution
 * - Thread-safe operation
 * - Separate from evdev listener
 */
class X11HotkeyMonitor {
public:
    X11HotkeyMonitor();
    ~X11HotkeyMonitor();
    
    // Start/stop monitoring
    bool Start(Display* display);
    void Stop();
    bool IsRunning() const { return running.load(); }
    
    // Hotkey registration
    void RegisterHotkey(int id, const HotKey& hotkey);
    void UnregisterHotkey(int id);
    void ClearHotkeys();
    
    // Get registered hotkey count
    size_t GetHotkeyCount() const;
    
private:
    // Main monitoring loop
    void MonitorLoop();
    
    // Check if keysym is a modifier
    static bool IsModifierKeySym(KeySym ks);
    
    // Clean modifier mask
    static unsigned int CleanMask(unsigned int mask);
    
    std::atomic<bool> running{false};
    std::atomic<bool> shutdown{false};
    std::thread monitorThread;
    
    Display* display = nullptr;
    Window rootWindow;
    
    // Hotkey management
    mutable std::mutex hotkeyMutex;
    std::map<int, HotKey> hotkeys;
    
    // Constants
    static constexpr unsigned int RELEVANT_MODIFIERS = 
        ShiftMask | LockMask | ControlMask | Mod1Mask | Mod4Mask | Mod5Mask;
};

} // namespace havel

#endif // __linux__
