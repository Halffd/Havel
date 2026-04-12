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
  ParseError(size_t line, size_t column, const std::string &message,
             size_t length = 1)
      : std::runtime_error(message), line(line), column(column),
        length(length) {}

  size_t line;
  size_t column;
  size_t length;
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

// ============================================================================
// PRATT PARSER (Top-Down Operator Precedence)
// ============================================================================

// Binding power (precedence) levels - higher = tighter binding
enum class BindingPower : int {
  None = 0,
  Assignment = 10,    // =, +=, -=, etc.
  Nullish = 20,       // ??
  LogicalOr = 30,     // ||
  LogicalAnd = 40,    // &&
  Equality = 50,      // ==, !=
  Comparison = 60,    // <, >, <=, >=
  Range = 70,         // ..
  Pipe = 75,          // |
  BitwiseOr = 80,     // |
  BitwiseXor = 90,    // ^
  BitwiseAnd = 100,   // &
  Shift = 110,        // <<, >>
  Additive = 120,     // +, -
  Multiplicative = 130, // *, /, %, **
  Prefix = 140,       // -x, !x, ~x, ++x, --x
  Postfix = 150,      // x++, x--, x.y, x[y], x()
  Call = 160,         // function calls
  Member = 170,       // ., [], ?., !
  Primary = 180       // literals, identifiers, (expr)
};

// Convert binding power to int for comparison
inline int bp(BindingPower power) { return static_cast<int>(power); }

// ============================================================================
// EXISTING PARSER CLASS
// ============================================================================

class Parser {
private:
  std::vector<Token> tokens;
  size_t position = 0;

  // Prevent copying/moving - Parser must not be copied or moved
  // to avoid memory corruption and invalid state
  Parser(const Parser &) = delete;
  Parser &operator=(const Parser &) = delete;
  Parser(Parser &&) = delete;
  Parser &operator=(Parser &&) = delete;

  // Controls whether `expr { ... }` is treated as call-sugar/lambda.
  // This must be disabled when parsing conditions for statements like
  // `if/while/when` to ensure the `{` starts the statement body.

  // Parser context - replaces individual bool flags
  struct ParserContext {
    bool inInputContext =
        false; // Inside hotkey block (bare expressions are input)
    bool allowBraceSugar = true; // Allow expr { ... } as call sugar
    bool inMatchExpression = false; // Inside match expression (disable arrow functions)
  };

  ParserContext context;

  // Debug options
  DebugOptions debug;

  // Error handling
  std::vector<CompilerError> errors;
  RecoveryMode recoveryMode = RecoveryMode::None;

  void reportError(const std::string &message);
  void reportErrorAt(const Token &token, const std::string &message);
  void synchronize();                 // Panic mode recovery
  void synchronizeTo(TokenType type); // Recover to specific token
  bool atStatementStart();
  bool isAtEndOfBlock();

  [[noreturn]] void fail(const std::string &message);
  [[noreturn]] void failAt(const Token &token, const std::string &message);

  // Error recovery
  void errorAt(const Token &token, const std::string &message);

  // Helper methods - return const references to avoid copies
  const Token &at(size_t offset = 0) const;
  const Token &advance();
  bool notEOF() const;

  // Parser methods (following Tyler's structure)
  std::unique_ptr<ast::Statement> parseStatement();
  std::unique_ptr<ast::Statement>
  parseInlineStatement(); // For inline forms (doesn't skip newlines)
  std::unique_ptr<ast::Expression> parseExpression();
  std::unique_ptr<ast::Expression> parseConfigAppend();
  std::unique_ptr<ast::Expression> parseAssignmentExpression();
  std::unique_ptr<ast::Expression> parseCastExpression();
  std::unique_ptr<ast::Expression> parseMatchExpression();
  std::unique_ptr<ast::Expression> parsePipelineExpression();
  std::unique_ptr<ast::Expression> parseQueryExpression();
  std::unique_ptr<ast::Expression> parseTernaryExpression();
  std::unique_ptr<ast::Expression> parseThreadExpression();
  std::unique_ptr<ast::Expression> parseIntervalExpression();
  std::unique_ptr<ast::Expression> parseTimeoutExpression();
  std::unique_ptr<ast::Expression> parseYieldExpression();
  std::unique_ptr<ast::Statement> parseGoStatement();
  std::unique_ptr<ast::Expression> parseChannelExpression();
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
  std::unique_ptr<ast::Statement> buildImplicitInputStatement(std::unique_ptr<ast::Expression> leadingExpr);
  std::unique_ptr<ast::Statement> parseMoreInputCommands(std::vector<ast::InputCommand> commands);
  std::unique_ptr<ast::Expression> parseGetInputExpression();
  std::unique_ptr<ast::Statement> parseWaitStatement();
  std::unique_ptr<ast::HotkeyBinding> parseHotkeyBinding();
  std::unique_ptr<ast::Statement> parseOnTapOrComboStatement();
  std::unique_ptr<ast::Statement> parseOnKeyDownOrKeyUpStatement();
  std::unique_ptr<ast::BlockStatement>
  parseBlockStatement(bool inputContext = false);
  std::unique_ptr<ast::Statement> parseWhenBlock();
  std::unique_ptr<ast::Statement> parseImportStatement();
  std::unique_ptr<ast::Statement> parseUseStatement();
  std::unique_ptr<ast::Statement> parseExportStatement();
  std::unique_ptr<ast::Statement> parseWithStatement();
  std::unique_ptr<ast::Statement> parseConfigBlock();
  std::unique_ptr<ast::Statement> parseDevicesBlock();
  std::unique_ptr<ast::Statement> parseModeDefinition(); // mode name { ... }
  std::unique_ptr<ast::Statement>
  parseModeBlock(); // mode name { statements } (shorthand)
  std::unique_ptr<ast::Statement> parseModesBlock(); // modes { ... }
  std::unique_ptr<ast::Statement>
  parseSignalDefinition(); // signal name = expression
  std::unique_ptr<ast::Statement>
  parseGroupDefinition(); // group name { modes: [...] }
  std::unique_ptr<ast::Statement> parseConfigSection();

