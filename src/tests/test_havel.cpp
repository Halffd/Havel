#include "havel-lang/lexer/Lexer.hpp"
#include "havel-lang/parser/Parser.h"
#include "havel-lang/runtime/Interpreter.hpp"
#include "havel-lang/runtime/Engine.h"

#ifdef HAVEL_ENABLE_LLVM
#include "havel-lang/compiler/Compiler.h"
#include "havel-lang/compiler/JIT.h"
#endif

#include <iostream>
#include <fstream>
#include <cassert>
#include <functional>
#include <chrono>
#include <vector>
#include <sstream>
#include "../havel-lang/tests/Tests.h"

// LEXER TESTS
void testLexer(Tests& tf) {
    std::cout << "\n=== TESTING LEXER ===" << std::endl;

    tf.test("Basic Token Recognition", []() {
        std::string code = "F1 => send \"Hello World!\"";
        havel::Lexer lexer(code);
        auto tokens = lexer.tokenize();

        return tokens.size() >= 4 &&
               tokens[0].type == havel::TokenType::Hotkey &&
               tokens[1].type == havel::TokenType::Arrow &&
               tokens[2].type == havel::TokenType::Identifier &&
               tokens[3].type == havel::TokenType::String;
    });

    tf.test("Complex Hotkey Recognition", []() {
        std::string code = "^+!F12 => {}";
        havel::Lexer lexer(code);
        auto tokens = lexer.tokenize();

        return tokens.size() >= 3 &&
               tokens[0].type == havel::TokenType::Hotkey &&
               tokens[0].value == "^+!F12";
    });

    tf.test("Pipeline Operator Recognition", []() {
        std::string code = "clipboard.get | text.upper | send";
        havel::Lexer lexer(code);
        auto tokens = lexer.tokenize();

        bool foundPipes = false;
        for (const auto& token : tokens) {
            if (token.type == havel::TokenType::Pipe) {
                foundPipes = true;
                break;
            }
        }
        return foundPipes;
    });

    tf.test("String Literal Parsing", []() {
        std::string code = "\"Hello World with spaces\"";
        havel::Lexer lexer(code);
        auto tokens = lexer.tokenize();

        return tokens.size() >= 1 &&
               tokens[0].type == havel::TokenType::String &&
               tokens[0].value.find("Hello") != std::string::npos;
    });

    tf.test("Number Literal Recognition", []() {
        std::string code = "42 3.14159 -100";
        havel::Lexer lexer(code);
        auto tokens = lexer.tokenize();

        int numberCount = 0;
        for (const auto& token : tokens) {
            if (token.type == havel::TokenType::Number) {
                numberCount++;
            }
        }
        return numberCount >= 2;
    });

    tf.test("Identifier Recognition", []() {
        std::string code = "clipboard window text send";
        havel::Lexer lexer(code);
        auto tokens = lexer.tokenize();

        int identifierCount = 0;
        for (const auto& token : tokens) {
            if (token.type == havel::TokenType::Identifier) {
                identifierCount++;
            }
        }
        return identifierCount >= 4;
    });

    tf.test("Dot Operator Recognition", []() {
        std::string code = "clipboard.get text.upper window.focus";
        havel::Lexer lexer(code);
        auto tokens = lexer.tokenize();

        int dotCount = 0;
        for (const auto& token : tokens) {
            if (token.type == havel::TokenType::Dot) {
                dotCount++;
            }
        }
        return dotCount >= 3;
    });

    tf.test("Brace Recognition", []() {
        std::string code = "{ send \"hello\" }";
        havel::Lexer lexer(code);
        auto tokens = lexer.tokenize();

        bool foundOpenBrace = false;
        bool foundCloseBrace = false;
        for (const auto& token : tokens) {
            if (token.type == havel::TokenType::OpenBrace) {
                foundOpenBrace = true;
            }
            if (token.type == havel::TokenType::CloseBrace) {
                foundCloseBrace = true;
            }
        }
        return foundOpenBrace && foundCloseBrace;
    });
}

