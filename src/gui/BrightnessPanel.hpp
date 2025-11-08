#pragma once

#include "types.hpp"
#include <QSystemTrayIcon>

#include "core/BrightnessManager.hpp"
#include "qt.hpp"
namespace havel {

class BrightnessPanel : public QWindow {
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

    Slider* brightnessSlider;
    Label* percentageLabel;
    QSystemTrayIcon* trayIcon;
    QTimer* scheduleTimer;
    BrightnessManager brightnessManager;
};

} // namespace havel
