// test_havel.cpp
#include "../lexer/Lexer.hpp"
#include "../parser/Parser.h"
#include "../runtime/Interpreter.hpp"
#include <iostream>
#include <fstream>
#include <cassert>
#include <functional>

// Test helper function to check if a condition is true
template <typename Func>
void test(const std::string& testName, Func testFunc) {
    try {
        bool result = testFunc();
        if (result) {
            std::cout << "✅ " << testName << ": PASS" << std::endl;
        } else {
            std::cout << "❌ " << testName << ": FAIL" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cout << "❌ " << testName << ": EXCEPTION - " << e.what() << std::endl;
    }
}

// Test the lexer functionality
void testLexer() {
    std::cout << "\n=== Testing Lexer ===" << std::endl;
    
    // Test basic lexing
    test("Basic Lexing", []() {
        std::string code = "F1 => send \"Hello World!\"";
        havel::Lexer lexer(code);
        auto tokens = lexer.tokenize();
        
        return tokens.size() > 3 && 
               tokens[0].type == havel::TokenType::Hotkey &&
               tokens[1].type == havel::TokenType::Arrow &&
               tokens[2].type == havel::TokenType::Identifier &&
               tokens[3].type == havel::TokenType::String;
    });
    
    // Test hotkey lexing
    test("Hotkey Lexing", []() {
        std::string code = "Ctrl+Shift+A => {}\nWin+Space => {}";
        havel::Lexer lexer(code);
        auto tokens = lexer.tokenize();
        
        return tokens.size() > 4 && 
               tokens[0].type == havel::TokenType::Hotkey &&
               tokens[0].value == "Ctrl+Shift+A" &&
               tokens[5].type == havel::TokenType::Hotkey &&
               tokens[5].value == "Win+Space";
    });
    
    // Test pipeline lexing
    test("Pipeline Lexing", []() {
        std::string code = "clipboard.out | text.upper | send";
        havel::Lexer lexer(code);
        auto tokens = lexer.tokenize();
        
        return tokens.size() > 5 && 
               tokens[0].type == havel::TokenType::Identifier &&
               tokens[1].type == havel::TokenType::Dot &&
               tokens[3].type == havel::TokenType::Pipe;
    });
}

// Test the parser functionality
void testParser() {
    std::cout << "\n=== Testing Parser ===" << std::endl;
    
    // Test basic parsing
    test("Basic Parsing", []() {
        std::string code = "F1 => send \"Hello World!\"";
        havel::parser::Parser parser;
        auto ast = parser.produceAST(code);
        
        return ast != nullptr && ast->body.size() == 1;
    });
    
    // Test hotkey binding parsing
    test("Hotkey Binding Parsing", []() {
        std::string code = "F1 => send \"Hello\"";
        havel::parser::Parser parser;
        auto ast = parser.produceAST(code);
        
        if (ast && ast->body.size() == 1) {
            auto hotkeyBinding = dynamic_cast<havel::ast::HotkeyBinding*>(ast->body[0].get());
            return hotkeyBinding != nullptr;
        }
        return false;
    });
    
    // Test block statement parsing
    test("Block Statement Parsing", []() {
        std::string code = "F1 => { send \"Hello\" }";
        havel::parser::Parser parser;
        auto ast = parser.produceAST(code);
        
        if (ast && ast->body.size() == 1) {
            auto hotkeyBinding = dynamic_cast<havel::ast::HotkeyBinding*>(ast->body[0].get());
            if (hotkeyBinding) {
                auto blockStmt = dynamic_cast<havel::ast::BlockStatement*>(hotkeyBinding->action.get());
                return blockStmt != nullptr && blockStmt->body.size() > 0;
            }
        }
        return false;
    });
    
    // Test pipeline expression parsing
    test("Pipeline Expression Parsing", []() {
        std::string code = "F1 => clipboard.out | text.upper | send";
        havel::parser::Parser parser;
        auto ast = parser.produceAST(code);
        
        if (ast && ast->body.size() == 1) {
            auto hotkeyBinding = dynamic_cast<havel::ast::HotkeyBinding*>(ast->body[0].get());
            if (hotkeyBinding) {
                auto pipelineExpr = dynamic_cast<havel::ast::PipelineExpression*>(hotkeyBinding->action.get());
                return pipelineExpr != nullptr;
            }
        }
        return false;
    });
}

// Test the interpreter functionality
void testInterpreter() {
    std::cout << "\n=== Testing Interpreter ===" << std::endl;
    
    // Test string evaluation
    test("String Evaluation", []() {
        havel::Interpreter interpreter;
        auto result = interpreter.Execute("F1 => \"Hello World!\"");
        
        return std::holds_alternative<std::string>(result) && 
               std::get<std::string>(result) == "Hello World!";
    });
    
    // Test number evaluation
    test("Number Evaluation", []() {
        havel::Interpreter interpreter;
        auto result = interpreter.Execute("F1 => 42");
        
        return std::holds_alternative<int>(result) && 
               std::get<int>(result) == 42;
    });
    
    // Test binary expression evaluation
    test("Binary Expression Evaluation", []() {
        havel::Interpreter interpreter;
        auto result = interpreter.Execute("F1 => 2 + 3 * 4");
        
        return std::holds_alternative<double>(result) && 
               std::get<double>(result) == 14.0;
    });
    
    // Test string concatenation
    test("String Concatenation", []() {
        havel::Interpreter interpreter;
        auto result = interpreter.Execute("F1 => \"Hello\" + \" \" + \"World\"");
        
        return std::holds_alternative<std::string>(result) && 
               std::get<std::string>(result) == "Hello World";
    });
    
    // Test text module functions
    test("Text Module Functions", []() {
        havel::Interpreter interpreter;
        auto result = interpreter.Execute("F1 => text.upper(\"hello\")");
        
        return std::holds_alternative<std::string>(result) && 
               std::get<std::string>(result) == "HELLO";
    });
    
    // Test pipeline expression
    test("Pipeline Expression", []() {
        havel::Interpreter interpreter;
        auto result = interpreter.Execute("F1 => \"hello world\" | text.upper | text.replace \" \" \"_\"");
        
        return std::holds_alternative<std::string>(result) && 
               std::get<std::string>(result) == "HELLO_WORLD";
    });
}

// Test the full Havel language with complex examples
void testFullLanguage() {
    std::cout << "\n=== Testing Full Language ===" << std::endl;
    
    // Test a complex script
    test("Complex Script", []() {
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

            // Complex hotkey with conditional
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
        
        havel::Interpreter interpreter;
        interpreter.RegisterHotkeys(havelCode);
        
        // If we got here without exceptions, consider it a success
        return true;
    });
}

int main() {
    try {
        // Run all tests
        testLexer();
        testParser();
        testInterpreter();
        testFullLanguage();
        
        std::cout << "\n🎯 All tests completed!" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "❌ Test error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
