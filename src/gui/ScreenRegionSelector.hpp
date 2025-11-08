#pragma once

#include <QWidget>
#include <QRect>
#include <QPoint>

QT_BEGIN_NAMESPACE
class QPaintEvent;
class QMouseEvent;
QT_END_NAMESPACE

namespace havel {

class ScreenRegionSelector : public ::QWidget {
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

} // namespace havel