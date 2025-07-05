#pragma once

#include "gui/ClipboardManager.hpp"
#include "gui/ScreenshotManager.hpp"
#include "gui/BrightnessPanel.hpp"
#include <QObject>
#include <QSystemTrayIcon>
#include <QMainWindow>
#include <memory>
#include "SettingsWindow.hpp"
namespace havel {
class SettingsWindow;
class AutomationSuite : public QObject {
    Q_OBJECT

public:
    static AutomationSuite* Instance();
    AutomationSuite(const AutomationSuite&) = delete;
    AutomationSuite& operator=(const AutomationSuite&) = delete;

    ClipboardManager* getClipboardManager() const { return clipboardMgr; }
    ScreenshotManager* getScreenshotManager() const { return screenshotMgr; }
    BrightnessPanel* getBrightnessManager() const { return brightnessMgr; }
    
    // Show settings window
    void showSettings();
    void hideSettings();

private:
    explicit AutomationSuite(QObject *parent = nullptr);
    static AutomationSuite* s_instance;

    ClipboardManager* clipboardMgr;
    ScreenshotManager* screenshotMgr;
    BrightnessPanel* brightnessMgr;
    QSystemTrayIcon* trayIcon;
    QMenu* trayMenu;
    
    std::unique_ptr<SettingsWindow> settingsWindow;
};
}