            #pragma once

#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace havel {

// Error severity levels
enum class ErrorSeverity { Error, Warning, Info };

// Compiler error with location and severity
struct CompilerError {
  ErrorSeverity severity;
  size_t line;
  size_t column;
  size_t length;
  std::string message;
  std::string sourceLine; // The source line where error occurred

  CompilerError(ErrorSeverity sev, size_t l, size_t c, const std::string &msg, size_t len = 1)
      : severity(sev), line(l), column(c), length(len), message(msg) {}
};

class LexError : public std::runtime_error {
public:
  LexError(size_t line, size_t column, const std::string &message,
           size_t length = 1)
      : std::runtime_error(message), line(line), column(column),
        length(length) {}

  size_t line;
  size_t column;
  size_t length;
};

enum class TokenType {
  Let,
  Const, // const - immutable binding
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
  Op, // op (operator overload)
  Return,
  Ret,
  Config,
  Devices,
  Modes,
  Signal,
  Group,
  Struct, // struct
  Class,  // class
  Enum,   // enum
  Trait,  // trait
  Impl,   // impl
  This,   // this - current object reference
  On,
  Off,
  When,
  Mode,
  Repeat, // repeat
  Identifier,
  Number,
  String,
  MultilineString, // """...""" multiline strings
  InterpolatedString,
  Backtick,            // `command` for shell output
  RegexLiteral,        // /pattern/ for regex
  ShellCommand,        // $ command for shell execution
  ShellCommandCapture, // $! command (capture output)
  Hotkey,
  Arrow,       // => (fat arrow for lambdas)
  ReturnType,  // -> (thin arrow for return types)
//  GlobalScope, // :: (global scope assignment) - REPLACED by ColonColon for hotkeys
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
  Power,     // ** power operator
  Backslash, // \ integer division
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
  Matches, // regex match operator
  Tilde,   // ~ regex match shorthand
  True,    // boolean literal true
  False,   // boolean literal false
  Null,    // null literal
  Assign,
  PlusAssign,
  MinusAssign,
  MultiplyAssign,
  DivideAssign,
  ModuloAssign, // %=
  PowerAssign,  // **=
  Nullish,      // ?? nullish coalescing
  Length,       // # length operator
  Pipe,
  Comment,
  NewLine,
  Import,       // import
  From,         // from
  As,           // as
  Use,          // use
  Export,       // export
  With,         // with
  Where,        // where - LINQ filter
  Select,       // select - LINQ transform
  ColonColon,   // :: hotkey block trigger
  Colon,        // :
  Question,     // ?
  OpenBracket,  // [
  CloseBracket, // ]
  DotDot,       // ..
  ShiftRight,   // >> (config append/get)
  Spread,       // ... for spread operator
  Hash,         // # for set literals
  At,           // @ for field access (this.field)
  SuperArrow,   // @-> for super method calls
  Underscore,   // _ for default match case
  Try,          // try
  Catch,        // catch
  Finally,      // finally
  Throw,        // throw
  EOF_TOKEN
};

struct Token {
  std::string value;
  TokenType type;
  std::string raw;
  size_t line;
  size_t column;
  size_t length;

  Token(const std::string &value, TokenType type, const std::string &raw,
        size_t line, size_t column, size_t length = 0)
      : value(value), type(type), raw(raw), line(line), column(column),
        length(length == 0 ? (raw.empty() ? value.length() : raw.length())
                           : length) {}

  std::string toString() const {
    return "Token(type=" + std::to_string(static_cast<int>(type)) +
           ", value=\"" + value + "\", raw=\"" + raw +
           "\", line=" + std::to_string(line) +
           ", column=" + std::to_string(column) +
           ", length=" + std::to_string(length) + ")";
  }
};

class Lexer {
public:
  Lexer(const std::string &sourceCode, bool debug_lexer = false);
  std::vector<Token> tokenize();
  void printTokens(const std::vector<Token> &tokens) const;

  // Error handling
  const std::vector<CompilerError> &getErrors() const { return errors; }
  bool hasErrors() const { return !errors.empty(); }

  static const std::unordered_map<std::string, TokenType> KEYWORDS;

private:
  std::string source;
  size_t position = 0;
  size_t line = 1;
  size_t column = 1;

  std::vector<CompilerError> errors; // Collected errors

  bool debug_lexer = false;

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
  Token scanString(bool isFString = false);
  Token scanMultilineString(bool isFString = false);
  Token scanBacktick();
  Token scanRegexLiteral();
  Token scanShellCommand(bool captureOutput = false);
  Token scanIdentifier();
  Token scanHotkey();

  // Error reporting
  void reportError(const std::string &message);
  void reportWarning(const std::string &message);
  std::string getSourceLine(size_t lineNum) const;
};

} // namespace havel
