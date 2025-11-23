
// src/havel-lang/parser/Parser.h

#pragma once
#include "../lexer/Lexer.hpp"
#include "../ast/AST.h"
#include "../common/Debug.hpp"
#include <vector>
#include <memory>

namespace havel::parser {

class Parser {
private:
    std::vector<Token> tokens;
    size_t position = 0;

    // Helper methods (like Tyler's at() and eat())
    Token at(size_t offset = 0) const;
    Token advance();
    bool notEOF() const;

    // Parser methods (following Tyler's structure)
    std::unique_ptr<ast::Statement> parseStatement();
    std::unique_ptr<ast::Expression> parseExpression();
    std::unique_ptr<ast::Expression> parseAssignmentExpression();
    std::unique_ptr<ast::Expression> parsePipelineExpression();
    std::unique_ptr<ast::Expression> parseTernaryExpression();
    std::unique_ptr<ast::Expression> parseBinaryExpression();
    std::unique_ptr<ast::Expression> parsePrimaryExpression();

    // Havel-specific parsers
    std::unique_ptr<ast::Statement> parseLetDeclaration();
    std::unique_ptr<ast::Statement> parseIfStatement();
    std::unique_ptr<ast::Statement> parseWhileStatement();
    std::unique_ptr<ast::Statement> parseForStatement();
    std::unique_ptr<ast::Statement> parseLoopStatement();
    std::unique_ptr<ast::Statement> parseBreakStatement();
    std::unique_ptr<ast::Statement> parseContinueStatement();
    std::unique_ptr<ast::Statement> parseOnModeStatement();
    std::unique_ptr<ast::Statement> parseOffModeStatement();
    std::unique_ptr<ast::Statement> parseFunctionDeclaration();
    std::unique_ptr<ast::Statement> parseReturnStatement();
    std::unique_ptr<ast::HotkeyBinding> parseHotkeyBinding();
    std::unique_ptr<ast::BlockStatement> parseBlockStatement();
    std::unique_ptr<ast::Statement> parseWhenBlock();
    std::unique_ptr<ast::Statement> parseImportStatement();
    std::unique_ptr<ast::Statement> parseConfigBlock();
    std::unique_ptr<ast::Statement> parseDevicesBlock();
    std::unique_ptr<ast::Statement> parseModesBlock();
    std::vector<std::pair<std::string, std::unique_ptr<ast::Expression>>> parseKeyValueBlock();
    ast::BinaryOperator tokenToBinaryOperator(TokenType tokenType);
    std::unique_ptr<ast::Expression> parseLogicalOr();
    std::unique_ptr<ast::Expression> parseLogicalAnd();
    std::unique_ptr<ast::Expression> parseEquality();
    std::unique_ptr<ast::Expression> parseComparison();
    std::unique_ptr<ast::Expression> parseRange();
    std::unique_ptr<ast::Expression> parseAdditive();
    std::unique_ptr<ast::Expression> parseMultiplicative();
    std::unique_ptr<ast::Expression> parseUnary();
    std::unique_ptr<ast::Expression> parseCallExpression(std::unique_ptr<ast::Expression> callee);
    std::unique_ptr<ast::Expression> parseMemberExpression(std::unique_ptr<ast::Expression> object);
    std::unique_ptr<ast::Expression> parseIndexExpression(std::unique_ptr<ast::Expression> object);
    std::unique_ptr<ast::Expression> parseArrayLiteral();
    std::unique_ptr<ast::Expression> parseObjectLiteral();
    std::unique_ptr<ast::Expression> parseLambdaFromParams(std::vector<std::unique_ptr<ast::Identifier>> params);
    std::unique_ptr<ast::Expression> parsePostfixExpression(std::unique_ptr<ast::Expression> expr);
    TokenType getBinaryOperatorToken(ast::BinaryOperator op);

    // Condition combination helpers
    std::unique_ptr<ast::Expression> combineConditions(std::unique_ptr<ast::Expression> left,
                                                      std::unique_ptr<ast::Expression> right);

    // Error recovery methods
    bool synchronize();
    bool atStatementStart();
    bool isAtEndOfBlock();
public:
    explicit Parser() = default;

    // Main entry point (like Tyler's produceAST)
    std::unique_ptr<ast::Program> produceAST(const std::string& sourceCode);

    void printAST(const ast::ASTNode& node, int indent = 0) const;
};

} // namespace havel::parser