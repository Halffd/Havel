#include "ScreenRegionSelector.hpp"
#include <QPainter>
#include <QMouseEvent>

// Implement in global namespace
havel::ScreenRegionSelector::ScreenRegionSelector(QWidget *parent) : QWidget(parent), selecting(false) {
    setWindowFlags(Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setCursor(Qt::CrossCursor);
    showFullScreen();
}

void havel::ScreenRegionSelector::paintEvent(::QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(rect(), QColor(0, 0, 0, 100));

    if (selecting) {
        painter.setPen(Qt::red, 2);
        painter.drawRect(selectionRect);
    }
}

void havel::ScreenRegionSelector::mousePressEvent(::QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        selecting = true;
        startPos = event->pos();
        selectionRect = QRect(startPos, QSize());
    }
}

void havel::ScreenRegionSelector::mouseMoveEvent(::QMouseEvent *event) {
    if (selecting) {
        selectionRect = QRect(startPos, event->pos()).normalized();
        update();
    }
}

void havel::ScreenRegionSelector::mouseReleaseEvent(::QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && selecting) {
        selecting = false;
        emit regionSelected(selectionRect);
        close();
    }
}
