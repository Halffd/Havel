#include "ScreenshotManager.hpp"
#include "ScreenRegionSelector.hpp"
#include <QApplication>
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
    auto screen = QApplication::primaryScreen();
    auto pixmap = screen->grabWindow(0);

    QString filename = QString("screenshot_%1.png").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd_hh-mm-ss-zzz"));
    pixmap.save(screenshotDir + "/" + filename);

    addToGrid(filename, pixmap.scaled(200, 150, Qt::KeepAspectRatio));
}

void ScreenshotManager::takeRegionScreenshot() {
    hide();
    QTimer::singleShot(200, [this]() {
        auto selector = new ScreenRegionSelector;
        connect(selector, &ScreenRegionSelector::regionSelected, this, &ScreenshotManager::captureRegion);
        selector->show();
    });
}

void ScreenshotManager::takeScreenshotOfCurrentMonitor() {
    auto screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    auto pixmap = screen->grabWindow(0, screen->geometry().x(), screen->geometry().y(), screen->geometry().width(), screen->geometry().height());

    QString filename = QString("screenshot_%1.png").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd_hh-mm-ss-zzz"));
    pixmap.save(screenshotDir + "/" + filename);

    addToGrid(filename, pixmap.scaled(200, 150, Qt::KeepAspectRatio));
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