// PARSER TESTS
void testParser(Tests& tf) {
    std::cout << "\n=== TESTING PARSER ===" << std::endl;

    tf.test("Basic AST Generation", []() {
        std::string code = "F1 => send \"Hello\"";
        havel::parser::Parser parser;
        auto ast = parser.produceAST(code);

        return ast != nullptr && ast->body.size() == 1;
    });

    tf.test("Hotkey Binding AST", []() {
        std::string code = "^V => clipboard.paste";
        havel::parser::Parser parser;
        auto ast = parser.produceAST(code);

        if (ast && ast->body.size() == 1) {
            auto hotkeyBinding = dynamic_cast<havel::ast::HotkeyBinding*>(ast->body[0].get());
            return hotkeyBinding != nullptr;
        }
        return false;
    });

    tf.test("If Statement In Hotkey Block", []() {
        std::string code = "F1 => { if true { send \"a\" } }";
        havel::parser::Parser parser;
        auto ast = parser.produceAST(code);
        return ast != nullptr && ast->body.size() == 1;
    });

    tf.test("Sequential If Statements In Block", []() {
        std::string code = "F1 => { if true { send \"a\" } if true { send \"b\" } }";
        havel::parser::Parser parser;
        auto ast = parser.produceAST(code);
        return ast != nullptr && ast->body.size() == 1;
    });

    tf.test("Pipeline Expression AST", []() {
        std::string code = "F1 => clipboard.get | text.upper | send";
        havel::parser::Parser parser;
        auto ast = parser.produceAST(code);

        if (ast && ast->body.size() == 1) {
            auto hotkeyBinding = dynamic_cast<havel::ast::HotkeyBinding*>(ast->body[0].get());
            if (hotkeyBinding) {
                auto exprStmt = dynamic_cast<havel::ast::ExpressionStatement*>(hotkeyBinding->action.get());
                if (exprStmt) {
                    auto pipeline = dynamic_cast<havel::ast::PipelineExpression*>(exprStmt->expression.get());
                    return pipeline != nullptr && pipeline->stages.size() >= 3;
                }
            }
        }
        return false;
    });

    tf.test("Block Statement AST", []() {
        std::string code = "F1 => { send \"Line 1\" send \"Line 2\" }";
        havel::parser::Parser parser;
        auto ast = parser.produceAST(code);

        if (ast && ast->body.size() == 1) {
            auto hotkeyBinding = dynamic_cast<havel::ast::HotkeyBinding*>(ast->body[0].get());
            if (hotkeyBinding) {
                auto block = dynamic_cast<havel::ast::BlockStatement*>(hotkeyBinding->action.get());
                return block != nullptr && block->body.size() >= 2;
            }
        }
        return false;
    });

    tf.test("Member Expression AST", []() {
        std::string code = "F1 => window.title";
        havel::parser::Parser parser;
        auto ast = parser.produceAST(code);

        if (ast && ast->body.size() == 1) {
            auto hotkeyBinding = dynamic_cast<havel::ast::HotkeyBinding*>(ast->body[0].get());
            if (hotkeyBinding) {
                 auto exprStmt = dynamic_cast<havel::ast::ExpressionStatement*>(hotkeyBinding->action.get());
                 if(exprStmt) {
                    auto member = dynamic_cast<havel::ast::MemberExpression*>(exprStmt->expression.get());
                    return member != nullptr;
                 }
            }
        }
        return false;
    });

    tf.test("Call Expression AST", []() {
        std::string code = "F1 => send(\"Hello\")";
        havel::parser::Parser parser;
        auto ast = parser.produceAST(code);

        if (ast && ast->body.size() == 1) {
            auto hotkeyBinding = dynamic_cast<havel::ast::HotkeyBinding*>(ast->body[0].get());
            if (hotkeyBinding) {
                auto exprStmt = dynamic_cast<havel::ast::ExpressionStatement*>(hotkeyBinding->action.get());
                if(exprStmt) {
                    auto call = dynamic_cast<havel::ast::CallExpression*>(exprStmt->expression.get());
                    return call != nullptr;
                }
            }
        }
        return false;
    });

    tf.test("String Literal AST", []() {
        std::string code = "F1 => \"Hello World\"";
        havel::parser::Parser parser;
        auto ast = parser.produceAST(code);

        if (ast && ast->body.size() == 1) {
            auto hotkeyBinding = dynamic_cast<havel::ast::HotkeyBinding*>(ast->body[0].get());
            if (hotkeyBinding) {
                auto exprStmt = dynamic_cast<havel::ast::ExpressionStatement*>(hotkeyBinding->action.get());
                if (exprStmt) {
                    auto stringLit = dynamic_cast<havel::ast::StringLiteral*>(exprStmt->expression.get());
                    return stringLit != nullptr && stringLit->value == "Hello World";
                }
            }
        }
        return false;
    });

    tf.test("Number Literal AST", []() {
        std::string code = "F1 => 42";
        havel::parser::Parser parser;
        auto ast = parser.produceAST(code);

        if (ast && ast->body.size() == 1) {
            auto hotkeyBinding = dynamic_cast<havel::ast::HotkeyBinding*>(ast->body[0].get());
            if (hotkeyBinding) {
                auto exprStmt = dynamic_cast<havel::ast::ExpressionStatement*>(hotkeyBinding->action.get());
                if (exprStmt) {
                    auto numberLit = dynamic_cast<havel::ast::NumberLiteral*>(exprStmt->expression.get());
                    return numberLit != nullptr && numberLit->value == 42.0;
                }
            }
        }
        return false;
    });

    tf.test("Multiple Hotkey Bindings", []() {
        std::string code = "F1 => send \"Hello\"\nF2 => send \"World\"";
        havel::parser::Parser parser;
        auto ast = parser.produceAST(code);

        return ast != nullptr && ast->body.size() == 2;
    });
}

