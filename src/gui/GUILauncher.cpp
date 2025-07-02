#include "GUILauncher.hpp"

GUILauncher::GUILauncher(QWidget* parent) : havel::Window(parent) {
    setupUI();
    systemMonitor = nullptr;
    fileAutomator = nullptr;
    scriptRunner = nullptr;
}

void GUILauncher::setupUI() {
    setWindowTitle("Havel GUI Launcher");
    resize(300, 200);

    havel::Widget* centralWidget = new havel::Widget(this);
    setCentralWidget(centralWidget);

    havel::Layout* layout = new havel::Layout(centralWidget);

    havel::Button* sysMonButton = new havel::Button("System Monitor", this);
    connect(sysMonButton, &havel::Button::clicked, this, &GUILauncher::showSystemMonitor);

    havel::Button* fileAutoButton = new havel::Button("File Automator", this);
    connect(fileAutoButton, &havel::Button::clicked, this, &GUILauncher::showFileAutomator);

    havel::Button* scriptRunButton = new havel::Button("Script Runner", this);
    connect(scriptRunButton, &havel::Button::clicked, this, &GUILauncher::showScriptRunner);

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
