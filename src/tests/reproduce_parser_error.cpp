#include "havel-lang/lexer/Lexer.hpp"
#include "havel-lang/parser/Parser.h"
#include <iostream>

void test_parsing(const std::string& input) {
    std::cout << "Testing input: '" << input << "'" << std::endl;
    try {
        havel::Lexer lexer(input);
        auto tokens = lexer.tokenize();
        std::cout << "Tokens:" << std::endl;
        for (const auto& t : tokens) {
            std::cout << "  " << t.value << " (Type: " << (int)t.type << ")" << std::endl;
        }

        havel::parser::Parser parser;
        auto ast = parser.produceAST(input);
        std::cout << "Parse success!" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "Parse error: " << e.what() << std::endl;
    }
    std::cout << "--------------------------------" << std::endl;
}

int main() {
    test_parsing("i++");
    test_parsing("let i = 0");
    test_parsing("i");
    
    // Function definition test
    std::string funcCode = "fn fac(n){ \n"
                           "if(n == 1) return 1 \n"
                           "let result = n * fac(n+1) \n"
                           "result \n"
                           "}";
    test_parsing(funcCode);
    
    return 0;
}
