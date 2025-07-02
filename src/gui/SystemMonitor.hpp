#pragma once

#include "types.hpp"
#include <QTimer>
#include <QtCharts/QChart>
#include <QtCharts/QLineSeries>
#include <QtCharts/QChartView>
class SystemMonitor : public havel::QWindow {
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

    havel::ProgressBar* cpuBar;
    havel::ProgressBar* memBar;
    havel::Label* uptimeLabel;
    havel::Label* cpuLabel;
    havel::Label* memLabel;
    havel::Label* netLabel;
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

    havel::TreeWidget* processTree;
};
