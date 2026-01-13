#include "ScreenRegionSelector.hpp"
#include <QPainter>
#include <QPen>
#include <QMouseEvent>

namespace havel {

ScreenRegionSelector::ScreenRegionSelector(QWidget *parent)
    : QWidget(parent), selecting(false) {
    setWindowFlags(Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);

    // Create a larger crosshair cursor
    QPixmap cursorPixmap(32, 32);
    cursorPixmap.fill(Qt::transparent);
    QPainter cursorPainter(&cursorPixmap);
    cursorPainter.setPen(QPen(Qt::white, 2));
    cursorPainter.drawLine(16, 0, 16, 32);  // Vertical line
    cursorPainter.drawLine(0, 16, 32, 16);  // Horizontal line
    cursorPainter.setPen(QPen(Qt::blue, 1));  // Inner blue lines
    cursorPainter.drawLine(16, 8, 16, 24);  // Vertical line
    cursorPainter.drawLine(8, 16, 24, 16);  // Horizontal line

    QCursor bigCrosshair(cursorPixmap, 16, 16);
    setCursor(bigCrosshair);

    showFullScreen();
}

void ScreenRegionSelector::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(rect(), QColor(0, 0, 0, 100));

    if (selecting) {
        // Draw blue border boxes with enhanced visibility
        painter.setPen(QPen(Qt::blue, 3, Qt::SolidLine));
        painter.drawRect(selectionRect);

        // Draw corner boxes for better visibility
        int cornerSize = 10;

        // Top-left corner
        painter.fillRect(selectionRect.x(), selectionRect.y(), cornerSize, cornerSize, Qt::blue);
        // Top-right corner
        painter.fillRect(selectionRect.right() - cornerSize + 1, selectionRect.y(), cornerSize, cornerSize, Qt::blue);
        // Bottom-left corner
        painter.fillRect(selectionRect.x(), selectionRect.bottom() - cornerSize + 1, cornerSize, cornerSize, Qt::blue);
        // Bottom-right corner
        painter.fillRect(selectionRect.right() - cornerSize + 1, selectionRect.bottom() - cornerSize + 1, cornerSize, cornerSize, Qt::blue);
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
        emit regionSelected(selectionRect);
        close();
    }
}

} // namespace havel