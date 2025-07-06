#include "ScreenshotManager.hpp"
#include <QApplication>
#include <QScreen>
#include <QPixmap>
#include <QDateTime>
#include <QDir>
#include <QPainter>
#include <QMouseEvent>
#include <QTableWidgetItem>
#include <QHBoxLayout>

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

    auto mainLayout = new QHBoxLayout(this);
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

    QString filename = QString("screenshot_%1.png").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd_hh-mm-ss"));
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

void ScreenshotManager::captureRegion(const QRect &region) {
    auto screen = QApplication::primaryScreen();
    auto pixmap = screen->grabWindow(0, region.x(), region.y(), region.width(), region.height());

    QString filename = QString("screenshot_%1.png").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd_hh-mm-ss"));
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

ScreenRegionSelector::ScreenRegionSelector(QWidget *parent) : QWidget(parent), selecting(false) {
    setWindowFlags(Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setCursor(Qt::CrossCursor);
    showFullScreen();
}

void ScreenRegionSelector::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(rect(), QColor(0, 0, 0, 100));

    if (selecting) {
        painter.setPen(QPen(Qt::red, 2));
        painter.drawRect(selectionRect);
    }
}

void ScreenRegionSelector::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        selecting = true;
        startPos = event->pos();
        selectionRect = QRect(startPos, QSize());
    }
}

void ScreenRegionSelector::mouseMoveEvent(QMouseEvent *event) {
    if (selecting) {
        selectionRect = QRect(startPos, event->pos()).normalized();
        update();
    }
}

void ScreenRegionSelector::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && selecting) {
        selecting = false;
        regionSelected(selectionRect);
        close();
    }
}

} // namespace havel

