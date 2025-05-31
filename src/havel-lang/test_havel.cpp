// test_lexer.cpp
#include "lexer/Lexer.hpp"
#include <iostream>
#include <fstream>

int main() {
    // Test Havel code
    std::string havelCode = R"(
        // Simple hotkey automation
        F1 => send "Hello World!"

        // Pipeline automation
        F2 => {
            clipboard.out
                | text.upper
                | text.replace " " "_"
                | send
        }

        // Complex hotkey
        Ctrl+V => {
            let content = clipboard.out
            if content | text.contains "error" {
                send "DEBUG: " + content
            } else {
                send content | text.sanitize
            }
        }

        // Window management
        #1 => window.focus("VS Code")
        !Tab => window.next()
    )";

    try {
        havel::Lexer lexer(havelCode);
        auto tokens = lexer.tokenize();
        lexer.printTokens(tokens);

        std::cout << "\n🎯 Successfully tokenized " << tokens.size() << " tokens!" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "❌ Lexer error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
