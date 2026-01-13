#pragma once

#include "qt.hpp"
#include "types.hpp"
#include "ScreenRegionSelector.hpp"
#include <QFileSystemWatcher>
#include <QClipboard>
#include <QProcess>
#include "ClipboardManager.hpp"  // Include ClipboardManager

namespace havel {

class ScreenshotManager : public QMainWindow {
    Q_OBJECT

public:
    explicit ScreenshotManager(ClipboardManager* clipboardManager = nullptr, QWidget *parent = nullptr);

public slots:
    QString takeScreenshot();
    QString takeRegionScreenshot();
    QString takeScreenshotOfCurrentMonitor();
    QString captureRegion(const QRect &region);

private:
    void setupUI();
    void addToGrid(const QString &filename, const QPixmap &pixmap);
    void copyImageToClipboard(const QString &imagePath);
    void copyPathToClipboard(const QString &path);
    void addToClipboardManager(const QString &imagePath);

    QTableWidget* screenshotGrid;
    QLabel* previewLabel;
    QTimer* autoSaveTimer;
    QFileSystemWatcher* folderWatcher;
    QString screenshotDir;
    QClipboard* clipboard;
    ClipboardManager* clipboardManager;
};

} // namespace havel