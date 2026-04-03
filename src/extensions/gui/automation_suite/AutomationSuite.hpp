#pragma once
#include "qt.hpp"

#include "core/automation/PixelAutomation.hpp"
// #include <QObject>
// #include <QSystemTrayIcon>
// #include <QMainWindow>
#include <memory>
#include "extensions/gui/common/SettingsWindow.hpp"
namespace havel {
class IO;  // Forward declare IO
class SettingsWindow;
class ClipboardManager;  // Forward declare - lazy init
class ScreenshotManager;  // Forward declare - lazy init
class BrightnessPanel;  // Forward declare - lazy init
class AutomationSuite : public QObject {
    Q_OBJECT

public:
    static AutomationSuite* Instance(IO* io = nullptr);
    AutomationSuite(const AutomationSuite&) = delete;
    AutomationSuite& operator=(const AutomationSuite&) = delete;

    // Lazy initialization - only creates GUI if QApplication exists
    ClipboardManager* getClipboardManager();
    ScreenshotManager* getScreenshotManager();
    BrightnessPanel* getBrightnessManager();
    PixelAutomation* getPixelAutomation() const { return pixelAutomation.get(); }

    void setIO(IO* io) { this->io = io; }

    // Show settings window
    void showSettings();
    void hideSettings();
    
    // Ensure tray icon is created (call only in GUI mode)
    void ensureTrayIcon();

private:
    explicit AutomationSuite(IO* io, QObject *parent = nullptr);
    IO* io = nullptr;
    static AutomationSuite* s_instance;

    // ALL lazy-initialized GUI components
    ClipboardManager* clipboardMgr = nullptr;
    ScreenshotManager* screenshotMgr = nullptr;
    BrightnessPanel* brightnessMgr = nullptr;
    std::unique_ptr<PixelAutomation> pixelAutomation;
    QSystemTrayIcon* trayIcon = nullptr;  // Lazy
    QMenu* trayMenu = nullptr;  // Lazy

    std::unique_ptr<SettingsWindow> settingsWindow;
    bool tray = false;
};
}