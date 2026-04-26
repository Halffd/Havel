#include "Lexer.hpp"
#include "../utils/Logger.hpp"
#include <cctype>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace havel {

// Static member definitions
const std::unordered_map<std::string, TokenType> Lexer::KEYWORDS = {
    {"let", TokenType::Let},
    {"val", TokenType::Const}, // val - immutable binding
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
    {"thread", TokenType::Thread},
    {"interval", TokenType::Interval},
    {"timeout", TokenType::Timeout},
    {"yield", TokenType::Yield},
    {"go", TokenType::Go},
    {"sync", TokenType::Sync},
    {"async", TokenType::Async},
    {"channel", TokenType::Channel},
    {"del", TokenType::Del},
    {"config", TokenType::Config},
    {"devices", TokenType::Devices},
    {"modes", TokenType::Modes},
    {"signal", TokenType::Signal},
    {"group", TokenType::Group},
    {"struct", TokenType::Struct},
    {"class", TokenType::Class},
    {"enum", TokenType::Enum},
    {"trait", TokenType::Trait},
    {"prot", TokenType::Prot},
    {"impl", TokenType::Impl},
    {"this", TokenType::This},
    {"on", TokenType::On},
    {"off", TokenType::Off},
    {"when", TokenType::When},
    {"mode", TokenType::Mode},
    {"repeat", TokenType::Repeat},
    {"pool", TokenType::Pool},
    {"true", TokenType::True},
    {"false", TokenType::False},
    {"null", TokenType::Null},
    {"nil", TokenType::Null}, // nil as alias for null
    {"is", TokenType::Is}, // identity comparison
    {"send", TokenType::Identifier},
    {"clipboard", TokenType::Identifier}, // Built-in module
    {"text", TokenType::Identifier},      // Built-in module
    {"window", TokenType::Identifier},    // Built-in module
    {"import", TokenType::Import},
    // English-style logical operators (aliases for &&, ||, !)
    {"and", TokenType::And},
    {"or", TokenType::Or},
    {"not", TokenType::Not},
    {"matches", TokenType::Matches}, // regex match operator
    {"from", TokenType::From},
    {"where", TokenType::Where},
    {"select", TokenType::Select},
    {"as", TokenType::As},
    {"use", TokenType::Use},
  {"with", TokenType::With},
    {"dsl", TokenType::Dsl}};

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
    {'\n', TokenType::NewLine},    {'!', TokenType::Not},
    {'_', TokenType::Underscore},  {'~', TokenType::Tilde}};

Lexer::Lexer(const std::string &sourceCode, bool debug_lexer)
    : source(sourceCode), debug_lexer(debug_lexer) {}

std::string Lexer::getSourceLine(size_t lineNum) const {
  std::istringstream iss(source);
  std::string currentLine;
  for (size_t i = 1; i < lineNum && std::getline(iss, currentLine); i++) {
  }
  std::getline(iss, currentLine);
  return currentLine;
}

void Lexer::reportError(const std::string &message) {
  CompilerError err(ErrorSeverity::Error, line, column, message);
  err.sourceLine = getSourceLine(line);
  errors.push_back(err);

  // Also report to unified ErrorReporter
  errors::ErrorReporter::instance().errorAt(
      ::havel::errors::ErrorStage::Lexer, message, line, column);
}

