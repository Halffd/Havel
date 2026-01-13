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
    // connect(folderWatcher, &QFileSystemWatcher::directoryChanged, this, &ScreenshotManager::onDirectoryChanged);
}

void ScreenshotManager::setupUI() {
    setWindowTitle("Screenshot Manager");
    setMinimumSize(1200, 800);

    auto centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    auto mainLayout = new QHBoxLayout(centralWidget);
    screenshotGrid = new QTableWidget(this);
    screenshotGrid->setColumnCount(3);  // Reduced columns to accommodate larger images
    screenshotGrid->setRowCount(0);
    screenshotGrid->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    screenshotGrid->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    // Set larger image size
    screenshotGrid->setIconSize(QSize(300, 225));  // Larger thumbnails
    screenshotGrid->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    mainLayout->addWidget(screenshotGrid);

    previewLabel = new QLabel(this);
    previewLabel->setMinimumSize(400, 300);
    mainLayout->addWidget(previewLabel);
}

QString ScreenshotManager::takeScreenshot() {
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
            copyImageToClipboard(fullPath);
            return fullPath;
        }
    }

#ifdef __linux__
    // Try X11 low-level capture for obscured windows using XGetImage (since XCompositeNameWindowPixmap doesn't work on root)
    x11::Display *display = x11::OpenDisplay(nullptr);
    if (display) {
        x11::Window root = DefaultRootWindow(display);
        XWindowAttributes attr;
        if (XGetWindowAttributes(display, root, &attr)) {
            // Capture the full desktop using XGetImage directly on root window
            XImage *image = XGetImage(display, root, 0, 0,
                                     attr.width, attr.height, AllPlanes, ZPixmap);
            if (image) {
                QImage qimg = QImage((uchar*)image->data, image->width, image->height,
                                     image->bytes_per_line, QImage::Format_RGB32);
                // Convert BGR to RGB if needed
                qimg = qimg.rgbSwapped();

                if (!qimg.isNull() && qimg.save(fullPath)) {
                    QPixmap qpixmap = QPixmap::fromImage(qimg);
                    addToGrid(filename, qpixmap.scaled(200, 150, Qt::KeepAspectRatio));
                    copyImageToClipboard(fullPath);
                    XDestroyImage(image);
                    x11::CloseDisplay(display);
                    return fullPath;
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
        // Single monitor - simple grab
        auto screen = screens[0];
        auto pixmap = screen->grabWindow(0);
        if (!pixmap.isNull() && pixmap.save(fullPath)) {
            addToGrid(filename, pixmap.scaled(200, 150, Qt::KeepAspectRatio));
            copyImageToClipboard(fullPath);
            return fullPath;
        }
    } else {
        // Multiple monitors - stitch them together
        int totalWidth = 0;
        int maxHeight = 0;

        // Calculate total dimensions
        for (auto *screen : screens) {
            QRect geo = screen->geometry();
            totalWidth += geo.width();
            maxHeight = std::max(maxHeight, geo.height());
        }

        // Create combined pixmap
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

        if (success && combinedPixmap.save(fullPath)) {
            addToGrid(filename, combinedPixmap.scaled(200, 150, Qt::KeepAspectRatio));
            copyImageToClipboard(fullPath);
            return fullPath;
        }
    }

    return QString(); // Return empty string if failed
}

QString ScreenshotManager::takeRegionScreenshot() {
    QString filename = QString("screenshot_%1.png").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd_hh-mm-ss-zzz"));
    QString fullPath = screenshotDir + "/" + filename;
    bool success = false;

    // Check for Wayland
    if (QApplication::platformName().contains("wayland", Qt::CaseInsensitive) ||
        qgetenv("XDG_SESSION_TYPE") == "wayland") {
        // Try slurp | grim
        // We need to use sh -c to pipe
        QString command = QString("slurp | grim -g - %1").arg(fullPath);
        if (QProcess::execute("sh", {"-c", command}) == 0) success = true;

        // Try spectacle (KDE)
        else if (QProcess::execute("spectacle", {"-r", "-b", "-n", "-o", fullPath}) == 0) success = true;

        // Try gnome-screenshot (GNOME)
        else if (QProcess::execute("gnome-screenshot", {"-a", "-f", fullPath}) == 0) success = true;
    }
    // Try X11-specific tools
    else {
        // Try gnome-screenshot area selection
        if (QProcess::execute("gnome-screenshot", {"-a", "-f", fullPath}) == 0) success = true;

        // Try scrot with selection (interactive)
        else if (QProcess::execute("scrot", {"-s", fullPath}) == 0) success = true;

        // Try import with user selection
        else if (QProcess::execute("import", {fullPath}) == 0) success = true;
    }

    if (success) {
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

    return QString(); // Return empty string for async operation
}

QString ScreenshotManager::takeScreenshotOfCurrentMonitor() {
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
             copyImageToClipboard(fullPath);
             return fullPath;
        }
    }

#ifdef __linux__
    // Try X11 low-level capture for current monitor using XGetImage (since XCompositeNameWindowPixmap doesn't work on root)
    auto currentScreen = QGuiApplication::screenAt(QCursor::pos());
    if (!currentScreen) {
        currentScreen = QGuiApplication::primaryScreen();
    }
    if (!currentScreen) {
        qWarning() << "Could not determine screen";
        return QString();
    }

    QRect monitorGeometry = currentScreen->geometry();

    x11::Display *display = x11::OpenDisplay(nullptr);
    if (display) {
        x11::Window root = DefaultRootWindow(display);
        XWindowAttributes attr;
        if (XGetWindowAttributes(display, root, &attr)) {
            // Capture the full desktop using XGetImage directly on root window
            XImage *image = XGetImage(display, root, 0, 0,
                                     attr.width, attr.height, AllPlanes, ZPixmap);
            if (image) {
                QImage fullImage = QImage((uchar*)image->data, image->width, image->height,
                                         image->bytes_per_line, QImage::Format_RGB32);
                fullImage = fullImage.rgbSwapped(); // Convert BGR to RGB if needed

                // Crop to current monitor's geometry (adjust coordinates relative to full desktop)
                QImage monitorImage = fullImage.copy(monitorGeometry);

                if (!monitorImage.isNull() && monitorImage.save(fullPath)) {
                    QPixmap qpixmap = QPixmap::fromImage(monitorImage);
                    addToGrid(filename, qpixmap.scaled(200, 150, Qt::KeepAspectRatio));
                    copyImageToClipboard(fullPath);
                    XDestroyImage(image);
                    x11::CloseDisplay(display);
                    return fullPath;
                }
                XDestroyImage(image);
            }
        }
        x11::CloseDisplay(display);
    }
#endif

    // Fallback to Qt's screen grabber for current monitor
    auto screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) return QString();

    auto pixmap = screen->grabWindow(0, screen->geometry().x(), screen->geometry().y(), screen->geometry().width(), screen->geometry().height());
    if (pixmap.isNull()) {
        qWarning() << "Failed to grab screen";
        return QString();
    }

    if (pixmap.save(fullPath)) {
        addToGrid(filename, pixmap.scaled(200, 150, Qt::KeepAspectRatio));
        copyImageToClipboard(fullPath);
        return fullPath;
    }

    return QString();
}