  // Type system parsers
  std::unique_ptr<ast::Statement> parseStructDeclaration();
  std::unique_ptr<ast::Statement> parseClassDeclaration();
  std::unique_ptr<ast::Statement> parseEnumDeclaration();
  std::unique_ptr<ast::Statement> parseTraitDeclaration();
  std::unique_ptr<ast::Statement> parseImplDeclaration();
  std::unique_ptr<ast::TypeDefinition> parseTypeDefinition();
  std::unique_ptr<ast::TypeAnnotation> parseTypeAnnotation();
  std::pair<std::vector<ast::StructFieldDef>,
            std::vector<std::unique_ptr<ast::StructMethodDef>>>
  parseStructMembers();
  std::pair<std::vector<ast::ClassFieldDef>,
            std::vector<std::unique_ptr<ast::ClassMethodDef>>>
  parseClassMembers();
  std::vector<ast::EnumVariantDef> parseEnumVariants();

  std::vector<std::pair<std::string, std::unique_ptr<ast::Expression>>>
  parseKeyValueBlock();
  ast::BinaryOperator tokenToBinaryOperator(TokenType tokenType);
  std::unique_ptr<ast::Expression> parseLogicalOr();
  std::unique_ptr<ast::Expression> parseNullishCoalescing();
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
  std::unique_ptr<ast::Expression> parseObjectLiteral(bool unsorted = false);
  std::unique_ptr<ast::Expression> parseBlockExpression();
  std::unique_ptr<ast::Expression> parseIfExpression();
  std::unique_ptr<ast::Expression> parseArrayPattern();
  std::unique_ptr<ast::Expression> parseObjectPattern();
  
  // Pattern parsing for match expressions
  std::unique_ptr<ast::Expression> parsePattern();
  std::unique_ptr<ast::Expression> parsePatternAtom();
  std::unique_ptr<ast::Expression> parseArrayPatternForMatch();
  std::unique_ptr<ast::Expression> parseObjectPatternForMatch();
  
  std::unique_ptr<ast::Statement> parseTryStatement();
  std::unique_ptr<ast::Statement> parseThrowStatement();
  std::unique_ptr<ast::Statement> parseUIDeclaration();
  void parseUIElementDeclaration(
      const std::string &parentVar, bool addToParent,
      std::vector<std::unique_ptr<ast::Statement>> &statements);
  std::unique_ptr<ast::Expression> parseLambdaFromParams(
      std::vector<std::unique_ptr<ast::FunctionParameter>> params);
  std::unique_ptr<ast::Expression>
  parsePostfixExpression(std::unique_ptr<ast::Expression> expr);
  TokenType getBinaryOperatorToken(ast::BinaryOperator op);

  // Helper to create Identifier with source location
  std::unique_ptr<ast::Identifier> makeIdentifier(const Token &token);

  // Lookahead helper to detect destructuring patterns like {a, b} = obj
  bool isDestructuringPattern() const;
  bool isObjectLiteral() const;

  // Condition combination helpers
  std::unique_ptr<ast::Expression>
  combineConditions(std::unique_ptr<ast::Expression> left,
                    std::unique_ptr<ast::Expression> right);

  // ============================================================================
  // PRATT PARSER METHODS (Top-Down Operator Precedence)
  // ============================================================================

  // Main Pratt expression parser - parse with given right binding power
  std::unique_ptr<ast::Expression> parsePrattExpression(int rbp = 0);

  // Get left binding power for a token type - inline for performance
  inline int getBindingPower(TokenType type) const;

  // Get right binding power for a token type - inline for performance
  inline int getRightBindingPower(TokenType type) const;

  // Null denotation - parse token at start of expression
  std::unique_ptr<ast::Expression> nud(const Token &token);

  // Left denotation - parse token in infix/postfix position with left operand
  std::unique_ptr<ast::Expression> led(const Token &token,
                                       std::unique_ptr<ast::Expression> left);

  // Check if token can start an expression (has nud) - inline for performance
  inline bool canStartExpression(TokenType type) const;

  // Check if token is an infix/postfix operator (has led) - inline for performance
  inline bool isInfixOperator(TokenType type) const;

  // Helper methods for Pratt parser
  std::unique_ptr<ast::Expression> parseParenthesizedExpression();
  std::unique_ptr<ast::Expression> parseBacktickExpression();
  std::unique_ptr<ast::Expression> parseLambdaExpression();
  std::unique_ptr<ast::Expression> parseExpressionFromString(const std::string &expr);

public:
  explicit Parser(const DebugOptions &debug_opts = {}) : debug(debug_opts) {}

  // Main entry point (like Tyler's produceAST)
  std::unique_ptr<ast::Program> produceAST(const std::string &sourceCode);

  std::unique_ptr<ast::Program> parseStrict(const std::string &sourceCode);

  void printAST(const ast::ASTNode &node, int indent = 0) const;

  // Error access
  const std::vector<CompilerError> &getErrors() const { return errors; }
  bool hasErrors() const { return !errors.empty(); }
};

} // namespace havel::parser
