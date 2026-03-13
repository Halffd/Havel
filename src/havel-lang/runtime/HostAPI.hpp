// HostAPI.hpp
// IHostAPI interface and HostAPI concrete implementation
// Composes subsystems rather than making IO do everything

#pragma once
#include <string>
#include <functional>
#include <vector>
#include <memory>

namespace havel {

// Forward declarations
using pID = int;

/**
 * Abstract interface for host system operations
 * 
 * This allows the language runtime to depend on an interface
 * rather than concrete implementations.
 */
class IHostAPI {
public:
    virtual ~IHostAPI() = default;
    
    // Window Operations
    virtual std::string GetActiveWindowTitle() = 0;
    virtual std::string GetActiveWindowClass() = 0;
    virtual pID GetActiveWindowPID() = 0;
    virtual std::string GetActiveWindowProcess() = 0;
    
    // Hotkey Operations
    virtual bool RegisterHotkey(const std::string& key, std::function<void()> callback) = 0;
    virtual bool UnregisterHotkey(int id) = 0;
    virtual void SuspendHotkeys(bool suspend) = 0;
    virtual bool AreHotkeysSuspended() const = 0;
    
    // IO Operations
    virtual void SendKeys(const std::string& keys) = 0;
    virtual void SendKey(const std::string& key, bool press) = 0;
    virtual void MouseMove(int x, int y) = 0;
    virtual void MouseClick(int button) = 0;
    virtual void Scroll(int dy, int dx) = 0;
    
    // Clipboard Operations
    virtual std::string GetClipboardText() = 0;
    virtual void SetClipboardText(const std::string& text) = 0;
    
    // Config Operations
    virtual std::string GetConfigString(const std::string& key, const std::string& defaultVal) = 0;
    virtual void SetConfig(const std::string& key, const std::string& value) = 0;
    
    // Mode Operations
    virtual std::string GetCurrentMode() const = 0;
    virtual void SetCurrentMode(const std::string& mode) = 0;
    
    // Window Group Operations
    virtual bool IsWindowInGroup(const std::string& windowTitle, const std::string& groupName) = 0;
    virtual std::vector<std::string> GetGroupNames() = 0;
    virtual std::vector<std::string> GetGroupWindows(const std::string& groupName) = 0;
    
    // Direct IO access for modules that need subsystem access
    virtual class IO* GetIO() = 0;
    virtual std::shared_ptr<class HotkeyManager> GetHotkeyManagerShared() = 0;
    virtual class HotkeyManager* GetHotkeyManager() = 0;
    virtual class Configs& GetConfig() = 0;
    
    // Additional managers for modules
    virtual class WindowManager* GetWindowManager() = 0;
    virtual class BrightnessManager* GetBrightnessManager() = 0;
    virtual class AudioManager* GetAudioManager() = 0;
    virtual class GUIManager* GetGUIManager() = 0;
    virtual class ScreenshotManager* GetScreenshotManager() = 0;
    virtual class ClipboardManager* GetClipboardManager() = 0;
    virtual class PixelAutomation* GetPixelAutomation() = 0;
    virtual class AutomationManager* GetAutomationManager() = 0;
    virtual class FileManager* GetFileManager() = 0;
    virtual class ProcessManager* GetProcessManager() = 0;
    virtual class MapManager* GetMapManager() = 0;
};

/**
 * HostAPI - Concrete implementation of IHostAPI
 * 
 * Composes subsystems (IO, HotkeyManager, Config) rather than
 * making IO do everything. This keeps IO focused on input/output only.
 */
class HostAPI : public IHostAPI {
public:
    HostAPI(std::shared_ptr<class IO> io,
            std::shared_ptr<class HotkeyManager> hotkeyManager,
            class Configs& config,
            class WindowManager* windowManager = nullptr,
            class BrightnessManager* brightnessManager = nullptr,
            class AudioManager* audioManager = nullptr,
            class GUIManager* guiManager = nullptr,
            class ScreenshotManager* screenshotManager = nullptr,
            class ClipboardManager* clipboardManager = nullptr,
            class PixelAutomation* pixelAutomation = nullptr,
            class AutomationManager* automationManager = nullptr,
            class FileManager* fileManager = nullptr,
            class ProcessManager* processManager = nullptr,
            class MapManager* mapManager = nullptr);

    // Window Operations
    std::string GetActiveWindowTitle() override;
    std::string GetActiveWindowClass() override;
    pID GetActiveWindowPID() override;
    std::string GetActiveWindowProcess() override;

    // Hotkey Operations
    bool RegisterHotkey(const std::string& key, std::function<void()> callback) override;
    bool UnregisterHotkey(int id) override;
    void SuspendHotkeys(bool suspend) override;
    bool AreHotkeysSuspended() const override;

    // IO Operations
    void SendKeys(const std::string& keys) override;
    void SendKey(const std::string& key, bool press) override;
    void MouseMove(int x, int y) override;
    void MouseClick(int button) override;
    void Scroll(int dy, int dx) override;

    // Clipboard Operations
    std::string GetClipboardText() override;
    void SetClipboardText(const std::string& text) override;

    // Config Operations
    std::string GetConfigString(const std::string& key, const std::string& defaultVal) override;
    void SetConfig(const std::string& key, const std::string& value) override;

    // Mode Operations
    std::string GetCurrentMode() const override;
    void SetCurrentMode(const std::string& mode) override;

    // Window Group Operations
    bool IsWindowInGroup(const std::string& windowTitle, const std::string& groupName) override;
    std::vector<std::string> GetGroupNames() override;
    std::vector<std::string> GetGroupWindows(const std::string& groupName) override;
    
    // Direct subsystem access for modules
    class IO* GetIO() override;
    std::shared_ptr<class HotkeyManager> GetHotkeyManagerShared() override;
    class HotkeyManager* GetHotkeyManager() override;
    class Configs& GetConfig() override;
    
    // Additional managers for modules
    class WindowManager* GetWindowManager() override;
    class BrightnessManager* GetBrightnessManager() override;
    class AudioManager* GetAudioManager() override;
    class GUIManager* GetGUIManager() override;
    class ScreenshotManager* GetScreenshotManager() override;
    class ClipboardManager* GetClipboardManager() override;
    class PixelAutomation* GetPixelAutomation() override;
    class AutomationManager* GetAutomationManager() override;
    class FileManager* GetFileManager() override;
    class ProcessManager* GetProcessManager() override;
    class MapManager* GetMapManager() override;

private:
    std::shared_ptr<class IO> io;
    std::shared_ptr<class HotkeyManager> hotkeyManager;
    class Configs& config;
    class WindowManager* windowManager;
    class BrightnessManager* brightnessManager;
    class AudioManager* audioManager;
    class GUIManager* guiManager;
    class ScreenshotManager* screenshotManager;
    class ClipboardManager* clipboardManager;
    class PixelAutomation* pixelAutomation;
    class AutomationManager* automationManager;
    class FileManager* fileManager;
    class ProcessManager* processManager;
    class MapManager* mapManager;
};

} // namespace havel
