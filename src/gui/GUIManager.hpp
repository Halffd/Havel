#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <QDialog>
#include <QMenu>
#include <QWidget>
#include "window/WindowManager.hpp"

namespace havel {

/**
 * @brief GUIManager provides high-level GUI functionality for dialogs, menus, and window effects
 * 
 * Wraps Qt functionality for:
 * - Interactive menus (gui.menu)
 * - Input dialogs (gui.input)
 * - Custom windows (gui.window)
 * - Window transparency (window.setTransparency)
 */
class GUIManager {
public:
    GUIManager(WindowManager& windowMgr);
    ~GUIManager();

    // === MENU FUNCTIONS ===
    /**
     * @brief Display a menu with options and return selected item
     * @param title Menu title
     * @param options List of menu options
     * @param multiSelect Allow multiple selections
     * @return Selected option(s), empty string if cancelled
     */
    std::string showMenu(const std::string& title, 
                        const std::vector<std::string>& options,
                        bool multiSelect = false);
    
    /**
     * @brief Display a context menu at cursor position
     * @param options Menu options
     * @return Selected option, empty if cancelled
     */
    std::string showContextMenu(const std::vector<std::string>& options);

    // === INPUT DIALOGS ===
    /**
     * @brief Show text input dialog
     * @param title Dialog title
     * @param prompt Prompt text
     * @param defaultValue Default input value
     * @return User input, empty if cancelled
     */
    std::string showInputDialog(const std::string& title,
                               const std::string& prompt = "",
                               const std::string& defaultValue = "");
    
    /**
     * @brief Show password input dialog (masked input)
     * @param title Dialog title
     * @param prompt Prompt text
     * @return Password input, empty if cancelled
     */
    std::string showPasswordDialog(const std::string& title,
                                   const std::string& prompt = "");
    
    /**
     * @brief Show number input dialog
     * @param title Dialog title
     * @param prompt Prompt text
     * @param defaultValue Default number
     * @param min Minimum value
     * @param max Maximum value
     * @param step Step increment
     * @return Selected number
     */
    double showNumberDialog(const std::string& title,
                           const std::string& prompt = "",
                           double defaultValue = 0.0,
                           double min = -1000000.0,
                           double max = 1000000.0,
                           double step = 1.0);

    // === CUSTOM WINDOWS ===
    /**
     * @brief Create a custom window with content
     * @param title Window title
     * @param content HTML or plain text content
     * @param width Window width
     * @param height Window height
     * @return Window handle/ID
     */
    uint64_t createWindow(const std::string& title,
                         const std::string& content,
                         int width = 400,
                         int height = 300);
    
    /**
     * @brief Close a custom window
     * @param windowId Window handle from createWindow
     */
    void closeWindow(uint64_t windowId);
    
    /**
     * @brief Update window content
     * @param windowId Window handle
     * @param content New content
     */
    void updateWindowContent(uint64_t windowId, const std::string& content);

    // === NOTIFICATION FUNCTIONS ===
    /**
     * @brief Show a notification bubble
     * @param title Notification title
     * @param message Notification message
     * @param icon Icon type: "info", "warning", "error", "success"
     * @param durationMs Duration in milliseconds (0 = default)
     */
    void showNotification(const std::string& title,
                         const std::string& message,
                         const std::string& icon = "info",
                         int durationMs = 0);

    // === WINDOW TRANSPARENCY ===
    /**
     * @brief Set transparency for active window
     * @param opacity Opacity level 0.0 (transparent) to 1.0 (opaque)
     * @return true if successful
     */
    bool setActiveWindowTransparency(double opacity);
    
    /**
     * @brief Set transparency for window by ID
     * @param windowId Window ID
     * @param opacity Opacity level 0.0 to 1.0
     * @return true if successful
     */
    bool setWindowTransparency(uint64_t windowId, double opacity);
    
    /**
     * @brief Set transparency for window by title
     * @param title Window title (partial match)
     * @param opacity Opacity level 0.0 to 1.0
     * @return true if successful
     */
    bool setWindowTransparencyByTitle(const std::string& title, double opacity);

    // === DIALOG FUNCTIONS ===
    /**
     * @brief Show confirmation dialog (Yes/No)
     * @param title Dialog title
     * @param message Message text
     * @return true if Yes, false if No
     */
    bool showConfirmDialog(const std::string& title, const std::string& message);
    
    /**
     * @brief Show file picker dialog
     * @param title Dialog title
     * @param startDir Starting directory
     * @param filter File filter (e.g., "*.txt")
     * @param save true for save dialog, false for open
     * @return Selected file path, empty if cancelled
     */
    std::string showFileDialog(const std::string& title,
                              const std::string& startDir = "",
                              const std::string& filter = "",
                              bool save = false);
    
    /**
     * @brief Show directory picker dialog
     * @param title Dialog title
     * @param startDir Starting directory
     * @return Selected directory path, empty if cancelled
     */
    std::string showDirectoryDialog(const std::string& title,
                                   const std::string& startDir = "");

    // === COLOR PICKER ===
    /**
     * @brief Show color picker dialog
     * @param title Dialog title
     * @param defaultColor Default color in hex format (#RRGGBB)
     * @return Selected color in hex format, empty if cancelled
     */
    std::string showColorPicker(const std::string& title,
                               const std::string& defaultColor = "#ffffff");

private:
    WindowManager& windowManager;
    
    // Track custom windows
    std::unordered_map<uint64_t, QWidget*> customWindows;
    uint64_t nextWindowId = 1;
    
    // Helper functions
    QWidget* getQWidgetForWindow(uint64_t windowId);
    void ensureQApplication();
};

} // namespace havel
