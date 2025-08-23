
// src/havel-lang/parser/Parser.h

#pragma once
#include "../lexer/Lexer.hpp"
#include "../ast/AST.h"
#include <vector>
#include <memory>

namespace havel::parser {

class Parser {
private:
    std::vector<havel::Token> tokens;
    size_t position = 0;

    // Helper methods (like Tyler's at() and eat())
    havel::Token at(size_t offset = 0) const;
    havel::Token advance();
    bool notEOF() const;

    // Parser methods (following Tyler's structure)
    std::unique_ptr<havel::ast::Statement> parseStatement();
    std::unique_ptr<havel::ast::Expression> parseExpression();
    std::unique_ptr<havel::ast::Expression> parsePipelineExpression();
    std::unique_ptr<havel::ast::Expression> parseBinaryExpression();
    std::unique_ptr<havel::ast::Expression> parsePrimaryExpression();

    // Havel-specific parsers
    std::unique_ptr<havel::ast::Statement> parseLetDeclaration();
    std::unique_ptr<havel::ast::Statement> parseIfStatement();
    std::unique_ptr<havel::ast::Statement> parseFunctionDeclaration();
    std::unique_ptr<havel::ast::Statement> parseReturnStatement();
    std::unique_ptr<havel::ast::HotkeyBinding> parseHotkeyBinding();
    std::unique_ptr<havel::ast::BlockStatement> parseBlockStatement();
    std::unique_ptr<havel::ast::Statement> parseImportStatement();
    havel::ast::BinaryOperator tokenToBinaryOperator(TokenType tokenType);
    std::unique_ptr<havel::ast::Expression> parseLogicalOr();
    std::unique_ptr<havel::ast::Expression> parseLogicalAnd();
    std::unique_ptr<havel::ast::Expression> parseEquality();
    std::unique_ptr<havel::ast::Expression> parseComparison();
    std::unique_ptr<havel::ast::Expression> parseAdditive();
    std::unique_ptr<havel::ast::Expression> parseMultiplicative();
    std::unique_ptr<havel::ast::Expression> parseUnary();
    std::unique_ptr<havel::ast::Expression> parseCallExpression(std::unique_ptr<havel::ast::Expression> callee);
    std::unique_ptr<havel::ast::Expression> parseMemberExpression(std::unique_ptr<havel::ast::Expression> object);
    havel::TokenType getBinaryOperatorToken(ast::BinaryOperator op);
public:
    explicit Parser() = default;

    // Main entry point (like Tyler's produceAST)
    std::unique_ptr<havel::ast::Program> produceAST(const std::string& sourceCode);

    void printAST(const havel::ast::ASTNode& node, int indent = 0) const;
};

} // namespace havel::parser