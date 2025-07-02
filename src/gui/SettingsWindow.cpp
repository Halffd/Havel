#include "SettingsWindow.hpp"
#include <QLabel>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QHBoxLayout>
#include <QCloseEvent>
#include <QLineEdit>

using namespace havel;
// SettingsWindow implementation
SettingsWindow::SettingsWindow(AutomationSuite* suite, QWidget* parent)
    : QMainWindow(parent), automationSuite(suite) {
    setWindowTitle("HvC Settings");
    setMinimumSize(600, 400);
    setupUI();
}

void SettingsWindow::setupUI() {
    auto* centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    
    auto* mainLayout = new QVBoxLayout(centralWidget);
    
    // Title
    auto* titleLabel = new QLabel("HvC - Havel Control Settings", this);
    titleLabel->setStyleSheet("font-size: 18px; font-weight: bold; margin: 10px;");
    titleLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(titleLabel);
    
    // Component groups
    auto* componentsGroup = new QGroupBox("Components", this);
    auto* componentsLayout = new QVBoxLayout(componentsGroup);
    
    auto* clipboardBtn = new QPushButton("Clipboard Manager", this);
    connect(clipboardBtn, &QPushButton::clicked, [this]() {
        automationSuite->getClipboardManager()->show();
    });
    componentsLayout->addWidget(clipboardBtn);
    
    auto* screenshotBtn = new QPushButton("Screenshot Manager", this);
    connect(screenshotBtn, &QPushButton::clicked, [this]() {
        automationSuite->getScreenshotManager()->show();
    });
    componentsLayout->addWidget(screenshotBtn);
    
    auto* brightnessBtn = new QPushButton("Brightness Manager", this);
    connect(brightnessBtn, &QPushButton::clicked, [this]() {
        automationSuite->getBrightnessManager()->show();
    });
    componentsLayout->addWidget(brightnessBtn);
    
    mainLayout->addWidget(componentsGroup);
    
    // Spacer
    mainLayout->addStretch();
    // Show Conffigs
    auto* configGroup = new QGroupBox("Configuration", this);
    auto* configLayout = new QVBoxLayout(configGroup);
    std::vector<std::string> configs = havel::Configs::Get().GetConfigs();
    for (auto& config : configs) {
        QString configName = QString::fromStdString(config);
        QString configKey = configName.section("=", 0, 0);
        QString configValue = configName.section("=", 1, 1);
        QLabel* configLabel = new QLabel(configKey, this);
        configLayout->addWidget(configLabel);
        // Edit config value
        QLineEdit* configEdit = new QLineEdit(configValue, this);
        configLayout->addWidget(configEdit);
        // Event handler
        connect(configEdit, &QLineEdit::textChanged, [configKey](const QString& text) {
            havel::Configs::Get().Set(configKey.toStdString(), text.toStdString());
            havel::Configs::Get().Save();
        });
    }
    mainLayout->addWidget(configGroup);
    // Close button
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    
    auto* closeBtn = new QPushButton("Close", this);
    connect(closeBtn, &QPushButton::clicked, this, &QWidget::close);
    buttonLayout->addWidget(closeBtn);
    
    mainLayout->addLayout(buttonLayout);
}

void SettingsWindow::closeEvent(QCloseEvent* event) {
    // Hide instead of closing completely
    hide();
    event->ignore();
}
