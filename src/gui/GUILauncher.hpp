#pragma once

#include "SystemMonitor.hpp"
#include "FileAutomator.hpp"
#include "ScriptRunner.hpp"
#include <QMainWindow>
#include <QWidget>
#include <QPushButton>
#include <QVBoxLayout>

#undef Window
#undef None

class GUILauncher : public QMainWindow {
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
