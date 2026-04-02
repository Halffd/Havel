#include "BaseWidget.hpp"
#include "qt.hpp"

namespace havel {

BaseWidget::BaseWidget(QWidget *parent) : QWidget(parent) {
}

BaseWidget::~BaseWidget() = default;

} // namespace havel