QString ScreenshotManager::captureRegion(const QRect &region) {
    QString filename = QString("screenshot_%1.png").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd_hh-mm-ss-zzz"));
    QString fullPath = screenshotDir + "/" + filename;

#ifdef __linux__
    // Try X11 low-level capture for region using XGetImage (since XCompositeNameWindowPixmap doesn't work on root)
    x11::Display *display = x11::OpenDisplay(nullptr);
    if (display) {
        x11::Window root = DefaultRootWindow(display);
        XWindowAttributes attr;
        if (XGetWindowAttributes(display, root, &attr)) {
            // Capture the full desktop using XGetImage directly on root window
            XImage *image = XGetImage(display, root, 0, 0,
                                     attr.width, attr.height, AllPlanes, ZPixmap);
            if (image) {
                QImage fullImage = QImage((uchar*)image->data, image->width, image->height,
                                         image->bytes_per_line, QImage::Format_RGB32);
                fullImage = fullImage.rgbSwapped(); // Convert BGR to RGB if needed

                // Crop to the selected region
                QImage regionImage = fullImage.copy(region);

                if (!regionImage.isNull() && regionImage.save(fullPath)) {
                    QPixmap qpixmap = QPixmap::fromImage(regionImage);
                    addToGrid(filename, qpixmap.scaled(200, 150, Qt::KeepAspectRatio));
                    copyImageToClipboard(fullPath);
                    XDestroyImage(image);
                    x11::CloseDisplay(display);
                    show();
                    return fullPath;
                }
                XDestroyImage(image);
            }
        }
        x11::CloseDisplay(display);
    }
#endif

    // Fallback to Qt's screen grabber for region
    auto screen = QApplication::primaryScreen();
    auto pixmap = screen->grabWindow(0, region.x(), region.y(), region.width(), region.height());

    if (pixmap.save(fullPath)) {
        addToGrid(filename, pixmap.scaled(200, 150, Qt::KeepAspectRatio));
        copyImageToClipboard(fullPath);
        show();
        return fullPath;
    }

    show();
    return QString();
}

