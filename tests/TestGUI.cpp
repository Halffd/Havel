#include <gtest/gtest.h>
#include <QApplication>
#include "types.hpp"


// Basic test to ensure the Qt Test GUI can be created
TEST(TestGUI, TestWindowCreation) {
    int argc = 1;
    char* argv[] = {(char*)"test"};
    havel::App app(argc, argv);
    havel::Window window;
    ASSERT_TRUE(window.isWindow());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
