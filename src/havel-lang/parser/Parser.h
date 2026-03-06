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

// Error recovery mode
enum class RecoveryMode {
  None,           // Throw immediately (old behavior)
  PanicSemicolon, // Panic to next semicolon
  PanicBrace,     // Panic to next closing brace
  PanicStatement  // Panic to next statement boundary
};

// Debug options for parser and lexer
struct DebugOptions {
  bool lexer = false;
  bool parser = false;
  bool ast = false;
};

class Parser {
private:
  std::vector<Token> tokens;
  size_t position = 0;

  // Controls whether `expr { ... }` is treated as call-sugar/lambda.
  // This must be disabled when parsing conditions for statements like
  // `if/while/when` to ensure the `{` starts the statement body.
  bool allowBraceCallSugar = true;

  // Track if we're inside a hotkey block where bare expressions are input commands
  bool inInputContext = false;

  // Debug options
  DebugOptions debug;
  
  // Error handling
  std::vector<CompilerError> errors;
  RecoveryMode recoveryMode = RecoveryMode::None;
  
  void reportError(const std::string& message);
  void reportErrorAt(const Token& token, const std::string& message);
  void synchronize();  // Panic mode recovery
  void synchronizeTo(TokenType type);  // Recover to specific token
  bool atStatementStart();
  bool isAtEndOfBlock();

  [[noreturn]] void fail(const std::string &message);
  [[noreturn]] void failAt(const Token &token, const std::string &message);

  // Helper methods (like Tyler's at() and eat())
  Token at(size_t offset = 0) const;
  Token advance();
  bool notEOF() const;

  // Parser methods (following Tyler's structure)
  std::unique_ptr<ast::Statement> parseStatement();
  std::unique_ptr<ast::Statement> parseInlineStatement();  // For inline forms (doesn't skip newlines)
  std::unique_ptr<ast::Expression> parseExpression();
  std::unique_ptr<ast::Expression> parseConfigAppend();
  std::unique_ptr<ast::Expression> parseAssignmentExpression();
  std::unique_ptr<ast::Expression> parsePipelineExpression();
  std::unique_ptr<ast::Expression> parseTernaryExpression();
  std::unique_ptr<ast::Expression> parseBinaryExpression();
  std::unique_ptr<ast::Expression> parsePrimaryExpression();

  // Havel-specific parsers
  std::unique_ptr<ast::Statement> parseLetDeclaration();
  std::unique_ptr<ast::Statement> parseIfStatement();
  std::unique_ptr<ast::Statement> parseWhileStatement();
  std::unique_ptr<ast::Statement> parseDoWhileStatement();
  std::unique_ptr<ast::Statement> parseForStatement();
  std::unique_ptr<ast::Statement> parseLoopStatement();
  std::unique_ptr<ast::Statement> parseSwitchStatement();
  std::unique_ptr<ast::Statement> parseBreakStatement();
  std::unique_ptr<ast::Statement> parseContinueStatement();
  std::unique_ptr<ast::Statement> parseRepeatStatement();
  std::unique_ptr<ast::Statement> parseOnStatement();
  std::unique_ptr<ast::Statement> parseOnModeStatement();
  std::unique_ptr<ast::Statement> parseOnModeStatementBody();
  std::unique_ptr<ast::Statement> parseOffModeStatement();
  std::unique_ptr<ast::Statement> parseOnReloadStatement();
  std::unique_ptr<ast::Statement> parseOnStartStatement();
  std::unique_ptr<ast::Statement> parseFunctionDeclaration();
  std::unique_ptr<ast::Statement> parseReturnStatement();
  std::unique_ptr<ast::Statement> parseSleepStatement();
  std::unique_ptr<ast::Statement> parseInputStatement();
  std::unique_ptr<ast::Statement> parseImplicitInputStatement();
  std::unique_ptr<ast::HotkeyBinding> parseHotkeyBinding();
  std::unique_ptr<ast::BlockStatement> parseBlockStatement(bool inputContext = false);
  std::unique_ptr<ast::Statement> parseWhenBlock();
  std::unique_ptr<ast::Statement> parseImportStatement();
  std::unique_ptr<ast::Statement> parseUseStatement();
  std::unique_ptr<ast::Statement> parseWithStatement();
  std::unique_ptr<ast::Statement> parseConfigBlock();
  std::unique_ptr<ast::Statement> parseDevicesBlock();
  std::unique_ptr<ast::Statement> parseModesBlock();
  std::unique_ptr<ast::Statement> parseConfigSection();

  // Type system parsers
  std::unique_ptr<ast::Statement> parseStructDeclaration();
  std::unique_ptr<ast::Statement> parseEnumDeclaration();
  std::unique_ptr<ast::Statement> parseTraitDeclaration();
  std::unique_ptr<ast::Statement> parseImplDeclaration();
  std::unique_ptr<ast::TypeDefinition> parseTypeDefinition();
  std::unique_ptr<ast::TypeAnnotation> parseTypeAnnotation();
  std::pair<std::vector<ast::StructFieldDef>, std::vector<std::unique_ptr<ast::StructMethodDef>>> parseStructMembers();
  std::vector<ast::EnumVariantDef> parseEnumVariants();
  
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
  std::unique_ptr<ast::Expression> parseBlockExpression();
  std::unique_ptr<ast::Expression> parseIfExpression();
  std::unique_ptr<ast::Expression> parseArrayPattern();
  std::unique_ptr<ast::Expression> parseObjectPattern();
  std::unique_ptr<ast::Statement> parseTryStatement();
  std::unique_ptr<ast::Statement> parseThrowStatement();
  std::unique_ptr<ast::Expression>
  parseLambdaFromParams(std::vector<std::unique_ptr<ast::FunctionParameter>> params);
  std::unique_ptr<ast::Expression>
  parsePostfixExpression(std::unique_ptr<ast::Expression> expr);
  TokenType getBinaryOperatorToken(ast::BinaryOperator op);

  // Condition combination helpers
  std::unique_ptr<ast::Expression>
  combineConditions(std::unique_ptr<ast::Expression> left,
                    std::unique_ptr<ast::Expression> right);

public:
  explicit Parser(const DebugOptions &debug_opts = {}) : debug(debug_opts) {}

  // Main entry point (like Tyler's produceAST)
  std::unique_ptr<ast::Program> produceAST(const std::string &sourceCode);

  std::unique_ptr<ast::Program> produceASTStrict(const std::string &sourceCode);

  void printAST(const ast::ASTNode &node, int indent = 0) const;
  
  // Error access
  const std::vector<CompilerError>& getErrors() const { return errors; }
  bool hasErrors() const { return !errors.empty(); }
};

} // namespace havel::parser