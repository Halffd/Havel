#pragma once

#include "qt.hpp"
#include "types.hpp"
#include "ScreenRegionSelector.hpp"
#include <QFileSystemWatcher>

namespace havel {

class ScreenshotManager : public QMainWindow {
    Q_OBJECT

public:
    explicit ScreenshotManager(QWidget *parent = nullptr);

public slots:
    void takeScreenshot();
    void takeRegionScreenshot();
    void takeScreenshotOfCurrentMonitor();
    void captureRegion(const QRect &region);

private:
    void setupUI();
    void addToGrid(const QString &filename, const QPixmap &pixmap);

    QTableWidget* screenshotGrid;
    QLabel* previewLabel;
    QTimer* autoSaveTimer;
    QFileSystemWatcher* folderWatcher;
    QString screenshotDir;
};

} // namespace havel