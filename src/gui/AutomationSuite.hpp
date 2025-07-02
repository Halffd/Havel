#pragma once

#include "gui/ClipboardManager.hpp"
#include "gui/ScreenshotManager.hpp"
#include "gui/BrightnessManager.hpp"
#include <QObject>
#include <QSystemTrayIcon>

namespace havel {

class AutomationSuite : public QObject {
    Q_OBJECT

public:
    explicit AutomationSuite(QObject *parent = nullptr);

private:
    ClipboardManager* clipboardMgr;
    ScreenshotManager* screenshotMgr;
    BrightnessManager* brightnessMgr;
    QSystemTrayIcon* trayIcon;
    Menu* trayMenu;
};

} // namespace havel
