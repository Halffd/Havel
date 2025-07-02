#include "BrightnessManager.hpp"
#include <QVBoxLayout>
#include <QFile>
#include <QTime>
#include <QShortcut>

havel::BrightnessManager::BrightnessManager(QWidget *parent) : Window(parent) {
    setupUI();

    scheduleTimer = new QTimer(this);
    connect(scheduleTimer, &QTimer::timeout, this, &BrightnessManager::scheduleAdjustment);
    scheduleTimer->start(60000); // Check every minute

    trayIcon = new QSystemTrayIcon(this);
    trayIcon->setIcon(QIcon::fromTheme("display-brightness"));
    trayIcon->show();

    auto brightnessUp = new QShortcut(QKeySequence("Ctrl+Alt+Up"), this);
    auto brightnessDown = new QShortcut(QKeySequence("Ctrl+Alt+Down"), this);

    connect(brightnessUp, &QShortcut::activated, [this]() {
        int current = brightnessSlider->value();
        setBrightness(qMin(100, current + 10));
    });

    connect(brightnessDown, &QShortcut::activated, [this]() {
        int current = brightnessSlider->value();
        setBrightness(qMax(0, current - 10));
    });
}

void havel::BrightnessManager::setupUI() {
    setWindowTitle("Brightness Manager");
    setMinimumSize(300, 100);

    auto mainLayout = new QVBoxLayout(this);
    brightnessSlider = new Slider(Qt::Horizontal, this);
    brightnessSlider->setRange(0, 100);
    mainLayout->addWidget(brightnessSlider);

    percentageLabel = new Label("100%", this);
    percentageLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(percentageLabel);

    connect(brightnessSlider, &Slider::valueChanged, this, &BrightnessManager::setBrightness);
}

void havel::BrightnessManager::setBrightness(int value) {
#ifdef Q_OS_LINUX
    QFile backlightFile("/sys/class/backlight/intel_backlight/brightness");
    if (backlightFile.open(QIODevice::WriteOnly)) {
        backlightFile.write(QString::number(value).toUtf8());
    }
#endif

    percentageLabel->setText(QString("%1%").arg(value));
    trayIcon->setToolTip(QString("Brightness: %1%").arg(value));
    brightnessSlider->setValue(value);
}

void havel::BrightnessManager::scheduleAdjustment() {
    auto now = QTime::currentTime();

    if (now.hour() >= 22 || now.hour() <= 6) {
        setBrightness(30); // Night mode
    } else if (now.hour() >= 6 && now.hour() <= 8) {
        setBrightness(70); // Morning
    } else {
        setBrightness(100); // Day
    }
}
