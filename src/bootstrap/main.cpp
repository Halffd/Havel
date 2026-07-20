// Bootstrap compiler main - minimal compiler for self-hosted modules
// Compiles .hv files to .hvc bytecode
// This is frozen - no new features

#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>

#include "Lexer.hpp"
#include "Parser.hpp"
#include "ByteCompiler.hpp"
#include "RuntimeSupport.hpp"
#include "ErrorSystem.h"
#include "ErrorPrinter.hpp"
#include "AST.hpp"

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: havel-bootstrap <input.hv> <output.hvc>\n";
        return 1;
    }

    std::string inputPath = argv[1];
    std::string outputPath = argv[2];

    // Read input file
    std::ifstream inFile(inputPath, std::ios::binary);
    if (!inFile) {
        std::cerr << "Cannot open input: " << inputPath << "\n";
        return 1;
    }
    std::string source((std::istreambuf_iterator<char>(inFile)),
                       std::istreambuf_iterator<char>());
    inFile.close();

    // Lex
    havel::Lexer lexer(source, false);
    std::vector<havel::Token> tokens;
    try {
        tokens = lexer.tokenize();
    } catch (const std::exception& e) {
        std::cerr << "Lex error: " << e.what() << "\n";
        return 1;
    }
    if (lexer.hasErrors()) {
        for (const auto& err : lexer.getErrors()) {
            std::cerr << "Lex error [" << err.line << ":" << err.column << "] "
                      << err.message << "\n";
        }
        return 1;
    }

    // Parse
    havel::parser::Parser parser;
    std::unique_ptr<havel::ast::Program> program;
    try {
        program = parser.produceAST(source);
    } catch (const std::exception& e) {
        std::cerr << "Parse error: " << e.what() << "\n";
        return 1;
    }
    if (parser.hasErrors()) {
        for (const auto& err : parser.getErrors()) {
            std::cerr << "Parse error [" << err.line << ":" << err.column << "] "
                      << err.message << "\n";
        }
        return 1;
    }
    if (!program) {
        std::cerr << "Parser returned null AST\n";
        return 1;
    }

    // Compile to bytecode
    havel::compiler::ByteCompiler compiler;
    std::unique_ptr<havel::compiler::BytecodeChunk> chunk;
    try {
        chunk = compiler.compile(*program);
    } catch (const std::exception& e) {
        std::cerr << "Compile error: " << e.what() << "\n";
        return 1;
    }
    if (compiler.hasErrors()) {
        for (const auto& err : compiler.errors()) {
            std::cerr << "Compile error [" << err.line << ":" << err.column << "] "
                      << err.what() << "\n";
        }
        return 1;
    }
    if (!chunk) {
        std::cerr << "Compiler returned null chunk\n";
        return 1;
    }

    // Serialize to .hvc
    havel::compiler::ValueSerializer serializer;
    std::vector<uint8_t> data;
    try {
        data = serializer.serializeChunk(*chunk);
    } catch (const std::exception& e) {
        std::cerr << "Serialize error: " << e.what() << "\n";
        return 1;
    }

    // Write output
    std::ofstream outFile(outputPath, std::ios::binary);
    if (!outFile) {
        std::cerr << "Cannot open output: " << outputPath << "\n";
        return 1;
    }
    outFile.write(reinterpret_cast<const char*>(data.data()), data.size());
    outFile.close();

    std::cout << "Built " << inputPath << " -> " << outputPath 
              << " (" << data.size() << " bytes, " << chunk->getFunctionCount() 
              << " functions)\n";

    return 0;
}