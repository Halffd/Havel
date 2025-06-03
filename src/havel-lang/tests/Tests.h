#include <iostream>
#include <chrono>
#include <vector>

// Test Framework
class Tests {
private:
    int totalTests = 0;
    int passedTests = 0;
    std::vector<std::string> failedTests;

public:
    template <typename Func>
    void test(const std::string& testName, Func testFunc) {
        totalTests++;
        try {
            auto start = std::chrono::high_resolution_clock::now();
            bool result = testFunc();
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

            if (result) {
                std::cout << "PASS " << testName << " (" << duration.count() << " us)" << std::endl;
                passedTests++;
            } else {
                std::cout << "FAIL " << testName << std::endl;
                failedTests.push_back(testName);
            }
        } catch (const std::exception& e) {
            std::cout << "EXCEPTION " << testName << " - " << e.what() << std::endl;
            failedTests.push_back(testName + " (Exception)");
        }
    }

    void printSummary() {
        std::cout << "\nTEST SUMMARY" << std::endl;
        std::cout << "Total Tests: " << totalTests << std::endl;
        std::cout << "Passed: " << passedTests << std::endl;
        std::cout << "Failed: " << (totalTests - passedTests) << std::endl;
        std::cout << "Success Rate: " << (double(passedTests) / totalTests * 100) << "%" << std::endl;

        if (!failedTests.empty()) {
            std::cout << "\nFailed Tests:" << std::endl;
            for (const auto& test : failedTests) {
                std::cout << "  - " << test << std::endl;
            }
        }
    }

    bool allTestsPassed() const {
        return passedTests == totalTests;
    }
};