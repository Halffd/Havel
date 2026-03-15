/*
 * HostContext.hpp
 *
 * Host context for Havel language modules.
 *
 * This file belongs to the HOST layer, not the language runtime.
 * It provides modules with access to system managers (WindowManager, etc.)
 *
 * The language runtime (havel-lang/runtime/) should NEVER include this file.
 */
#pragma once

#include <memory>

// Forward declarations - HostContext doesn't own these
namespace havel {

class WindowManager;
class HotkeyManager;
class BrightnessManager;
class AudioManager;
class GUIManager;
class ScreenshotManager;
class ClipboardManager;
class PixelAutomation;
class AutomationManager;
class FileManager;
class ProcessManager;
class IO;
class ModeManager;
class WindowMonitor;

/**
 * HostContext - Bridge between language runtime and host system
 *
 * Modules receive this context to access system managers.
 * The interpreter never sees this - it's purely for module registration.
 */
struct HostContext {
    // Core IO - use shared_ptr to ensure IO stays alive
    std::shared_ptr<IO> io;

    // Window management
    WindowManager* windowManager = nullptr;

    // Input handling - use shared_ptr to ensure lifetime
    std::shared_ptr<HotkeyManager> hotkeyManager;

    // Mode management - use shared_ptr to ensure lifetime
    std::shared_ptr<ModeManager> modeManager;

    // Window monitoring - use shared_ptr to ensure lifetime
    std::shared_ptr<WindowMonitor> windowMonitor;

    // Display
    BrightnessManager* brightnessManager = nullptr;

    // Audio
    AudioManager* audioManager = nullptr;

    // GUI
    GUIManager* guiManager = nullptr;

    // Screenshot
    ScreenshotManager* screenshotManager = nullptr;

    // Clipboard
    ClipboardManager* clipboardManager = nullptr;

    // Automation
    PixelAutomation* pixelAutomation = nullptr;
    AutomationManager* automationManager = nullptr;

    // File/Process
    FileManager* fileManager = nullptr;
    ProcessManager* processManager = nullptr;

    // Network
    std::shared_ptr<net::NetworkManager> networkManager;

    // Clear all components (call before destruction to stop threads)
    void clear() {
        // Stop window monitor first (has a thread)
        if (windowMonitor) {
            windowMonitor->Stop();
        }
        windowMonitor.reset();
        
        // Clear hotkey manager (stops its threads)
        if (hotkeyManager) {
            hotkeyManager->cleanup();
        }
        hotkeyManager.reset();
        
        // Clear other shared resources
        modeManager.reset();
        networkManager.reset();
        io.reset();
        
        // Raw pointers don't need cleanup (owned elsewhere)
        windowManager = nullptr;
        brightnessManager = nullptr;
        audioManager = nullptr;
        guiManager = nullptr;
        screenshotManager = nullptr;
        clipboardManager = nullptr;
        pixelAutomation = nullptr;
        automationManager = nullptr;
        fileManager = nullptr;
        processManager = nullptr;
    }

    // Validation helper
    bool isValid() const {
        return io != nullptr;
    }
};

} // namespace havel
