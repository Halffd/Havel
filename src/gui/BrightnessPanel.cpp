#include "BrightnessPanel.hpp"

#include <QShortcut>

havel::BrightnessPanel::BrightnessPanel(QWidget *parent) : QWindow(parent) {
    setupUI();

    //scheduleTimer = new QTimer(this);
    //connect(scheduleTimer, &QTimer::timeout, this, &BrightnessPanel::scheduleAdjustment);
    //scheduleTimer->start(60000); // Check every minute

    trayIcon = new QSystemTrayIcon(this);
    trayIcon->setIcon(QIcon::fromTheme("display-brightness"));
    trayIcon->show();

    auto brightnessUp = new QShortcut(QKeySequence("Ctrl+Alt+Up"), this);
    auto brightnessDown = new QShortcut(QKeySequence("Ctrl+Alt+Down"), this);

    connect(brightnessUp, &QShortcut::activated, [this]() {
        brightnessManager.increaseBrightness(1);
    });

    connect(brightnessDown, &QShortcut::activated, [this]() {
        brightnessManager.decreaseBrightness(1);
    });
}

void havel::BrightnessPanel::setupUI() {
    setWindowTitle("Brightness Manager");
    setMinimumSize(300, 100);

    auto mainLayout = new QVBoxLayout(this);
    brightnessSlider = new Slider(Qt::Horizontal, this);
    brightnessSlider->setRange(0, 100);
    mainLayout->addWidget(brightnessSlider);

    percentageLabel = new Label("100%", this);
    percentageLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(percentageLabel);

    connect(brightnessSlider, &Slider::valueChanged, this, &BrightnessPanel::setBrightness);
}

void havel::BrightnessPanel::setBrightness(int value) {
    double brightness = value / 100.0;
    brightnessManager.setBrightness(brightness);
    percentageLabel->setText(QString("%1%").arg(value));
    trayIcon->setToolTip(QString("Brightness: %1%").arg(value));
    brightnessSlider->setValue(value);
}

void havel::BrightnessPanel::scheduleAdjustment() {


    auto now = QTime::currentTime();

    if (now.hour() >= 22 || now.hour() <= 6) {
        setBrightness(30); // This will call the method above
    } else if (now.hour() >= 6 && now.hour() <= 8) {
        setBrightness(70);
    } else {
        setBrightness(100);
    }
}
