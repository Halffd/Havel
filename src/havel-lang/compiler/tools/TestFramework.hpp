#pragma once

#include <string>
#include <vector>
#include <functional>
#include <iostream>

namespace havel::compiler {

// Minimal test framework stub
class TestFramework {
public:
    struct TestCase {
        std::string name;
        std::function<bool()> test;
    };

    struct TestResult {
        bool passed;
        std::string message;
    };

    static TestFramework& instance();

    void registerTest(const std::string& suite, const TestCase& test);
    bool runAll();
    bool runSuite(const std::string& name);
    bool runTest(const std::string& suite, const std::string& test);

private:
    TestFramework() = default;
    std::unordered_map<std::string, std::vector<TestCase>> suites_;
};

// Test assertion macros
#define ASSERT_TRUE(expr) if (!(expr)) { return false; }
#define ASSERT_FALSE(expr) if (expr) { return false; }
#define ASSERT_EQ(a, b) if ((a) != (b)) { return false; }
#define ASSERT_NE(a, b) if ((a) == (b)) { return false; }

} // namespace havel::compiler
