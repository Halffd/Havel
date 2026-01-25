#include "havel-lang/lexer/Lexer.hpp"
#include "havel-lang/parser/Parser.h"
#include <iostream>
#include <fstream>
#include <sstream>

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
        auto ast = parser.produceASTStrict(input);
        std::cout << "Parse success!" << std::endl;
    } catch (const havel::parser::ParseError& e) {
        std::cout << "Parse error (" << e.line << ":" << e.column << "): " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cout << "Parse error: " << e.what() << std::endl;
    }
    std::cout << "--------------------------------" << std::endl;
}

int main() {
    test_parsing("i++");
    test_parsing("let i = 0");
    test_parsing("i");

    test_parsing("^. => {}\n");
    test_parsing("^, => {}\n");

    {
        std::ifstream f("hotkeys_batch_16.hv");
        if (f) {
            std::stringstream buf;
            buf << f.rdbuf();
            test_parsing(buf.str());

            // Find earliest failing line (incremental parse)
            f.clear();
            f.seekg(0);
            std::string line;
            std::string accum;
            int lineNo = 0;
            while (std::getline(f, line)) {
                lineNo++;
                accum += line;
                accum += "\n";
                try {
                    havel::parser::Parser parser;
                    (void)parser.produceASTStrict(accum);
                } catch (const havel::parser::ParseError& e) {
                    std::cout << "First failing line <= " << lineNo << ": parse error (" << e.line << ":" << e.column << "): " << e.what() << "\n";
                    break;
                }
            }
        } else {
            std::cout << "Could not open hotkeys_batch_16.hv\n";
        }
    }
    
    // Function definition test
    std::string funcCode = "fn fac(n){ \n"
                           "if(n == 1) return 1 \n"
                           "let result = n * fac(n+1) \n"
                           "result \n"
                           "}";
    test_parsing(funcCode);
    
    return 0;
}
