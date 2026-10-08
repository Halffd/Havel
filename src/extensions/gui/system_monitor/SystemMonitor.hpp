#pragma once
#include "qt.hpp"

#undef Window
#undef None

// #include <QTimer>
// #include <QWidget>
// #include <QProgressBar>
// #include <QLabel>
// #include <QTreeWidget>
// #include <QMainWindow>
// #include <QVBoxLayout>
#include "types.hpp"

class SystemMonitor : public QMainWindow {
    Q_OBJECT

public:
    SystemMonitor(QWidget* parent = nullptr);

private slots:
    void updateData();

private:
    void setupUI();
    void getCpuUsage(long long& total, long long& idle);
    double getMemoryUsage();
    QString getUptime();
    void updateProcessList();
    void updateNetworkUsage();

    QProgressBar* cpuBar;
    QProgressBar* memBar;
    QLabel* uptimeLabel;
    QLabel* cpuLabel;
    QLabel* memLabel;
    QLabel* netLabel;
    QTimer* timer;

    long long prevTotal = 0;
    long long prevIdle = 0;
    long long prevNetBytes = 0;

    QTreeWidget* processTree;
};
