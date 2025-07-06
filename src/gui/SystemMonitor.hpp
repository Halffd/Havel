#pragma once

#undef Window
#undef None

#include <QTimer>
#include <QtCharts/QChart>
#include <QtCharts/QLineSeries>
#include <QtCharts/QChartView>
#include <QWidget>
#include <QProgressBar>
#include <QLabel>
#include <QTreeWidget>
#include <QMainWindow>
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

    QChart* cpuChart;
    QLineSeries* cpuSeries;
    QChartView* cpuChartView;

    QChart* memChart;
    QLineSeries* memSeries;
    QChartView* memChartView;

    QTreeWidget* processTree;
};
