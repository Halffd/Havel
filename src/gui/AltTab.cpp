#include "AltTab.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTimer>
#include <QPainter>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QApplication>
#include <QScreen>
#include <QPixmap>
#include <QImage>
#include <QWindow>
#include <QScroller>
#include <QScrollBar>
#include <QScrollArea>
#include <QPainterPath>
#include <QStyleOption>
#include <QStyle>
#include <QGuiApplication>
#include <QCursor>

namespace havel {

AltTabWindow::AltTabWindow(QWidget *parent)
    : QWidget(parent), display(nullptr), rootWindow(x11::XNone), currentIndex(0),
      thumbnailWidth(200), thumbnailHeight(150), isVisibleFlag(false) {

    setupUI();
    setupX11();

    // Set up the window as an override-redirect window
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);

    // Timer for refreshing window list
    refreshTimer = new QTimer(this);
    connect(refreshTimer, &QTimer::timeout, this, &AltTabWindow::refreshWindows);

    // Center the window initially
    centerWindow();
}

AltTabWindow::~AltTabWindow() {
    if (display) {
        XCloseDisplay(display);
    }
}

void AltTabWindow::setupUI() {
    setStyleSheet(
        "background-color: rgba(30, 30, 30, 200);"
        "border: 2px solid rgba(100, 100, 100, 200);"
        "border-radius: 10px;"
    );

    mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(15);

    // Title label
    titleLabel = new QLabel("Alt+Tab Switcher", this);
    titleLabel->setStyleSheet(
        "color: white;"
        "font-size: 18px;"
        "font-weight: bold;"
        "margin-bottom: 10px;"
    );
    titleLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(titleLabel);

    // Container for thumbnails
    thumbnailsContainer = new QWidget(this);
    thumbnailsLayout = new QHBoxLayout(thumbnailsContainer);
    thumbnailsLayout->setAlignment(Qt::AlignCenter);
    thumbnailsLayout->setSpacing(20);

    // Scroll area for thumbnails
    scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setWidget(thumbnailsContainer);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setStyleSheet(
        "QScrollArea { border: none; }"
        "QScrollBar:horizontal {"
        "    height: 15px;"
        "    background: rgba(50, 50, 50, 150);"
        "    border-radius: 7px;"
        "}"
        "QScrollBar::handle:horizontal {"
        "    background: rgba(100, 100, 100, 200);"
        "    border-radius: 7px;"
        "    min-width: 30px;"
        "}"
    );

    mainLayout->addWidget(scrollArea);

    // Enable touch scrolling
    QScroller::grabGesture(thumbnailsContainer, QScroller::TouchGesture);
}

void AltTabWindow::setupX11() {
    display = XOpenDisplay(nullptr);
    if (!display) {
        return;
    }
    
    rootWindow = DefaultRootWindow(display);
    
    // Get atoms for EWMH properties
    netClientListAtom = XInternAtom(display, "_NET_CLIENT_LIST", x11::XFalse);
    netActiveWindowAtom = XInternAtom(display, "_NET_ACTIVE_WINDOW", x11::XFalse);
    netWMNameAtom = XInternAtom(display, "_NET_WM_NAME", x11::XFalse);
    wmNameAtom = XInternAtom(display, "WM_NAME", x11::XFalse);
    netWMStateAtom = XInternAtom(display, "_NET_WM_STATE", x11::XFalse);
    netWMStateHiddenAtom = XInternAtom(display, "_NET_WM_STATE_HIDDEN", x11::XFalse);
    netWMStateDemandsAttentionAtom = XInternAtom(display, "_NET_WM_STATE_DEMANDS_ATTENTION", x11::XFalse);
}

void AltTabWindow::showAltTab() {
    if (!isVisibleFlag) {
        refreshWindows();
        updateWindowList();
        centerWindow();
        show();
        raise();
        QWidget::activateWindow();
        setFocus(Qt::OtherFocusReason);
        isVisibleFlag = true;
        
        // Start refresh timer
        refreshTimer->start(500); // Refresh every 500ms
    }
}

void AltTabWindow::hideAltTab() {
    if (isVisibleFlag) {
        hide();
        isVisibleFlag = false;
        
        // Stop refresh timer
        refreshTimer->stop();
    }
}

