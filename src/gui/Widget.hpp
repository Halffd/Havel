#pragma once

#include <QWidget>
#include <memory>

namespace havel {

class Widget : public QWidget {
    Q_OBJECT

public:
    explicit Widget(QWidget *parent = nullptr);
    ~Widget() override;
};

} // namespace havel