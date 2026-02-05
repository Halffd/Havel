// src/havel-lang/parser/Parser.h

#pragma once
#include "../ast/AST.h"
#include "../common/Debug.hpp"
#include "../lexer/Lexer.hpp"
#include <memory>
#include <stdexcept>
#include <vector>

namespace havel::parser {

class ParseError : public std::runtime_error {
public:
  ParseError(size_t line, size_t column, const std::string &message)
      : std::runtime_error(message), line(line), column(column) {}

  size_t line;
  size_t column;
};

class Parser {
private:
  std::vector<Token> tokens;
  size_t position = 0;

  // Controls whether `expr { ... }` is treated as call-sugar/lambda.
  // This must be disabled when parsing conditions for statements like
  // `if/while/when` to ensure the `{` starts the statement body.
  bool allowBraceCallSugar = true;

  [[noreturn]] void fail(const std::string &message);
  [[noreturn]] void failAt(const Token &token, const std::string &message);

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
  std::unique_ptr<ast::Statement> parseUseStatement();
  std::unique_ptr<ast::Statement> parseWithStatement();
  std::unique_ptr<ast::Statement> parseConfigBlock();
  std::unique_ptr<ast::Statement> parseDevicesBlock();
  std::unique_ptr<ast::Statement> parseModesBlock();
  std::vector<std::pair<std::string, std::unique_ptr<ast::Expression>>>
  parseKeyValueBlock();
  ast::BinaryOperator tokenToBinaryOperator(TokenType tokenType);
  std::unique_ptr<ast::Expression> parseLogicalOr();
  std::unique_ptr<ast::Expression> parseLogicalAnd();
  std::unique_ptr<ast::Expression> parseEquality();
  std::unique_ptr<ast::Expression> parseComparison();
  std::unique_ptr<ast::Expression> parseRange();
  std::unique_ptr<ast::Expression> parseAdditive();
  std::unique_ptr<ast::Expression> parseMultiplicative();
  std::unique_ptr<ast::Expression> parseUnary();
  std::unique_ptr<ast::Expression>
  parseCallExpression(std::unique_ptr<ast::Expression> callee);
  std::unique_ptr<ast::Expression>
  parseMemberExpression(std::unique_ptr<ast::Expression> object);
  std::unique_ptr<ast::Expression>
  parseIndexExpression(std::unique_ptr<ast::Expression> object);
  std::unique_ptr<ast::Expression> parseArrayLiteral();
  std::unique_ptr<ast::Expression> parseObjectLiteral();
  std::unique_ptr<ast::Expression>
  parseLambdaFromParams(std::vector<std::unique_ptr<ast::Identifier>> params);
  std::unique_ptr<ast::Expression>
  parsePostfixExpression(std::unique_ptr<ast::Expression> expr);
  TokenType getBinaryOperatorToken(ast::BinaryOperator op);

  // Condition combination helpers
  std::unique_ptr<ast::Expression>
  combineConditions(std::unique_ptr<ast::Expression> left,
                    std::unique_ptr<ast::Expression> right);

  // Error recovery methods
  bool synchronize();
  bool atStatementStart();
  bool isAtEndOfBlock();

public:
  explicit Parser() = default;

  // Main entry point (like Tyler's produceAST)
  std::unique_ptr<ast::Program> produceAST(const std::string &sourceCode);

  std::unique_ptr<ast::Program> produceASTStrict(const std::string &sourceCode);

  void printAST(const ast::ASTNode &node, int indent = 0) const;
};

} // namespace havel::parser