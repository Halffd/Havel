#include "AutomationSuite.hpp"
#include "gui/ClipboardManager.hpp"
#include "core/automation/PixelAutomation.hpp"
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
        // Reinitialize clipboard manager with the new IO instance if it exists
        if (s_instance->clipboardMgr) {
            delete s_instance->clipboardMgr;
            s_instance->clipboardMgr = new ClipboardManager(io);
        }
    }
    return s_instance;
}

ClipboardManager* AutomationSuite::getClipboardManager() {
    // Lazy initialization - only create if QApplication exists
    if (!clipboardMgr) {
        if (!QApplication::instance()) {
            qWarning() << "Warning: ClipboardManager requested but no QApplication exists";
            return nullptr;
        }
        if (!io) {
            qWarning() << "Warning: ClipboardManager requested but IO is null";
            return nullptr;
        }
        clipboardMgr = new ClipboardManager(io);
    }
    return clipboardMgr;
}

AutomationSuite::AutomationSuite(IO* io, QObject *parent)
    : QObject(parent), io(io), clipboardMgr(nullptr) {
    // Don't create GUI components in constructor - lazy init instead
    screenshotMgr = new ScreenshotManager();
    brightnessMgr = new BrightnessPanel();
    pixelAutomation = std::make_unique<PixelAutomation>();

    // Tray icon can be created lazily too, but keep for now
    trayIcon = new QSystemTrayIcon(this);
    trayIcon->setIcon(QIcon::fromTheme("applications-utilities"));
    trayIcon->show();

    trayMenu = new QMenu();
    trayMenu->addAction("Clipboard History", [this]() {
        if (auto* cb = getClipboardManager()) cb->show();
    });
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