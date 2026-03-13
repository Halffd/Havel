// HostAPI.hpp
// Abstract interface for host operations
// Interpreter depends on this interface, not concrete implementations

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
 * Interpreter depends on this interface instead of concrete managers.
 * This allows:
 * - Testing language core without GUI/hotkey infrastructure
 * - Swapping host implementations
 * - Clear separation between language and OS integration
 */
class IHostAPI {
public:
    virtual ~IHostAPI() = default;
    
    // ========================================================================
    // Window Operations
    // ========================================================================
    
    virtual std::string GetActiveWindowTitle() = 0;
    virtual std::string GetActiveWindowClass() = 0;
    virtual pID GetActiveWindowPID() = 0;
    virtual std::string GetActiveWindowProcess() = 0;
    
    // ========================================================================
    // Hotkey Operations
    // ========================================================================
    
    virtual bool RegisterHotkey(const std::string& key, std::function<void()> callback) = 0;
    virtual bool UnregisterHotkey(int id) = 0;
    virtual void SuspendHotkeys(bool suspend) = 0;
    virtual bool AreHotkeysSuspended() const = 0;
    
    // ========================================================================
    // IO Operations
    // ========================================================================
    
    virtual void SendKeys(const std::string& keys) = 0;
    virtual void SendKey(const std::string& key, bool press) = 0;
    virtual void MouseMove(int x, int y) = 0;
    virtual void MouseClick(int button) = 0;
    virtual void Scroll(int dy, int dx) = 0;
    
    // ========================================================================
    // Clipboard Operations
    // ========================================================================
    
    virtual std::string GetClipboardText() = 0;
    virtual void SetClipboardText(const std::string& text) = 0;
    
    // ========================================================================
    // Config Operations
    // ========================================================================
    
    template<typename T>
    T GetConfig(const std::string& key, const T& defaultVal) {
        return GetConfigString(key, std::to_string(defaultVal));
    }
    
    virtual std::string GetConfigString(const std::string& key, const std::string& defaultVal) = 0;
    virtual void SetConfig(const std::string& key, const std::string& value) = 0;
    
    // ========================================================================
    // Mode Operations
    // ========================================================================
    
    virtual std::string GetCurrentMode() const = 0;
    virtual void SetCurrentMode(const std::string& mode) = 0;
    
    // ========================================================================
    // Window Group Operations
    // ========================================================================
    
    virtual bool IsWindowInGroup(const std::string& windowTitle, const std::string& groupName) = 0;
    virtual std::vector<std::string> GetGroupNames() = 0;
    virtual std::vector<std::string> GetGroupWindows(const std::string& groupName) = 0;
};

/**
 * Null implementation for testing
 */
class NullHostAPI : public IHostAPI {
public:
    std::string GetActiveWindowTitle() override { return ""; }
    std::string GetActiveWindowClass() override { return ""; }
    pID GetActiveWindowPID() override { return 0; }
    std::string GetActiveWindowProcess() override { return ""; }
    
    bool RegisterHotkey(const std::string&, std::function<void()>) override { return false; }
    bool UnregisterHotkey(int) override { return false; }
    void SuspendHotkeys(bool) override {}
    bool AreHotkeysSuspended() const override { return false; }
    
    void SendKeys(const std::string&) override {}
    void SendKey(const std::string&, bool) override {}
    void MouseMove(int, int) override {}
    void MouseClick(int) override {}
    void Scroll(int, int) override {}
    
    std::string GetClipboardText() override { return ""; }
    void SetClipboardText(const std::string&) override {}
    
    std::string GetConfigString(const std::string&, const std::string& defaultVal) override {
        return defaultVal;
    }
    void SetConfig(const std::string&, const std::string&) override {}
    
    std::string GetCurrentMode() const override { return "default"; }
    void SetCurrentMode(const std::string&) override {}
    
    bool IsWindowInGroup(const std::string&, const std::string&) override { return false; }
    std::vector<std::string> GetGroupNames() override { return {}; }
    std::vector<std::string> GetGroupWindows(const std::string&) override { return {}; }
};

} // namespace havel
