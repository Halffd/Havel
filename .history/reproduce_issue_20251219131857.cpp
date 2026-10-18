#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include "havel-lang/runtime/Interpreter.hpp"
#include "havel-lang/parser/Parser.h"
#include "havel-lang/lexer/Lexer.hpp"
#include "core/IO.hpp"
#include "window/WindowManager.hpp"
#include "core/HotkeyManager.hpp"
#include "core/BrightnessManager.hpp"
#include "media/AudioManager.hpp"
#include "gui/GUIManager.hpp"
#include "gui/ScreenshotManager.hpp"

// Mock dependencies
class MockIO : public havel::IO {
public:
    void Hotkey(const std::string& key, std::function<void()> callback) override {}
};

class MockWindowManager : public havel::WindowManager {
public:
    std::string GetActiveWindowTitle() override { return ""; }
    std::string GetActiveWindowClass() override { return ""; }
    havel::pID GetActiveWindowPID() override { return 0; }
    static std::string getProcessName(havel::pID pid) { return ""; }
};

int main() {
    MockIO io;
    MockWindowManager wm;
    havel::HotkeyManager hm;
    havel::BrightnessManager bm;
    havel::AudioManager am;
    havel::GUIManager gm;
    havel::ScreenshotManager sm;

    havel::Interpreter interpreter(io, wm, &hm, &bm, &am, &gm, &sm);

    std::cout << "Executing: let v = 2" << std::endl;
    auto result1 = interpreter.Execute("let v = 2");
    if (std::holds_alternative<havel::HavelRuntimeError>(result1)) {
        std::cerr << "Error 1: " << std::get<havel::HavelRuntimeError>(result1).what() << std::endl;
    } else {
        std::cout << "Result 1: " << havel::Interpreter::ValueToString(std::get<havel::HavelValue>(result1)) << std::endl;
    }

    std::cout << "Executing: v" << std::endl;
    auto result2 = interpreter.Execute("v");
    if (std::holds_alternative<havel::HavelRuntimeError>(result2)) {
        std::cerr << "Error 2: " << std::get<havel::HavelRuntimeError>(result2).what() << std::endl;
    } else {
        std::cout << "Result 2: " << havel::Interpreter::ValueToString(std::get<havel::HavelValue>(result2)) << std::endl;
    }

    return 0;
}
