#include "Lexer.hpp"
#include <cctype>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace havel {

// Static member definitions
const std::unordered_map<std::string, TokenType> Lexer::KEYWORDS = {
    {"let", TokenType::Let},
    {"val", TokenType::Const},      // val - immutable binding
    {"if", TokenType::If},
    {"else", TokenType::Else},
    {"while", TokenType::While},
    {"do", TokenType::Do},
    {"switch", TokenType::Switch},
    {"for", TokenType::For},
    {"in", TokenType::In},
    {"loop", TokenType::Loop},
    {"break", TokenType::Break},
    {"continue", TokenType::Continue},
    {"match", TokenType::Match},
    {"case", TokenType::Case},
    {"default", TokenType::Default},
    {"fn", TokenType::Fn},
    {"op", TokenType::Op},
    {"return", TokenType::Return},
    {"ret", TokenType::Ret},
    {"try", TokenType::Try},
    {"catch", TokenType::Catch},
    {"finally", TokenType::Finally},
    {"throw", TokenType::Throw},
    {"config", TokenType::Config},
    {"devices", TokenType::Devices},
    {"modes", TokenType::Modes},
    {"signal", TokenType::Signal},
    {"group", TokenType::Group},
    {"struct", TokenType::Struct},
    {"enum", TokenType::Enum},
    {"trait", TokenType::Trait},
    {"impl", TokenType::Impl},
    {"on", TokenType::On},
    {"off", TokenType::Off},
    {"when", TokenType::When},
    {"mode", TokenType::Mode},
    {"repeat", TokenType::Repeat},
    {"true", TokenType::True},
    {"false", TokenType::False},
    {"send", TokenType::Identifier},
    {"clipboard", TokenType::Identifier}, // Built-in module
    {"text", TokenType::Identifier},      // Built-in module
    {"window", TokenType::Identifier},    // Built-in module
    {"import", TokenType::Import},
    // English-style logical operators (aliases for &&, ||, !)
    {"and", TokenType::And},
    {"or", TokenType::Or},
    {"not", TokenType::Not},
    {"matches", TokenType::Matches},  // regex match operator
    {"from", TokenType::From},
    {"as", TokenType::As},
    {"use", TokenType::Use},
    {"with", TokenType::With}};

const std::unordered_map<char, TokenType> Lexer::SINGLE_CHAR_TOKENS = {
    {'(', TokenType::OpenParen},   {')', TokenType::CloseParen},
    {'{', TokenType::OpenBrace},   {'}', TokenType::CloseBrace},
    {'[', TokenType::OpenBracket}, {']', TokenType::CloseBracket},
    {'.', TokenType::Dot},         {',', TokenType::Comma},
    {';', TokenType::Semicolon},   {':', TokenType::Colon},
    {'?', TokenType::Question},    {'|', TokenType::Pipe},
    {'+', TokenType::Plus},        {'-', TokenType::Minus},
    {'*', TokenType::Multiply},    {'/', TokenType::Divide},
    {'%', TokenType::Modulo},      {'\\', TokenType::Backslash},
    {'\n', TokenType::NewLine},
    {'!', TokenType::Not},         {'_', TokenType::Underscore},
    {'~', TokenType::Tilde}};

Lexer::Lexer(const std::string &sourceCode, bool debug_lexer)
    : source(sourceCode), debug_lexer(debug_lexer) {}

std::string Lexer::getSourceLine(size_t lineNum) const {
  std::istringstream iss(source);
  std::string currentLine;
  for (size_t i = 1; i < lineNum && std::getline(iss, currentLine); i++) {}
  std::getline(iss, currentLine);
  return currentLine;
}

void Lexer::reportError(const std::string& message) {
  CompilerError err(ErrorSeverity::Error, line, column, message);
  err.sourceLine = getSourceLine(line);
  errors.push_back(err);
}

void Lexer::reportWarning(const std::string& message) {
  CompilerError err(ErrorSeverity::Warning, line, column, message);
  err.sourceLine = getSourceLine(line);
  errors.push_back(err);
}

