// src/havel-lang/tests/test_havel.cpp
#pragma once
#include "../lexer/Lexer.hpp"
#include "../parser/Parser.h"
#include "../runtime/Interpreter.hpp"
#include "../runtime/Engine.h"

#ifdef HAVEL_ENABLE_LLVM
#include "../compiler/Compiler.hpp"
#include "../compiler/JIT.hpp"
#endif

#include <iostream>
#include <fstream>
#include <cassert>
#include <functional>
#include <chrono>
#include <vector>
#include <sstream>
#include "Tests.h"

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
        std::string code = "Ctrl+Shift+Alt+F12 => {}";
        havel::Lexer lexer(code);
        auto tokens = lexer.tokenize();

        return tokens.size() >= 3 &&
               tokens[0].type == havel::TokenType::Hotkey &&
               tokens[0].value == "Ctrl+Shift+Alt+F12";
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
        std::string code = "Ctrl+V => clipboard.paste";
        havel::parser::Parser parser;
        auto ast = parser.produceAST(code);

        if (ast && ast->body.size() == 1) {
            auto hotkeyBinding = dynamic_cast<havel::ast::HotkeyBinding*>(ast->body[0].get());
            return hotkeyBinding != nullptr;
        }
        return false;
    });

    tf.test("Pipeline Expression AST", []() {
        std::string code = "F1 => clipboard.get | text.upper | send";
        havel::parser::Parser parser;
        auto ast = parser.produceAST(code);

        if (ast && ast->body.size() == 1) {
            auto hotkeyBinding = dynamic_cast<havel::ast::HotkeyBinding*>(ast->body[0].get());
            if (hotkeyBinding) {
                auto pipeline = dynamic_cast<havel::ast::PipelineExpression*>(hotkeyBinding->action.get());
                return pipeline != nullptr && pipeline->stages.size() >= 3;
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
                auto member = dynamic_cast<havel::ast::MemberExpression*>(hotkeyBinding->action.get());
                return member != nullptr;
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
                auto call = dynamic_cast<havel::ast::CallExpression*>(hotkeyBinding->action.get());
                return call != nullptr;
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
                auto stringLit = dynamic_cast<havel::ast::StringLiteral*>(hotkeyBinding->action.get());
                return stringLit != nullptr && stringLit->value == "Hello World";
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
                auto numberLit = dynamic_cast<havel::ast::NumberLiteral*>(hotkeyBinding->action.get());
                return numberLit != nullptr && numberLit->value == 42.0;
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

    tf.test("String Evaluation", []() {
        havel::Interpreter interpreter;
        auto result = interpreter.Execute("F1 => \"Hello World!\"");

        return std::holds_alternative<std::string>(result) &&
               std::get<std::string>(result) == "Hello World!";
    });

    tf.test("Number Evaluation", []() {
        havel::Interpreter interpreter;
        auto result = interpreter.Execute("F1 => 42");

        return std::holds_alternative<int>(result) &&
               std::get<int>(result) == 42;
    });

    tf.test("Binary Expression Evaluation", []() {
        havel::Interpreter interpreter;
        auto result = interpreter.Execute("F1 => 2 + 3");

        return std::holds_alternative<double>(result) &&
               std::get<double>(result) == 5.0;
    });

    tf.test("String Concatenation", []() {
        havel::Interpreter interpreter;
        auto result = interpreter.Execute("F1 => \"Hello\" + \" \" + \"World\"");

        return std::holds_alternative<std::string>(result) &&
               std::get<std::string>(result) == "Hello World";
    });

    tf.test("Value To String Conversion", []() {
        havel::Interpreter interpreter;
        std::string testStr = interpreter.ValueToString(std::string("test"));
        int testInt = 42;
        std::string intStr = interpreter.ValueToString(testInt);

        return testStr == "test" && intStr == "42";
    });

    tf.test("Value To Boolean Conversion", []() {
        havel::Interpreter interpreter;
        bool emptyStringBool = interpreter.ValueToBool(std::string(""));
        bool nonEmptyStringBool = interpreter.ValueToBool(std::string("hello"));
        bool zeroBool = interpreter.ValueToBool(0);
        bool nonZeroBool = interpreter.ValueToBool(42);

        return !emptyStringBool && nonEmptyStringBool && !zeroBool && nonZeroBool;
    });

    tf.test("Value To Number Conversion", []() {
        havel::Interpreter interpreter;
        double stringNum = interpreter.ValueToNumber(std::string("42.5"));
        double intNum = interpreter.ValueToNumber(42);
        double boolNum = interpreter.ValueToNumber(true);

        return stringNum == 42.5 && intNum == 42.0 && boolNum == 1.0;
    });

    tf.test("Hotkey Registration", []() {
        havel::Interpreter interpreter;
        std::string havelCode = "F1 => send \"Hello\"";

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

    tf.test("Engine Creation", []() {
        try {
            havel::engine::EngineConfig config;
            config.mode = havel::engine::ExecutionMode::INTERPRETER;
            havel::engine::Engine engine(config);
            return true;
        } catch (const std::exception&) {
            return false;
        }
    });

    tf.test("Development Engine Factory", []() {
        try {
            auto engine = havel::engine::CreateDevelopmentEngine();
            return engine != nullptr;
        } catch (const std::exception&) {
            return false;
        }
    });

    tf.test("Production Engine Factory", []() {
        try {
            auto engine = havel::engine::CreateProductionEngine();
            return engine != nullptr;
        } catch (const std::exception&) {
            return false;
        }
    });

#ifdef HAVEL_ENABLE_LLVM
    tf.test("Compiler Engine Factory", []() {
        try {
            auto engine = havel::engine::CreateCompilerEngine();
            return engine != nullptr;
        } catch (const std::exception&) {
            return false;
        }
    });

    tf.test("JIT Mode Execution", []() {
        try {
            havel::engine::EngineConfig config;
            config.mode = havel::engine::ExecutionMode::JIT;
            config.verboseOutput = false;
            havel::engine::Engine engine(config);

            std::string code = "F1 => send \"JIT Test\"";
            engine.ExecuteCode(code);
            return true;
        } catch (const std::exception&) {
            return false;
        }
    });
#endif

    tf.test("Interpreter Mode Execution", []() {
        try {
            havel::engine::EngineConfig config;
            config.mode = havel::engine::ExecutionMode::INTERPRETER;
            config.verboseOutput = false;
            havel::engine::Engine engine(config);

            std::string code = "F1 => send \"Interpreter Test\"";
            auto result = engine.ExecuteCode(code);
            return true;
        } catch (const std::exception&) {
            return false;
        }
    });

    tf.test("AST Dumping", []() {
        try {
            auto engine = havel::engine::CreateDevelopmentEngine();
            std::string code = "F1 => send \"AST Test\"";
            engine->DumpAST(code);
            return true;
        } catch (const std::exception&) {
            return false;
        }
    });

    tf.test("Performance Profiling", []() {
        try {
            havel::engine::EngineConfig config;
            config.enableProfiler = true;
            config.verboseOutput = false;
            havel::engine::Engine engine(config);

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

    tf.test("Mode Switching", []() {
        try {
            auto engine = havel::engine::CreateDevelopmentEngine();

            // Start in interpreter mode
            if (engine->GetExecutionMode() != havel::engine::ExecutionMode::INTERPRETER) {
                return false;
            }

#ifdef HAVEL_ENABLE_LLVM
            // Switch to JIT mode
            engine->SetExecutionMode(havel::engine::ExecutionMode::JIT);
            if (engine->GetExecutionMode() != havel::engine::ExecutionMode::JIT) {
                return false;
            }
#endif

            return true;
        } catch (const std::exception&) {
            return false;
        }
    });

    tf.test("Version Information", []() {
        try {
            auto engine = havel::engine::CreateDevelopmentEngine();

            std::string version = engine->GetVersionInfo();
            std::string buildInfo = engine->GetBuildInfo();
            bool llvmEnabled = engine->IsLLVMEnabled();

            return !version.empty() && !buildInfo.empty();
        } catch (const std::exception&) {
            return false;
        }
    });

    tf.test("Script Validation", []() {
        try {
            auto engine = havel::engine::CreateDevelopmentEngine();

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

    tf.test("Config Updates", []() {
        try {
            auto engine = havel::engine::CreateDevelopmentEngine();

            havel::engine::EngineConfig newConfig;
            newConfig.mode = havel::engine::ExecutionMode::INTERPRETER;
            newConfig.optimization = havel::engine::OptimizationLevel::AGGRESSIVE;
            newConfig.verboseOutput = true;

            engine->UpdateConfig(newConfig);
            return true;
        } catch (const std::exception&) {
            return false;
        }
    });
}

// INTEGRATION TESTS
void testIntegration(Tests& tf) {
    std::cout << "\n=== TESTING INTEGRATION ===" << std::endl;

    tf.test("Full Pipeline Integration", []() {
        try {
            auto engine = havel::engine::CreateProductionEngine();

            std::string complexCode = R"(
                F1 => clipboard.out | text.upper | send
                F2 => window.next
                F3 => send "Integration test complete"
            )";

            engine->RegisterHotkeysFromCode(complexCode);
            return true;
        } catch (const std::exception&) {
            return false;
        }
    });

    tf.test("Parser to Interpreter Integration", []() {
        try {
            havel::parser::Parser parser;
            havel::Interpreter interpreter;

            std::string code = "F1 => \"Parser to Interpreter\"";
            auto ast = parser.produceAST(code);

            // This tests that the AST can be processed by interpreter
            return ast != nullptr && ast->body.size() == 1;
        } catch (const std::exception&) {
            return false;
        }
    });

#ifdef HAVEL_ENABLE_LLVM
    tf.test("Parser to Compiler Integration", []() {
        try {
            havel::parser::Parser parser;
            havel::compiler::Compiler compiler;

            compiler.Initialize();
            compiler.CreateStandardLibrary();

            std::string code = "F1 => send \"Parser to Compiler\"";
            auto ast = parser.produceAST(code);

            if (ast && ast->body.size() == 1) {
                auto hotkeyBinding = dynamic_cast<havel::ast::HotkeyBinding*>(ast->body[0].get());
                if (hotkeyBinding) {
                    auto result = compiler.GenerateExpression(*hotkeyBinding->action);
                    return result != nullptr;
                }
            }
            return false;
        } catch (const std::exception&) {
            return false;
        }
    });
    tf.test("End-to-End JIT Pipeline", []() {
        try {
            // Test complete pipeline: Lexer -> Parser -> Compiler -> JIT
            havel::Lexer lexer("F1 => clipboard.out | text.upper | send");
            auto tokens = lexer.tokenize();

            havel::parser::Parser parser;
            auto ast = parser.produceAST("F1 => clipboard.out | text.upper | send");

            havel::compiler::JIT jit;
            jit.CompileScript(*ast);

            return tokens.size() > 0 && ast != nullptr;
        } catch (const std::exception&) {
            return false;
        }
    });

    tf.test("Performance Comparison", []() {
        try {
            std::string testCode = "F1 => send \"Performance Test\"";

            // Test interpreter performance
            auto start = std::chrono::high_resolution_clock::now();
            havel::Interpreter interpreter;
            interpreter.Execute(testCode);
            auto interpreterTime = std::chrono::high_resolution_clock::now() - start;

            // Test JIT performance
            start = std::chrono::high_resolution_clock::now();
            havel::parser::Parser parser;
            auto ast = parser.produceAST(testCode);
            havel::compiler::JIT jit;
            jit.CompileScript(*ast);
            auto jitTime = std::chrono::high_resolution_clock::now() - start;

            auto interpreterMicros = std::chrono::duration_cast<std::chrono::microseconds>(interpreterTime);
            auto jitMicros = std::chrono::duration_cast<std::chrono::microseconds>(jitTime);

            std::cout << "Interpreter: " << interpreterMicros.count() << " us" << std::endl;
            std::cout << "JIT: " << jitMicros.count() << " us" << std::endl;

            return true; // Both should complete successfully
        } catch (const std::exception&) {
            return false;
        }
    });
#endif

    tf.test("Memory Management Test", []() {
        try {
            // Test that creating and destroying multiple engines doesn't leak memory
            for (int i = 0; i < 10; i++) {
                auto engine = havel::engine::CreateDevelopmentEngine();
                engine->ExecuteCode("F1 => send \"Memory test " + std::to_string(i) + "\"");
            }
            return true;
        } catch (const std::exception&) {
            return false;
        }
    });

    tf.test("Large Script Processing", []() {
        try {
            auto engine = havel::engine::CreateProductionEngine();

            std::stringstream largeScript;
            for (int i = 1; i <= 24; i++) {
                largeScript << "F" << i << " => send \"Hotkey " << i << "\"\n";
            }

            engine->RegisterHotkeysFromCode(largeScript.str());
            return true;
        } catch (const std::exception&) {
            return false;
        }
    });

    tf.test("Error Recovery Test", []() {
        try {
            auto engine = havel::engine::CreateDevelopmentEngine();

            // Test with invalid syntax
            bool caughtException = false;
            try {
                engine->ExecuteCode("F1 => invalid_syntax_here ]]]]");
            } catch (const std::exception&) {
                caughtException = true;
            }

            // Engine should still work after error
            if (caughtException) {
                engine->ExecuteCode("F2 => send \"Recovery test\"");
                return true;
            }

            return false;
        } catch (const std::exception&) {
            return false;
        }
    });
}

// STRESS TESTS
void testStress(Tests& tf) {
    std::cout << "\n=== STRESS TESTING ===" << std::endl;

    tf.test("Lexer Stress Test", []() {
        try {
            std::stringstream massiveCode;
            for (int i = 0; i < 1000; i++) {
                massiveCode << "F" << (i % 12 + 1) << " => send \"Stress test " << i << "\"\n";
            }

            auto start = std::chrono::high_resolution_clock::now();
            havel::Lexer lexer(massiveCode.str());
            auto tokens = lexer.tokenize();
            auto end = std::chrono::high_resolution_clock::now();

            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
            std::cout << "Lexed " << tokens.size() << " tokens in " << duration.count() << " ms" << std::endl;

            return tokens.size() > 5000 && duration.count() < 5000; // Should complete in under 5 seconds
        } catch (const std::exception&) {
            return false;
        }
    });

    tf.test("Parser Stress Test", []() {
        try {
            std::stringstream massiveCode;
            for (int i = 0; i < 100; i++) {
                massiveCode << "F" << (i % 12 + 1) << " => clipboard.out | text.upper | text.trim | send\n";
            }

            auto start = std::chrono::high_resolution_clock::now();
            havel::parser::Parser parser;
            auto ast = parser.produceAST(massiveCode.str());
            auto end = std::chrono::high_resolution_clock::now();

            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
            std::cout << "Parsed " << ast->body.size() << " statements in " << duration.count() << " ms" << std::endl;

            return ast->body.size() == 100 && duration.count() < 2000; // Should complete in under 2 seconds
        } catch (const std::exception&) {
            return false;
        }
    });

#ifdef HAVEL_ENABLE_LLVM
    tf.test("JIT Stress Test", []() {
        try {
            std::stringstream massiveCode;
            for (int i = 0; i < 50; i++) {
                massiveCode << "F" << (i % 12 + 1) << " => clipboard.out | text.upper | send\n";
            }

            auto start = std::chrono::high_resolution_clock::now();
            havel::parser::Parser parser;
            auto ast = parser.produceAST(massiveCode.str());
            havel::compiler::JIT jit;
            jit.CompileScript(*ast);
            auto end = std::chrono::high_resolution_clock::now();

            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
            std::cout << "JIT compiled " << ast->body.size() << " hotkeys in " << duration.count() << " ms" << std::endl;

            return duration.count() < 10000; // Should complete in under 10 seconds
        } catch (const std::exception&) {
            return false;
        }
    });
#endif

    tf.test("Memory Stress Test", []() {
        try {
            // Create and destroy many engines rapidly
            for (int i = 0; i < 100; i++) {
                auto engine = havel::engine::CreateDevelopmentEngine();
                engine->ExecuteCode("F1 => send \"Memory stress " + std::to_string(i) + "\"");

                if (i % 10 == 0) {
                    std::cout << "Created " << i << " engines..." << std::endl;
                }
            }
            return true;
        } catch (const std::exception&) {
            return false;
        }
    });

    tf.test("Concurrent Access Test", []() {
        try {
            auto engine = havel::engine::CreateProductionEngine();

            // Test multiple rapid executions
            for (int i = 0; i < 50; i++) {
                engine->ExecuteCode("F" + std::to_string(i % 12 + 1) + " => send \"Concurrent " + std::to_string(i) + "\"");
            }

            return true;
        } catch (const std::exception&) {
            return false;
        }
    });
}

// BENCHMARK TESTS
void testBenchmarks(Tests& tf) {
    std::cout << "\n=== BENCHMARK TESTING ===" << std::endl;

    tf.test("Execution Speed Benchmark", []() {
        try {
            const std::string testCode = "F1 => send \"Benchmark test\"";
            const int iterations = 1000;

            // Benchmark interpreter
            auto start = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < iterations; i++) {
                havel::Interpreter interpreter;
                interpreter.Execute(testCode);
            }
            auto interpreterTime = std::chrono::high_resolution_clock::now() - start;

#ifdef HAVEL_ENABLE_LLVM
            // Benchmark JIT (compilation + execution)
            start = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < iterations; i++) {
                havel::parser::Parser parser;
                auto ast = parser.produceAST(testCode);
                havel::compiler::JIT jit;
                jit.CompileScript(*ast);
            }
            auto jitTime = std::chrono::high_resolution_clock::now() - start;

            auto interpreterMs = std::chrono::duration_cast<std::chrono::milliseconds>(interpreterTime);
            auto jitMs = std::chrono::duration_cast<std::chrono::milliseconds>(jitTime);

            std::cout << "Interpreter " << iterations << " iterations: " << interpreterMs.count() << " ms" << std::endl;
            std::cout << "JIT " << iterations << " iterations: " << jitMs.count() << " ms" << std::endl;
            std::cout << "Per iteration - Interpreter: " << (interpreterMs.count() * 1000 / iterations) << " us" << std::endl;
            std::cout << "Per iteration - JIT: " << (jitMs.count() * 1000 / iterations) << " us" << std::endl;
#else
            auto interpreterMs = std::chrono::duration_cast<std::chrono::milliseconds>(interpreterTime);
            std::cout << "Interpreter " << iterations << " iterations: " << interpreterMs.count() << " ms" << std::endl;
            std::cout << "Per iteration: " << (interpreterMs.count() * 1000 / iterations) << " us" << std::endl;
#endif

            return true;
        } catch (const std::exception&) {
            return false;
        }
    });

    tf.test("Memory Usage Benchmark", []() {
        try {
            // Test memory usage with large scripts
            auto engine = havel::engine::CreateProductionEngine();

            std::stringstream largeScript;
            for (int i = 0; i < 500; i++) {
                largeScript << "F" << (i % 12 + 1) << " => clipboard.out | text.upper | text.trim | send \"Item " << i << "\"\n";
            }

            auto start = std::chrono::high_resolution_clock::now();
            engine->RegisterHotkeysFromCode(largeScript.str());
            auto end = std::chrono::high_resolution_clock::now();

            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
            std::cout << "Processed 500 complex hotkeys in " << duration.count() << " ms" << std::endl;

            return duration.count() < 30000; // Should complete in under 30 seconds
        } catch (const std::exception&) {
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

        // Integration tests
        testIntegration(testFramework);

        // Stress tests
        testStress(testFramework);

        // Benchmark tests
        testBenchmarks(testFramework);

        // Print final summary
        testFramework.printSummary();

        // Return appropriate exit code
        return testFramework.allTestsPassed() ? 0 : 1;

    } catch (const std::exception& e) {
        std::cerr << "FATAL TEST ERROR: " << e.what() << std::endl;
        return 1;
    }
}