// INTERPRETER TESTS
void testInterpreter(Tests& tf) {
    std::cout << "\n=== TESTING INTERPRETER ===" << std::endl;
    havel::IO io;
    havel::WindowManager wm;

    tf.test("String Evaluation", [&]() {
        havel::Interpreter interpreter(io, wm);
        auto result = interpreter.Execute("F1 => \"Hello World!\"");

        if (auto* value = std::get_if<havel::HavelValue>(&result)) {
            return std::holds_alternative<std::string>(*value) &&
                   std::get<std::string>(*value) == "Hello World!";
        }
        return false;
    });

    tf.test("Number Evaluation", [&]() {
        havel::Interpreter interpreter(io, wm);
        auto result = interpreter.Execute("F1 => 42");

        if (auto* value = std::get_if<havel::HavelValue>(&result)) {
            return std::holds_alternative<double>(*value) &&
                   std::get<double>(*value) == 42.0;
        }
        return false;
    });

    tf.test("Binary Expression Evaluation", [&]() {
        havel::Interpreter interpreter(io, wm);
        auto result = interpreter.Execute("F1 => 2 + 3");
        
        if (auto* value = std::get_if<havel::HavelValue>(&result)) {
            return std::holds_alternative<double>(*value) &&
                   std::get<double>(*value) == 5.0;
        }
        return false;
    });

    tf.test("String Concatenation", [&]() {
        havel::Interpreter interpreter(io, wm);
        auto result = interpreter.Execute("F1 => \"Hello\" + \" \" + \"World\"");
        
        if (auto* value = std::get_if<havel::HavelValue>(&result)) {
            return std::holds_alternative<std::string>(*value) &&
                   std::get<std::string>(*value) == "Hello World";
        }
        return false;
    });

    tf.test("Value To String Conversion", []() {
        std::string testStr = havel::Interpreter::ValueToString(std::string("test"));
        int testInt = 42;
        std::string intStr = havel::Interpreter::ValueToString(testInt);

        return testStr == "test" && intStr == "42";
    });

    tf.test("Value To Boolean Conversion", []() {
        bool emptyStringBool = havel::Interpreter::ValueToBool(std::string(""));
        bool nonEmptyStringBool = havel::Interpreter::ValueToBool(std::string("hello"));
        bool zeroBool = havel::Interpreter::ValueToBool(0);
        bool nonZeroBool = havel::Interpreter::ValueToBool(42);

        return !emptyStringBool && nonEmptyStringBool && !zeroBool && nonZeroBool;
    });

    tf.test("Value To Number Conversion", []() {
        double stringNum = havel::Interpreter::ValueToNumber(std::string("42.5"));
        double intNum = havel::Interpreter::ValueToNumber(42);
        double boolNum = havel::Interpreter::ValueToNumber(true);

        return stringNum == 42.5 && intNum == 42.0 && boolNum == 1.0;
    });

    tf.test("Hotkey Registration", [&]() {
        havel::Interpreter interpreter(io, wm);
        std::string havelCode = "F1 => print(\"Hello\")";

        // Should not throw exception
        interpreter.RegisterHotkeys(havelCode);
        return true;
    });
}

