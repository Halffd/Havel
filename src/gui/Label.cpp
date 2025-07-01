#include "Label.hpp"

namespace havel {

Label::Label(const QString &text, QWidget *parent) : QLabel(text, parent) {
}

Label::~Label() = default;

} // namespace havel