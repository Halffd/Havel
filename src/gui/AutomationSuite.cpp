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

AutomationSuite* AutomationSuite::Instance() {
    if (!s_instance) {
        s_instance = new AutomationSuite();
    }
    return s_instance;
}

AutomationSuite::AutomationSuite(QObject *parent) : QObject(parent) {
    clipboardMgr = new ClipboardManager();
    screenshotMgr = new ScreenshotManager();
    brightnessMgr = new BrightnessManager();

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