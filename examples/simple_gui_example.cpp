#include "types.hpp"
#include <iostream>

int main(int argc, char** argv) {
    havel::App app(argc, argv);

    havel::Window window;
    window.setWindowTitle("Simple Qt Example");
    window.resize(400, 300);

    havel::Widget* centralWidget = new havel::Widget();
    window.setCentralWidget(centralWidget);

    havel::HLayout* layout = new havel::HLayout(centralWidget);

    havel::Label* label = new havel::Label("Hello, Havel GUI with Qt!");
    layout->addWidget(label);

    havel::Button* button = new havel::Button("Click Me!");
    layout->addWidget(button);

    QObject::connect(button, &havel::Button::clicked, []() {
        havel::MessageBox::information(nullptr, "Info", "Button clicked!");
    });

    window.show();

    return app.exec();
}
