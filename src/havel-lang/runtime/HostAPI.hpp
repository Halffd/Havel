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
            class Configs& config);
    
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
    
private:
    std::shared_ptr<class IO> io;
    std::shared_ptr<class HotkeyManager> hotkeyManager;
    class Configs& config;
};

} // namespace havel
