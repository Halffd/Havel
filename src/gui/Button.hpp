#pragma once

#include "Widget.hpp"
#include <QPushButton>
#include <memory>

namespace havel {

class Button : public QPushButton {
    Q_OBJECT

public:
    explicit Button(const QString &text, QWidget *parent = nullptr);
    ~Button() override;
};

} // namespace havel