char Lexer::peek(size_t offset) const {
  size_t pos = position + offset;
  if (pos >= source.length())
    return '\0';
  return source[pos];
}

char Lexer::advance() {
  if (isAtEnd())
    return '\0';

  char current = source[position++];

  if (current == '\n') {
    line++;
    column = 1;
  } else {
    column++;
  }

  return current;
}

bool Lexer::isAtEnd() const { return position >= source.length(); }

bool Lexer::isAlpha(char c) const { return std::isalpha(c) || c == '_'; }

bool Lexer::isDigit(char c) const { return std::isdigit(c); }

bool Lexer::isAlphaNumeric(char c) const { return isAlpha(c) || isDigit(c); }

bool Lexer::isSkippable(char c) const {
  return c == ' ' || c == '\t' || c == '\r';
}

bool Lexer::isHotkeyChar(char c) const {
  return isAlphaNumeric(c) || c == '+' || c == '-' || c == '^' || c == '!' ||
         c == '#' || c == '@' || c == '|' || c == '*' || c == '&' || c == ':' ||
         c == '~' || c == '$' || c == '=' || c == '.' || c == ',' || c == '/';
}

Token Lexer::makeToken(const std::string &value, TokenType type,
                       const std::string &raw) {
  const std::string tokenRaw = raw.empty() ? value : raw;
  const size_t tokenLength = tokenRaw.length() == 0 ? 1 : tokenRaw.length();
  size_t tokenColumn = column;
  if (tokenColumn > tokenLength) {
    tokenColumn -= tokenLength;
  } else {
    tokenColumn = 1;
  }
  return Token(value, type, tokenRaw, line, tokenColumn, tokenLength);
}

void Lexer::skipWhitespace() {
  while (!isAtEnd() && isSkippable(peek())) {
    advance();
  }
}

void Lexer::skipComment() {
  // At this point, we've already consumed the first '/' and verified the next
  // char Single line comment //
  if (peek() == '/') {
    advance(); // consume second '/'
    while (!isAtEnd() && peek() != '\n') {
      advance();
    }
  }
  // Multi-line comment /* */
  else if (peek() == '*') {
    advance(); // consume '*'

    while (!isAtEnd()) {
      if (peek() == '*' && peek(1) == '/') {
        advance(); // *
        advance(); // /
        break;
      }
      advance();
    }
  }
}

Token Lexer::scanNumber() {
  size_t start = position - 1; // Include the first digit we already consumed
  std::string number;
  number += source[start];

  // Handle negative numbers
  bool isNegative = (start > 0 && source[start - 1] == '-');
  if (isNegative) {
    number = "-" + number;
    start--;
  }

  // Scan integer part
  while (!isAtEnd() && isDigit(peek())) {
    number += advance();
  }

  // Scan fractional part
  if (!isAtEnd() && peek() == '.' && isDigit(peek(1))) {
    number += advance(); // consume '.'
    while (!isAtEnd() && isDigit(peek())) {
      number += advance();
    }
  }

  return makeToken(number, TokenType::Number);
}

