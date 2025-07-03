#pragma once

#include <QSystemTrayIcon>
#include <QTimer>
#include <QVBoxLayout>
#include <QFile>
#include <QTime>
#include <QShortcut>
#include "qt.hpp"
#include "types.hpp"

namespace havel {

class BrightnessManager : public havel::QWindow {
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
