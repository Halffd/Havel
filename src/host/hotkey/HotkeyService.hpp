/*
 * HotkeyService.hpp
 *
 * Pure C++ hotkey service - no VM, no interpreter, no HavelValue.
 * This is the business logic layer for hotkey operations.
 */
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <variant>

namespace havel { class HotkeyManager; class KeyTap; }  // Forward declarations

namespace havel::host {

/**
 * HotkeyInfo - Information about a registered hotkey
 */
struct HotkeyInfo {
    int id = 0;
    std::string key;
    std::string condition;
    bool enabled = false;
    std::string type;  // "regular" or "conditional"
};

/**
 * HotkeyService - Pure hotkey business logic
 *
 * Provides system-level hotkey operations without any language runtime coupling.
 * All methods return simple C++ types (bool, int, string, vector, etc.)
 */
class HotkeyService {
public:
    explicit HotkeyService(std::shared_ptr<havel::HotkeyManager> manager);
    ~HotkeyService() = default;

    // =========================================================================
    // Basic hotkey registration
    // =========================================================================

    /// Register a simple hotkey
    /// @param key Key combination string (e.g., "Ctrl+Alt+K")
    /// @param callback Callback to execute
    /// @param id Optional hotkey ID
    /// @return true on success
    bool registerHotkey(const std::string& key, std::function<void()> callback, int id = 0);

    /// Remove a hotkey by ID
    /// @param id Hotkey ID
    /// @return true on success
    bool removeHotkey(int id);

    /// Grab a hotkey (temporarily disable)
    /// @param id Hotkey ID
    /// @return true on success
    bool grabHotkey(int id);

    /// Ungrab a hotkey (re-enable)
    /// @param id Hotkey ID
    /// @return true on success
    bool ungrabHotkey(int id);

    /// Clear all hotkeys
    void clearAllHotkeys();

    /// Get list of all hotkeys
    /// @return vector of HotkeyInfo
    std::vector<HotkeyInfo> getHotkeyList() const;

    // =========================================================================
    // Contextual hotkeys (with conditions)
    // =========================================================================

    /// Register a contextual hotkey with string condition
    /// @param key Key combination
    /// @param condition Condition string
    /// @param trueAction Callback when condition is true
    /// @param falseAction Optional callback when condition is false
    /// @param id Optional hotkey ID
    /// @return hotkey ID on success, 0 on failure
    int addContextualHotkey(const std::string& key, const std::string& condition,
                            std::function<void()> trueAction,
                            std::function<void()> falseAction = nullptr,
                            int id = 0);

    // =========================================================================
    // Advanced KeyTap functionality
    // =========================================================================

    /// Create a KeyTap (advanced hotkey with conditions)
    /// @param keyName Key name
    /// @param onTap Callback on tap
    /// @param tapCondition Tap condition (string or function)
    /// @param comboCondition Combo condition (string or function)
    /// @param onCombo Callback on combo
    /// @param grabDown Grab on key down
    /// @param grabUp Grab on key up
    /// @return KeyTap instance wrapped in shared_ptr
    std::shared_ptr<KeyTap> createKeyTap(
        const std::string& keyName,
        std::function<void()> onTap,
        std::variant<std::string, std::function<bool()>> tapCondition,
        std::variant<std::string, std::function<bool()>> comboCondition,
        std::function<void()> onCombo,
        bool grabDown,
        bool grabUp
    );

    // =========================================================================
    // Utility operations
    // =========================================================================

    /// Toggle fake desktop overlay
    void toggleFakeDesktopOverlay();

    /// Show black overlay
    void showBlackOverlay();

    /// Print active window info
    void printActiveWindowInfo();

    /// Toggle window focus tracking
    void toggleWindowFocusTracking();

    /// Update all conditional hotkeys
    void updateAllConditionalHotkeys();

    /// Get current mode
    /// @return current mode string
    std::string getCurrentMode() const;

    /// Set current mode
    /// @param mode Mode name
    void setCurrentMode(const std::string& mode);

private:
    std::shared_ptr<havel::HotkeyManager> m_manager;  // Shared ownership
};

} // namespace havel::host