Token Lexer::scanString() {
  std::string value;
  std::string raw;
  bool hasInterpolation = false;

  // Skip opening quote
  char quote = source[position - 1];
  int braceDepth = 0; // Tracks depth inside ${ ... }
  
  // Single quotes = literal string (no interpolation)
  // Double quotes = interpolated string
  bool allowInterpolation = (quote == '"');

  while (!isAtEnd()) {
    char c = peek();

    // If we're not inside an interpolation, a matching quote ends the string
    if (braceDepth == 0 && c == quote) {
      break;
    }

    raw += c;

    if (braceDepth == 0 && c == '\\' && !isAtEnd()) {
      // Only process escape sequences in the outer string portion
      advance(); // consume backslash
      raw += peek();

      char escaped = advance();
      switch (escaped) {
      case 'n':
        value += '\n';
        break;
      case 't':
        value += '\t';
        break;
      case 'r':
        value += '\r';
        break;
      case '\\':
        value += '\\';
        break;
      case '"':
        value += '"';
        break;
      case '\'':
        value += '\'';
        break;
      default:
        value += '\\';
        value += escaped;
        break;
      }
    } else if (c == '$' && allowInterpolation) {
      // Found interpolation marker (only in double-quoted strings)
      hasInterpolation = true;
      value += advance(); // $

      // Check if it's ${expr} or $var
      if (peek() == '{') {
        value += advance(); // {
        braceDepth++;       // Enter interpolation context
      } else if (isAlpha(peek()) || peek() == '_') {
        // Bash-style $var - consume the variable name
        // Add implicit { } around the variable name for consistent parsing
        value += '{';
        while (!isAtEnd() && (isAlphaNumeric(peek()) || peek() == '_')) {
          value += advance();
        }
        value += '}';
        // No change to braceDepth since we synthetically closed it immediately
      } else {
        // Just a $ not followed by { or identifier, treat as literal '$'
        // Do not mark as interpolation in this case
      }
    } else {
      // Regular character processing
      char consumed = advance();
      value += consumed;

      if (braceDepth > 0) {
        if (consumed == '{') {
          // Track nested braces within interpolation (e.g., object literals)
          braceDepth++;
        } else if (consumed == '}') {
          braceDepth--;
        }
      }
    }
  }

  if (isAtEnd()) {
    reportError("Unterminated string");
    // Create error token and try to recover
    std::string value = raw.substr(1);  // Skip opening quote
    return makeToken(value, TokenType::String, raw);
  }

  // Consume closing quote
  advance();

  TokenType type =
      hasInterpolation ? TokenType::InterpolatedString : TokenType::String;
  return makeToken(value, type, raw);
}

Token Lexer::scanMultilineString() {
  std::string value;
  std::string raw;
  bool hasInterpolation = false;
  int braceDepth = 0;  // Tracks depth inside ${ ... }

  // Skip opening """ (already consumed by caller)
  // Multiline strings support interpolation like regular double-quoted strings
  
  // Skip initial newline if present (for """\n... style)
  if (!isAtEnd() && peek() == '\n') {
    advance();
    raw += '\n';
  }

  while (!isAtEnd()) {
    // Check for closing """
    if (peek() == '"' && 
        position + 2 < source.length() &&
        source[position + 1] == '"' && 
        source[position + 2] == '"') {
      break;
    }

    char c = peek();
    raw += c;

    if (braceDepth == 0 && c == '\\' && !isAtEnd()) {
      // Process escape sequences
      advance();  // consume backslash
      raw += peek();

      char escaped = advance();
      switch (escaped) {
      case 'n':
        value += '\n';
        break;
      case 't':
        value += '\t';
        break;
      case 'r':
        value += '\r';
        break;
      case '\\':
        value += '\\';
        break;
      case '"':
        value += '"';
        break;
      case '\'':
        value += '\'';
        break;
      default:
        value += '\\';
        value += escaped;
        break;
      }
    } else if (c == '$' && braceDepth == 0) {
      // Interpolation in multiline strings
      hasInterpolation = true;
      value += advance();  // $

      if (peek() == '{') {
        value += advance();  // {
        braceDepth++;
      } else if (isAlpha(peek()) || peek() == '_') {
        value += '{';
        while (!isAtEnd() && (isAlphaNumeric(peek()) || peek() == '_')) {
          value += advance();
        }
        value += '}';
      }
    } else {
      char consumed = advance();
      value += consumed;

      if (braceDepth > 0) {
        if (consumed == '{') {
          braceDepth++;
        } else if (consumed == '}') {
          braceDepth--;
        }
      }
    }
  }

  if (isAtEnd()) {
    reportError("Unterminated multiline string");
    return makeToken(value, TokenType::MultilineString, raw);
  }

  // Consume closing """
  advance();
  advance();
  advance();

  TokenType type = hasInterpolation ? TokenType::InterpolatedString : TokenType::MultilineString;
  return makeToken(value, type, raw);
}

