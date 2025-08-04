#include <gtest/gtest.h>
#include <QApplication>
#include <QTimer>
#include <QtTest/QTest>
#include "qt.hpp"
#include "types.hpp"

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
        // Clean up any windows
        QApplication::closeAllWindows();
        QApplication::processEvents();
    }
    
    int argc = 1;
    char* argv[1] = {(char*)"test"};
    std::unique_ptr<QApplication> app;
};

// Basic test to ensure the Qt GUI can be created
TEST_F(QtTestFixture, TestWindowCreation) {
    havel::QWindow window;
    ASSERT_TRUE(window.isWindow());
    EXPECT_FALSE(window.isVisible());
}

TEST_F(QtTestFixture, TestWindowProperties) {
    havel::QWindow window;
    
    // Test title
    window.setTitle("Test Window");
    EXPECT_EQ(window.getTitle(), "Test Window");
    
    // Test size
    window.resize(400, 300);
    EXPECT_EQ(window.width(), 400);
    EXPECT_EQ(window.height(), 300);
    
    // Test visibility
    window.show();
    QApplication::processEvents(); // Process show event
    EXPECT_TRUE(window.isVisible());
    
    window.hide();
    QApplication::processEvents(); // Process hide event
    EXPECT_FALSE(window.isVisible());
}

TEST_F(QtTestFixture, TestWidgetCreation) {
    havel::QWindow window;
    
    // Create widgets
    auto button = std::make_unique<havel::QButton>("Click Me");
    auto label = std::make_unique<havel::QLabel>("Test Label");
    
    EXPECT_EQ(button->getText(), "Click Me");
    EXPECT_EQ(label->getText(), "Test Label");
    
    // Add widgets to window
    window.addWidget(button.get());
    window.addWidget(label.get());
    
    // Test layout
    auto layout = std::make_unique<havel::QVBoxLayout>();
    window.setLayout(layout.get());
    
    EXPECT_TRUE(window.hasLayout());
    EXPECT_EQ(window.getWidgetCount(), 2);
}

TEST_F(QtTestFixture, TestEventHandling) {
    havel::QWindow window;
    auto button = std::make_unique<havel::QButton>("Click Me");
    
    // Test click event
    bool clicked = false;
    button->addEventListener("click", [&clicked]() {
        clicked = true;
    });
    
    window.addWidget(button.get());
    window.show();
    QApplication::processEvents();
    
    // Simulate button click
    button->simulateClick();
    QApplication::processEvents();
    
    EXPECT_TRUE(clicked);
}

TEST_F(QtTestFixture, TestKeyEventHandling) {
    havel::QWindow window;
    
    bool keyPressed = false;
    window.addEventListener("keyPress", "a", [&keyPressed]() {
        keyPressed = true;
    });
    
    window.show();
    window.setFocus();
    QApplication::processEvents();
    
    // Simulate key press
    QTest::keyClick(&window, Qt::Key_A);
    QApplication::processEvents();
    
    EXPECT_TRUE(keyPressed);
}

TEST_F(QtTestFixture, TestMultipleEventListeners) {
    havel::QWindow window;
    auto button = std::make_unique<havel::QButton>("Test Button");
    
    int clickCount = 0;
    bool doubleClicked = false;
    
    // Multiple event listeners on same widget
    button->addEventListener("click", [&clickCount]() {
        clickCount++;
    });
    
    button->addEventListener("doubleClick", [&doubleClicked]() {
        doubleClicked = true;
    });
    
    window.addWidget(button.get());
    window.show();
    QApplication::processEvents();
    
    // Test single clicks
    button->simulateClick();
    button->simulateClick();
    QApplication::processEvents();
    
    EXPECT_EQ(clickCount, 2);
    
    // Test double click
    button->simulateDoubleClick();
    QApplication::processEvents();
    
    EXPECT_TRUE(doubleClicked);
}

TEST_F(QtTestFixture, TestWindowLifecycle) {
    auto window = std::make_unique<havel::QWindow>();
    
    // Test window creation
    EXPECT_TRUE(window->isWindow());
    EXPECT_FALSE(window->isVisible());
    
    // Test show/hide cycle
    window->show();
    QApplication::processEvents();
    EXPECT_TRUE(window->isVisible());
    
    window->hide();
    QApplication::processEvents();
    EXPECT_FALSE(window->isVisible());
    
    // Test window destruction
    window.reset();
    QApplication::processEvents();
    // Window should be destroyed without crashing
}

TEST_F(QtTestFixture, TestLayoutManagement) {
    havel::QWindow window;
    
    auto vboxLayout = std::make_unique<havel::QVBoxLayout>();
    auto hboxLayout = std::make_unique<havel::QHBoxLayout>();
    
    auto button1 = std::make_unique<havel::QButton>("Button 1");
    auto button2 = std::make_unique<havel::QButton>("Button 2");
    auto button3 = std::make_unique<havel::QButton>("Button 3");
    
    // Test vertical layout
    vboxLayout->addWidget(button1.get());
    vboxLayout->addWidget(button2.get());
    
    // Test horizontal layout
    hboxLayout->addWidget(button3.get());
    
    // Test nested layouts
    vboxLayout->addLayout(hboxLayout.get());
    window.setLayout(vboxLayout.get());
    
    window.show();
    QApplication::processEvents();
    
    EXPECT_TRUE(window.hasLayout());
    EXPECT_EQ(vboxLayout->getWidgetCount(), 2);
    EXPECT_EQ(hboxLayout->getWidgetCount(), 1);
}

TEST_F(QtTestFixture, TestAsyncEventHandling) {
    havel::QWindow window;
    auto button = std::make_unique<havel::QButton>("Async Test");
    
    bool asyncEventFired = false;
    
    // Test async event with timer
    button->addEventListener("click", [&asyncEventFired]() {
        QTimer::singleShot(100, [&asyncEventFired]() {
            asyncEventFired = true;
        });
    });
    
    window.addWidget(button.get());
    window.show();
    QApplication::processEvents();
    
    button->simulateClick();
    QApplication::processEvents();
    
    // Wait for async event
    QTest::qWait(200);
    
    EXPECT_TRUE(asyncEventFired);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    
    // Ensure we have a QApplication for all tests
    QApplication app(argc, argv);
    
    int result = RUN_ALL_TESTS();
    
    return result;
}