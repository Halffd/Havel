#include "AutomationSuite.hpp"
#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    havel::AutomationSuite suite;
    
    return app.exec();
}
