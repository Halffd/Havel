#pragma once

#include <QVBoxLayout>
#include <memory>

namespace havel {

class Layout : public QVBoxLayout {
    Q_OBJECT

public:
    explicit Layout(QWidget *parent = nullptr);
    ~Layout() override;
};

} // namespace havel