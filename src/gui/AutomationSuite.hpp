#pragma once

#include "gui/ScreenshotManager.hpp"
#include "gui/BrightnessPanel.hpp"
#include "core/automation/PixelAutomation.hpp"
#include <QObject>
#include <QSystemTrayIcon>
#include <QMainWindow>
#include <memory>
#include "SettingsWindow.hpp"
namespace havel {
class SettingsWindow;
class ClipboardManager;  // Forward declare - lazy init
class AutomationSuite : public QObject {
    Q_OBJECT

public:
    static AutomationSuite* Instance(IO* io = nullptr);
    AutomationSuite(const AutomationSuite&) = delete;
    AutomationSuite& operator=(const AutomationSuite&) = delete;

    // Lazy initialization - only creates GUI if QApplication exists
    ClipboardManager* getClipboardManager();
    ScreenshotManager* getScreenshotManager() const { return screenshotMgr; }
    BrightnessPanel* getBrightnessManager() const { return brightnessMgr; }
    PixelAutomation* getPixelAutomation() const { return pixelAutomation.get(); }

    void setIO(IO* io) { this->io = io; }

    // Show settings window
    void showSettings();
    void hideSettings();

private:
    explicit AutomationSuite(IO* io, QObject *parent = nullptr);
    IO* io = nullptr;
    static AutomationSuite* s_instance;

    // Lazy-initialized GUI components
    ClipboardManager* clipboardMgr = nullptr;  // Lazy init
    ScreenshotManager* screenshotMgr;
    BrightnessPanel* brightnessMgr;
    std::unique_ptr<PixelAutomation> pixelAutomation;
    QSystemTrayIcon* trayIcon;
    QMenu* trayMenu;

    std::unique_ptr<SettingsWindow> settingsWindow;
    bool tray = false;
};
}