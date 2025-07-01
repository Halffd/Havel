#pragma once

#include <QObject>
#include <memory>

class QMainWindow;
class QApplication;

namespace havel {

class GUI : public QObject {
    Q_OBJECT

public:
    explicit GUI(QObject *parent = nullptr);
    ~GUI() override;

    void run();

private:
    std::unique_ptr<QApplication> app;
    std::unique_ptr<QMainWindow> mainWindow;
};

} // namespace havel