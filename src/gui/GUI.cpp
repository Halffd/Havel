#include "GUI.hpp"
#include <QApplication>
#include <QMainWindow>

namespace havel {

GUI::GUI(QObject *parent) : QObject(parent) {
    int argc = 0;
    char **argv = nullptr;
    app = std::make_unique<QApplication>(argc, argv);
    mainWindow = std::make_unique<QMainWindow>();
}

GUI::~GUI() = default;

void GUI::run() {
    mainWindow->show();
    QApplication::exec();
}

} // namespace havel