#ifdef HAVEL_ENABLE_LLVM
// COMPILER TESTS
void testCompiler(Tests& tf) {
    std::cout << "\n=== TESTING LLVM COMPILER ===" << std::endl;

    tf.test("Compiler Initialization", []() {
        try {
            havel::compiler::Compiler compiler;
            compiler.Initialize();
            return true;
        } catch (const std::exception&) {
            return false;
        }
    });

    tf.test("String Literal Compilation", []() {
        try {
            havel::compiler::Compiler compiler;
            compiler.Initialize();

            havel::ast::StringLiteral stringLit("Hello World");
            auto result = compiler.GenerateStringLiteral(stringLit);

            return result != nullptr;
        } catch (const std::exception&) {
            return false;
        }
    });

    tf.test("Number Literal Compilation", []() {
        try {
            havel::compiler::Compiler compiler;
            compiler.Initialize();

            havel::ast::NumberLiteral numberLit(42.0);
            auto result = compiler.GenerateNumberLiteral(numberLit);

            return result != nullptr;
        } catch (const std::exception&) {
            return false;
        }
    });

    tf.test("Identifier Compilation", []() {
        try {
            havel::compiler::Compiler compiler;
            compiler.Initialize();
            compiler.CreateStandardLibrary();

            havel::ast::Identifier identifier("send");
            auto result = compiler.GenerateIdentifier(identifier);

            return result != nullptr;
        } catch (const std::exception&) {
            return false;
        }
    });

    tf.test("Standard Library Creation", []() {
        try {
            havel::compiler::Compiler compiler;
            compiler.Initialize();
            compiler.CreateStandardLibrary();

            return true;
        } catch (const std::exception&) {
            return false;
        }
    });

    tf.test("Module Verification", []() {
        try {
            havel::compiler::Compiler compiler;
            compiler.Initialize();
            compiler.CreateStandardLibrary();

            return compiler.VerifyModule();
        } catch (const std::exception&) {
            return false;
        }
    });

    tf.test("Variable Management", []() {
        try {
            havel::compiler::Compiler compiler;
            compiler.Initialize();

            havel::ast::StringLiteral stringLit("test");
            auto value = compiler.GenerateStringLiteral(stringLit);

            compiler.SetVariable("testVar", value);
            auto retrieved = compiler.GetVariable("testVar");

            return retrieved == value;
        } catch (const std::exception&) {
            return false;
        }
    });

    tf.test("Pipeline Compilation", []() {
        try {
            havel::compiler::Compiler compiler;
            compiler.Initialize();
            compiler.CreateStandardLibrary();

            havel::ast::PipelineExpression pipeline;

            auto stage1 = std::make_unique<havel::ast::Identifier>("clipboard.out");
            auto stage2 = std::make_unique<havel::ast::Identifier>("text.upper");
            auto stage3 = std::make_unique<havel::ast::Identifier>("send");

            pipeline.stages.push_back(std::move(stage1));
            pipeline.stages.push_back(std::move(stage2));
            pipeline.stages.push_back(std::move(stage3));

            auto result = compiler.GeneratePipeline(pipeline);

            return result != nullptr;
        } catch (const std::exception&) {
            return false;
        }
    });
}

