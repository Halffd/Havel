#include "GUILauncher.hpp"
#include <QMainWindow>
#include <QWidget>
#include <QPushButton>
#include <QVBoxLayout>

GUILauncher::GUILauncher(QWidget* parent) : QMainWindow(parent) {
    setupUI();
    systemMonitor = nullptr;
    fileAutomator = nullptr;
    scriptRunner = nullptr;
}

void GUILauncher::setupUI() {
    setWindowTitle("Havel GUI Launcher");
    resize(300, 200);

    QWidget* centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    QVBoxLayout* layout = new QVBoxLayout(centralWidget);

    QPushButton* sysMonButton = new QPushButton("System Monitor", this);
    connect(sysMonButton, &QPushButton::clicked, this, &GUILauncher::showSystemMonitor);

    QPushButton* fileAutoButton = new QPushButton("File Automator", this);
    connect(fileAutoButton, &QPushButton::clicked, this, &GUILauncher::showFileAutomator);

    QPushButton* scriptRunButton = new QPushButton("Script Runner", this);
    connect(scriptRunButton, &QPushButton::clicked, this, &GUILauncher::showScriptRunner);

    layout->addWidget(sysMonButton);
    layout->addWidget(fileAutoButton);
    layout->addWidget(scriptRunButton);
}

void GUILauncher::showSystemMonitor() {
    if (!systemMonitor) {
        systemMonitor = new SystemMonitor();
    }
    systemMonitor->show();
}

void GUILauncher::showFileAutomator() {
    if (!fileAutomator) {
        fileAutomator = new FileAutomator();
    }
    fileAutomator->show();
}

void GUILauncher::showScriptRunner() {
    if (!scriptRunner) {
        scriptRunner = new ScriptRunner();
    }
    scriptRunner->show();
}
