#include "AutomationSuite.hpp"
#include <QApplication>

havel::AutomationSuite::AutomationSuite(QObject *parent) : QObject(parent) {
    clipboardMgr = new ClipboardManager();
    screenshotMgr = new ScreenshotManager();
    brightnessMgr = new BrightnessManager();

    trayIcon = new QSystemTrayIcon(this);
    trayIcon->setIcon(QIcon::fromTheme("applications-utilities"));
    trayIcon->show();

    trayMenu = new Menu();
    trayMenu->addAction("Clipboard History", [this]() { clipboardMgr->show(); });
    trayMenu->addAction("Screenshots", [this]() { screenshotMgr->show(); });
    trayMenu->addAction("Brightness", [this]() { brightnessMgr->show(); });
    trayMenu->addSeparator();
    trayMenu->addAction("Quit", QApplication::quit);

    trayIcon->setContextMenu(trayMenu);
}
