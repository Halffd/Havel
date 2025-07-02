#include "GUILauncher.hpp"
#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    GUILauncher launcher;
    launcher.show();
    return app.exec();
}