// JIT TESTS
void testJIT(Tests& tf) {
    std::cout << "\n=== TESTING JIT ENGINE ===" << std::endl;

    tf.test("JIT Initialization", []() {
        try {
            havel::compiler::JIT jit;
            return true;
        } catch (const std::exception&) {
            return false;
        }
    });

    tf.test("Simple Hotkey Compilation", []() {
        try {
            havel::parser::Parser parser;
            havel::compiler::JIT jit;

            std::string code = "F1 => send \"Hello\"";
            auto ast = parser.produceAST(code);

            jit.CompileScript(*ast);
            return true;
        } catch (const std::exception&) {
            return false;
        }
    });

    tf.test("Pipeline Hotkey Compilation", []() {
        try {
            havel::parser::Parser parser;
            havel::compiler::JIT jit;

            std::string code = "F1 => clipboard.out | text.upper | send";
            auto ast = parser.produceAST(code);

            jit.CompileScript(*ast);
            return true;
        } catch (const std::exception&) {
            return false;
        }
    });

    tf.test("Multiple Hotkey Compilation", []() {
        try {
            havel::parser::Parser parser;
            havel::compiler::JIT jit;

            std::string code = "F1 => send \"Hello\"\nF2 => send \"World\"";
            auto ast = parser.produceAST(code);

            jit.CompileScript(*ast);
            return true;
        } catch (const std::exception&) {
            return false;
        }
    });

    tf.test("Block Statement Compilation", []() {
        try {
            havel::parser::Parser parser;
            havel::compiler::JIT jit;

            std::string code = "F1 => { send \"Line 1\" send \"Line 2\" }";
            auto ast = parser.produceAST(code);

            jit.CompileScript(*ast);
            return true;
        } catch (const std::exception&) {
            return false;
        }
    });

    tf.test("JIT Performance Test", []() {
        try {
            havel::parser::Parser parser;
            havel::compiler::JIT jit;

            // Compile 10 hotkeys to test performance
            std::string code =
                "F1 => send \"Hello1\"\n"
                "F2 => send \"Hello2\"\n"
                "F3 => send \"Hello3\"\n"
                "F4 => send \"Hello4\"\n"
                "F5 => send \"Hello5\"\n"
                "F6 => clipboard.out | text.upper | send\n"
                "F7 => window.next\n"
                "F8 => window.focus\n"
                "F9 => text.upper \"test\"\n"
                "F10 => send \"Last hotkey\"";

            auto start = std::chrono::high_resolution_clock::now();
            auto ast = parser.produceAST(code);
            jit.CompileScript(*ast);
            auto end = std::chrono::high_resolution_clock::now();

            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
            std::cout << "JIT compilation of 10 hotkeys took " << duration.count() << " ms" << std::endl;

            return duration.count() < 1000; // Should compile in under 1 second
        } catch (const std::exception&) {
            return false;
        }
    });
}
#endif

