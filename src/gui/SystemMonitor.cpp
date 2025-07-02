#include "SystemMonitor.hpp"
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <sys/sysinfo.h>
#include <dirent.h>
#include <QtCharts/QAbstractAxis>
#include <QtCharts/QValueAxis>

SystemMonitor::SystemMonitor(QWidget* parent) : havel::Window(parent) {
    setupUI();

    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &SystemMonitor::updateData);
    timer->start(1000);

    updateData();
}

void SystemMonitor::setupUI() {
    setWindowTitle("System Monitor");
    resize(800, 600);

    havel::Widget* centralWidget = new havel::Widget(this);
    setCentralWidget(centralWidget);

    havel::HLayout* mainLayout = new havel::HLayout(centralWidget);
    havel::VLayout* leftLayout = new havel::VLayout();
    havel::VLayout* rightLayout = new havel::VLayout();
    mainLayout->addLayout(leftLayout, 1);
    mainLayout->addLayout(rightLayout, 2);

    // Left side: Gauges and Labels
    cpuLabel = new havel::Label("CPU Usage:", this);
    cpuBar = new havel::ProgressBar(this);
    cpuBar->setRange(0, 100);

    memLabel = new havel::Label("Memory Usage:", this);
    memBar = new havel::ProgressBar(this);
    memBar->setRange(0, 100);

    uptimeLabel = new havel::Label(this);
    netLabel = new havel::Label(this);

    leftLayout->addWidget(cpuLabel);
    leftLayout->addWidget(cpuBar);
    leftLayout->addWidget(memLabel);
    leftLayout->addWidget(memBar);
    leftLayout->addWidget(uptimeLabel);
    leftLayout->addWidget(netLabel);

    // Right side: Charts and Process List
    havel::TabWidget* tabWidget = new havel::TabWidget(this);
    rightLayout->addWidget(tabWidget);

    // -- Charts Tab --
    havel::Widget* chartsTab = new havel::Widget(this);
    havel::VLayout* chartsLayout = new havel::VLayout(chartsTab);
    tabWidget->addTab(chartsTab, "Usage Graphs");

    cpuChart = new QChart();
    cpuSeries = new QLineSeries();
    cpuChart->addSeries(cpuSeries);
    cpuChart->setTitle("CPU Usage History");
    cpuChart->createDefaultAxes();
    cpuChart->axes(Qt::Vertical).first()->setRange(0, 100);
    cpuChartView = new QChartView(cpuChart);
    chartsLayout->addWidget(cpuChartView);

    memChart = new QChart();
    memSeries = new QLineSeries();
    memChart->addSeries(memSeries);
    memChart->setTitle("Memory Usage History");
    memChart->createDefaultAxes();
    memChart->axes(Qt::Vertical).first()->setRange(0, 100);
    memChartView = new QChartView(memChart);
    chartsLayout->addWidget(memChartView);

    // -- Process List Tab --
    processTree = new havel::TreeWidget(this);
    processTree->setHeaderLabels({"PID", "Name", "CPU %", "Memory"});
    tabWidget->addTab(processTree, "Processes");
}

void SystemMonitor::updateData() {
    // CPU
    long long total, idle;
    getCpuUsage(total, idle);
    double cpuUsage = 0.0;
    if (prevTotal > 0) {
        long long totalDiff = total - prevTotal;
        long long idleDiff = idle - prevIdle;
        cpuUsage = 100.0 * (totalDiff - idleDiff) / totalDiff;
        cpuBar->setValue(static_cast<int>(cpuUsage));
        cpuSeries->append(cpuSeries->count(), cpuUsage);
        if (cpuSeries->count() > 60) cpuSeries->remove(0);
        cpuChart->axes(Qt::Horizontal).first()->setRange(0, cpuSeries->count());
    }
    prevTotal = total;
    prevIdle = idle;

    // Memory
    double memUsage = getMemoryUsage();
    memBar->setValue(static_cast<int>(memUsage));
    memSeries->append(memSeries->count(), memUsage);
    if (memSeries->count() > 60) memSeries->remove(0);
    memChart->axes(Qt::Horizontal).first()->setRange(0, memSeries->count());


    // Uptime
    uptimeLabel->setText("Uptime: " + getUptime());

    // Network
    updateNetworkUsage();

    // Processes
    updateProcessList();
}

void SystemMonitor::getCpuUsage(long long& total, long long& idle) {
    std::ifstream file("/proc/stat");
    std::string line;
    std::getline(file, line);
    file.close();

    sscanf(line.c_str(), "cpu %lld %*d %*d %lld", &total, &idle);
}

double SystemMonitor::getMemoryUsage() {
    struct sysinfo memInfo;
    sysinfo(&memInfo);
    long long totalPhysMem = memInfo.totalram * memInfo.mem_unit;
    long long physMemUsed = totalPhysMem - (memInfo.freeram * memInfo.mem_unit);
    return (double)physMemUsed / totalPhysMem * 100.0;
}

QString SystemMonitor::getUptime() {
    struct sysinfo s_info;
    sysinfo(&s_info);
    long uptime = s_info.uptime;
    int days = uptime / (24 * 3600);
    uptime %= (24 * 3600);
    int hours = uptime / 3600;
    uptime %= 3600;
    int minutes = uptime / 60;
    return QString("%1 days, %2h %3m").arg(days).arg(hours).arg(minutes);
}

void SystemMonitor::updateProcessList() {
    processTree->clear();
    DIR* dir = opendir("/proc");
    if (!dir) return;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        int pid = atoi(entry->d_name);
        if (pid > 0) {
            std::string path = std::string("/proc/") + entry->d_name + "/stat";
            std::ifstream file(path);
            if (file.is_open()) {
                std::string comm;
                unsigned long rss;
                long utime, stime;
                file >> comm >> comm >> comm; // Skip pid, state
                file.seekg(0);
                file >> pid >> comm;
                for(int i=0; i<11; ++i) file >> utime; // skip to utime
                file >> utime >> stime; // utime, stime
                for(int i=0; i<9; ++i) file >> rss; // skip to rss
                file >> rss;


                QTreeWidgetItem* item = new QTreeWidgetItem(processTree);
                item->setText(0, QString::number(pid));
                item->setText(1, QString::fromStdString(comm));
                // CPU and Memory calculation is complex, so placeholders for now
                item->setText(2, "N/A");
                item->setText(3, QString::number(rss * 4) + " KB");
            }
            file.close();
        }
    }
    closedir(dir);
}

void SystemMonitor::updateNetworkUsage() {
    std::ifstream file("/proc/net/dev");
    std::string line;
    long long currentBytes = 0;
    while (std::getline(file, line)) {
        if (line.find(":") != std::string::npos) {
            long long r_bytes, t_bytes;
            sscanf(line.substr(line.find(":") + 1).c_str(), "%lld %*d %*d %*d %*d %*d %*d %*d %lld", &r_bytes, &t_bytes);
            currentBytes += r_bytes + t_bytes;
        }
    }
    file.close();

    if (prevNetBytes > 0) {
        double speed = (currentBytes - prevNetBytes) / 1024.0; // KB/s
        netLabel->setText(QString("Network: %1 KB/s").arg(speed, 0, 'f', 2));
    }
    prevNetBytes = currentBytes;
}
