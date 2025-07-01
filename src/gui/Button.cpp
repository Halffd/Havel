#include "Button.hpp"

namespace havel {

Button::Button(const QString &text, QWidget *parent) : QPushButton(text, parent) {
}

Button::~Button() = default;

} // namespace havel