// ENGINE TESTS
void testEngine(Tests& tf) {
    std::cout << "\n=== TESTING ENGINE ===" << std::endl;
    havel::IO io;
    havel::WindowManager wm;

    tf.test("Engine Creation", [&]() {
        try {
            havel::engine::EngineConfig config;
            config.mode = havel::engine::ExecutionMode::INTERPRETER;
            havel::engine::Engine engine(io, wm, config);
            return true;
        } catch (const std::exception&) {
            return false;
        }
    });

    tf.test("Development Engine Factory", [&]() {
        try {
            havel::engine::EngineConfig config;
            config.mode = havel::engine::ExecutionMode::INTERPRETER;
            auto engine = std::make_unique<havel::engine::Engine>(io, wm, config);
            return engine != nullptr;
        } catch (const std::exception&) {
            return false;
        }
    });

#ifdef HAVEL_ENABLE_LLVM
    tf.test("JIT Mode Execution", [&]() {
        try {
            havel::engine::EngineConfig config;
            config.mode = havel::engine::ExecutionMode::JIT;
            config.verboseOutput = false;
            havel::engine::Engine engine(io, wm, config);

            std::string code = "F1 => send \"JIT Test\"";
            engine.ExecuteCode(code);
            return true;
        } catch (const std::exception&) {
            return false;
        }
    });
#endif

    tf.test("Interpreter Mode Execution", [&]() {
        try {
            havel::engine::EngineConfig config;
            config.mode = havel::engine::ExecutionMode::INTERPRETER;
            config.verboseOutput = false;
            havel::engine::Engine engine(io, wm, config);

            std::string code = "F1 => send \"Interpreter Test\"";
            auto result = engine.ExecuteCode(code);
            return true;
        } catch (const std::exception&) {
            return false;
        }
    });

    tf.test("AST Dumping", [&]() {
        try {
            havel::engine::EngineConfig config;
            config.mode = havel::engine::ExecutionMode::INTERPRETER;
            auto engine = std::make_unique<havel::engine::Engine>(io, wm, config);
            std::string code = "F1 => send \"AST Test\"";
            engine->DumpAST(code);
            return true;
        } catch (const std::exception&) {
            return false;
        }
    });

    tf.test("Performance Profiling", [&]() {
        try {
            havel::engine::EngineConfig config;
            config.enableProfiler = true;
            config.verboseOutput = false;
            havel::engine::Engine engine(io, wm, config);

            engine.StartProfiling();
            std::string code = "F1 => send \"Profiling Test\"";
            engine.ExecuteCode(code);
            engine.StopProfiling();

            auto stats = engine.GetPerformanceStats();
            return stats.executionTime.count() >= 0;
        } catch (const std::exception&) {
            return false;
        }
    });

    tf.test("Version Information", [&]() {
        try {
            havel::engine::EngineConfig config;
            auto engine = std::make_unique<havel::engine::Engine>(io, wm, config);

            std::string version = engine->GetVersionInfo();
            std::string buildInfo = engine->GetBuildInfo();
            bool llvmEnabled = engine->IsLLVMEnabled();

            return !version.empty() && !buildInfo.empty();
        } catch (const std::exception&) {
            return false;
        }
    });

    tf.test("Script Validation", [&]() {
        try {
            havel::engine::EngineConfig config;
            auto engine = std::make_unique<havel::engine::Engine>(io, wm, config);

            // Create a temporary test file
            std::ofstream testFile("test_script.hav");
            testFile << "F1 => send \"Validation Test\"";
            testFile.close();

            engine->ValidateScript("test_script.hav");

            // Clean up
            std::remove("test_script.hav");

            return true;
        } catch (const std::exception&) {
            // Clean up on exception
            std::remove("test_script.hav");
            return false;
        }
    });
}

// MAIN TEST RUNNER
int main() {
    std::cout << "HAVEL LANGUAGE COMPREHENSIVE TEST SUITE" << std::endl;
    std::cout << "========================================" << std::endl;

    Tests testFramework;

    try {
        // Core component tests
        testLexer(testFramework);
        testParser(testFramework);
        testInterpreter(testFramework);

#ifdef HAVEL_ENABLE_LLVM
        // LLVM component tests
        testCompiler(testFramework);
        testJIT(testFramework);
#endif

        // Engine tests
        testEngine(testFramework);

        // Print final summary
        testFramework.printSummary();

        // Return appropriate exit code
        return testFramework.allTestsPassed() ? 0 : 1;

    } catch (const std::exception& e) {
        std::cerr << "FATAL TEST ERROR: " << e.what() << std::endl;
        return 1;
    }
}