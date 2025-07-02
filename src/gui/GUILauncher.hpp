#pragma once

#include "types.hpp"
#include "SystemMonitor.hpp"
#include "FileAutomator.hpp"
#include "ScriptRunner.hpp"

class GUILauncher : public havel::Window {
    Q_OBJECT

public:
    GUILauncher(QWidget* parent = nullptr);

private slots:
    void showSystemMonitor();
    void showFileAutomator();
    void showScriptRunner();

private:
    void setupUI();

    SystemMonitor* systemMonitor;
    FileAutomator* fileAutomator;
    ScriptRunner* scriptRunner;
};
