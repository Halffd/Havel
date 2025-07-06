#pragma once

#include <QFileSystemWatcher>
#include <QTimer>
#include <QWidget>
#include <QMainWindow>
#include <QTableWidget>
#include <QLabel>
#include "qt.hpp"
#include "types.hpp"

QT_BEGIN_NAMESPACE
class QPaintEvent;
QT_END_NAMESPACE

namespace havel {

class ScreenRegionSelector : public QWidget {
    Q_OBJECT

public:
    explicit ScreenRegionSelector(QWidget *parent = nullptr);

signals:
    void regionSelected(const QRect &region);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QRect selectionRect;
    bool selecting;
    QPoint startPos;
};

class ScreenshotManager : public QMainWindow {
    Q_OBJECT

public:
    explicit ScreenshotManager(QWidget *parent = nullptr);

public slots:
    void takeScreenshot();
    void takeRegionScreenshot();
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