std::vector<WindowInfo> AltTabWindow::getWindows() {
    std::vector<WindowInfo> result;
    
    if (!display) return result;
    
    Atom actual_type;
    int actual_format;
    unsigned long n_items, bytes_after;
    Window *client_list = nullptr;
    
    // Get the list of windows from _NET_CLIENT_LIST
    int status = XGetWindowProperty(
        display, rootWindow, netClientListAtom,
        0, (~0L), x11::XFalse, XA_WINDOW,
        &actual_type, &actual_format,
        &n_items, &bytes_after,
        (unsigned char**)&client_list
    );
    
    if (status == x11::XSuccess && client_list) {
        for (unsigned long i = 0; i < n_items; i++) {
            Window win = client_list[i];
            
            // Get window info
            WindowInfo info = getWindowInfo(win);
            if (!info.title.empty()) { // Only add windows with titles
                result.push_back(info);
            }
        }
        XFree(client_list);
    }
    
    return result;
}

WindowInfo AltTabWindow::getWindowInfo(Window win) {
    WindowInfo info;
    info.window = win;
    
    // Get window title
    XTextProperty text_prop;
    if (XGetTextProperty(display, win, &text_prop, netWMNameAtom) ||
        XGetTextProperty(display, win, &text_prop, wmNameAtom)) {
        
        if (text_prop.value) {
            info.title = std::string(reinterpret_cast<char*>(text_prop.value));
            XFree(text_prop.value);
        }
    }
    
    // Get class name
    XClassHint class_hint;
    if (XGetClassHint(display, win, &class_hint)) {
        if (class_hint.res_class) {
            info.className = std::string(class_hint.res_class);
            XFree(class_hint.res_class);
        }
        if (class_hint.res_name) {
            if (info.className.empty()) {
                info.className = std::string(class_hint.res_name);
            }
            XFree(class_hint.res_name);
        }
    }
    
    // Get window geometry
    Window root_ret, child_ret;
    int x, y;
    unsigned int width, height, border_width, depth;
    
    if (XGetGeometry(display, win, &root_ret, &x, &y, &width, &height, &border_width, &depth)) {
        info.width = width;
        info.height = height;
    }
    
    // Check if window is active
    Window active_win = x11::XNone;
    Atom actual_type;
    int actual_format;
    unsigned long n_items, bytes_after;
    Window *active_window_prop = nullptr;
    
    int status = XGetWindowProperty(
        display, rootWindow, netActiveWindowAtom,
        0, 1, x11::XFalse, XA_WINDOW,
        &actual_type, &actual_format,
        &n_items, &bytes_after,
        (unsigned char**)&active_window_prop
    );
    
    if (status == x11::XSuccess && active_window_prop) {
        if (n_items > 0) {
            active_win = active_window_prop[0];
        }
        XFree(active_window_prop);
    }
    
    info.isActive = (win == active_win);
    
    // Check window state
    Atom *state_list = nullptr;
    status = XGetWindowProperty(
        display, win, netWMStateAtom,
        0, (~0L), x11::XFalse, XA_ATOM,
        &actual_type, &actual_format,
        &n_items, &bytes_after,
        (unsigned char**)&state_list
    );
    
    if (status == x11::XSuccess && state_list) {
        for (unsigned long i = 0; i < n_items; i++) {
            if (state_list[i] == netWMStateHiddenAtom) {
                info.isMinimized = true;
            }
            // Add other state checks as needed
        }
        XFree(state_list);
    }
    
    return info;
}

