#pragma once

#include "types.hpp"
#include <QSystemTrayIcon>
#include <QMainWindow> // Added for QWindow (QMainWindow) definition

#include "core/BrightnessManager.hpp"
#include "qt.hpp"
namespace havel {

class BrightnessPanel : public ::QMainWindow {
    Q_OBJECT

public:
    explicit BrightnessPanel(QWidget *parent = nullptr);
    BrightnessPanel(const BrightnessPanel&) = delete;
    BrightnessPanel& operator=(const BrightnessPanel&) = delete;
    BrightnessPanel(BrightnessPanel&&) = delete;
    BrightnessPanel& operator=(BrightnessPanel&&) = delete;

public slots:
    void setBrightness(int value);
    void scheduleAdjustment();

private:
    void setupUI();

    ::QSlider* brightnessSlider;
    ::QLabel* percentageLabel;
    QSystemTrayIcon* trayIcon;
    QTimer* scheduleTimer;
    BrightnessManager brightnessManager;
    bool tray = false;
};

} // namespace havel
