// HostAPI.cpp
// Concrete implementation of split host API interfaces
// Composes subsystems rather than inheriting from IO

#include "HostAPI.hpp"
#include "core/IO.hpp"
#include "core/HotkeyManager.hpp"
#include "core/ConfigManager.hpp"
#include "window/WindowManager.hpp"
#include "core/BrightnessManager.hpp"
#include "media/AudioManager.hpp"
#include "gui/GUIManager.hpp"
#include "gui/ScreenshotManager.hpp"
#include "gui/ClipboardManager.hpp"
#include "core/automation/PixelAutomation.hpp"
#include "core/automation/AutomationManager.hpp"
#include "fs/FileManager.hpp"
#include "core/process/ProcessManager.hpp"
#include "core/io/MapManager.hpp"
#include <QGuiApplication>
#include <QClipboard>

namespace havel {

HostAPI::HostAPI(IO* io,
                 HotkeyManager* hotkeyManager,
                 Configs& config,
                 WindowManager* windowManager,
                 BrightnessManager* brightnessManager,
                 AudioManager* audioManager,
                 GUIManager* guiManager,
                 ScreenshotManager* screenshotManager,
                 ClipboardManager* clipboardManager,
                 PixelAutomation* pixelAutomation,
                 AutomationManager* automationManager,
                 FileManager* fileManager,
                 ProcessManager* processManager,
                 MapManager* mapManager)
    : io(io), hotkeyManager(hotkeyManager), config(config),
      windowManager(windowManager), brightnessManager(brightnessManager),
      audioManager(audioManager), guiManager(guiManager),
      screenshotManager(screenshotManager), clipboardManager(clipboardManager),
      pixelAutomation(pixelAutomation), automationManager(automationManager),
      fileManager(fileManager), processManager(processManager),
      mapManager(mapManager) {}

// IWindowAPI implementation
std::string HostAPI::GetActiveWindowTitle() {
    return io->GetActiveWindowTitle();
}

std::string HostAPI::GetActiveWindowClass() {
    return io->GetActiveWindowClass();
}

pID HostAPI::GetActiveWindowPID() {
    return io->GetActiveWindowPID();
}

std::string HostAPI::GetActiveWindowProcess() {
    return WindowManager::getProcessName(GetActiveWindowPID());
}

bool HostAPI::IsWindowInGroup(const std::string& windowTitle, const std::string& groupName) {
    return WindowManager::IsWindowInGroup(windowTitle.c_str(), groupName.c_str());
}

std::vector<std::string> HostAPI::GetGroupNames() {
    return WindowManager::GetGroupNames();
}

std::vector<std::string> HostAPI::GetGroupWindows(const std::string& groupName) {
    return WindowManager::GetGroupWindows(groupName.c_str());
}

// IHotkeyAPI implementation
bool HostAPI::RegisterHotkey(const std::string& key, std::function<void()> callback) {
    return io->Hotkey(key, callback);
}

bool HostAPI::UnregisterHotkey(int id) {
    return io->RemoveHotkey(id);
}

void HostAPI::SuspendHotkeys(bool suspend) {
    if (io) {
        if (suspend) {
            io->Suspend();
        }
        // Note: Resume requires an id, which we don't have here
        // For now, just don't suspend - the hotkey system handles resume internally
    }
}

bool HostAPI::AreHotkeysSuspended() const {
    return io ? io->isSuspended : false;
}

std::string HostAPI::GetCurrentMode() const {
    if (hotkeyManager) {
        return hotkeyManager->getMode();
    }
    return "default";
}

void HostAPI::SetCurrentMode(const std::string& mode) {
    if (hotkeyManager) {
        hotkeyManager->setMode(mode);
    }
}

// IIOAPI implementation
void HostAPI::SendKeys(const std::string& keys) {
    io->Send(keys.c_str());
}

void HostAPI::SendKey(const std::string& key, bool press) {
    io->SendX11Key(key, press);
}

void HostAPI::MouseMove(int x, int y) {
    io->MouseMoveTo(x, y);
}

void HostAPI::MouseClick(int button) {
    io->MouseClick(button);
}

void HostAPI::Scroll(int dy, int dx) {
    io->Scroll(dy, dx);
}

// IClipboardAPI implementation
std::string HostAPI::GetClipboardText() {
    if (QGuiApplication::instance()) {
        return QGuiApplication::clipboard()->text().toStdString();
    }
    return "";
}

void HostAPI::SetClipboardText(const std::string& text) {
    if (QGuiApplication::instance()) {
        QGuiApplication::clipboard()->setText(QString::fromStdString(text));
    }
}

// IConfigAPI implementation
std::string HostAPI::GetConfigString(const std::string& key, const std::string& defaultVal) {
    return config.Get(key, defaultVal);
}

void HostAPI::SetConfig(const std::string& key, const std::string& value) {
    config.Set(key, value, true);
}

// IHostAPI implementation
Configs& HostAPI::GetConfig() {
    return config;
}

// Manager access for modules
IO* HostAPI::GetIO() { return io; }
HotkeyManager* HostAPI::GetHotkeyManager() { return hotkeyManager; }
WindowManager* HostAPI::GetWindowManager() { return windowManager; }
BrightnessManager* HostAPI::GetBrightnessManager() { return brightnessManager; }
AudioManager* HostAPI::GetAudioManager() { return audioManager; }
GUIManager* HostAPI::GetGUIManager() { return guiManager; }
ScreenshotManager* HostAPI::GetScreenshotManager() { return screenshotManager; }
ClipboardManager* HostAPI::GetClipboardManager() { return clipboardManager; }
PixelAutomation* HostAPI::GetPixelAutomation() { return pixelAutomation; }
AutomationManager* HostAPI::GetAutomationManager() { return automationManager; }
FileManager* HostAPI::GetFileManager() { return fileManager; }
ProcessManager* HostAPI::GetProcessManager() { return processManager; }
MapManager* HostAPI::GetMapManager() { return mapManager; }

// Update manager pointers (called after managers are created)
void HostAPI::SetHotkeyManager(HotkeyManager* hm) {
    hotkeyManager = hm;
}

void HostAPI::SetIO(IO* newIo) {
    io = newIo;
}

} // namespace havel
