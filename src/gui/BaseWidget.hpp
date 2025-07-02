#pragma once

#include <QWidget>
#include <memory>

namespace havel {

class BaseWidget : public QWidget {
    Q_OBJECT

public:
    explicit BaseWidget(QWidget *parent = nullptr);
    ~BaseWidget() override;
};

} // namespace havel