#include "ScreenshotManager.hpp"
#include "ScreenRegionSelector.hpp"
#include <QApplication>
#include <QProcess>
#include <QScreen>
#include <QPixmap>
#include <QDateTime>
#include <QDir>
#include <QPainter>
#include <QMouseEvent>
#include <QTableWidgetItem>
#include <QHBoxLayout>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QGuiApplication>
#include <QCursor>

#undef Window
#undef None

namespace havel {

ScreenshotManager::ScreenshotManager(QWidget *parent) : QMainWindow(parent) {
    screenshotDir = QDir::homePath() + "/Screenshots";
    if (!QDir(screenshotDir).exists()) {
        QDir().mkdir(screenshotDir);
    }

    setupUI();

    folderWatcher = new QFileSystemWatcher(this);
    folderWatcher->addPath(screenshotDir);
    // connect(folderWatcher, &QFileSystemWatcher::directoryChanged, this, &ScreenshotManager::onDirectoryChanged);
}

void ScreenshotManager::setupUI() {
    setWindowTitle("Screenshot Manager");
    setMinimumSize(800, 600);

    auto centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    auto mainLayout = new QHBoxLayout(centralWidget);
    screenshotGrid = new QTableWidget(this);
    screenshotGrid->setColumnCount(4);
    screenshotGrid->setRowCount(0);
    mainLayout->addWidget(screenshotGrid);

    previewLabel = new QLabel(this);
    previewLabel->setMinimumSize(400, 300);
    mainLayout->addWidget(previewLabel);
}

void ScreenshotManager::takeScreenshot() {
    QString filename = QString("screenshot_%1.png").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd_hh-mm-ss-zzz"));
    QString fullPath = screenshotDir + "/" + filename;
    
    // Check for Wayland
    if (QApplication::platformName().contains("wayland", Qt::CaseInsensitive) || 
        qgetenv("XDG_SESSION_TYPE") == "wayland") {
        
        bool success = false;
        
        // Try grim (wlroots-based: Sway, Hyprland, etc.)
        if (QProcess::execute("grim", {fullPath}) == 0) success = true;
        
        // Try spectacle (KDE)
        else if (QProcess::execute("spectacle", {"-b", "-n", "-o", fullPath}) == 0) success = true;
        
        // Try gnome-screenshot (GNOME)
        else if (QProcess::execute("gnome-screenshot", {"-f", fullPath}) == 0) success = true;
        
        if (success) {
            addToGrid(filename, QPixmap(fullPath).scaled(200, 150, Qt::KeepAspectRatio));
            return;
        }
    }

    auto screen = QApplication::primaryScreen();
    if (!screen) return;
    
    auto pixmap = screen->grabWindow(0);
    if (pixmap.isNull()) {
        qWarning() << "Failed to grab screen";
        return;
    }

    if (pixmap.save(fullPath)) {
        addToGrid(filename, pixmap.scaled(200, 150, Qt::KeepAspectRatio));
    }
}

void ScreenshotManager::takeRegionScreenshot() {
    // Check for Wayland
    if (QApplication::platformName().contains("wayland", Qt::CaseInsensitive) || 
        qgetenv("XDG_SESSION_TYPE") == "wayland") {
        
        QString filename = QString("screenshot_%1.png").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd_hh-mm-ss-zzz"));
        QString fullPath = screenshotDir + "/" + filename;
        bool success = false;
        
        // Try slurp | grim
        // We need to use sh -c to pipe
        QString command = QString("slurp | grim -g - %1").arg(fullPath);
        if (QProcess::execute("sh", {"-c", command}) == 0) success = true;
        
        // Try spectacle (KDE)
        else if (QProcess::execute("spectacle", {"-r", "-b", "-n", "-o", fullPath}) == 0) success = true;
        
        // Try gnome-screenshot (GNOME)
        else if (QProcess::execute("gnome-screenshot", {"-a", "-f", fullPath}) == 0) success = true;
        
        if (success) {
             addToGrid(filename, QPixmap(fullPath).scaled(200, 150, Qt::KeepAspectRatio));
             return;
        }
    }

    hide();
    QTimer::singleShot(200, [this]() {
        auto selector = new ScreenRegionSelector;
        connect(selector, &ScreenRegionSelector::regionSelected, this, &ScreenshotManager::captureRegion);
        selector->show();
    });
}

void ScreenshotManager::takeScreenshotOfCurrentMonitor() {
    QString filename = QString("screenshot_%1.png").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd_hh-mm-ss-zzz"));
    QString fullPath = screenshotDir + "/" + filename;

    // Check for Wayland
    if (QApplication::platformName().contains("wayland", Qt::CaseInsensitive) || 
        qgetenv("XDG_SESSION_TYPE") == "wayland") {
        
        bool success = false;
        
        // Try spectacle (KDE) -m for current monitor
        if (QProcess::execute("spectacle", {"-m", "-b", "-n", "-o", fullPath}) == 0) success = true;
        
        if (success) {
             addToGrid(filename, QPixmap(fullPath).scaled(200, 150, Qt::KeepAspectRatio));
             return;
        }
    }

    auto screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) return;

    auto pixmap = screen->grabWindow(0, screen->geometry().x(), screen->geometry().y(), screen->geometry().width(), screen->geometry().height());
    if (pixmap.isNull()) {
        qWarning() << "Failed to grab screen";
        return;
    }

    if (pixmap.save(fullPath)) {
        addToGrid(filename, pixmap.scaled(200, 150, Qt::KeepAspectRatio));
    }
}

void ScreenshotManager::captureRegion(const QRect &region) {
    auto screen = QApplication::primaryScreen();
    auto pixmap = screen->grabWindow(0, region.x(), region.y(), region.width(), region.height());

    QString filename = QString("screenshot_%1.png").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd_hh-mm-ss-zzz"));
    pixmap.save(screenshotDir + "/" + filename);

    addToGrid(filename, pixmap.scaled(200, 150, Qt::KeepAspectRatio));
    show();
}

void ScreenshotManager::addToGrid(const QString &filename, const QPixmap &pixmap) {
    int row = screenshotGrid->rowCount();
    screenshotGrid->insertRow(row);

    auto item = new QTableWidgetItem(QIcon(pixmap), filename);
    screenshotGrid->setItem(row, 0, item);
}

} // namespace havel