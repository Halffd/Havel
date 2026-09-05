#pragma once
#include "qt.hpp"

#include "types.hpp"
#include "ScreenRegionSelector.hpp"
#include "../clipboard_manager/ClipboardManager.hpp"

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
    
    // Additional methods for Havel module
    QString getScreenshotDirectory() const { return screenshotDir; }
    void setScreenshotDirectory(const QString &dir) { 
        screenshotDir = dir; 
        if (!QDir(dir).exists()) {
            QDir().mkpath(dir);
        }
    }
    void showManager() { show(); raise(); activateWindow(); }
    void hideManager() { hide(); }

public:
    void addToGrid(const QString &filename, const QPixmap &pixmap);
    void copyImageToClipboard(const QString &imagePath);

private:
    void setupUI();
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