QPixmap AltTabWindow::captureWindowThumbnail(Window win) {
    if (!display) return QPixmap();

    Window root_ret, child_ret;
    int x, y;
    unsigned int width, height, border_width, depth;

    if (!XGetGeometry(display, win, &root_ret, &x, &y, &width, &height, &border_width, &depth)) {
        return QPixmap();
    }

    if (width == 0 || height == 0) {
        return QPixmap();
    }

    // Check if XComposite is available for proper window capture
    int event_base, error_base;
    if (XCompositeQueryExtension(display, &event_base, &error_base)) {
        // Redirect the window temporarily to capture it independently of what's underneath
        XCompositeRedirectWindow(display, win, CompositeRedirectAutomatic);

        // Get the off-screen pixmap for the window
        Pixmap pixmap = XCompositeNameWindowPixmap(display, win);
        if (pixmap) {
            // Get the image from the pixmap
            XImage *image = XGetImage(display, pixmap, 0, 0, width, height, AllPlanes, ZPixmap);
            if (image) {
                QImage qimg = QImage(
                    reinterpret_cast<uchar*>(image->data),
                    image->width, image->height,
                    image->bytes_per_line,
                    QImage::Format_ARGB32
                );
                qimg = qimg.rgbSwapped(); // Convert BGR to RGB if needed
                QPixmap result = QPixmap::fromImage(qimg);

                XDestroyImage(image);
                XFreePixmap(display, pixmap);

                // Stop redirecting the window
                XCompositeUnredirectWindow(display, win, CompositeRedirectAutomatic);

                return result.scaled(thumbnailWidth, thumbnailHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }
            XFreePixmap(display, pixmap);
        }

        // Stop redirecting the window
        XCompositeUnredirectWindow(display, win, CompositeRedirectAutomatic);
    }

    // Fallback: Try to capture from the window directly using XGetImage on the window
    // This may not work for obscured windows but is the best we can do without composite
    XImage *image = XGetImage(display, win, 0, 0, width, height, AllPlanes, ZPixmap);
    if (image) {
        QImage qimg = QImage(
            reinterpret_cast<uchar*>(image->data),
            image->width, image->height,
            image->bytes_per_line,
            QImage::Format_ARGB32
        );
        qimg = qimg.rgbSwapped(); // Convert BGR to RGB
        QPixmap result = QPixmap::fromImage(qimg);
        XDestroyImage(image);
        return result.scaled(thumbnailWidth, thumbnailHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    return QPixmap(); // Return empty pixmap if capture failed
}

void AltTabWindow::refreshWindows() {
    windows = getWindows();
    updateWindowList();
}

void AltTabWindow::updateWindowList() {
    // Clear existing widgets
    QLayoutItem *child;
    while ((child = thumbnailsLayout->takeAt(0)) != nullptr) {
        delete child->widget();
        delete child;
    }
    
    // Add window thumbnails
    for (size_t i = 0; i < windows.size(); i++) {
        auto &winInfo = windows[i];
        
        // Create a widget for each window
        QWidget *windowWidget = new QWidget();
        windowWidget->setFixedSize(thumbnailWidth + 20, thumbnailHeight + 60);
        windowWidget->setStyleSheet(
            i == currentIndex ?
            "background-color: rgba(70, 130, 180, 200); border: 2px solid white; border-radius: 8px;" :
            "background-color: rgba(60, 60, 60, 180); border: 1px solid gray; border-radius: 8px;"
        );
        
        QVBoxLayout *layout = new QVBoxLayout(windowWidget);
        layout->setContentsMargins(5, 5, 5, 5);
        layout->setSpacing(5);
        
        // Thumbnail
        QLabel *thumbnailLabel = new QLabel();
        thumbnailLabel->setFixedSize(thumbnailWidth, thumbnailHeight);
        thumbnailLabel->setAlignment(Qt::AlignCenter);
        
        // Try to get cached thumbnail first
        auto it = thumbnailCache.find(windows[i].window);
        if (it != thumbnailCache.end()) {
            thumbnailLabel->setPixmap(it->second);
        } else {
            // Capture thumbnail and cache it
            QPixmap thumb = captureWindowThumbnail(windows[i].window);
            if (!thumb.isNull()) {
                thumbnailCache[windows[i].window] = thumb;
                thumbnailLabel->setPixmap(thumb);
            } else {
                // Create a placeholder
                QPixmap placeholder(thumbnailWidth, thumbnailHeight);
                placeholder.fill(Qt::darkGray);
                QPainter painter(&placeholder);
                painter.setPen(Qt::white);
                painter.drawText(placeholder.rect(), Qt::AlignCenter, "No Preview");
                thumbnailLabel->setPixmap(placeholder);
            }
        }
        
        layout->addWidget(thumbnailLabel);
        
        // Title
        QLabel *titleLabel = new QLabel(QString::fromStdString(windows[i].title.substr(0, 30) + (windows[i].title.length() > 30 ? "..." : "")));
        titleLabel->setStyleSheet("color: white; font-size: 12px;");
        titleLabel->setAlignment(Qt::AlignCenter);
        titleLabel->setWordWrap(true);
        layout->addWidget(titleLabel);
        
        // Add to layout
        thumbnailsLayout->addWidget(windowWidget);
    }

    // Update selection highlight
    updateSelection();
}

void AltTabWindow::updateSelection() {
    // Update highlights for all window widgets
    for (int i = 0; i < thumbnailsLayout->count(); i++) {
        QWidget *widget = thumbnailsLayout->itemAt(i)->widget();
        if (widget) {
            widget->setStyleSheet(
                static_cast<int>(i) == currentIndex ?
                "background-color: rgba(70, 130, 180, 200); border: 2px solid white; border-radius: 8px;" :
                "background-color: rgba(60, 60, 60, 180); border: 1px solid gray; border-radius: 8px;"
            );
        }
    }
    
    // Scroll to show current selection
    if (currentIndex < thumbnailsLayout->count()) {
        QWidget *currentWidget = thumbnailsLayout->itemAt(currentIndex)->widget();
        if (currentWidget) {
            currentWidget->ensurePolished();
            scrollArea->ensureWidgetVisible(currentWidget, 50, 50);
        }
    }
}

void AltTabWindow::nextWindow() {
    if (windows.empty()) return;
    
    currentIndex = (currentIndex + 1) % windows.size();
    updateSelection();
}

void AltTabWindow::prevWindow() {
    if (windows.empty()) return;
    
    currentIndex = (currentIndex - 1 + windows.size()) % windows.size();
    updateSelection();
}

void AltTabWindow::selectCurrentWindow() {
    if (static_cast<size_t>(currentIndex) < windows.size()) {
        activateWindow(windows[currentIndex].window);
        hideAltTab();
    }
}

void AltTabWindow::activateWindow(Window win) {
    if (!display) return;
    
    // Use _NET_ACTIVE_WINDOW to activate the window
    XEvent event;
    memset(&event, 0, sizeof(event));
    event.xclient.type = x11::XClientMessage;
    event.xclient.message_type = netActiveWindowAtom;
    event.xclient.display = display;
    event.xclient.window = win;
    event.xclient.format = 32;
    event.xclient.data.l[0] = 1; // Source indication: 1 = application
    event.xclient.data.l[1] = CurrentTime;
    event.xclient.data.l[2] = 0;
    event.xclient.data.l[3] = 0;
    event.xclient.data.l[4] = 0;
    
    XSendEvent(display, rootWindow, x11::XFalse, SubstructureRedirectMask | SubstructureNotifyMask, &event);
    XFlush(display);
    
    // Fallback: Use XSetInputFocus and XRaiseWindow
    XSetInputFocus(display, win, RevertToParent, CurrentTime);
    XRaiseWindow(display, win);
    XFlush(display);
}

void AltTabWindow::centerWindow() {
    QScreen *screen = QGuiApplication::primaryScreen();
    QRect screenGeometry = screen->geometry();
    
    int x = (screenGeometry.width() - width()) / 2;
    int y = (screenGeometry.height() - height()) / 3; // Higher position
    
    move(x, y);
}

void AltTabWindow::paintEvent(QPaintEvent *event) {
    // Draw rounded corners and transparency
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Draw background with rounded corners
    QPainterPath path;
    path.addRoundedRect(rect(), 10, 10);
    painter.fillPath(path, QColor(30, 30, 30, 200));
    
    // Draw border
    painter.setPen(QPen(QColor(100, 100, 100, 200), 2));
    painter.drawPath(path);
    
    QWidget::paintEvent(event);
}

void AltTabWindow::keyPressEvent(QKeyEvent *event) {
    switch (event->key()) {
        case Qt::Key_Tab:
            if (event->modifiers() & Qt::AltModifier) {
                nextWindow();
            } else {
                QWidget::keyPressEvent(event);
            }
            break;
        case Qt::Key_Backtab: // Shift+Tab
            if (event->modifiers() & Qt::AltModifier) {
                prevWindow();
            } else {
                QWidget::keyPressEvent(event);
            }
            break;
        case Qt::Key_Enter:
        case Qt::Key_Return:
            selectCurrentWindow();
            break;
        case Qt::Key_Escape:
            hideAltTab();
            break;
        default:
            QWidget::keyPressEvent(event);
            break;
    }
}

void AltTabWindow::keyReleaseEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Alt) {
        // If Alt key is released, select the current window
        selectCurrentWindow();
    } else {
        QWidget::keyReleaseEvent(event);
    }
}

void AltTabWindow::mousePressEvent(QMouseEvent *event) {
    // Find which window was clicked
    QPoint pos = event->pos();

    // Calculate which thumbnail was clicked based on layout
    int x_offset = mainLayout->contentsMargins().left();
    int y_offset = mainLayout->contentsMargins().top() + titleLabel->height() + mainLayout->spacing();

    // Iterate through the thumbnail widgets to find the clicked one
    for (int i = 0; i < thumbnailsLayout->count(); i++) {
        QWidget *widget = thumbnailsLayout->itemAt(i)->widget();
        if (widget) {
            // Get the widget's position relative to the thumbnails container
            QPoint widget_pos = widget->mapToParent(QPoint(0, 0));
            QRect widget_rect = QRect(widget_pos, widget->size());

            if (widget_rect.contains(pos)) {
                currentIndex = i;
                updateSelection();
                selectCurrentWindow();
                break;
            }
        }
    }

    QWidget::mousePressEvent(event);
}

void AltTabWindow::mouseDoubleClickEvent(QMouseEvent *event) {
    // On double click, activate the selected window
    selectCurrentWindow();
    QWidget::mouseDoubleClickEvent(event);
}

void AltTabWindow::setThumbnailSize(int width, int height) {
    thumbnailWidth = width;
    thumbnailHeight = height;
    refreshWindows(); // Refresh to update thumbnails with new size
}

void AltTabWindow::onWindowActivated() {
    // Update the active window indicator
    refreshWindows();
}

} // namespace havel