void Lexer::reportWarning(const std::string &message) {
  CompilerError err(ErrorSeverity::Warning, line, column, message);
  err.sourceLine = getSourceLine(line);
  errors.push_back(err);

  // Also report to unified ErrorReporter
  errors::ErrorReporter::instance().warning(
      ::havel::errors::ErrorStage::Lexer, message);
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

bool Lexer::isAlpha(char c) const { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }

bool Lexer::isDigit(char c) const { return std::isdigit(static_cast<unsigned char>(c)); }

bool Lexer::isAlphaNumeric(char c) const { return isAlpha(c) || isDigit(c); }

bool Lexer::isHexDigit(char c) const {
  c = static_cast<char>(std::toupper(c));
  return std::isdigit(c) || (c >= 'A' && c <= 'F');
}

bool Lexer::isOctalDigit(char c) const {
  return c >= '0' && c <= '7';
}

bool Lexer::isBinaryDigit(char c) const {
  return c == '0' || c == '1';
}

bool Lexer::isSkippable(char c) const {
    return c == ' ' || c == '\t' || c == '\r';
}

bool Lexer::isHotkeyChar(char c) const {
    return isAlphaNumeric(c) || c == '+' || c == '-' || c == '^' || c == '!' ||
           c == '#' || c == '@' || c == '|' || c == '*' || c == '&' || c == ':' ||
           c == '~' || c == '$' || c == '=' || c == '.' || c == ',' || c == '/';
}

// UTF-8 decoding support
size_t Lexer::codepointLength(char firstByte) const {
    unsigned char c = static_cast<unsigned char>(firstByte);
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

uint32_t Lexer::decodeUTF8(size_t& pos) const {
    if (pos >= source.length()) return 0;

    unsigned char c = static_cast<unsigned char>(source[pos]);

    if (c < 0x80) {
        pos++;
        return c;
    }

    if ((c & 0xE0) == 0xC0) {
        if (pos + 1 >= source.length()) return 0xFFFD;
        unsigned char c2 = static_cast<unsigned char>(source[pos + 1]);
        if ((c2 & 0xC0) != 0x80) return 0xFFFD;
        pos += 2;
        return ((c & 0x1F) << 6) | (c2 & 0x3F);
    }

    if ((c & 0xF0) == 0xE0) {
        if (pos + 2 >= source.length()) return 0xFFFD;
        unsigned char c2 = static_cast<unsigned char>(source[pos + 1]);
        unsigned char c3 = static_cast<unsigned char>(source[pos + 2]);
        if ((c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80) return 0xFFFD;
        pos += 3;
        return ((c & 0x0F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
    }

    if ((c & 0xF8) == 0xF0) {
        if (pos + 3 >= source.length()) return 0xFFFD;
        unsigned char c2 = static_cast<unsigned char>(source[pos + 1]);
        unsigned char c3 = static_cast<unsigned char>(source[pos + 2]);
        unsigned char c4 = static_cast<unsigned char>(source[pos + 3]);
        if ((c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80 || (c4 & 0xC0) != 0x80) return 0xFFFD;
        pos += 4;
        return ((c & 0x07) << 18) | ((c2 & 0x3F) << 12) | ((c3 & 0x3F) << 6) | (c4 & 0x3F);
    }

    return 0xFFFD;
}

uint32_t Lexer::peekCodepoint() const {
    size_t tempPos = position;
    return decodeUTF8(tempPos);
}

void Lexer::advanceUTF8() {
    size_t start = position;
    uint32_t cp = decodeUTF8(position);
    if (cp == '\n') {
        line++;
        column = 1;
    } else {
        column++;
    }
}

bool Lexer::isUnicodeLetter(uint32_t cp) const {
    if (cp < 0x80) return std::isalpha(static_cast<unsigned char>(cp)) || cp == '_';

    // Latin-1 Supplement (U+00C0-U+00FF)
    if (cp >= 0x00C0 && cp <= 0x00D6) return true;
    if (cp >= 0x00D8 && cp <= 0x00F6) return true;
    if (cp >= 0x00F8 && cp <= 0x00FF) return true;

    // Latin Extended-A, B, etc.
    if (cp >= 0x0100 && cp <= 0x017F) return true;
    if (cp >= 0x0180 && cp <= 0x024F) return true;

    // Cyrillic (U+0400-U+04FF)
    if (cp >= 0x0400 && cp <= 0x04FF) return true;

    // Arabic (U+0600-U+06FF)
    if (cp >= 0x0600 && cp <= 0x06FF) return true;

    // Devanagari (U+0900-U+097F)
    if (cp >= 0x0900 && cp <= 0x097F) return true;

    // Hangul (U+AC00-U+D7AF)
    if (cp >= 0xAC00 && cp <= 0xD7AF) return true;

    // CJK (U+4E00-U+9FFF)
    if (cp >= 0x4E00 && cp <= 0x9FFF) return true;

    // Hiragana (U+3040-U+309F)
    if (cp >= 0x3040 && cp <= 0x309F) return true;

    // Katakana (U+30A0-U+30FF)
    if (cp >= 0x30A0 && cp <= 0x30FF) return true;

    return false;
}

bool Lexer::isUnicodeDigit(uint32_t cp) const {
    if (cp < 0x80) return std::isdigit(static_cast<unsigned char>(cp));

    // Arabic-Indic digits
    if (cp >= 0x0660 && cp <= 0x0669) return true;

    // Fullwidth digits
    if (cp >= 0xFF10 && cp <= 0xFF19) return true;

    return false;
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

// Progress guard: report error and skip one char if a lexer loop made no forward progress
void Lexer::assertProgress(size_t prevPos, const char* context) {
  if (position == prevPos && !isAtEnd()) {
    std::string msg = "lexer made no progress";
    if (context) msg += std::string(" at ") + context;
    msg += " line " + std::to_string(line) + " col " + std::to_string(column);
    if (position < source.size()) {
      msg += " char '";
      msg += source[position];
      msg += "'";
    }
    reportError(msg);
    advance(); // skip one char to prevent infinite loop
  }
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

    size_t commentStartPos = position;
    while (!isAtEnd()) {
      if (peek() == '*' && peek(1) == '/') {
        advance(); // *
        advance(); // /
        break;
      }
      advance();
      // Progress guard: prevent infinite loop on unterminated comments
      assertProgress(commentStartPos, "multiline comment");
    }
  }
}

Token Lexer::scanNumber() {
size_t start = position - 1;
std::string number;
number += source[start];

bool isNegative = (start > 0 && source[start - 1] == '-');
if (isNegative) {
    number = "-" + number;
    start--;
}

    if (peek() == 'x' || peek() == 'X') {
        number += advance();
        while (!isAtEnd() && isHexDigit(peek())) number += advance();
        return makeToken(number, TokenType::Number);
    }
    if (peek() == 'o' || peek() == 'O') {
        number += advance();
        while (!isAtEnd() && isOctalDigit(peek())) number += advance();
        return makeToken(number, TokenType::Number);
    }
    if (peek() == 'b' || peek() == 'B') {
        number += advance();
        while (!isAtEnd() && isBinaryDigit(peek())) number += advance();
        return makeToken(number, TokenType::Number);
    }
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

Token Lexer::scanString(bool isFString, bool isRegexString, char quote) {
    std::string value;
    std::string raw;
    bool hasInterpolation = isFString;

    int braceDepth = 0;

    size_t stringStartPos = position;
    size_t stringStartLine = line;
    size_t stringStartColumn = column;
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
    } else if (c == '$' && isFString && braceDepth == 0) {
      // $var interpolation in f-strings only
      hasInterpolation = true;
      value += advance(); // $

      if (isAlpha(peek()) || peek() == '_') {
        value += '{';
        while (!isAtEnd() && (isAlphaNumeric(peek()) || peek() == '_')) {
          value += advance();
        }
        if (!isAtEnd() && peek() == '?') {
          value += advance();
        }
        value += '}';
      } else if (peek() == '@') {
        value += '{';
        value += advance(); // @
        while (!isAtEnd() && (isAlphaNumeric(peek()) || peek() == '_')) {
          value += advance();
        }
        if (!isAtEnd() && peek() == '?') {
          value += advance();
        }
        value += '}';
      }
    } else if (c == '{' && isFString && braceDepth == 0) {
      // F-string interpolation: {...}
      // Check if this is a format specifier (has :) or just an expression
      // Simple heuristic: if next char is not {, it's an interpolation
      if (peek(1) != '{') {
        hasInterpolation = true;
        value += advance(); // {
        braceDepth++;       // Enter interpolation context
      } else {
        // Escaped brace {{ - consume both and add single {
        advance(); // first {
        advance(); // second {
        value += '{';
      }
    } else if (c == '}' && isFString && braceDepth == 0) {
      // Check for escaped brace }}
      if (peek(1) == '}') {
        advance(); // first }
        advance(); // second }
        value += '}';
      } else {
        // Single } in f-string without matching { - treat as literal
        value += advance();
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
    // Progress guard: ensure we always make forward progress in string scanning
    assertProgress(stringStartPos, "string literal");
  }

if (isAtEnd()) {
        size_t savedLine = line;
        size_t savedColumn = column;
        line = stringStartLine;
        column = stringStartColumn;
        reportError("Unterminated string");
        line = savedLine;
        column = savedColumn;
        std::string value = raw.substr(1);
        return makeToken(value, TokenType::String, raw);
    }

  // Consume closing quote
  advance();

  TokenType type;
  if (isRegexString) {
    type = TokenType::RegexString;
  } else if (hasInterpolation) {
    type = TokenType::InterpolatedString;
  } else {
    type = TokenType::String;
  }
  return makeToken(value, type, raw);
}

Token Lexer::scanMultilineString(bool isFString, char quote) {
    std::string value;
    std::string raw;
    bool hasInterpolation = isFString;
    int braceDepth = 0;

    size_t stringStartLine = line;
    size_t stringStartColumn = column;

  // Skip opening quotes (already consumed by caller)
  // Multiline strings support interpolation like regular double-quoted strings
  // f-strings always support interpolation

  // Skip initial newline if present (for """\n... style)
  if (!isAtEnd() && peek() == '\n') {
    advance();
    raw += '\n';
  }

  while (!isAtEnd()) {
    // Check for closing triple-quote
    if (peek() == quote && position + 2 < source.length() &&
        source[position + 1] == quote && source[position + 2] == quote) {
      break;
    }

    char c = peek();
    raw += c;

    if (braceDepth == 0 && c == '\\' && !isAtEnd()) {
      // Process escape sequences
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
    } else if (c == '$' && isFString && braceDepth == 0) {
      // $var interpolation in f-strings only
      hasInterpolation = true;
      value += advance(); // $

      if (peek() == '{') {
        value += advance(); // {
        braceDepth++;
      } else if (isAlpha(peek()) || peek() == '_') {
        value += '{';
        while (!isAtEnd() && (isAlphaNumeric(peek()) || peek() == '_')) {
          value += advance();
        }
        if (!isAtEnd() && peek() == '?') {
          value += advance();
        }
        value += '}';
      } else if (peek() == '@') {
        value += '{';
        value += advance(); // @
        while (!isAtEnd() && (isAlphaNumeric(peek()) || peek() == '_')) {
          value += advance();
        }
        value += '}';
      }
    } else if (c == '{' && isFString && braceDepth == 0) {
      // F-string interpolation: {...}
      if (peek(1) != '{') {
        hasInterpolation = true;
        value += advance(); // {
        braceDepth++;
      } else {
        // Escaped brace {{
        advance();
        advance();
        value += '{';
      }
    } else if (c == '}' && isFString && braceDepth == 0) {
      // Check for escaped brace }}
      if (peek(1) == '}') {
        advance();
        advance();
        value += '}';
      } else {
        value += advance();
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
        size_t savedLine = line;
        size_t savedColumn = column;
        line = stringStartLine;
        column = stringStartColumn;
        reportError("Unterminated multiline string");
        line = savedLine;
        column = savedColumn;
        return makeToken(value, TokenType::MultilineString, raw);
    }

  // Consume closing triple-quote
  advance();
  advance();
  advance();

  TokenType type = hasInterpolation ? TokenType::InterpolatedString
                                    : TokenType::MultilineString;
  return makeToken(value, type, raw);
}

Token Lexer::scanBacktick(bool isMultiline) {
  std::string value;
  std::string raw;
  bool hasInterpolation = isMultiline;

  int braceDepth = 0;
  size_t stringStartLine = line;
  size_t stringStartColumn = column;

  if (isMultiline) {
    if (!isAtEnd() && peek() == '\n') {
      advance();
      raw += '\n';
    }
  }

  while (!isAtEnd()) {
    if (isMultiline) {
      if (peek() == '`' && position + 2 < source.length() &&
          source[position + 1] == '`' && source[position + 2] == '`') {
        break;
      }
    } else if (peek() == '`') {
      break;
    }

    char c = peek();
    raw += c;

    if (braceDepth == 0 && c == '\\' && !isAtEnd()) {
      advance();
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
      case '`':
        value += '`';
        break;
      default:
        value += '\\';
        value += escaped;
        break;
      }
    } else if (c == '{' && isMultiline && braceDepth == 0) {
      if (peek(1) != '{') {
        hasInterpolation = true;
        value += advance();
        braceDepth++;
      } else {
        advance();
        advance();
        value += '{';
      }
    } else if (c == '}' && isMultiline && braceDepth == 0) {
      if (peek(1) == '}') {
        advance();
        advance();
        value += '}';
      } else {
        value += advance();
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
    size_t savedLine = line;
    size_t savedColumn = column;
    line = stringStartLine;
    column = stringStartColumn;
    reportError("Unterminated backtick string");
    line = savedLine;
    column = savedColumn;
    return makeToken(value, TokenType::Backtick, raw);
  }

  if (isMultiline) {
    advance();
    advance();
    advance();
  } else {
    advance();
  }

  TokenType type = hasInterpolation ? TokenType::InterpolatedBacktick
                                         : TokenType::Backtick;
  return makeToken(value, type, raw);
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
  TokenType type =
      captureOutput ? TokenType::ShellCommandCapture : TokenType::ShellCommand;
  return makeToken(captureOutput ? "$!" : "$", type,
                   captureOutput ? "$!" : "$");
}

Token Lexer::scanIdentifier() {
    std::string identifier;

    char first = source[position - 1];
    identifier += first;

    while (!isAtEnd()) {
        unsigned char c = static_cast<unsigned char>(peek());
        if (c < 0x80) {
            if (!isAlphaNumeric(c) && c != '_') break;
            identifier += advance();
        } else {
            uint32_t cp = peekCodepoint();
            if (!isUnicodeLetter(cp) && !isUnicodeDigit(cp)) break;
            size_t len = codepointLength(peek());
            for (size_t i = 0; i < len && !isAtEnd(); i++) {
                identifier += advance();
            }
        }
    }

    if (!isAtEnd() && peek() == '?' && peek(1) != '.') {
        identifier += advance();
    }

    auto keywordIt = KEYWORDS.find(identifier);
    TokenType type =
        (keywordIt != KEYWORDS.end()) ? keywordIt->second : TokenType::Identifier;

    return makeToken(identifier, type);
}

Token Lexer::scanHotkey() {
  std::string hotkey;
  size_t safetyPos = position;

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
        safetyPos = position;
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
    safetyPos = position;
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
  size_t tokenCount = 0;

  while (!isAtEnd()) {
    size_t loopStartPos = position;
    skipWhitespace();

    if (isAtEnd())
      break;

    if (++tokenCount > 5'000'000) {
      reportError("token limit exceeded (possible infinite loop)");
      break;
    }

    char c = advance();

    // Handle comments BEFORE other tokens (especially '/' and '#')
    if (c == '/' && (peek() == '/' || peek() == '*')) {
      skipComment();
      continue;
    }

    // Handle # as length operator or hotkey modifier
    if (c == '#') {
      // Determine if we're in expression context or statement context
      bool inExpressionContext = false;
      if (!tokens.empty()) {
        TokenType prevType = tokens.back().type;
        inExpressionContext = (prevType == TokenType::Number ||
          prevType == TokenType::String || prevType == TokenType::RegexString ||
          prevType == TokenType::Identifier ||
          prevType == TokenType::True || prevType == TokenType::False ||
          prevType == TokenType::Null ||
          prevType == TokenType::CloseParen ||
          prevType == TokenType::CloseBracket ||
          prevType == TokenType::CloseBrace ||
          prevType == TokenType::Not ||
          prevType == TokenType::Or || prevType == TokenType::And ||
          prevType == TokenType::Assign ||
          prevType == TokenType::If || prevType == TokenType::While ||
          prevType == TokenType::For || prevType == TokenType::In ||
          prevType == TokenType::Matches ||
          prevType == TokenType::Tilde ||
          prevType == TokenType::Comma ||
          prevType == TokenType::Dot ||
          prevType == TokenType::Arrow || prevType == TokenType::ReturnType ||
          prevType == TokenType::Plus || prevType == TokenType::Minus ||
          prevType == TokenType::Multiply || prevType == TokenType::Divide ||
          prevType == TokenType::Modulo || prevType == TokenType::Power ||
          prevType == TokenType::Backslash ||
          prevType == TokenType::Equals || prevType == TokenType::NotEquals ||
          prevType == TokenType::Less || prevType == TokenType::Greater ||
          prevType == TokenType::LessEquals || prevType == TokenType::GreaterEquals ||
          prevType == TokenType::PlusAssign || prevType == TokenType::MinusAssign ||
          prevType == TokenType::MultiplyAssign || prevType == TokenType::DivideAssign ||
          prevType == TokenType::Question || prevType == TokenType::Colon ||
          prevType == TokenType::ColonColon ||
          prevType == TokenType::PlusPlus || prevType == TokenType::MinusMinus ||
prevType == TokenType::Not ||
        prevType == TokenType::Nullish || prevType == TokenType::Pipe ||
        prevType == TokenType::Matches || prevType == TokenType::Tilde ||
        prevType == TokenType::DoubleCloseParen ||
        prevType == TokenType::BitwiseOr ||
        prevType == TokenType::BitwiseAnd ||
        prevType == TokenType::BitwiseXor ||
        prevType == TokenType::ShiftLeft ||
        prevType == TokenType::Return);
      }

      // If followed by identifier, '(', '[', string, or number
      if (isAlpha(peek()) || peek() == '(' || peek() == '[' || peek() == '"' || peek() == '\'' || isDigit(peek())) {
        // In expression context, treat as length operator
        // In statement context, treat as hotkey modifier
        if (inExpressionContext) {
          tokens.push_back(makeToken("#", TokenType::Length));
          if (debug_lexer) {
                havel::debug("LEX: {}", tokens.back().toString());
          }
          continue;
        }
        // Otherwise, '#' starts a modifier hotkey (e.g. "#f1", "#!Esc")
        tokens.push_back(scanHotkey());
        if (debug_lexer) {
            havel::debug("LEX: {}", tokens.back().toString());
        }
        continue;
      }

      // If not followed by identifier-starting char, scan as hotkey
      tokens.push_back(scanHotkey());
      if (debug_lexer) {
        havel::debug("LEX: {}", tokens.back().toString());
      }
      continue;
    }

    // Handle numbers (including negative numbers in certain contexts)
    if (isDigit(c) || (c == '-' && isDigit(peek()))) {
      tokens.push_back(scanNumber());
      if (debug_lexer) {
        havel::debug("LEX: {}", tokens.back().toString());
      }
continue;
    }

    // Handle strings - both single and double quotes work the same
    if (c == '"' || c == '\'') {
        char quote = c;
        bool isFString = true;
        bool isRegexString = false;
        if (!tokens.empty() && tokens.back().type == TokenType::Identifier) {
            if (tokens.back().value == "f" || tokens.back().value == "F") {
                tokens.pop_back();
            } else if (tokens.back().value == "u" || tokens.back().value == "U") {
                isFString = false;
                tokens.pop_back();
            } else if (tokens.back().value == "r" || tokens.back().value == "R") {
                isRegexString = true;
                isFString = false;
                tokens.pop_back();
            }
        }

        // Check for multiline string """ or '''
        if (position + 2 < source.length() &&
            source[position] == quote && source[position + 1] == quote) {
            advance();
          
                isFString = true;  advance();
            tokens.push_back(scanMultilineString(isFString, quote));
        } else {
            tokens.push_back(scanString(isFString, isRegexString, quote));
        }
        if (debug_lexer) {
                havel::debug("LEX: {}", tokens.back().toString());
        }
        continue;
    }

    // Handle backtick expressions: `command` or ```...``` for shell commandd
    if (c == '`') {
        bool isMultilineBacktick = false;
        // Check for multiline backtick ```
        if (position + 2 < source.length() &&
            source[position] == '`' && source[position + 1] == '`') {
            advance();
            advance();
            isMultilineBacktick = true;
        }
        tokens.push_back(scanBacktick(isMultilineBacktick));
        if (debug_lexer) {
                havel::debug("LEX: {}", tokens.back().toString());
        }
        continue;
    }

    // Handle regex literals: /pattern/
    // Only if not followed by '/' (which would be // comment) or '*' (/*
    // comment) and not preceded by something that would make it division
    if (c == '/' && peek() != '/' && peek() != '*' && peek() != '=') {
      // Check if this is a hotkey: / followed by => (with optional whitespace)
      // e.g. "/ => { }" for the slash key
      size_t la = position + 1;
      while (la < source.length() && source[la] == ' ') la++;
      if (la + 1 < source.length() && source[la] == '=' && source[la + 1] == '>') {
        tokens.push_back(makeToken("/", TokenType::Hotkey));
        advance(); // consume '/'
        if (debug_lexer) {
            havel::debug("LEX: {}", tokens.back().toString());
        }
        continue;
      }

      // Check if this is a conditional hotkey: / identifier ... =>
      // e.g. "/ if mode == "genshin" => { }" for slash key with condition
      // Peek ahead: skip whitespace, check if identifier starts, then look for =>
      bool isSlashHotkey = false;
      size_t hotkeyEnd = 0;
      bool identifierIsKeyword = false;
      if (la < source.length() && (isAlpha(source[la]) || source[la] == '_')) {
        size_t la2 = la;
        size_t idStart = la;
        while (la2 < source.length() && (isAlphaNumeric(source[la2]) || source[la2] == '_')) la2++;
        // Check if the identifier is a keyword that starts a condition (if/when)
        std::string ident = source.substr(idStart, la2 - idStart);
        if (ident == "if" || ident == "when") {
          identifierIsKeyword = true;
        }
        // Search for => within a reasonable window
        size_t searchEnd = std::min(la2 + 200, source.length());
        bool foundArrow = false;
        for (size_t s = la2; s + 1 < searchEnd; s++) {
          if (source[s] == '=' && source[s + 1] == '>') { foundArrow = true; break; }
          if (source[s] == '\n') break;
        }
        if (foundArrow) {
          isSlashHotkey = true;
          hotkeyEnd = la2;
        }
      }
      if (isSlashHotkey) {
        if (identifierIsKeyword) {
          // / followed by condition keyword — only consume /
          tokens.push_back(makeToken("/", TokenType::Hotkey));
          advance(); // consume '/'
        } else {
          // /identifier — consume the whole thing as one token
          std::string hotkeyValue = source.substr(position, hotkeyEnd - position);
          advance(); // consume '/'
          while (position < source.length() && position < hotkeyEnd) {
            advance();
          }
          tokens.push_back(makeToken(hotkeyValue, TokenType::Hotkey));
        }
        if (debug_lexer) {
            havel::debug("LEX: {}", tokens.back().toString());
        }
        continue;
      }

      // Check if this looks like a regex (not division)
      // Simple heuristic: if previous non-whitespace token suggests expression
      // context
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
            havel::debug("LEX: {}", tokens.back().toString());
        }
        continue;
      }
    }

    // Handle return type arrow ->
    if (c == '-' && peek() == '>') {
      advance(); // consume '>'
      tokens.push_back(makeToken("->", TokenType::ReturnType));
      if (debug_lexer) {
        havel::debug("LEX: {}", tokens.back().toString());
      }
      continue;
    }

    // Handle arrow operator =>
    if (c == '=' && peek() == '>') {
      advance(); // consume '>'
      tokens.push_back(makeToken("=>", TokenType::Arrow));
      continue;
    }

    // Handle hotkey block trigger ::
    if (c == ':' && peek() == ':') {
      advance(); // consume second ':'
      tokens.push_back(makeToken("::", TokenType::ColonColon));
      if (debug_lexer) {
        havel::debug("LEX: {}", tokens.back().toString());
      }
      continue;
    }

    // Handle ++ and --
    if (c == '+' && peek() == '+') {
      advance();
      tokens.push_back(makeToken("++", TokenType::PlusPlus));
      if (debug_lexer) {
        havel::debug("LEX: {}", tokens.back().toString());
      }
      continue;
    }
    if (c == '-' && peek() == '-') {
      advance();
      tokens.push_back(makeToken("--", TokenType::MinusMinus));
      if (debug_lexer) {
        havel::debug("LEX: {}", tokens.back().toString());
      }
      continue;
    }

    // Handle compound assignments first: +=, -=, *=, /=
    if (c == '+' && peek() == '=') {
      advance();
      tokens.push_back(makeToken("+=", TokenType::PlusAssign));
      if (debug_lexer) {
        havel::debug("LEX: {}", tokens.back().toString());
      }
      continue;
    }
    if (c == '-' && peek() == '=') {
      advance();
      tokens.push_back(makeToken("-=", TokenType::MinusAssign));
      if (debug_lexer) {
        havel::debug("LEX: {}", tokens.back().toString());
      }
      continue;
    }
    if (c == '*' && peek() == '=') {
      advance();
      tokens.push_back(makeToken("*=", TokenType::MultiplyAssign));
      if (debug_lexer) {
        havel::debug("LEX: {}", tokens.back().toString());
      }
      continue;
    }
    if (c == '/' && peek() == '=') {
      advance();
      tokens.push_back(makeToken("/=", TokenType::DivideAssign));
      if (debug_lexer) {
        havel::debug("LEX: {}", tokens.back().toString());
      }
      continue;
    }
    if (c == '%' && peek() == '=') {
      advance();
      tokens.push_back(makeToken("%=", TokenType::ModuloAssign));
      if (debug_lexer) {
        havel::debug("LEX: {}", tokens.back().toString());
      }
      continue;
    }
    if (c == '*' && peek() == '*') {
      // Check for **= (power assign) or ** (power)
      size_t lookAhead = position + 1;
      if (lookAhead < source.length() && source[lookAhead] == '=') {
        advance(); // consume first *
        advance(); // consume second *
        advance(); // consume =
        tokens.push_back(makeToken("**=", TokenType::PowerAssign));
        if (debug_lexer) {
            havel::debug("LEX: {}", tokens.back().toString());
        }
        continue;
      } else {
        advance(); // consume first *
        advance(); // consume second *
        tokens.push_back(makeToken("**", TokenType::Power));
        if (debug_lexer) {
            havel::debug("LEX: {}", tokens.back().toString());
        }
        continue;
      }
    }

    // Handle ?? (nullish coalescing)
    if (c == '?' && peek() == '?') {
      advance();
      tokens.push_back(makeToken("??", TokenType::Nullish));
      if (debug_lexer) {
        havel::debug("LEX: {}", tokens.back().toString());
      }
      continue;
    }

    // Handle ?. (optional chaining)
    if (c == '?' && peek() == '.') {
      advance();
      tokens.push_back(makeToken("?.", TokenType::QuestionDot));
      if (debug_lexer) {
        havel::debug("LEX: {}", tokens.back().toString());
      }
      continue;
    }

    // Handle == and !=
    if (c == '=' && peek() == '=') {
      advance();
      tokens.push_back(makeToken("==", TokenType::Equals));
      if (debug_lexer) {
        havel::debug("LEX: {}", tokens.back().toString());
      }
      continue;
    }
    if (c == '!' && peek() == '=') {
      advance();
      tokens.push_back(makeToken("!=", TokenType::NotEquals));
      if (debug_lexer) {
        havel::debug("LEX: {}", tokens.back().toString());
      }
      continue;
    }

        // Handle (( )) bitwise expression delimiters
        if (c == '(' && peek() == '(' && !inBitwiseExpr) {
        // If previous token suggests function call context, emit two separate OpenParens
        // e.g. print((expr)) should be: print ( ( expr ) )
        if (!tokens.empty()) {
            TokenType prevType = tokens.back().type;
            if (prevType == TokenType::Identifier ||
                prevType == TokenType::CloseParen ||
                prevType == TokenType::CloseBracket ||
                prevType == TokenType::String ||
                prevType == TokenType::Number) {
                tokens.push_back(makeToken("(", TokenType::OpenParen));
                advance(); // consume second '('
                tokens.push_back(makeToken("(", TokenType::OpenParen));
                continue;
            }
        }
        // Look ahead for lambda indicators (comma or =>) to avoid misidentifying
        // Higher-Order Function arguments like ob.map((v, k) => ...) as bitwise blocks.
        size_t look = position + 1; // Start scanning AFTER the initial '(('
        int parenDepth = 2;         // Already at depth 2 from '(('
        bool looksLikeLambdaFlag = false;
        
        while (look < source.length() && parenDepth > 0) {
            char lc = source[look];
            if (lc == '(') {
                parenDepth++;
            } else if (lc == ')') {
                if (parenDepth == 2) {
                    // Check if followed by =>
                    size_t next = look + 1;
                    while (next < source.length() && (source[next] == ' ' || source[next] == '\t' || source[next] == '\n')) {
                        next++;
                    }
                    if (next + 1 < source.length() && source[next] == '=' && source[next + 1] == '>') {
                        looksLikeLambdaFlag = true;
                        break;
                    }
                }
                parenDepth--;
            } else if (lc == ',' && parenDepth == 2) {
                // Comma at the second level indicates parameter list (v, k)
                looksLikeLambdaFlag = true;
                break;
            } else if (lc == '\n') {
                // Heuristic: lambdas usually don't have newlines in param lists 
                // but bitwise blocks might. If we see a newline before we find 
                // lambda markers, it's likely NOT a simple lambda param list.
                break;
            }
            look++;
        }

        if (looksLikeLambdaFlag) {
            // Treat as regular nested parentheses, not a bitwise block start.
            // Emit both '(' as separate tokens to ensure the parser sees them correctly.
            if (debug_lexer) {
                havel::debug("LEX: Lambda heuristic triggered. Emitting two OpenParens.");
            }
            tokens.push_back(makeToken("(", TokenType::OpenParen));
            advance(); // consume second '('
            tokens.push_back(makeToken("(", TokenType::OpenParen));
            continue;
        }

        advance(); // consume second '('
        inBitwiseExpr = true;
        tokens.push_back(makeToken("((", TokenType::DoubleOpenParen));
        if (debug_lexer) {
            havel::debug("LEX: Bitwise block detected at pos {}. Emitting DoubleOpenParen.", position - 2);
            havel::debug("LEX: {}", tokens.back().toString());
        }
        continue;
    }
    if (c == ')' && peek() == ')' && inBitwiseExpr) {
        advance(); // consume second ')'
        inBitwiseExpr = false;
	tokens.push_back(makeToken("))", TokenType::DoubleCloseParen));
	if (debug_lexer) {
            havel::debug("LEX: {}", tokens.back().toString());
        }
    continue;
  }

  // Handle && and ||
    if (c == '&' && peek() == '&') {
      advance();
      tokens.push_back(makeToken("&&", TokenType::And));
      if (debug_lexer) {
        havel::debug("LEX: {}", tokens.back().toString());
      }
      continue;
    }
      if (c == '|' && peek() == '|') {
        advance();
        tokens.push_back(makeToken("||", TokenType::Or));
        if (debug_lexer) {
          havel::debug("LEX: {}", tokens.back().toString());
        }
        continue;
      }
      // |> pipeline operator
      if (c == '|' && peek() == '>') {
        advance();
        tokens.push_back(makeToken("|>", TokenType::PipeRight));
        if (debug_lexer) {
          havel::debug("LEX: {}", tokens.back().toString());
        }
        continue;
      }
      // Inside (( )), single | is bitwise OR, not pipeline
      if (c == '|' && inBitwiseExpr) {
	tokens.push_back(makeToken("|", TokenType::BitwiseOr));
	if (debug_lexer) {
      havel::debug("LEX: {}", tokens.back().toString());
    }
    continue;
  }

  // Handle <= and >=
  if (c == '<' && peek() == '=') {
    advance();
    tokens.push_back(makeToken("<=", TokenType::LessEquals));
    if (debug_lexer) {
      havel::debug("LEX: {}", tokens.back().toString());
    }
    continue;
  }
  if (c == '>' && peek() == '=') {
    advance();
    tokens.push_back(makeToken(">=", TokenType::GreaterEquals));
    if (debug_lexer) {
      havel::debug("LEX: {}", tokens.back().toString());
    }
    continue;
  }

  // Handle << (bitwise left shift) - must check before single <
  if (c == '<' && peek() == '<') {
    advance(); // consume second '<'
    tokens.push_back(makeToken("<<", TokenType::ShiftLeft));
    if (debug_lexer) {
      havel::debug("LEX: {}", tokens.back().toString());
    }
    continue;
  }

  // Handle >> (config append/get or bitwise right shift) - must check before single >
    if (c == '>' && peek() == '>') {
      advance(); // consume second '>'
      tokens.push_back(makeToken(">>", TokenType::ShiftRight));
      if (debug_lexer) {
        havel::debug("LEX: {}", tokens.back().toString());
      }
      continue;
    }

    // Handle single < and >
    if (c == '<') {
      tokens.push_back(makeToken("<", TokenType::Less));
      if (debug_lexer) {
        havel::debug("LEX: {}", tokens.back().toString());
      }
      continue;
    }
    if (c == '>') {
      tokens.push_back(makeToken(">", TokenType::Greater));
      if (debug_lexer) {
        havel::debug("LEX: {}", tokens.back().toString());
      }
      continue;
    }

    // Handle single equals (assignment)
    if (c == '=') {
      tokens.push_back(makeToken("=", TokenType::Assign));
      if (debug_lexer) {
        havel::debug("LEX: {}", tokens.back().toString());
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

    // Handle ..= (inclusive range pattern) - check before ..
    if (c == '.' && peek() == '.' && peek(1) == '=') {
      advance(); // consume second '.'
      advance(); // consume '='
      tokens.push_back(makeToken("..=", TokenType::DotDotEquals));
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
        advance(); // consume '!'
        captureOutput = true;
      }

      // Don't skip whitespace - let parser handle it
      // Just emit the token and let parser parse the expression

      tokens.push_back(scanShellCommand(captureOutput));
      if (debug_lexer) {
        havel::debug("LEX: {}", tokens.back().toString());
      }
      continue;
    }

    // Handle @-> (super call operator) - must be before hotkey handling
    if (c == '@' && peek() == '-' && peek(1) == '>') {
      advance(); // consume '-'
      advance(); // consume '>'
      tokens.push_back(makeToken("@->", TokenType::SuperArrow));
      if (debug_lexer) {
        havel::debug("LEX: {}", tokens.back().toString());
      }
      continue;
    }

    // Handle @@ (class member marker) - must be before single @ handling
    if (c == '@' && peek() == '@') {
      advance(); // consume second '@'
      tokens.push_back(makeToken("@@", TokenType::AtAt));
      if (debug_lexer) {
        havel::debug("LEX: {}", tokens.back().toString());
      }
      continue;
    }

    // Handle @ (at/this field access) - must be before hotkey handling
    // Peek ahead to decide: @identifier => is hotkey, everything else is field access
    if (c == '@' && (isAlpha(peek()) || peek() == '_')) {
      // Look ahead past the identifier
      size_t look = position;
      while (look < source.size() && (isAlphaNumeric(source[look]) || source[look] == '_')) {
        look++;
      }
      // Skip whitespace
      while (look < source.size() && (source[look] == ' ' || source[look] == '\t')) {
        look++;
      }
      // Check what follows: => is hotkey, & is compound hotkey, : is hotkey with timing
      // Everything else (=, ., (, \n, etc) is field access
      bool isHotkey = false;
      if (look + 1 < source.size() && source[look] == '=' && source[look + 1] == '>') {
        isHotkey = true; // @identifier =>
      } else if (look < source.size() && source[look] == '&') {
        isHotkey = true; // @identifier & ... (compound hotkey)
      } else if (look < source.size() && source[look] == ':') {
        isHotkey = true; // @identifier:timing
      }
      if (isHotkey) {
        // Fall through to scanHotkey
      } else {
        // @identifier with anything else ( = / . / ( / \n / etc ) - field access
        tokens.push_back(makeToken("@", TokenType::At));
        if (debug_lexer) {
            havel::debug("LEX: {}", tokens.back().toString());
        }
        continue;
      }
    }

  // Handle modifier-based hotkeys starting with special characters like # and
  // combo '&' — but inside (( )), & is bitwise AND
  if (c == '&') {
    if (inBitwiseExpr) {
      tokens.push_back(makeToken("&", TokenType::BitwiseAnd));
      if (debug_lexer) {
        havel::debug("LEX: {}", tokens.back().toString());
      }
      continue;
    }
    tokens.push_back(scanHotkey());
    continue;
  }
  if (c == '#') {
    tokens.push_back(scanHotkey());
    continue;
  }

  // Handle modifier-based hotkeys starting with special characters like ^ + !
  // @ ~ $ — but inside (( )), ^ is bitwise XOR and ~ is bitwise NOT
  if (c == '^' && inBitwiseExpr) {
    tokens.push_back(makeToken("^", TokenType::BitwiseXor));
    if (debug_lexer) {
      havel::debug("LEX: {}", tokens.back().toString());
    }
    continue;
  }
  if (c == '^' || c == '!' || c == '+' || c == '@' || c == '~' || c == '$') {
      // Special case: !{ for unsorted object literals - emit ! then { separately
      if (c == '!' && peek() == '{') {
        tokens.push_back(makeToken("!", TokenType::Not));
        if (debug_lexer) {
            havel::debug("LEX: {}", tokens.back().toString());
        }
        // Don't consume '{' - let it be handled normally
        continue;
      }
        // Special case for +, !, and ~: check context to distinguish operator
        // from hotkey Note: CloseBrace is NOT in expression context - after }
        // we're at statement level
        // Inside bitwise (( )) blocks, ~ is always bitwise NOT
        if (c == '~' && inBitwiseExpr) {
            // Fall through to SINGLE_CHAR_TOKENS for Tilde
        } else if ((c == '+' || c == '!' || c == '~') && !tokens.empty()) {
        TokenType prevType = tokens.back().type;
        // If previous token suggests expression context, treat as operator
        // Exclude CloseBrace - after } we're at statement level (could be
        // hotkey) Include statement starters that are followed by expressions
        // (if, while, for, etc.)
        if (prevType == TokenType::Number ||
            prevType == TokenType::Identifier ||
            prevType == TokenType::String ||
            prevType == TokenType::CloseParen ||
            prevType == TokenType::OpenParen ||
            prevType == TokenType::CloseBracket ||
            prevType == TokenType::Not ||
            prevType == TokenType::Or || prevType == TokenType::And ||
            prevType == TokenType::Assign || prevType == TokenType::If ||
            prevType == TokenType::While || prevType == TokenType::For ||
            prevType == TokenType::In || prevType == TokenType::Matches ||
            prevType == TokenType::Tilde || prevType == TokenType::Comma) {
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
        if (debug_lexer) {
                havel::debug("[LEXER] Looking up char '{}' in SINGLE_CHAR_TOKENS, found={}",
                             c, (singleCharIt != SINGLE_CHAR_TOKENS.end()));
                if (singleCharIt != SINGLE_CHAR_TOKENS.end()) {
                    havel::debug("[LEXER] Mapped to type={}", static_cast<int>(singleCharIt->second));
            }
        }
        if (singleCharIt != SINGLE_CHAR_TOKENS.end()) {
            // Special case: '_' followed by alphanumeric is an identifier (_G, _foo, etc.)
            // Not a standalone underscore wildcard token
            if (c == '_' && !isAtEnd() && (isAlphaNumeric(peek()) || peek() == '_')) {
                // Fall through to identifier scanning below
            } else {
                tokens.push_back(makeToken(std::string(1, c), singleCharIt->second));
                continue;
            }
        }

        // Handle UTF-8 Unicode characters
        unsigned char utfByte = static_cast<unsigned char>(c);
        if (utfByte >= 0x80) {
            size_t len = codepointLength(c);
            for (size_t i = 1; i < len && !isAtEnd(); i++) {
                advance();
            }
            continue;
        }

        // Handle identifiers and potential hotkeys
        if (isAlpha(c)) {
      // Check if this might be a hotkey starting with F (F1..F12)
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

    // Progress guard: ensure we always make forward progress
    assertProgress(loopStartPos, "tokenize");

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
    throw havel::LexError(line, error_col, "Unexpected character " + repr.str(),
                          1);
  }

  // Add EOF token
  tokens.push_back(makeToken("EndOfFile", TokenType::EOF_TOKEN));

  return tokens;
}

void Lexer::printTokens(const std::vector<Token> &tokens) const {
    havel::debug("=== HAVEL TOKENS ===");
    for (size_t i = 0; i < tokens.size(); ++i) {
        havel::debug("[{}] {}", i, tokens[i].toString());
    }
    havel::debug("===================");
}

} // namespace havel
