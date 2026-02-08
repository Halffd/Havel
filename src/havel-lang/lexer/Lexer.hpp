#pragma once

#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace havel {

class LexError : public std::runtime_error {
public:
  LexError(size_t line, size_t column, const std::string &message)
      : std::runtime_error(message), line(line), column(column) {}

  size_t line;
  size_t column;
};

enum class TokenType {
  Let,
  If,
  Else,
  While,
  Do,
  Switch,
  For,
  In,
  Loop,
  Break,
  Continue,
  Match,
  Case,
  Default,
  Fn,
  Return,
  Ret,
  Config,
  Devices,
  Modes,
  On,
  Off,
  When,
  Mode,
  Identifier,
  Number,
  String,
  InterpolatedString,
  Hotkey,
  Arrow,
  BinaryOp,
  OpenParen,
  CloseParen,
  OpenBrace,
  CloseBrace,
  Dot,
  Comma,
  Semicolon,
  // Operators
  Plus,
  Minus,
  Multiply,
  Divide,
  Modulo,
  PlusPlus,
  MinusMinus, // Increment/Decrement
  Equals,
  NotEquals,
  Less,
  Greater,
  LessEquals,
  GreaterEquals,
  And,
  Or,
  Not,
  Assign,
  PlusAssign,
  MinusAssign,
  MultiplyAssign,
  DivideAssign,
  Pipe,
  Comment,
  NewLine,
  Import,       // import
  From,         // from
  As,           // as
  Use,          // use
  With,         // with
  Colon,        // :
  Question,     // ?
  OpenBracket,  // [
  CloseBracket, // ]
  DotDot,       // ..
  Hash,         // # for set literals
  EOF_TOKEN
};

struct Token {
  std::string value;
  TokenType type;
  std::string raw;
  size_t line;
  size_t column;

  Token(const std::string &value, TokenType type, const std::string &raw,
        size_t line, size_t column)
      : value(value), type(type), raw(raw), line(line), column(column) {}

  std::string toString() const {
    return "Token(type=" + std::to_string(static_cast<int>(type)) +
           ", value=\"" + value + "\", raw=\"" + raw +
           "\", line=" + std::to_string(line) +
           ", column=" + std::to_string(column) + ")";
  }
};

class Lexer {
public:
  Lexer(const std::string &sourceCode);
  std::vector<Token> tokenize();
  void printTokens(const std::vector<Token> &tokens) const;

  static const std::unordered_map<std::string, TokenType> KEYWORDS;

private:
  std::string source;
  size_t position = 0;
  size_t line = 1;
  size_t column = 1;

  static const std::unordered_map<char, TokenType> SINGLE_CHAR_TOKENS;

  bool isAtEnd() const;
  char peek(size_t offset = 0) const;
  char advance();

  bool isAlpha(char c) const;
  bool isDigit(char c) const;
  bool isAlphaNumeric(char c) const;
  bool isSkippable(char c) const;
  bool isHotkeyChar(char c) const;

  void skipWhitespace();
  void skipComment();

  Token makeToken(const std::string &value, TokenType type,
                  const std::string &raw = "");

  Token scanNumber();
  Token scanString();
  Token scanIdentifier();
  Token scanHotkey();
};

} // namespace havel