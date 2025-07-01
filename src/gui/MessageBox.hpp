#pragma once

#include <QMessageBox>
#include <memory>

namespace havel {

class MessageBox : public QMessageBox {
    Q_OBJECT

public:
    explicit MessageBox(QWidget *parent = nullptr);
    ~MessageBox() override;
};

} // namespace havel