#include "ScreenshotManager.hpp"
#include "qt.hpp"
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
#include <QWindow>
#include <QOpenGLContext>
#include <QOffscreenSurface>
#include <QProcess>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QDesktopServices>
#include <QHeaderView>
#include <QFile>

namespace havel {

// Default ShareX-like style settings
struct ScreenshotStyle {
    double dimFactor = 0.8;           // 20% dim
    bool showCursorCross = true;      // Show blue crosshair at cursor
    uint32_t cursorCrossColor = 0xFF0000FF;  // Blue
    int cursorCrossSize = 24;
    int cursorCrossWidth = 3;
    int selectionBorderWidth = 8;     // 8px red border
    uint32_t selectionBorderColor = 0xFFFF0000;  // Red
    double selectionOpacity = 0.3;    // 30% fill
};

static QImage applyDefaultStyle(const QImage& image, const ScreenshotStyle& style = {}) {
    if (image.isNull()) return image;
    
    QImage result = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    
    // 1. Dim the entire image by dimFactor (20% darker)
    if (style.dimFactor < 1.0 && style.dimFactor > 0.0) {
        int w = result.width();
        int h = result.height();
        for (int y = 0; y < h; ++y) {
            QRgb* line = reinterpret_cast<QRgb*>(result.scanLine(y));
            for (int x = 0; x < w; ++x) {
                QRgb pixel = line[x];
                int r = qRed(pixel);
                int g = qGreen(pixel);
                int b = qBlue(pixel);
                int a = qAlpha(pixel);
                
                r = static_cast<int>(r * style.dimFactor);
                g = static_cast<int>(g * style.dimFactor);
                b = static_cast<int>(b * style.dimFactor);
                
                line[x] = qRgba(r, g, b, a);
            }
        }
    }
    
    // 2. Draw blue crosshair at cursor position
    if (style.showCursorCross) {
        QPainter painter(&result);
        painter.setRenderHint(QPainter::Antialiasing);
        
        QPen pen(QColor::fromRgba(style.cursorCrossColor), style.cursorCrossWidth, Qt::SolidLine, Qt::RoundCap);
        painter.setPen(pen);
        
        // Get global cursor position
        QPoint cursorPos = QCursor::pos();
        
        // Adjust for virtual desktop offset if needed
        QScreen* primary = QGuiApplication::primaryScreen();
        if (primary) {
            QRect geo = primary->virtualGeometry();
            cursorPos -= geo.topLeft();
        }
        
        int cx = cursorPos.x();
        int cy = cursorPos.y();
        int halfSize = style.cursorCrossSize / 2;
        
        // Draw horizontal line
        painter.drawLine(cx - halfSize, cy, cx + halfSize, cy);
        // Draw vertical line
        painter.drawLine(cx, cy - halfSize, cx, cy + halfSize);
        
        // Draw center circle
        painter.setBrush(QColor::fromRgba(style.cursorCrossColor));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(cx - style.cursorCrossWidth, cy - style.cursorCrossWidth, style.cursorCrossWidth * 2, style.cursorCrossWidth * 2);
        
        painter.end();
    }
    
    // 3. Draw 8px red border with 30% opacity fill around the entire image
    if (style.selectionBorderWidth > 0) {
        QPainter painter(&result);
        painter.setRenderHint(QPainter::Antialiasing);
        
        int borderWidth = style.selectionBorderWidth;
        uint32_t borderColor = style.selectionBorderColor;
        double opacity = style.selectionOpacity;
        
        // Draw border
        QPen pen(QColor::fromRgba(borderColor), borderWidth, Qt::SolidLine, Qt::RoundCap, Qt::MiterJoin);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        
        int inset = borderWidth / 2;
        painter.drawRect(inset, inset, result.width() - borderWidth, result.height() - borderWidth);
        
        // Draw semi-transparent fill
        if (opacity > 0 && opacity < 1.0) {
            QColor fillColor = QColor::fromRgba(borderColor);
            fillColor.setAlphaF(opacity);
            painter.setPen(Qt::NoPen);
            painter.setBrush(fillColor);
            painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
            painter.drawRect(inset, inset, result.width() - borderWidth, result.height() - borderWidth);
        }
        
        painter.end();
    }
    
    return result;
}

ScreenshotManager::ScreenshotManager(ClipboardManager* clipboardManager, QWidget *parent) : QMainWindow(parent) {
    this->clipboardManager = clipboardManager;
    screenshotDir = QDir::homePath() + "/Screenshots";
    if (!QDir(screenshotDir).exists()) {
        QDir().mkdir(screenshotDir);
    }

    setupUI();

    clipboard = QApplication::clipboard();
    folderWatcher = new QFileSystemWatcher(this);
    folderWatcher->addPath(screenshotDir);
}

void ScreenshotManager::setupUI() {
    setWindowTitle("Screenshot Manager");
    setMinimumSize(1200, 800);

    auto centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    auto mainLayout = new QHBoxLayout(centralWidget);
    screenshotGrid = new QTableWidget(this);
    screenshotGrid->setColumnCount(3);
    screenshotGrid->setRowCount(0);
    screenshotGrid->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    screenshotGrid->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    screenshotGrid->setIconSize(QSize(300, 225));
    screenshotGrid->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    mainLayout->addWidget(screenshotGrid);

    previewLabel = new QLabel(this);
    previewLabel->setMinimumSize(400, 300);
    mainLayout->addWidget(previewLabel);
}

// Helper to capture, apply style, and save
static QString captureAndStyle(const QString& filename, const QString& fullPath, QImage image, ScreenshotManager* self) {
    if (image.isNull()) return QString();
    
    // Apply default ShareX-like style
    ScreenshotStyle style;
    QImage styled = applyDefaultStyle(image, style);
    
    if (!styled.save(fullPath)) return QString();
    
    QPixmap qpixmap = QPixmap::fromImage(styled);
    self->addToGrid(filename, qpixmap.scaled(200, 150, Qt::KeepAspectRatio));
    self->copyImageToClipboard(fullPath);
    return fullPath;
}

QString ScreenshotManager::takeScreenshot() {
    QString filename = QString("screenshot_%1.png").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd_hh-mm-ss-zzz"));
    QString fullPath = screenshotDir + "/" + filename;

    // Check for Wayland
    if (QApplication::platformName().contains("wayland", Qt::CaseInsensitive) ||
        qgetenv("XDG_SESSION_TYPE") == "wayland") {

        bool success = false;

        if (QProcess::execute("grim", {fullPath}) == 0) success = true;
        else if (QProcess::execute("spectacle", {"-b", "-n", "-o", fullPath}) == 0) success = true;
        else if (QProcess::execute("gnome-screenshot", {"-f", fullPath}) == 0) success = true;

        if (success) {
            QImage img(fullPath);
            if (!img.isNull()) {
                return captureAndStyle(filename, fullPath, img, this);
            }
            addToGrid(filename, QPixmap(fullPath).scaled(200, 150, Qt::KeepAspectRatio));
            copyImageToClipboard(fullPath);
            return fullPath;
        }
    }

#ifdef __linux__
    x11::Display *display = x11::OpenDisplay(nullptr);
    if (display) {
        x11::Window root = DefaultRootWindow(display);
        XWindowAttributes attr;
        if (XGetWindowAttributes(display, root, &attr)) {
            XImage *image = XGetImage(display, root, 0, 0,
                                     attr.width, attr.height, AllPlanes, ZPixmap);
            if (image) {
                QImage qimg = QImage((uchar*)image->data, image->width, image->height,
                                     image->bytes_per_line, QImage::Format_RGB32);
                qimg = qimg.rgbSwapped();

                if (!qimg.isNull()) {
                    XDestroyImage(image);
                    x11::CloseDisplay(display);
                    return captureAndStyle(filename, fullPath, qimg, this);
                }
                XDestroyImage(image);
            }
        }
        x11::CloseDisplay(display);
    }
#endif

    // Fallback to Qt's screen grabber for all monitors
    auto screens = QGuiApplication::screens();
    if (screens.isEmpty()) return QString();

    if (screens.size() == 1) {
        auto screen = screens[0];
        auto pixmap = screen->grabWindow(0);
        if (!pixmap.isNull()) {
            QImage img = pixmap.toImage();
            return captureAndStyle(filename, fullPath, img, this);
        }
    } else {
        int totalWidth = 0;
        int maxHeight = 0;

        for (auto *screen : screens) {
            QRect geo = screen->geometry();
            totalWidth += geo.width();
            maxHeight = std::max(maxHeight, geo.height());
        }

        QPixmap combinedPixmap(totalWidth, maxHeight);
        combinedPixmap.fill(Qt::black);
        QPainter painter(&combinedPixmap);

        int currentX = 0;
        bool success = true;

        for (auto *screen : screens) {
            auto screenPixmap = screen->grabWindow(0);
            if (screenPixmap.isNull()) {
                success = false;
                break;
            }

            QRect geo = screen->geometry();
            painter.drawPixmap(currentX, 0, screenPixmap);
            currentX += geo.width();
        }

        if (success) {
            QImage img = combinedPixmap.toImage();
            return captureAndStyle(filename, fullPath, img, this);
        }
    }

    return QString();
}

QString ScreenshotManager::takeRegionScreenshot() {
    QString filename = QString("screenshot_%1.png").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd_hh-mm-ss-zzz"));
    QString fullPath = screenshotDir + "/" + filename;
    bool success = false;

    if (QApplication::platformName().contains("wayland", Qt::CaseInsensitive) ||
        qgetenv("XDG_SESSION_TYPE") == "wayland") {
        QString command = QString("slurp | grim -g - %1").arg(fullPath);
        if (QProcess::execute("sh", {"-c", command}) == 0) success = true;
        else if (QProcess::execute("spectacle", {"-r", "-b", "-n", "-o", fullPath}) == 0) success = true;
        else if (QProcess::execute("gnome-screenshot", {"-a", "-f", fullPath}) == 0) success = true;
    } else {
        if (QProcess::execute("gnome-screenshot", {"-a", "-f", fullPath}) == 0) success = true;
        else if (QProcess::execute("scrot", {"-s", fullPath}) == 0) success = true;
        else if (QProcess::execute("import", {fullPath}) == 0) success = true;
    }

    if (success) {
        QImage img(fullPath);
        if (!img.isNull()) {
            return captureAndStyle(filename, fullPath, img, this);
        }
        addToGrid(filename, QPixmap(fullPath).scaled(200, 150, Qt::KeepAspectRatio));
        copyImageToClipboard(fullPath);
        return fullPath;
    }

    // Fallback to Qt's region selector
    hide();
    QTimer::singleShot(200, [this, fullPath, filename]() {
        auto selector = new ScreenRegionSelector;
        connect(selector, &ScreenRegionSelector::regionSelected, this, [this, fullPath, filename](const QRect &region) {
            QString result = captureRegion(region);
            if (!result.isEmpty()) {
                addToGrid(filename, QPixmap(result).scaled(200, 150, Qt::KeepAspectRatio));
                copyImageToClipboard(result);
            }
        });
        selector->show();
    });

    return QString();
}

QString ScreenshotManager::takeScreenshotOfCurrentMonitor() {
    QString filename = QString("screenshot_%1.png").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd_hh-mm-ss-zzz"));
    QString fullPath = screenshotDir + "/" + filename;

    if (QApplication::platformName().contains("wayland", Qt::CaseInsensitive) ||
        qgetenv("XDG_SESSION_TYPE") == "wayland") {

        bool success = false;
        if (QProcess::execute("spectacle", {"-m", "-b", "-n", "-o", fullPath}) == 0) success = true;

        if (success) {
            QImage img(fullPath);
            if (!img.isNull()) {
                return captureAndStyle(filename, fullPath, img, this);
            }
            addToGrid(filename, QPixmap(fullPath).scaled(200, 150, Qt::KeepAspectRatio));
            copyImageToClipboard(fullPath);
            return fullPath;
        }
    }

#ifdef __linux__
    auto currentScreen = QGuiApplication::screenAt(QCursor::pos());
    if (!currentScreen) currentScreen = QGuiApplication::primaryScreen();
    if (!currentScreen) return QString();

    QRect monitorGeometry = currentScreen->geometry();

    x11::Display *display = x11::OpenDisplay(nullptr);
    if (display) {
        x11::Window root = DefaultRootWindow(display);
        XWindowAttributes attr;
        if (XGetWindowAttributes(display, root, &attr)) {
            XImage *image = XGetImage(display, root, 0, 0,
                                     attr.width, attr.height, AllPlanes, ZPixmap);
            if (image) {
                QImage fullImage = QImage((uchar*)image->data, image->width, image->height,
                                         image->bytes_per_line, QImage::Format_RGB32);
                fullImage = fullImage.rgbSwapped();

                QImage monitorImage = fullImage.copy(monitorGeometry);

                if (!monitorImage.isNull()) {
                    XDestroyImage(image);
                    x11::CloseDisplay(display);
                    return captureAndStyle(filename, fullPath, monitorImage, this);
                }
                XDestroyImage(image);
            }
        }
        x11::CloseDisplay(display);
    }
#endif

    auto screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (!screen) return QString();

    auto pixmap = screen->grabWindow(0, screen->geometry().x(), screen->geometry().y(), screen->geometry().width(), screen->geometry().height());
    if (pixmap.isNull()) return QString();

    QImage img = pixmap.toImage();
    return captureAndStyle(filename, fullPath, img, this);
}

QString ScreenshotManager::captureRegion(const QRect &region) {
    QString filename = QString("screenshot_%1.png").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd_hh-mm-ss-zzz"));
    QString fullPath = screenshotDir + "/" + filename;

#ifdef __linux__
    x11::Display *display = x11::OpenDisplay(nullptr);
    if (display) {
        x11::Window root = DefaultRootWindow(display);
        XWindowAttributes attr;
        if (XGetWindowAttributes(display, root, &attr)) {
            XImage *image = XGetImage(display, root, 0, 0,
                                     attr.width, attr.height, AllPlanes, ZPixmap);
            if (image) {
                QImage fullImage = QImage((uchar*)image->data, image->width, image->height,
                                         image->bytes_per_line, QImage::Format_RGB32);
                fullImage = fullImage.rgbSwapped();

                QImage regionImage = fullImage.copy(region);

                if (!regionImage.isNull()) {
                    XDestroyImage(image);
                    x11::CloseDisplay(display);
                    return captureAndStyle(filename, fullPath, regionImage, this);
                }
                XDestroyImage(image);
            }
        }
        x11::CloseDisplay(display);
    }
#endif

    // Fallback to Qt's screen grabber
    auto screen = QApplication::primaryScreen();
    auto pixmap = screen->grabWindow(0, region.x(), region.y(), region.width(), region.height());

    if (!pixmap.isNull()) {
        QImage img = pixmap.toImage();
        return captureAndStyle(filename, fullPath, img, this);
    }

    show();
    return QString();
}

void ScreenshotManager::addToGrid(const QString &filename, const QPixmap &pixmap) {
    int currentRowCount = screenshotGrid->rowCount();
    int currentColumnCount = screenshotGrid->columnCount();

    int totalCells = currentRowCount * currentColumnCount;
    int currentCell = totalCells;

    int row = currentCell / currentColumnCount;
    int col = currentCell % currentColumnCount;

    if (col == 0 && currentCell > 0) {
        screenshotGrid->insertRow(currentRowCount);
    }

    if (col == 0 && currentCell > 0) {
        row = currentRowCount;
    }

    auto widget = new QWidget();
    auto layout = new QVBoxLayout(widget);

    QPixmap scaledPixmap = pixmap.scaled(300, 225, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    auto imageLabel = new QLabel();
    imageLabel->setPixmap(scaledPixmap);
    imageLabel->setAlignment(Qt::AlignCenter);
    imageLabel->setFixedSize(300, 225);
    imageLabel->setStyleSheet("border: 1px solid gray;");

    auto buttonLayout = new QHBoxLayout();
    auto copyPathBtn = new QPushButton("Copy Path");
    auto copyImageBtn = new QPushButton("Copy Image");
    auto deleteBtn = new QPushButton("Delete");
    auto openEditorBtn = new QPushButton("Open Editor");

    QString fullPath = screenshotDir + "/" + filename;
    copyPathBtn->setProperty("filepath", fullPath);
    copyImageBtn->setProperty("filepath", fullPath);
    deleteBtn->setProperty("filepath", fullPath);
    openEditorBtn->setProperty("filepath", fullPath);

    connect(copyPathBtn, &QPushButton::clicked, [this, copyPathBtn]() {
        QString path = copyPathBtn->property("filepath").toString();
        copyPathToClipboard(path);
    });

    connect(copyImageBtn, &QPushButton::clicked, [this, copyImageBtn]() {
        QString path = copyImageBtn->property("filepath").toString();
        copyImageToClipboard(path);
    });

    connect(deleteBtn, &QPushButton::clicked, [this, deleteBtn, widget, row, col]() {
        QString path = deleteBtn->property("filepath").toString();
        QFile::remove(path);
        screenshotGrid->removeCellWidget(row, col);
        delete widget;
    });

    connect(openEditorBtn, &QPushButton::clicked, [openEditorBtn]() {
        QString path = openEditorBtn->property("filepath").toString();
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });

    buttonLayout->addWidget(copyPathBtn);
    buttonLayout->addWidget(copyImageBtn);
    buttonLayout->addWidget(deleteBtn);
    buttonLayout->addWidget(openEditorBtn);

    layout->addWidget(imageLabel);
    layout->addLayout(buttonLayout);

    screenshotGrid->setCellWidget(row, col, widget);
}

void ScreenshotManager::copyImageToClipboard(const QString &imagePath) {
    if (!imagePath.isEmpty()) {
        QPixmap pixmap(imagePath);
        if (!pixmap.isNull()) {
            clipboard->setPixmap(pixmap);
            addToClipboardManager(imagePath);
        }
    }
}

void ScreenshotManager::copyPathToClipboard(const QString &path) {
    if (!path.isEmpty()) {
        clipboard->setText(path);
    }
}

void ScreenshotManager::addToClipboardManager(const QString &imagePath) {
    if (clipboardManager && !imagePath.isEmpty()) {
        QPixmap pixmap(imagePath);
        if (!pixmap.isNull()) {
            clipboardManager->getClipboard()->setPixmap(pixmap);
        }
    }
}

} // namespace havel
