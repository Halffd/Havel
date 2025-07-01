#pragma once

#include <QLabel>
#include <memory>

namespace havel {

class Label : public QLabel {
    Q_OBJECT

public:
    explicit Label(const QString &text, QWidget *parent = nullptr);
    ~Label() override;
};

} // namespace havel