/*
 * HostContext.cpp
 *
 * Host context implementation.
 */
#include "HostContext.hpp"
#include "window/WindowMonitor.hpp"
#include "core/HotkeyManager.hpp"

namespace havel {

void HostContext::clear() {
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

} // namespace havel