Token Lexer::scanBacktick() {
  std::string value;
  std::string raw;

  // Consume characters until closing backtick
  while (!isAtEnd() && peek() != '`') {
    char c = advance();
    value += c;
    raw += c;
  }

  // Consume closing backtick
  if (!isAtEnd()) {
    advance();
  }

  return makeToken(value, TokenType::Backtick, raw);
}

Token Lexer::scanRegexLiteral() {
  std::string value;
  std::string raw;

  // Consume characters until closing slash
  while (!isAtEnd() && peek() != '/') {
    char c = advance();
    // Handle escape sequences
    if (c == '\\' && !isAtEnd()) {
      value += c;
      c = advance();
    }
    value += c;
    raw += c;
  }

  // Consume closing slash
  if (!isAtEnd()) {
    advance();
  }

  return makeToken(value, TokenType::RegexLiteral, "/" + raw + "/");
}

Token Lexer::scanShellCommand(bool captureOutput) {
  // $ already consumed, just return the $ as a token
  // The parser will handle the expression that follows
  TokenType type = captureOutput ? TokenType::ShellCommandCapture : TokenType::ShellCommand;
  return makeToken(captureOutput ? "$!" : "$", type, captureOutput ? "$!" : "$");
}

Token Lexer::scanIdentifier() {
  std::string identifier;

  // First character (already consumed)
  identifier += source[position - 1];

  // Subsequent characters
  while (!isAtEnd() && isAlphaNumeric(peek())) {
    identifier += advance();
  }

  // Check if it's a keyword
  auto keywordIt = KEYWORDS.find(identifier);
  TokenType type =
      (keywordIt != KEYWORDS.end()) ? keywordIt->second : TokenType::Identifier;

  return makeToken(identifier, type);
}

Token Lexer::scanHotkey() {
  std::string hotkey;

  // Include the already consumed character
  hotkey += source[position - 1];

  // Continue consuming characters that are part of a hotkey until a terminator
  while (!isAtEnd()) {
    char c = peek();
    // Allow space-separated combo hotkeys like "RShift & WheelUp" or "LButton &
    // RButton:" Only keep whitespace if it is part of a combo expression
    // (followed by '&' or ':')
    if (c == ' ' || c == '\t') {
      if (hotkey.find('&') != std::string::npos) {
        hotkey += advance();
        continue;
      }
      size_t look = position;
      while (look < source.size() &&
             (source[look] == ' ' || source[look] == '\t')) {
        look++;
      }
      if (look < source.size() &&
          (source[look] == '&' || source[look] == ':')) {
        while (position < look) {
          hotkey += advance();
        }
        continue;
      }
      break;
    }

    // Stop at whitespace or special characters that end hotkeys or start other
    // tokens
    if (c == '\r' || c == '\n' || c == '{' || c == '(') {
      break;
    }

    // Do not consume the '=' that begins the '=>' arrow operator
    if (c == '=' && peek(1) == '>') {
      break;
    }
    if (!isHotkeyChar(c))
      break;
    hotkey += advance();
  }

  // Special handling for plain F-keys (F1..F12)
  if (hotkey.size() >= 2 && hotkey[0] == 'F') {
    bool allDigits = true;
    for (size_t i = 1; i < hotkey.size(); ++i)
      allDigits &= std::isdigit(static_cast<unsigned char>(hotkey[i]));
    if (allDigits) {
      try {
        int fnum = std::stoi(hotkey.substr(1));
        if (fnum >= 1 && fnum <= 12)
          return makeToken(hotkey, TokenType::Hotkey);
      } catch (...) {
      }
    }
  }

  // Accept raw modifier-based forms and combo hotkeys (e.g. "RShift &
  // WheelDown") as Hotkey
  if (!hotkey.empty() &&
      (hotkey.find('^') != std::string::npos ||
       hotkey.find('!') != std::string::npos ||
       hotkey.find('+') != std::string::npos ||
       hotkey.find('#') != std::string::npos ||
       hotkey.find('@') != std::string::npos ||
       hotkey.find('~') != std::string::npos ||
       hotkey.find('$') != std::string::npos ||
       hotkey.find('&') != std::string::npos ||
       hotkey.find(':') != std::string::npos || hotkey[0] == 'F')) {
    return makeToken(hotkey, TokenType::Hotkey);
  }

  // Fallback: not a recognizable hotkey, rewind and treat as identifier
  position -= (hotkey.size() - 1);
  return scanIdentifier();
}

