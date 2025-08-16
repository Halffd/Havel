#include <gtest/gtest.h>
#include <QApplication>
#include <QTimer>
#include <QtTest/QTest>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include "qt.hpp"

class QtTestFixture : public ::testing::Test {
protected:
    void SetUp() override {
        if (!QApplication::instance()) {
            argc = 1;
            argv[0] = (char*)"test";
            app = std::make_unique<QApplication>(argc, argv);
        }
    }
    
    void TearDown() override {
        QApplication::closeAllWindows();
        QApplication::processEvents();
    }
    
    int argc = 1;
    char* argv[1] = {(char*)"test"};
    std::unique_ptr<QApplication> app;
};

// Basic window tests
TEST_F(QtTestFixture, TestWindowCreation) {
    havel::QWindow window;
    ASSERT_TRUE(window.isWindow());
    EXPECT_FALSE(window.isVisible());
}

TEST_F(QtTestFixture, TestWindowProperties) {
    havel::QWindow window;
    
    // Test title
    window.setWindowTitle("Test Window");
    EXPECT_EQ(window.windowTitle().toStdString(), "Test Window");
    
    // Test size
    window.resize(400, 300);
    EXPECT_EQ(window.width(), 400);
    EXPECT_EQ(window.height(), 300);
    
    // Test visibility
    window.show();
    QApplication::processEvents();
    EXPECT_TRUE(window.isVisible());
    
    window.hide();
    QApplication::processEvents();
    EXPECT_FALSE(window.isVisible());
}

TEST_F(QtTestFixture, TestWidgetCreation) {
    havel::QWindow window;
    
    // Create widgets
    auto button = std::make_unique<QPushButton>("Click Me");
    auto label = std::make_unique<QLabel>("Test Label");
    
    EXPECT_EQ(button->text().toStdString(), "Click Me");
    EXPECT_EQ(label->text().toStdString(), "Test Label");
    
    // Create central widget and layout
    auto centralWidget = new QWidget();
    auto layout = new QVBoxLayout(centralWidget);
    
    // Add widgets to layout
    layout->addWidget(button.get());
    layout->addWidget(label.get());
    
    // Set central widget
    window.setCentralWidget(centralWidget);
    
    // Verify layout and widgets
    EXPECT_TRUE(centralWidget->layout() != nullptr);
    EXPECT_EQ(layout->count(), 2);
}

TEST_F(QtTestFixture, TestButtonClicks) {
    havel::QWindow window;
    auto button = std::make_unique<QPushButton>("Click Me");
    
    // Test click event using lambda
    bool clicked = false;
    QObject::connect(button.get(), &QPushButton::clicked, [&]() {
        clicked = true;
    });
    
    // Setup window
    auto centralWidget = new QWidget();
    auto layout = new QVBoxLayout(centralWidget);
    layout->addWidget(button.get());
    window.setCentralWidget(centralWidget);
    
    window.show();
    QApplication::processEvents();
    
    // Simulate button click
    QTest::mouseClick(button.get(), Qt::LeftButton);
    QApplication::processEvents();
    
    EXPECT_TRUE(clicked);
}

TEST_F(QtTestFixture, TestLayoutManagement) {
    havel::QWindow window;
    
    auto centralWidget = new QWidget();
    auto vboxLayout = new QVBoxLayout(centralWidget);
    auto hboxLayout = new QHBoxLayout();
    
    auto button1 = new QPushButton("Button 1");
    auto button2 = new QPushButton("Button 2");
    auto button3 = new QPushButton("Button 3");
    
    // Test vertical layout
    vboxLayout->addWidget(button1);
    vboxLayout->addWidget(button2);
    
    // Test horizontal layout
    hboxLayout->addWidget(button3);
    
    // Test nested layouts
    vboxLayout->addLayout(hboxLayout);
    
    // Set central widget
    window.setCentralWidget(centralWidget);
    
    window.show();
    QApplication::processEvents();
    
    EXPECT_TRUE(centralWidget->layout() != nullptr);
    EXPECT_EQ(vboxLayout->count(), 3); // 2 widgets + 1 layout
}

TEST_F(QtTestFixture, TestKeyboardInput) {
    havel::QWindow window;
    auto lineEdit = new QLineEdit();
    
    // Setup window
    auto centralWidget = new QWidget();
    auto layout = new QVBoxLayout(centralWidget);
    layout->addWidget(lineEdit);
    window.setCentralWidget(centralWidget);
    
    window.show();
    lineEdit->setFocus();
    QApplication::processEvents();
    
    // Test keyboard input
    QTest::keyClicks(lineEdit, "Hello World");
    QApplication::processEvents();
    
    EXPECT_EQ(lineEdit->text().toStdString(), "Hello World");
}

TEST_F(QtTestFixture, TestAsyncTimer) {
    havel::QWindow window;
    bool timerTriggered = false;
    
    // Test async operation with QTimer
    QTimer::singleShot(50, [&]() {
        timerTriggered = true;
    });
    
    window.show();
    QApplication::processEvents();
    
    // Wait for timer
    QTest::qWait(100);
    QApplication::processEvents();
    
    EXPECT_TRUE(timerTriggered);
}

TEST_F(QtTestFixture, TestMultipleButtons) {
    havel::QWindow window;
    
    auto button1 = new QPushButton("Button 1");
    auto button2 = new QPushButton("Button 2");
    
    int button1Clicks = 0;
    int button2Clicks = 0;
    
    // Connect both buttons
    QObject::connect(button1, &QPushButton::clicked, [&]() { button1Clicks++; });
    QObject::connect(button2, &QPushButton::clicked, [&]() { button2Clicks++; });
    
    // Setup window
    auto centralWidget = new QWidget();
    auto layout = new QHBoxLayout(centralWidget);
    layout->addWidget(button1);
    layout->addWidget(button2);
    window.setCentralWidget(centralWidget);
    
    window.show();
    QApplication::processEvents();
    
    // Test clicking both buttons
    QTest::mouseClick(button1, Qt::LeftButton);
    QTest::mouseClick(button2, Qt::LeftButton);
    QTest::mouseClick(button1, Qt::LeftButton);
    QApplication::processEvents();
    
    EXPECT_EQ(button1Clicks, 2);
    EXPECT_EQ(button2Clicks, 1);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    
    // Ensure we have a QApplication for all tests
    QApplication app(argc, argv);
    
    int result = RUN_ALL_TESTS();
    
    return result;
}