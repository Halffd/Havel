#pragma once

#include "types.hpp"
#include <QSystemTrayIcon>

namespace havel {

class BrightnessManager : public Window {
    Q_OBJECT

public:
    explicit BrightnessManager(QWidget *parent = nullptr);

public slots:
    void setBrightness(int value);
    void scheduleAdjustment();

private:
    void setupUI();

    Slider* brightnessSlider;
    Label* percentageLabel;
    QSystemTrayIcon* trayIcon;
    QTimer* scheduleTimer;
};

} // namespace havel
