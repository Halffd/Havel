#pragma once
#include "WindowMonitor.hpp"
#include <unordered_map>
#include <functional>
#include <atomic>
#include <thread>
#include <chrono>
#include <mutex>

namespace havel {

// Mode management
class ModeManager {
public:
    using ModeCallback = std::function<void(const std::string& oldMode, const std::string& newMode)>;
    
    void SetMode(const std::string& mode);
    std::string GetMode() const;
    void SetModeChangeCallback(ModeCallback callback);
    
private:
    std::string currentMode = "default";
    ModeCallback modeChangeCallback;
    mutable std::mutex modeMutex;
};

// Key event monitoring
class KeyEventListener {
public:
    using KeyCallback = std::function<void(int keyCode, bool isKeyDown, const std::string& keyName)>;
    
    void SetKeyDownCallback(KeyCallback callback);
    void SetKeyUpCallback(KeyCallback callback);
    void StartListening();
    void StopListening();
    
private:
    KeyCallback keyDownCallback;
    KeyCallback keyUpCallback;
    std::atomic<bool> listening{false};
    std::unique_ptr<std::thread> listenerThread;
    mutable std::mutex callbackMutex;
};

// Custom update loop manager
class UpdateLoopManager {
public:
    using UpdateCallback = std::function<void()>;
    
    int StartUpdateLoop(UpdateCallback callback, int intervalMs = 16); // ~60 FPS
    void StopUpdateLoop(int loopId);
    void SetUpdateFunction(UpdateCallback callback);
    
private:
    std::unordered_map<int, std::unique_ptr<std::thread>> updateLoops;
    std::unordered_map<int, std::atomic<bool>> loopFlags;
    std::atomic<int> nextLoopId{1};
    UpdateCallback updateFunction;
    mutable std::mutex loopMutex;
};

// Enhanced Event Monitor that combines all features
class EventMonitor {
public:
    explicit EventMonitor(std::chrono::milliseconds pollInterval = std::chrono::milliseconds(16));
    ~EventMonitor();
    
    // Mode management
    void SetMode(const std::string& mode);
    std::string GetMode() const;
    void OnModeChange(std::function<void(const std::string& oldMode, const std::string& newMode)> callback);
    
    // Key event monitoring
    void OnKeyDown(std::function<void(int keyCode, const std::string& keyName)> callback);
    void OnKeyUp(std::function<void(int keyCode, const std::string& keyName)> callback);
    void StartKeyListening();
    void StopKeyListening();
    
    // Custom update loops
    int StartUpdateLoop(std::function<void()> callback, int intervalMs = 16);
    void StopUpdateLoop(int loopId);
    
    // Window monitoring (inherited from WindowMonitor)
    void Start();
    void Stop();
    bool IsRunning() const;
    
    // Get access to underlying components
    ModeManager& GetModeManager() { return modeManager; }
    KeyEventListener& GetKeyListener() { return keyListener; }
    UpdateLoopManager& GetUpdateManager() { return updateManager; }
    
private:
    WindowMonitor windowMonitor;
    ModeManager modeManager;
    KeyEventListener keyListener;
    UpdateLoopManager updateManager;
};

} // namespace havel