void ScreenshotManager::addToGrid(const QString &filename, const QPixmap &pixmap) {
    int currentRowCount = screenshotGrid->rowCount();
    int currentColumnCount = screenshotGrid->columnCount();

    // Calculate which cell to insert into
    int totalCells = currentRowCount * currentColumnCount;
    int currentCell = totalCells; // Position for the new item

    int row = currentCell / currentColumnCount;
    int col = currentCell % currentColumnCount;

    // Add a new row if needed
    if (col == 0 && currentCell > 0) {
        screenshotGrid->insertRow(currentRowCount);
    }

    // Recalculate row if we just added one
    if (col == 0 && currentCell > 0) {
        row = currentRowCount;
    }

    // Create a widget to hold the image and buttons
    auto widget = new QWidget();
    auto layout = new QVBoxLayout(widget);

    // Scale the pixmap to fit the larger thumbnail size
    QPixmap scaledPixmap = pixmap.scaled(300, 225, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    // Create label for the image
    auto imageLabel = new QLabel();
    imageLabel->setPixmap(scaledPixmap);
    imageLabel->setAlignment(Qt::AlignCenter);
    imageLabel->setFixedSize(300, 225);
    imageLabel->setStyleSheet("border: 1px solid gray;");

    // Create buttons
    auto buttonLayout = new QHBoxLayout();
    auto copyPathBtn = new QPushButton("Copy Path");
    auto copyImageBtn = new QPushButton("Copy Image");
    auto deleteBtn = new QPushButton("Delete");
    auto openEditorBtn = new QPushButton("Open Editor");

    // Store the file path in the button's property for later use
    QString fullPath = screenshotDir + "/" + filename;
    copyPathBtn->setProperty("filepath", fullPath);
    copyImageBtn->setProperty("filepath", fullPath);
    deleteBtn->setProperty("filepath", fullPath);
    openEditorBtn->setProperty("filepath", fullPath);

    // Connect button signals
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
        // Remove the widget from the table cell
        screenshotGrid->removeCellWidget(row, col);
        delete widget; // Clean up the widget
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

    // Add the widget to the table cell
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
        // Add the image to the clipboard manager's history
        // This would depend on the specific API of ClipboardManager
        // For now, we'll just ensure the image is in the system clipboard
        QPixmap pixmap(imagePath);
        if (!pixmap.isNull()) {
            clipboardManager->getClipboard()->setPixmap(pixmap);
        }
    }
}

} // namespace havel