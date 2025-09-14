#include "AutomationSuite.hpp"
#include <QApplication>
#include <QMenu>
#include <QIcon>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QGroupBox>
namespace havel {

AutomationSuite* AutomationSuite::s_instance = nullptr;

AutomationSuite* AutomationSuite::Instance(IO* io) {
    if (!s_instance) {
        if (!io) {
            qWarning() << "Warning: AutomationSuite::Instance called with null IO";
        }
        s_instance = new AutomationSuite(io);
    } else if (io && !s_instance->io) {
        s_instance->io = io;
        // Reinitialize clipboard manager with the new IO instance
        if (s_instance->clipboardMgr) {
            delete s_instance->clipboardMgr;
            s_instance->clipboardMgr = new ClipboardManager(io);
        }
    }
    return s_instance;
}

AutomationSuite::AutomationSuite(IO* io, QObject *parent) 
    : QObject(parent), io(io) {
    clipboardMgr = new ClipboardManager(io);
    screenshotMgr = new ScreenshotManager();
    brightnessMgr = new BrightnessPanel();

    trayIcon = new QSystemTrayIcon(this);
    trayIcon->setIcon(QIcon::fromTheme("applications-utilities"));
    trayIcon->show();

    trayMenu = new QMenu();
    trayMenu->addAction("Clipboard History", [this]() { clipboardMgr->show(); });
    trayMenu->addAction("Screenshots", [this]() { screenshotMgr->show(); });
    trayMenu->addAction("Brightness", [this]() { brightnessMgr->show(); });
    trayMenu->addSeparator();
    trayMenu->addAction("Settings", [this]() { showSettings(); });
    trayMenu->addSeparator();
    trayMenu->addAction("Quit", QApplication::quit);

    trayIcon->setContextMenu(trayMenu);
}

void AutomationSuite::showSettings() {
    if (!settingsWindow) {
        settingsWindow = std::make_unique<SettingsWindow>(this);
    }
    
    settingsWindow->show();
    settingsWindow->raise();
    settingsWindow->activateWindow();
}

void AutomationSuite::hideSettings() {
    if (settingsWindow) {
        settingsWindow->hide();
    }
}
} // namespace havel