std::vector<Token> Lexer::tokenize() {
  std::vector<Token> tokens;

  while (!isAtEnd()) {
    skipWhitespace();

    if (isAtEnd())
      break;

    char c = advance();

    // Handle comments BEFORE other tokens (especially '/' and '#')
    if (c == '/' && (peek() == '/' || peek() == '*')) {
      skipComment();
      continue;
    }

    // Handle # as hotkey modifier or set literal ONLY
    // Comments MUST use // or /* */ - # is NEVER a comment
    if (c == '#') {
      // If the next char is '{', treat as set literal start
      if (peek() == '{') {
        tokens.push_back(makeToken("#", TokenType::Hash));
        if (debug_lexer) {
          std::cout << "LEX: " << tokens.back().toString() << std::endl;
        }
        continue;
      }

      // Otherwise, '#' starts a modifier hotkey (e.g. "#f1", "#!Esc")
      tokens.push_back(scanHotkey());
      if (debug_lexer) {
        std::cout << "LEX: " << tokens.back().toString() << std::endl;
      }
      continue;
    }

    // Handle numbers (including negative numbers in certain contexts)
    if (isDigit(c) || (c == '-' && isDigit(peek()))) {
      tokens.push_back(scanNumber());
      if (debug_lexer) {
        std::cout << "LEX: " << tokens.back().toString() << std::endl;
      }
      continue;
    }

    // Handle strings
    if (c == '"' || c == '\'') {
      // Check for multiline string """
      if (c == '"' && 
          position + 2 < source.length() &&
          source[position] == '"' && 
          source[position + 1] == '"') {
        advance();  // consume first " (second one)
        advance();  // consume second " (third one)
        tokens.push_back(scanMultilineString());
      } else {
        tokens.push_back(scanString());
      }
      if (debug_lexer) {
        std::cout << "LEX: " << tokens.back().toString() << std::endl;
      }
      continue;
    }

    // Handle backtick expressions: `command`
    if (c == '`') {
      tokens.push_back(scanBacktick());
      if (debug_lexer) {
        std::cout << "LEX: " << tokens.back().toString() << std::endl;
      }
      continue;
    }

    // Handle regex literals: /pattern/
    // Only if not followed by '/' (which would be // comment) or '*' (/* comment)
    // and not preceded by something that would make it division
    if (c == '/' && peek() != '/' && peek() != '*' && peek() != '=') {
      // Check if this looks like a regex (not division)
      // Simple heuristic: if previous non-whitespace token suggests expression context
      bool isRegexContext = tokens.empty() || 
          tokens.back().type == TokenType::OpenParen ||
          tokens.back().type == TokenType::OpenBracket ||
          tokens.back().type == TokenType::Comma ||
          tokens.back().type == TokenType::Assign ||
          tokens.back().type == TokenType::Arrow ||
          tokens.back().type == TokenType::And ||
          tokens.back().type == TokenType::Or ||
          tokens.back().type == TokenType::Not ||
          tokens.back().type == TokenType::In ||
          tokens.back().type == TokenType::Matches ||
          tokens.back().type == TokenType::Tilde ||
          tokens.back().type == TokenType::Colon ||
          tokens.back().type == TokenType::Question ||
          tokens.back().type == TokenType::Pipe ||
          tokens.back().type == TokenType::NewLine ||
          tokens.back().type == TokenType::Semicolon;
      
      if (isRegexContext && !isDigit(peek())) {
        tokens.push_back(scanRegexLiteral());
        if (debug_lexer) {
          std::cout << "LEX: " << tokens.back().toString() << std::endl;
        }
        continue;
      }
    }

    // Handle return type arrow ->
    if (c == '-' && peek() == '>') {
      advance(); // consume '>'
      tokens.push_back(makeToken("->", TokenType::ReturnType));
      if (debug_lexer) {
        std::cout << "LEX: " << tokens.back().toString() << std::endl;
      }
      continue;
    }

    // Handle arrow operator =>
    if (c == '=' && peek() == '>') {
      advance(); // consume '>'
      tokens.push_back(makeToken("=>", TokenType::Arrow));
      continue;
    }

    // Handle global scope operator ::
    if (c == ':' && peek() == ':') {
      advance(); // consume second ':'
      tokens.push_back(makeToken("::", TokenType::GlobalScope));
      if (debug_lexer) {
        std::cout << "LEX: " << tokens.back().toString() << std::endl;
      }
      continue;
    }

    // Handle ++ and --
    if (c == '+' && peek() == '+') {
      advance();
      tokens.push_back(makeToken("++", TokenType::PlusPlus));
      if (debug_lexer) {
        std::cout << "LEX: " << tokens.back().toString() << std::endl;
      }
      continue;
    }
    if (c == '-' && peek() == '-') {
      advance();
      tokens.push_back(makeToken("--", TokenType::MinusMinus));
      if (debug_lexer) {
        std::cout << "LEX: " << tokens.back().toString() << std::endl;
      }
      continue;
    }

    // Handle compound assignments first: +=, -=, *=, /=
    if (c == '+' && peek() == '=') {
      advance();
      tokens.push_back(makeToken("+=", TokenType::PlusAssign));
      if (debug_lexer) {
        std::cout << "LEX: " << tokens.back().toString() << std::endl;
      }
      continue;
    }
    if (c == '-' && peek() == '=') {
      advance();
      tokens.push_back(makeToken("-=", TokenType::MinusAssign));
      if (debug_lexer) {
        std::cout << "LEX: " << tokens.back().toString() << std::endl;
      }
      continue;
    }
    if (c == '*' && peek() == '=') {
      advance();
      tokens.push_back(makeToken("*=", TokenType::MultiplyAssign));
      if (debug_lexer) {
        std::cout << "LEX: " << tokens.back().toString() << std::endl;
      }
      continue;
    }
    if (c == '/' && peek() == '=') {
      advance();
      tokens.push_back(makeToken("/=", TokenType::DivideAssign));
      if (debug_lexer) {
        std::cout << "LEX: " << tokens.back().toString() << std::endl;
      }
      continue;
    }
    if (c == '%' && peek() == '=') {
      advance();
      tokens.push_back(makeToken("%=", TokenType::ModuloAssign));
      if (debug_lexer) {
        std::cout << "LEX: " << tokens.back().toString() << std::endl;
      }
      continue;
    }
    if (c == '*' && peek() == '*') {
      // Check for **= (power assign) or ** (power)
      size_t look = position + 1;
      if (look < source.length() && source[look] == '=') {
        advance();  // consume first *
        advance();  // consume second *
        advance();  // consume =
        tokens.push_back(makeToken("**=", TokenType::PowerAssign));
        if (debug_lexer) {
          std::cout << "LEX: " << tokens.back().toString() << std::endl;
        }
        continue;
      } else {
        advance();  // consume first *
        advance();  // consume second *
        tokens.push_back(makeToken("**", TokenType::Power));
        if (debug_lexer) {
          std::cout << "LEX: " << tokens.back().toString() << std::endl;
        }
        continue;
      }
    }

    // Handle ?? (nullish coalescing)
    if (c == '?' && peek() == '?') {
      advance();
      advance();
      tokens.push_back(makeToken("??", TokenType::Nullish));
      if (debug_lexer) {
        std::cout << "LEX: " << tokens.back().toString() << std::endl;
      }
      continue;
    }

    // Handle == and !=
    if (c == '=' && peek() == '=') {
      advance();
      tokens.push_back(makeToken("==", TokenType::Equals));
      if (debug_lexer) {
        std::cout << "LEX: " << tokens.back().toString() << std::endl;
      }
      continue;
    }
    if (c == '!' && peek() == '=') {
      advance();
      tokens.push_back(makeToken("!=", TokenType::NotEquals));
      if (debug_lexer) {
        std::cout << "LEX: " << tokens.back().toString() << std::endl;
      }
      continue;
    }

    // Handle && and ||
    if (c == '&' && peek() == '&') {
      advance();
      tokens.push_back(makeToken("&&", TokenType::And));
      if (debug_lexer) {
        std::cout << "LEX: " << tokens.back().toString() << std::endl;
      }
      continue;
    }
    if (c == '|' && peek() == '|') {
      advance();
      tokens.push_back(makeToken("||", TokenType::Or));
      if (debug_lexer) {
        std::cout << "LEX: " << tokens.back().toString() << std::endl;
      }
      continue;
    }

    // Handle <= and >=
    if (c == '<' && peek() == '=') {
      advance();
      tokens.push_back(makeToken("<=", TokenType::LessEquals));
      if (debug_lexer) {
        std::cout << "LEX: " << tokens.back().toString() << std::endl;
      }
      continue;
    }
    if (c == '>' && peek() == '=') {
      advance();
      tokens.push_back(makeToken(">=", TokenType::GreaterEquals));
      if (debug_lexer) {
        std::cout << "LEX: " << tokens.back().toString() << std::endl;
      }
      continue;
    }

    // Handle >> (config append/get operator) - must check before single >
    if (c == '>' && peek() == '>') {
      advance(); // consume second '>'
      tokens.push_back(makeToken(">>", TokenType::ShiftRight));
      if (debug_lexer) {
        std::cout << "LEX: " << tokens.back().toString() << std::endl;
      }
      continue;
    }

    // Handle single < and >
    if (c == '<') {
      tokens.push_back(makeToken("<", TokenType::Less));
      if (debug_lexer) {
        std::cout << "LEX: " << tokens.back().toString() << std::endl;
      }
      continue;
    }
    if (c == '>') {
      tokens.push_back(makeToken(">", TokenType::Greater));
      if (debug_lexer) {
        std::cout << "LEX: " << tokens.back().toString() << std::endl;
      }
      continue;
    }

    // Handle single equals (assignment)
    if (c == '=') {
      tokens.push_back(makeToken("=", TokenType::Assign));
      if (debug_lexer) {
        std::cout << "LEX: " << tokens.back().toString() << std::endl;
      }
      continue;
    }

    // Handle ... (spread operator) - must check before ..
    // Note: c is already consumed by advance(), so peek() is at position+1
    if (c == '.' && peek() == '.' && peek(1) == '.') {
      advance(); // consume second '.'
      advance(); // consume third '.'
      tokens.push_back(makeToken("...", TokenType::Spread));
      continue;
    }

    // Handle .. (range operator)
    if (c == '.' && peek() == '.') {
      advance(); // consume second '.'
      tokens.push_back(makeToken("..", TokenType::DotDot));
      continue;
    }

    // Handle shell command prefix: $ command (must be before hotkey handling)
    // But NOT if followed by => (which would make it a hotkey like $Esc =>)
    if (c == '$') {
      // Check for capture mode: $!
      bool captureOutput = false;
      if (!isAtEnd() && peek() == '!') {
        advance();  // consume '!'
        captureOutput = true;
      }

      // Don't skip whitespace - let parser handle it
      // Just emit the token and let parser parse the expression

      tokens.push_back(scanShellCommand(captureOutput));
      if (debug_lexer) {
        std::cout << "LEX: " << tokens.back().toString() << std::endl;
      }
      continue;
    }

    // Handle modifier-based hotkeys starting with special characters like ^ + !
    // @ ~ $ This must happen before SINGLE_CHAR_TOKENS so '+' isn't tokenized
    // as Plus. EXCEPTION: + after expression context should be Plus operator
    if (c == '^' || c == '!' || c == '+' || c == '@' || c == '~' || c == '$') {
      // Special case for +, !, and ~: check context to distinguish operator from hotkey
      // Note: CloseBrace is NOT in expression context - after } we're at statement level
      if ((c == '+' || c == '!' || c == '~') && !tokens.empty()) {
        TokenType prevType = tokens.back().type;
        // If previous token suggests expression context, treat as operator
        // Exclude CloseBrace - after } we're at statement level (could be hotkey)
        // Include statement starters that are followed by expressions (if, while, for, etc.)
        if (prevType == TokenType::Number ||
            prevType == TokenType::Identifier ||
            prevType == TokenType::String ||
            prevType == TokenType::CloseParen ||
            prevType == TokenType::CloseBracket ||
            prevType == TokenType::Not ||
            prevType == TokenType::Or ||
            prevType == TokenType::And ||
            prevType == TokenType::Assign ||
            prevType == TokenType::If ||
            prevType == TokenType::While ||
            prevType == TokenType::For ||
            prevType == TokenType::In ||
            prevType == TokenType::Matches ||
            prevType == TokenType::Tilde) {
          // Fall through to SINGLE_CHAR_TOKENS to get Plus, Not, or Tilde
        } else {
          tokens.push_back(scanHotkey());
          continue;
        }
      } else {
        tokens.push_back(scanHotkey());
        continue;
      }
    }

    // Handle single character tokens
    auto singleCharIt = SINGLE_CHAR_TOKENS.find(c);
    if (singleCharIt != SINGLE_CHAR_TOKENS.end()) {
      tokens.push_back(makeToken(std::string(1, c), singleCharIt->second));
      continue;
    }

    // Handle identifiers and potential hotkeys
    if (isAlpha(c)) {
      // Check if this might be a hotkey starting with F (F1-F12)
      if (c == 'F' && isDigit(peek())) {
        // Could be F-key hotkey, but check if it's followed by assignment or
        // other non-hotkey syntax If next non-digit char is '=' or
        // whitespace+identifier, treat as identifier
        size_t lookahead = 1;
        while (position + lookahead < source.length() &&
               isDigit(source[position + lookahead])) {
          lookahead++;
        }
        // If followed by assignment or space, it's likely an identifier
        if (position + lookahead < source.length()) {
          char after = source[position + lookahead];
          if (after == '=' || after == ' ' || after == '\t' || after == ';' ||
              after == ',') {
            tokens.push_back(scanIdentifier());
            continue;
          }
        }
        tokens.push_back(scanHotkey());
      } else {
        tokens.push_back(scanIdentifier());
      }
      continue;
    }

    // Handle modifier-based hotkeys starting with special characters like # and
    // combo '&'
    if (c == '#' || c == '&') {
      tokens.push_back(scanHotkey());
      continue;
    }

    // Handle unrecognized characters
    const size_t error_col = column > 0 ? column - 1 : 1;
    std::ostringstream repr;
    const unsigned char uc = static_cast<unsigned char>(c);
    if (std::isprint(uc)) {
      repr << "'" << c << "'";
    } else {
      repr << "'\\x" << std::hex << std::setw(2) << std::setfill('0')
           << static_cast<int>(uc) << "'";
    }
    throw havel::LexError(line, error_col,
                          "Unexpected character " + repr.str(), 1);
  }

  // Add EOF token
  tokens.push_back(makeToken("EndOfFile", TokenType::EOF_TOKEN));

  return tokens;
}

void Lexer::printTokens(const std::vector<Token> &tokens) const {
  std::cout << "=== HAVEL TOKENS ===" << std::endl;
  for (size_t i = 0; i < tokens.size(); ++i) {
    std::cout << "[" << i << "] " << tokens[i].toString() << std::endl;
  }
  std::cout << "===================" << std::endl;
}

} // namespace havel
