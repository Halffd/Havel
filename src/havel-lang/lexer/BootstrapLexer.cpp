#include "BootstrapLexer.hpp"
#include "core/io/KeyMap.hpp"
#include "../../utils/Logger.hpp"
#include <cctype>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <unordered_set>

namespace havel {

// Static member definitions
const std::unordered_map<std::string, TokenType> Lexer::KEYWORDS = {
    {"let", TokenType::Let},
    {"val", TokenType::Val},
    {"const", TokenType::Const},
    {"import", TokenType::Import},
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
    {"update", TokenType::Update},
    {"timeout", TokenType::Timeout},
    {"yield", TokenType::Yield},
    {"go", TokenType::Go},
    {"sync", TokenType::Sync},
    {"async", TokenType::Async},
  {"channel", TokenType::Channel},
  {"waitgroup", TokenType::WaitGroup},
  {"wait", TokenType::Wait},
  {"defer", TokenType::Defer},
  {"co", TokenType::Co},
    {"del", TokenType::Del},
    {"config", TokenType::Config},
    {"devices", TokenType::Devices},
 {"modes", TokenType::Modes},
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
  if (isAtEnd()) {
    return '\0';
  }
  char current = source[position++];
  if (current == '\n') {
        line++;
        column = 1;
    } else if (current == '\t') {
        column = ((column + 7) / 8) * 8 + 1;
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
           c == '~' || c == '$' || c == '.' || c == ',' || c == '/';
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
    size_t commentStart = position;
    size_t commentStartCol = column;
    while (!isAtEnd() && peek() != '\n') {
      advance();
    }
    // Check for #unsafe marker
    std::string comment = source.substr(commentStart, position - commentStart);
    // Trim leading/trailing whitespace for marker matching
    size_t start = comment.find_first_not_of(" \t");
    size_t end = comment.find_last_not_of(" \t");
    std::string trimmedComment = (start != std::string::npos) ? comment.substr(start, end - start + 1) : "";
    if (trimmedComment == "#unsafe") {
      // Create an UnsafeMarker token
      Token token("#unsafe", TokenType::UnsafeMarker, "#unsafe", line, commentStartCol, 7);
      currentTokens.push_back(std::move(token));
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

    // Scan exponent part (e.g. 6.67430e-11, 1.5E+3)
    if (!isAtEnd() && (peek() == 'e' || peek() == 'E')) {
        number += advance(); // consume 'e' or 'E'
        if (!isAtEnd() && (peek() == '+' || peek() == '-')) {
            number += advance(); // consume sign
        }
        while (!isAtEnd() && isDigit(peek())) {
            number += advance();
        }
    }

    return makeToken(number, TokenType::Number);
}

std::string Lexer::processEscapeSequence(bool isFString, bool &suppressInterpolation) {
    suppressInterpolation = false;
    if (isAtEnd()) return "\\";

    char escaped = peek();
    switch (escaped) {
    case 'n': advance(); return "\n";
    case 't': advance(); return "\t";
    case 'r': advance(); return "\r";
    case '\\': advance(); return "\\";
    case '"': advance(); return "\"";
    case '\'': advance(); return "'";
    case '0': advance(); return std::string(1, '\0');
    case '$': advance(); suppressInterpolation = true; return "$";
    case '{': advance(); suppressInterpolation = true; return "{";
    case '}': advance(); suppressInterpolation = true; return "}";
    case 'x': {
        advance(); // consume 'x'
        if (!isAtEnd() && isHexDigit(peek())) {
            char h = advance();
            if (!isAtEnd() && isHexDigit(peek())) {
                char l = advance();
                int val = 0;
                if (h >= '0' && h <= '9') val = h - '0';
                else if (h >= 'a' && h <= 'f') val = 10 + h - 'a';
                else if (h >= 'A' && h <= 'F') val = 10 + h - 'A';
                if (l >= '0' && l <= '9') val = val * 16 + l - '0';
                else if (l >= 'a' && l <= 'f') val = val * 16 + 10 + l - 'a';
                else if (l >= 'A' && l <= 'F') val = val * 16 + 10 + l - 'A';
                return std::string(1, static_cast<char>(val));
            }
            reportError("Invalid hex escape: \\x" + std::string(1, h) + " requires two hex digits");
            return "\\x" + std::string(1, h);
        }
        reportError("Invalid hex escape: \\x requires two hex digits");
        return "\\x";
    }
    case 'u': {
        advance(); // consume 'u'
        if (isAtEnd()) { reportError("Invalid unicode escape: \\u requires 4 hex digits"); return "\\u"; }
        if (peek() == '{') { reportError("\\u{...} is not supported; use \\UXXXXXXXX for full Unicode"); return "\\u{"; }
        // \uXXXX format (exactly 4 hex digits)
        std::string hexStr;
        for (int i = 0; i < 4 && !isAtEnd(); ++i) {
            if (isHexDigit(peek())) {
                hexStr += advance();
            } else {
                reportError("Invalid unicode escape: \\u requires 4 hex digits");
                return "\\u" + hexStr;
            }
        }
        if (hexStr.size() != 4) { reportError("Invalid unicode escape: \\u requires 4 hex digits"); return "\\u" + hexStr; }
        try {
            uint32_t cp = static_cast<uint32_t>(std::stoul(hexStr, nullptr, 16));
            std::string result;
            if (cp <= 0x7F) {
                result += static_cast<char>(cp);
            } else if (cp <= 0x7FF) {
                result += static_cast<char>(0xC0 | (cp >> 6));
                result += static_cast<char>(0x80 | (cp & 0x3F));
            } else {
                result += static_cast<char>(0xE0 | (cp >> 12));
                result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                result += static_cast<char>(0x80 | (cp & 0x3F));
            }
            return result;
        } catch (...) {
            reportError("Invalid unicode escape: \\u" + hexStr);
            return "\\u" + hexStr;
        }
    }
    default:
        // Unknown escape: keep as-is (\X → \X)
        advance();
        return "\\" + std::string(1, escaped);
    }
}

Token Lexer::scanString(bool isFString, bool isRegexString, bool isRawString, char quote) {
    std::string value;
    std::string raw;
    bool hasInterpolation = false;

    int braceDepth = 0;

    size_t stringStartPos = position;
    size_t stringStartLine = line;
    size_t stringStartColumn = column;

    // Raw strings: no escape sequences, no interpolation
    if (isRawString) {
        while (!isAtEnd()) {
            char c = peek();
            if (c == quote) break;
            raw += c;
            advance();
            value += c;
            assertProgress(stringStartPos, "raw string literal");
        }
        if (isAtEnd()) {
            line = stringStartLine;
            column = stringStartColumn;
            reportError("Unterminated string");
            line = stringStartLine;
            column = stringStartColumn;
            return makeToken(value, TokenType::String);
        }
        advance(); // consume closing quote
        raw = std::string(1, quote) + raw + std::string(1, quote);
        return makeToken(value, TokenType::String, raw);
    }


  while (!isAtEnd()) {
    char c = peek();

    // If we're not inside an interpolation, a matching quote ends the string
    if (braceDepth == 0 && c == quote) {
      break;
    }

    raw += c;

    if (braceDepth == 0 && c == '\\' && !isAtEnd()) {
        advance(); // consume backslash
        raw += peek();
        bool suppressInterp = false;
        std::string decoded = processEscapeSequence(isFString, suppressInterp);
        value += decoded;
        if (suppressInterp) continue;
 } else if (c == '$' && braceDepth == 0) {
  char next = peek(1);
  if (next == '{') {
    // ${expr} syntax
    hasInterpolation = true;
    advance(); // consume $
    advance(); // consume {
    value += '\x01'; // start interpolation marker
    braceDepth++;
  } else if (isAlpha(next) || next == '_' || next == '@') {
  hasInterpolation = true;
  advance(); // consume $
  value += '\x01'; // start interpolation marker

  if (isAlpha(peek()) || peek() == '_') {
  while (!isAtEnd() && (isAlphaNumeric(peek()) || peek() == '_')) {
  value += advance();
  }
  if (!isAtEnd() && peek() == '?') {
  value += advance();
  }
  } else if (peek() == '@') {
  value += advance(); // @
  while (!isAtEnd() && (isAlphaNumeric(peek()) || peek() == '_')) {
  value += advance();
  }
  if (!isAtEnd() && peek() == '?') {
  value += advance();
  }
  }
  value += '\x02'; // end interpolation marker
    } else {
    value += advance(); // $ as literal
    }

} else if (c == '{' && braceDepth == 0) {
            if (isFString && peek(1) != '{') {
                // f-string brace interpolation {expr}
                hasInterpolation = true;
                advance(); // consume {
                value += '\x01'; // start interpolation marker
                braceDepth++;
            } else if (peek(1) != '{') {
                // Literal { — not interpolation. Use ${expr} or $var.
                value += advance();
            } else {
                // Escaped brace {{ - consume both and add single {
                advance(); // first {
                advance(); // second {
                value += '{';
            }
        } else if (c == '}' && braceDepth == 0) {
            if (peek(1) == '}') {
                advance(); // first }
                advance(); // second }
                value += '}';
            } else {
                value += advance();
            }
 } else {
      // Regular character processing
      char consumed = advance();

      if (braceDepth > 0) {
        if (consumed == '{') {
          braceDepth++;
          value += consumed;
        } else if (consumed == '}') {
          braceDepth--;
          if (braceDepth == 0) {
            value += '\x02'; // end interpolation marker
          } else {
            value += consumed;
          }
        } else {
          value += consumed;
        }
      } else {
        value += consumed;
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
        raw = std::string(1, quote) + raw + std::string(1, quote);
        return makeToken(value, TokenType::String, raw);
    }

    advance(); // consume closing quote

    TokenType type;
    if (isRegexString) {
        type = TokenType::RegexString;
    } else if (hasInterpolation) {
        type = TokenType::InterpolatedString;
    } else {
        type = TokenType::String;
    }
    raw = std::string(1, quote) + raw + std::string(1, quote);
    return makeToken(value, type, raw);
}

Token Lexer::scanMultilineString(bool isFString, char quote) {
    std::string value;
    std::string raw;
    bool hasInterpolation = false;
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
        advance(); // consume backslash
        raw += peek();
        bool suppressInterp = false;
        std::string decoded = processEscapeSequence(isFString, suppressInterp);
        value += decoded;
        if (suppressInterp) continue;
    } else if (c == '$' && braceDepth == 0) {
        char next = peek(1);
        if (next == '{') {
            // ${expr} syntax
            hasInterpolation = true;
            advance(); // consume $
            advance(); // consume {
            value += '\x01'; // start interpolation marker
            braceDepth++;
        } else if (isAlpha(next) || next == '_' || next == '@') {
            hasInterpolation = true;
            advance(); // consume $
            value += '\x01'; // start interpolation marker

            if (isAlpha(peek()) || peek() == '_') {
                while (!isAtEnd() && (isAlphaNumeric(peek()) || peek() == '_')) {
                    value += advance();
                }
                if (!isAtEnd() && peek() == '?') {
                    value += advance();
                }
            } else if (peek() == '@') {
                value += advance(); // @
                while (!isAtEnd() && (isAlphaNumeric(peek()) || peek() == '_')) {
                    value += advance();
                }
                if (!isAtEnd() && peek() == '?') {
                    value += advance();
                }
            }
            value += '\x02'; // end interpolation marker
        } else {
            value += advance(); // $ as literal
        }
} else if (c == '{' && braceDepth == 0) {
            if (isFString && peek(1) != '{') {
                // f-string brace interpolation {expr}
                hasInterpolation = true;
                advance(); // consume {
                value += '\x01'; // start interpolation marker
                braceDepth++;
            } else if (peek(1) != '{') {
                value += advance(); // literal {
            } else {
                advance();
                advance();
                value += '{';
            }
        } else if (c == '}' && braceDepth == 0) {
            if (peek(1) == '}') {
                advance();
                advance();
                value += '}';
            } else {
                value += advance();
            }
 } else {
      char consumed = advance();

      if (braceDepth > 0) {
        if (consumed == '{') {
          braceDepth++;
          value += consumed;
        } else if (consumed == '}') {
          braceDepth--;
          if (braceDepth == 0) {
            value += '\x02'; // end interpolation marker
          } else {
            value += consumed;
          }
        } else {
          value += consumed;
        }
      } else {
        value += consumed;
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
    raw = std::string(3, quote) + raw + std::string(3, quote);
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
        raw = "```" + raw + "```";
    } else {
        advance();
        raw = "`" + raw + "`";
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

// Fast path for ASCII-only identifiers (common case)
static inline bool isAsciiIdentChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || 
           (c >= '0' && c <= '9') || c == '_';
}

Token Lexer::scanIdentifier() {
    // Fast path: scan ASCII-only identifier without function call overhead
    size_t start = position - 1;
    
    // Fast path: scan ASCII-only identifier in a tight loop
    while (!isAtEnd()) {
        char c = peek();
        if (static_cast<unsigned char>(c) >= 0x80) break;
        if (!isAsciiIdentChar(c)) break;
        advance();
    }
    
    // Slow path: handle Unicode identifiers
    if (!isAtEnd()) {
        while (!isAtEnd()) {
            unsigned char c = static_cast<unsigned char>(peek());
            if (c < 0x80) {
                if (!isAsciiIdentChar(c)) break;
                advance();
            } else {
                uint32_t cp = peekCodepoint();
                if (!isUnicodeLetter(cp) && !isUnicodeDigit(cp)) break;
                size_t len = codepointLength(peek());
                for (size_t i = 0; i < len && !isAtEnd(); i++) {
                    advance();
                }
            }
        }
    }

    // Extract identifier from source (avoids incremental string building)
    size_t end = position;
    std::string identifier = source.substr(start, end - start);

    if (!isAtEnd() && peek() == '?' && peek(1) != '.') {
        advance(); // consume '?'
    }

    auto keywordIt = KEYWORDS.find(identifier);
    TokenType type =
        (keywordIt != KEYWORDS.end()) ? keywordIt->second : TokenType::Identifier;

    if (type == TokenType::Let || type == TokenType::Const) {
        std::cerr << "WARNING: '" << identifier << "' is deprecated, use 'val' instead at line " 
                  << line << ", col " << (column - identifier.length()) << std::endl;
    }
    else if (type == TokenType::Import) {
        std::cerr << "WARNING: '" << identifier << "' is deprecated, use 'use' instead at line " 
                  << line << ", col " << (column - identifier.length()) << std::endl;
    }

    return makeToken(identifier, type);
}

Token Lexer::scanHotkey() {
std::string hotkey;
size_t safetyPos [[maybe_unused]] = position;
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

  // Special handling for plain F-keys (F1..F24)
  if (hotkey.size() >= 2 && hotkey[0] == 'F') {
    bool allDigits = true;
    for (size_t i = 1; i < hotkey.size(); ++i)
      allDigits &= std::isdigit(static_cast<unsigned char>(hotkey[i]));
    if (allDigits) {
      try {
        int fnum = std::stoi(hotkey.substr(1));
        if (fnum >= 1 && fnum <= 24)
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
hotkey.find('|') != std::string::npos ||
hotkey.find(':') != std::string::npos || hotkey[0] == 'F')) {
    return makeToken(hotkey, TokenType::Hotkey);
  }

  // Accept known key names (Esc, Return, Delete, Tab, Space, Backspace, etc.)
  // that don't match the modifier or F-key patterns above
  static const std::unordered_set<std::string> knownKeys = {
    "Esc", "Escape", "Return", "Enter", "Delete", "Del", "Tab", "Space",
    "Backspace", "BackSpace", "Insert", "Ins", "Home", "End", "PageUp",
    "PgUp", "PageDown", "PgDn", "Up", "Down", "Left", "Right",
    "PrintScreen", "PrtSc", "ScrollLock", "Pause", "Break",
    "NumLock", "CapsLock", "Shift", "Ctrl", "Control", "Alt", "Meta",
    "Super", "Win", "Command", "Menu", "Apps", "Help"
  };
  if (!hotkey.empty() && knownKeys.count(hotkey)) {
    return makeToken(hotkey, TokenType::Hotkey);
  }

  // Fallback: not a recognizable hotkey, rewind and treat as identifier
  position -= (hotkey.size() - 1);
  return scanIdentifier();
}

bool Lexer::isHotkeyLookahead() {
    size_t look = position;
    while (look < source.size() && isHotkeyChar(source[look])) look++;
    while (look < source.size() && (source[look] == ' ' || source[look] == '\t')) look++;
    if (look + 1 < source.size() && source[look] == '=' && source[look+1] == '>')
        return true;
    if (look + 2 < source.size() && source[look] == 'i' && source[look+1] == 'f') {
        look += 2;
        while (look + 1 < source.size()) {
            if (source[look] == '=' && source[look+1] == '>') return true;
            look++;
        }
        return false;
    }
    if (look + 4 < source.size() && source[look] == 'w' && source[look+1] == 'h' &&
        source[look+2] == 'e' && source[look+3] == 'n') {
        look += 4;
        while (look + 1 < source.size()) {
            if (source[look] == '=' && source[look+1] == '>') return true;
            look++;
        }
        return false;
    }
    return false;
}

// Parse a hotkey binding at statement start or after assignment
// Handles: modifier combinations (+, #, ^, !, $, @, |, ~, &)
// Validates key names against KeyMap
// Checks for triggers: if, =>, ::, & (combo), :up/:down/up/down
std::optional<Token> Lexer::tryParseHotkeyBinding() {
    size_t startPos = position;
    size_t startLine = line;
    size_t startCol = column;
    std::string hotkey;

    // Parse modifier prefix: +, #, ^, !, $, @, |, ~, &
    std::string modifiers;
    while (!isAtEnd()) {
        char c = peek();
        if (c == '+' || c == '#' || c == '^' || c == '!' || 
            c == '$' || c == '@' || c == '|' || c == '~' || c == '&') {
            modifiers += advance();
        } else {
            break;
        }
    }

    // Skip whitespace after modifiers
    while (!isAtEnd() && (peek() == ' ' || peek() == '\t')) {
        advance();
    }

    // Parse key name (alphanumeric + some special chars)
    std::string keyName;
    while (!isAtEnd()) {
        char c = peek();
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') {
            keyName += advance();
        } else {
            break;
        }
    }

    if (keyName.empty()) {
        return std::nullopt;
    }

    // Validate key name against KeyMap
    std::string fullKey = keyName;
    if (!havel::KeyMap::FromString(keyName)) {
        // Try to find aliases
        // Fallback: check if it's a known key name
        static const std::unordered_set<std::string> knownKeys = {
            "Esc", "Escape", "Return", "Enter", "Delete", "Del", "Tab", "Space",
            "Backspace", "BackSpace", "Insert", "Ins", "Home", "End", "PageUp",
            "PgUp", "PageDown", "PgDn", "Up", "Down", "Left", "Right",
            "PrintScreen", "PrtSc", "ScrollLock", "Pause", "Break",
            "NumLock", "CapsLock", "Shift", "Ctrl", "Control", "Alt", "Meta",
            "Super", "Win", "Command", "Menu", "Apps", "Help",
            "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
            "F13", "F14", "F15", "F16", "F17", "F18", "F19", "F20", "F21", "F22", "F23", "F24",
            "Esc", "Enter", "Tab", "Space", "Backspace", "Delete", "Insert", "Home", "End",
            "PageUp", "PageDown", "Up", "Down", "Left", "Right",
            "CapsLock", "ScrollLock", "NumLock",
            "Shift", "Ctrl", "Alt", "Meta", "Super", "Win", "Command", "Menu", "Apps"
        };
        if (!knownKeys.count(keyName)) {
            return std::nullopt;
        }
    }

    // Build the hotkey string with modifiers
    hotkey = modifiers + keyName;

    // Check for combo hotkeys with & (e.g., "LButton & RButton")
    while (!isAtEnd()) {
        // Skip whitespace
        while (!isAtEnd() && (peek() == ' ' || peek() == '\t')) {
            hotkey += advance();
        }
        // Check for & (combo)
        if (peek() == '&') {
            hotkey += advance();
            // Skip whitespace after &
            while (!isAtEnd() && (peek() == ' ' || peek() == '\t')) {
                hotkey += advance();
            }
            // Parse next key in combo
            std::string nextKey;
            while (!isAtEnd()) {
                char c = peek();
                if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') {
                    nextKey += advance();
                } else {
                    break;
                }
            }
            if (nextKey.empty()) {
                return std::nullopt;
            }
            hotkey += nextKey;
        } else {
            break;
        }
    }

    // Check for triggers: if, =>, ::, &, :up, :down, up, down
    size_t savePos = position;
    size_t saveLine = line;
    size_t saveCol = column;

    // Skip whitespace
    while (!isAtEnd() && (peek() == ' ' || peek() == '\t')) {
        advance();
    }

    bool hasTrigger = false;
    std::string trigger;

    // Check for =>
    if (peek() == '=' && peek(1) == '>') {
        hasTrigger = true;
        trigger = "=>";
        advance(); advance();
    }
    // Check for ::
    else if (peek() == ':' && peek(1) == ':') {
        hasTrigger = true;
        trigger = "::";
        advance(); advance();
    }
    // Check for &
    else if (peek() == '&' && peek(1) != '&') {
        hasTrigger = true;
        trigger = "&";
        advance();
    }
    // Check for if
    else if (peek() == 'i' && peek(1) == 'f' && 
             (!std::isalnum(static_cast<unsigned char>(peek(2))) && peek(2) != '_')) {
        hasTrigger = true;
        trigger = "if";
        advance(); advance();
    }
    // Check for :up, :down, up, down
    else if (peek() == ':') {
        if (peek(1) == 'u' && peek(2) == 'p' && 
            (!std::isalnum(static_cast<unsigned char>(peek(3))) && peek(3) != '_')) {
            hasTrigger = true;
            trigger = ":up";
            advance(); advance(); advance();
        } else if (peek(1) == 'd' && peek(2) == 'o' && peek(3) == 'w' && 
                   peek(4) == 'n' && 
                   (!std::isalnum(static_cast<unsigned char>(peek(5))) && peek(5) != '_')) {
            hasTrigger = true;
            trigger = ":down";
            advance(); advance(); advance(); advance(); advance();
        }
    }
    else if (peek() == 'u' && peek(1) == 'p' && 
             (!std::isalnum(static_cast<unsigned char>(peek(2))) && peek(2) != '_')) {
        hasTrigger = true;
        trigger = "up";
        advance(); advance();
    }
    else if (peek() == 'd' && peek(1) == 'o' && peek(2) == 'w' && peek(3) == 'n' &&
             (!std::isalnum(static_cast<unsigned char>(peek(4))) && peek(4) != '_')) {
        hasTrigger = true;
        trigger = "down";
        advance(); advance(); advance(); advance();
    }

    if (!hasTrigger) {
        // No valid trigger, rewind
        position = savePos;
        line = saveLine;
        column = saveCol;
        return std::nullopt;
    }

    // Build the final hotkey token value including trigger
    std::string fullHotkey = hotkey;
    if (!trigger.empty()) {
        fullHotkey += " " + trigger;
    }

    return makeToken(fullHotkey, TokenType::Hotkey);
}

std::vector<Token> Lexer::tokenize() {
  currentTokens.clear();
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
      bool isStatementStart = currentTokens.empty();
      TokenType prevType = TokenType::EOF_TOKEN;
      if (!isStatementStart) {
        prevType = currentTokens.back().type;
        if (debug_lexer) {
          havel::debug("[DEBUG] # char at line {} col {}. Prev token type: {} value: '{}'", 
                       line, column, static_cast<int>(prevType), currentTokens.back().value);
        }
        isStatementStart = (prevType == TokenType::NewLine ||
                            prevType == TokenType::Semicolon ||
                            prevType == TokenType::Arrow ||
                            prevType == TokenType::OpenBrace ||
                            prevType == TokenType::CloseBrace);
      }

      // # followed by underscore is always length operator (hotkey keys never start with _)
      if (!isAtEnd() && peek() == '_') {
        currentTokens.push_back(makeToken("#", TokenType::Length));
        continue;
      }

      bool hasModifierPrefix = (!isAtEnd() && (peek() == '!' || peek() == '&' || peek() == '^'));
      bool hasKeyName = (!isAtEnd() && isAlpha(peek()));
      bool afterAssign = (prevType == TokenType::Assign && hasKeyName && isHotkeyLookahead());

      if ((isStatementStart && isHotkeyLookahead()) || afterAssign || (hasModifierPrefix && hasKeyName && isHotkeyLookahead())) {
        currentTokens.push_back(scanHotkey());
        if (debug_lexer) {
          havel::debug("LEX: hotkey modifier {}", currentTokens.back().toString());
        }
        continue;
      }

      // If it's not a hotkey, it's the length operator
      currentTokens.push_back(makeToken("#", TokenType::Length));
      if (debug_lexer) {
        havel::debug("LEX: {}", currentTokens.back().toString());
      }
      continue;
    }


    // Handle numbers (including negative numbers in certain contexts)
// Only treat -digit as a negative number when NOT after an expression
bool canBeNegativeNumber = (c == '-' && isDigit(peek()));
 if (canBeNegativeNumber && !currentTokens.empty()) {
 TokenType prevType = currentTokens.back().type;
 if (prevType == TokenType::Number ||
 prevType == TokenType::String ||
 prevType == TokenType::InterpolatedString ||
 prevType == TokenType::MultilineString ||
 prevType == TokenType::Identifier ||
 prevType == TokenType::True ||
 prevType == TokenType::False ||
 prevType == TokenType::Null ||
 prevType == TokenType::CloseParen ||
 prevType == TokenType::CloseBracket ||
 prevType == TokenType::CloseBrace ||
 prevType == TokenType::Length ||
 prevType == TokenType::PlusPlus ||
 prevType == TokenType::MinusMinus ||
 prevType == TokenType::LeftArrow) {
 canBeNegativeNumber = false;
 }
}
if (isDigit(c) || canBeNegativeNumber) {
            currentTokens.push_back(scanNumber());
            if (debug_lexer) {
                havel::debug("LEX: {}", currentTokens.back().toString());
            }
            continue;
        }

    // Handle strings - both single and double quotes work the same
    if (c == '"' || c == '\'') {
        char quote = c;
        bool isFString = false;
        bool isRegexString = false;
        bool isRawString = false;
        if (!currentTokens.empty() && currentTokens.back().type == TokenType::Identifier) {
            if (currentTokens.back().value == "f" || currentTokens.back().value == "F") {
                isFString = true;
                currentTokens.pop_back();
            } else if (currentTokens.back().value == "u" || currentTokens.back().value == "U") {
                isFString = false;
                currentTokens.pop_back();
            } else if (currentTokens.back().value == "r" || currentTokens.back().value == "R") {
                isRegexString = true;
                isFString = false;
                currentTokens.pop_back();
            } else if (currentTokens.back().value == "raw" || currentTokens.back().value == "RAW") {
                isRawString = true;
                isFString = false;
                currentTokens.pop_back();
            }
        }

        // Check for multiline string """ or '''
        if (position + 2 < source.length() &&
            source[position] == quote && source[position + 1] == quote) {
            advance();
            advance();
            currentTokens.push_back(scanMultilineString(isFString, quote));
        } else {
            currentTokens.push_back(scanString(isFString, isRegexString, isRawString, quote));
        }
        if (debug_lexer) {
                havel::debug("LEX: {}", currentTokens.back().toString());
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
        currentTokens.push_back(scanBacktick(isMultilineBacktick));
        if (debug_lexer) {
                havel::debug("LEX: {}", currentTokens.back().toString());
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
 currentTokens.push_back(makeToken("/", TokenType::Hotkey));
 if (debug_lexer) {
 havel::debug("LEX: {}", currentTokens.back().toString());
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
 // If identifier is immediately followed by '/', this is a regex literal /pattern/
 if (la2 < source.length() && source[la2] == '/') {
 // Not a hotkey — will be handled by regex scan below
 }
 else {
 // Check if there's a closing '/' between the identifier and '=>'
 // If so, this is /pattern/ regex, not a /hotkey => 
 bool hasClosingSlashBeforeArrow = false;
 {
 size_t searchEnd2 = std::min(la2 + 200, source.length());
 for (size_t s = la2; s + 1 < searchEnd2; s++) {
 if (source[s] == '/' && s > la) { hasClosingSlashBeforeArrow = true; break; }
 if (source[s] == '=' && source[s + 1] == '>') break;
 if (source[s] == '\n') break;
 }
 }
 if (hasClosingSlashBeforeArrow) {
 // Not a hotkey — regex literal with pattern after identifier chars
 }
 else {
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
 }
 }
 if (isSlashHotkey) {
 if (identifierIsKeyword) {
 // / followed by condition keyword — only consume /
 currentTokens.push_back(makeToken("/", TokenType::Hotkey));
 advance(); // consume '/'
 } else {
 // /identifier — consume the whole thing as one token
 std::string hotkeyValue = source.substr(position, hotkeyEnd - position);
 advance(); // consume '/'
 while (position < source.length() && position < hotkeyEnd) {
 advance();
 }
 currentTokens.push_back(makeToken(hotkeyValue, TokenType::Hotkey));
 }
 if (debug_lexer) {
 havel::debug("LEX: {}", currentTokens.back().toString());
 }
 continue;
 }

 // Check if this looks like a regex (not division)
 // Simple heuristic: if previous non-whitespace token suggests expression
 // context
 bool isRegexContext = currentTokens.empty() ||
 currentTokens.back().type == TokenType::OpenParen ||
 currentTokens.back().type == TokenType::OpenBracket ||
 currentTokens.back().type == TokenType::OpenBrace ||
 currentTokens.back().type == TokenType::Comma ||
 currentTokens.back().type == TokenType::Assign ||
 currentTokens.back().type == TokenType::Arrow ||
 currentTokens.back().type == TokenType::And ||
 currentTokens.back().type == TokenType::Or ||
 currentTokens.back().type == TokenType::Not ||
 currentTokens.back().type == TokenType::In ||
 currentTokens.back().type == TokenType::Matches ||
 currentTokens.back().type == TokenType::Tilde ||
 currentTokens.back().type == TokenType::Colon ||
 currentTokens.back().type == TokenType::Question ||
 currentTokens.back().type == TokenType::Pipe ||
 currentTokens.back().type == TokenType::NewLine ||
 currentTokens.back().type == TokenType::Semicolon;

 if (isRegexContext && !isDigit(peek())) {
 currentTokens.push_back(scanRegexLiteral());
 if (debug_lexer) {
 havel::debug("LEX: {}", currentTokens.back().toString());
 }
 continue;
 }
 }

    // Handle return type arrow ->
    if (c == '-' && peek() == '>') {
      advance(); // consume '>'
      currentTokens.push_back(makeToken("->", TokenType::ReturnType));
      if (debug_lexer) {
        havel::debug("LEX: {}", currentTokens.back().toString());
      }
      continue;
    }

    // Handle arrow operator =>
    if (c == '=' && peek() == '>') {
      advance(); // consume '>'
      currentTokens.push_back(makeToken("=>", TokenType::Arrow));
      continue;
    }

    // Handle hotkey block trigger ::
    if (c == ':' && peek() == ':') {
      advance(); // consume second ':'
      currentTokens.push_back(makeToken("::", TokenType::ColonColon));
      if (debug_lexer) {
        havel::debug("LEX: {}", currentTokens.back().toString());
      }
      continue;
    }

    // Handle ++ and --
    if (c == '+' && peek() == '+') {
      advance();
      currentTokens.push_back(makeToken("++", TokenType::PlusPlus));
      if (debug_lexer) {
        havel::debug("LEX: {}", currentTokens.back().toString());
      }
      continue;
    }
    if (c == '-' && peek() == '-') {
      advance();
      currentTokens.push_back(makeToken("--", TokenType::MinusMinus));
      if (debug_lexer) {
        havel::debug("LEX: {}", currentTokens.back().toString());
      }
      continue;
    }

    // Handle compound assignments first: +=, -=, *=, /=
    if (c == '+' && peek() == '=') {
      advance();
      currentTokens.push_back(makeToken("+=", TokenType::PlusAssign));
      if (debug_lexer) {
        havel::debug("LEX: {}", currentTokens.back().toString());
      }
      continue;
    }
    if (c == '-' && peek() == '=') {
      advance();
      currentTokens.push_back(makeToken("-=", TokenType::MinusAssign));
      if (debug_lexer) {
        havel::debug("LEX: {}", currentTokens.back().toString());
      }
      continue;
    }
    if (c == '*' && peek() == '=') {
      advance();
      currentTokens.push_back(makeToken("*=", TokenType::MultiplyAssign));
      if (debug_lexer) {
        havel::debug("LEX: {}", currentTokens.back().toString());
      }
      continue;
    }
    if (c == '/' && peek() == '=') {
      advance();
      currentTokens.push_back(makeToken("/=", TokenType::DivideAssign));
      if (debug_lexer) {
        havel::debug("LEX: {}", currentTokens.back().toString());
      }
      continue;
    }
if (c == '%' && peek() == '=') {
    advance();
    currentTokens.push_back(makeToken("%=", TokenType::ModuloAssign));
    if (debug_lexer) {
      havel::debug("LEX: {}", currentTokens.back().toString());
    }
    continue;
  }
  // %% remainder operator or %%= remainder assign
  if (c == '%' && peek() == '%') {
    advance(); // consume second %
    if (peek() == '=') {
      advance(); // consume =
      currentTokens.push_back(makeToken("%%=", TokenType::DoubleModuloAssign));
    } else {
      currentTokens.push_back(makeToken("%%", TokenType::DoubleModulo));
    }
    if (debug_lexer) {
      havel::debug("LEX: {}", currentTokens.back().toString());
    }
    continue;
  }
        // \\ divmod operator
        if (c == '\\' && peek() == '\\') {
            advance(); // consume second backslash
            currentTokens.push_back(makeToken("\\\\", TokenType::DoubleBackslash));
            if (debug_lexer) {
                havel::debug("LEX: {}", currentTokens.back().toString());
            }
            continue;
        }
  // \= integer division assign
  if (c == '\\' && peek() == '=') {
    advance(); // consume =
    currentTokens.push_back(makeToken("\\=", TokenType::BackslashAssign));
    if (debug_lexer) {
      havel::debug("LEX: {}", currentTokens.back().toString());
    }
    continue;
  }
  // &= bitwise AND assign
  if (c == '&' && peek() == '=') {
    advance();
    currentTokens.push_back(makeToken("&=", TokenType::BitwiseAndAssign));
    if (debug_lexer) {
      havel::debug("LEX: {}", currentTokens.back().toString());
    }
    continue;
  }
  // |= bitwise OR assign
  if (c == '|' && peek() == '=') {
    advance();
    currentTokens.push_back(makeToken("|=", TokenType::BitwiseOrAssign));
    if (debug_lexer) {
      havel::debug("LEX: {}", currentTokens.back().toString());
    }
    continue;
  }
  // ^= bitwise XOR assign
  if (c == '^' && peek() == '=') {
    advance();
    currentTokens.push_back(makeToken("^=", TokenType::BitwiseXorAssign));
    if (debug_lexer) {
      havel::debug("LEX: {}", currentTokens.back().toString());
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
        currentTokens.push_back(makeToken("**=", TokenType::PowerAssign));
        if (debug_lexer) {
            havel::debug("LEX: {}", currentTokens.back().toString());
        }
        continue;
      } else {
        advance(); // consume first *
        advance(); // consume second *
        currentTokens.push_back(makeToken("**", TokenType::Power));
        if (debug_lexer) {
            havel::debug("LEX: {}", currentTokens.back().toString());
        }
        continue;
      }
    }

    // Handle ?? (nullish coalescing)
    if (c == '?' && peek() == '?') {
      advance();
      currentTokens.push_back(makeToken("??", TokenType::Nullish));
      if (debug_lexer) {
        havel::debug("LEX: {}", currentTokens.back().toString());
      }
      continue;
    }

    // Handle ?. (optional chaining)
    if (c == '?' && peek() == '.') {
      advance();
      currentTokens.push_back(makeToken("?.", TokenType::QuestionDot));
      if (debug_lexer) {
        havel::debug("LEX: {}", currentTokens.back().toString());
      }
      continue;
    }

// Handle == and !=
    if (c == '=' && peek() == '=') {
      advance();
      currentTokens.push_back(makeToken("==", TokenType::Equals));
      if (debug_lexer) {
        havel::debug("LEX: {}", currentTokens.back().toString());
      }
      continue;
    }
    if (c == '!' && peek() == '=') {
      // Check if this is a hotkey (!= followed by => with optional whitespace)
      size_t look = position + 1;
      // Skip the '=' in '!='
      look++;
      // Skip whitespace after '!='
      while (look < source.length() && (source[look] == ' ' || source[look] == '\t')) {
        look++;
      }
      if (look + 1 < source.length() && source[look] == '=' && source[look + 1] == '>') {
        // This is a hotkey (!= =>), consume != and =>
        advance(); // consume '!'
        advance(); // consume '='
        // Skip whitespace
        while (!isAtEnd() && (peek() == ' ' || peek() == '\t')) advance();
        // Consume =>
        advance(); // consume '='
        advance(); // consume '>'
        // Emit hotkey token AND arrow token
        currentTokens.push_back(makeToken("!=", TokenType::Hotkey));
        currentTokens.push_back(makeToken("=>", TokenType::Arrow));
        if (debug_lexer) {
          havel::debug("LEX: {}", currentTokens.back().toString());
        }
        continue;
      }
      // Not a hotkey, treat as NotEquals operator
      advance(); // consume '!'
      advance(); // consume '='
      currentTokens.push_back(makeToken("!=", TokenType::NotEquals));
      if (debug_lexer) {
        havel::debug("LEX: {}", currentTokens.back().toString());
      }
      continue;
}
      // Handle !~ - not matches operator
      if (c == '!' && peek() == '~') {
        advance(); // consume '!'
        advance(); // consume '~'
        currentTokens.push_back(makeToken("!~", TokenType::NotTilde));
        if (debug_lexer) {
          havel::debug("LEX: {}", currentTokens.back().toString());
        }
        continue;
      }
      // Handle !#= and !#- as hotkey prefixes (e.g., !#= =>, !#- =>)
    if (c == '!' && peek() == '#') {
      char nextAfterHash = peek(1);
      if (nextAfterHash == '=' || nextAfterHash == '-') {
        // This is a hotkey prefix (!#= or !#-), scan the full hotkey
        // We need to consume the trigger (=> or :: or if) and emit both tokens
        // The main loop already consumed '!', position is at '#'
        advance(); // consume '#'
        char prefixChar = advance(); // consume '=' or '-'
        // Now scan the key part - but first check if next is whitespace + trigger
        // If so, the hotkey is just !#= or !#- (complete hotkey, not a prefix)
        size_t savePos = position;
        size_t saveLine = line;
        size_t saveCol = column;
        std::string keyName;
        while (!isAtEnd()) {
          char c2 = peek();
          if (std::isalnum(static_cast<unsigned char>(c2)) || c2 == '_' || c2 == '-') {
            keyName += advance();
          } else {
            break;
          }
        }
        // Skip whitespace
        while (!isAtEnd() && (peek() == ' ' || peek() == '\t')) advance();
        // Check for triggers
        std::string trigger;
        if (peek() == '=' && peek(1) == '>') {
          trigger = "=>";
          advance(); advance();
        } else if (peek() == ':' && peek(1) == ':') {
          trigger = "::";
          advance(); advance();
        } else if (peek() == '&' && peek(1) != '&') {
          trigger = "&";
          advance();
        } else if (peek() == 'i' && peek(1) == 'f' && 
                   (!std::isalnum(static_cast<unsigned char>(peek(2))) && peek(2) != '_')) {
          trigger = "if";
          advance(); advance();
        } else if (peek() == ':') {
          if (peek(1) == 'u' && peek(2) == 'p' && 
              (!std::isalnum(static_cast<unsigned char>(peek(3))) && peek(3) != '_')) {
            trigger = ":up";
            advance(); advance(); advance();
          } else if (peek(1) == 'd' && peek(2) == 'o' && peek(3) == 'w' && peek(4) == 'n' && 
                     (!std::isalnum(static_cast<unsigned char>(peek(4))) && peek(4) != '_')) {
            trigger = ":down";
            advance(); advance(); advance(); advance(); advance();
          }
        } else if (peek() == 'u' && peek(1) == 'p' && 
                   (!std::isalnum(static_cast<unsigned char>(peek(2))) && peek(2) != '_')) {
          trigger = "up";
          advance(); advance();
        } else if (peek() == 'd' && peek(1) == 'o' && peek(2) == 'w' && peek(3) == 'n' &&
                   (!std::isalnum(static_cast<unsigned char>(peek(4))) && peek(4) != '_')) {
          trigger = "down";
          advance(); advance(); advance(); advance();
        }
        // If we found a trigger but no keyName, the hotkey is just !#= or !#-
        // If we found a keyName, include it
        std::string hotkey = "!#";
        hotkey += prefixChar;
        if (!keyName.empty()) {
          hotkey += keyName;
        }
        if (!trigger.empty()) {
          // Hotkey token contains only the key part, trigger is separate token
          currentTokens.push_back(makeToken(hotkey, TokenType::Hotkey));
          currentTokens.push_back(makeToken(trigger, trigger == "=>" ? TokenType::Arrow : 
                                     trigger == "::" ? TokenType::ColonColon :
                                     trigger == "&" ? TokenType::BitwiseAnd :
                                     trigger == "if" ? TokenType::If :
                                     trigger == ":up" || trigger == ":down" || trigger == "up" || trigger == "down" ? TokenType::Identifier :
                                     TokenType::Hotkey));
        } else {
          // No trigger found - this might be a standalone hotkey reference
          // Emit just the hotkey part
          currentTokens.push_back(makeToken(hotkey, TokenType::Hotkey));
        }
        if (debug_lexer) {
          havel::debug("LEX: {}", currentTokens.back().toString());
        }
        continue;
      }
    }

        // Handle (( )) bitwise expression delimiters
        if (c == '(' && peek() == '(' && !inBitwiseExpr) {
        // If previous token suggests function call context, emit two separate OpenParens
        // e.g. print((expr)) should be: print ( ( expr ) )
        if (!currentTokens.empty()) {
            TokenType prevType = currentTokens.back().type;
 if (prevType == TokenType::Identifier ||
 prevType == TokenType::CloseParen ||
 prevType == TokenType::CloseBracket ||
 prevType == TokenType::String ||
 prevType == TokenType::InterpolatedString ||
 prevType == TokenType::MultilineString ||
 prevType == TokenType::Number ||
            prevType == TokenType::Plus ||
            prevType == TokenType::Minus ||
            prevType == TokenType::Multiply ||
            prevType == TokenType::Divide ||
            prevType == TokenType::Modulo ||
            prevType == TokenType::Equals ||
            prevType == TokenType::NotEquals ||
            prevType == TokenType::Less ||
            prevType == TokenType::Greater ||
            prevType == TokenType::LessEquals ||
            prevType == TokenType::GreaterEquals ||
            prevType == TokenType::Comma ||
            prevType == TokenType::Return ||
            prevType == TokenType::Colon) {
            currentTokens.push_back(makeToken("(", TokenType::OpenParen));
            advance(); // consume second '('
            currentTokens.push_back(makeToken("(", TokenType::OpenParen));
            continue;
        }
        // After Assign, (( could be either bitwise or arithmetic.
        // Look ahead for bitwise operators (& | ^ << >>) inside the ((...))
        // If no bitwise ops found, treat as two separate parens.
        if (prevType == TokenType::Assign) {
            bool hasBitwiseOp = false;
            size_t look = position + 1; // after '(('
            int depth = 2;
            while (look < source.length() && depth > 0) {
                char lc = source[look];
                if (lc == '(') { depth++; }
                else if (lc == ')') { depth--; if (depth == 0) break; }
                else if (depth == 2 && (lc == '&' || lc == '|' || lc == '^')) {
                    // Must not be && or ||
                    if (lc == '&' && look + 1 < source.length() && source[look + 1] == '&') { /* skip */ }
                    else if (lc == '|' && look + 1 < source.length() && source[look + 1] == '|') { /* skip */ }
                    else { hasBitwiseOp = true; break; }
                }
                else if (depth == 2 && lc == '<' && look + 1 < source.length() && source[look + 1] == '<') {
                    hasBitwiseOp = true; break;
                }
                else if (depth == 2 && lc == '>' && look + 1 < source.length() && source[look + 1] == '>') {
                    hasBitwiseOp = true; break;
                }
                look++;
            }
            if (!hasBitwiseOp) {
                currentTokens.push_back(makeToken("(", TokenType::OpenParen));
                advance(); // consume second '('
                currentTokens.push_back(makeToken("(", TokenType::OpenParen));
                continue;
            }
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
            currentTokens.push_back(makeToken("(", TokenType::OpenParen));
            advance(); // consume second '('
            currentTokens.push_back(makeToken("(", TokenType::OpenParen));
            continue;
        }

        advance(); // consume second '('
        inBitwiseExpr = true;
        currentTokens.push_back(makeToken("((", TokenType::DoubleOpenParen));
        if (debug_lexer) {
            havel::debug("LEX: Bitwise block detected at pos {}. Emitting DoubleOpenParen.", position - 2);
            havel::debug("LEX: {}", currentTokens.back().toString());
        }
        continue;
    }
    if (c == ')' && peek() == ')' && inBitwiseExpr) {
        advance(); // consume second ')'
        inBitwiseExpr = false;
	currentTokens.push_back(makeToken("))", TokenType::DoubleCloseParen));
	if (debug_lexer) {
            havel::debug("LEX: {}", currentTokens.back().toString());
        }
    continue;
  }

  // Handle && and ||
    if (c == '&' && peek() == '&') {
      advance();
      currentTokens.push_back(makeToken("&&", TokenType::And));
      if (debug_lexer) {
        havel::debug("LEX: {}", currentTokens.back().toString());
      }
      continue;
    }
      if (c == '|' && peek() == '|') {
        advance();
        currentTokens.push_back(makeToken("||", TokenType::Or));
        if (debug_lexer) {
          havel::debug("LEX: {}", currentTokens.back().toString());
        }
        continue;
      }
// |> pipeline operator
if (c == '|' && peek() == '>') {
advance();
currentTokens.push_back(makeToken("|>", TokenType::PipeRight));
if (debug_lexer) {
havel::debug("LEX: {}", currentTokens.back().toString());
}
continue;
}
// | at statement start followed by hotkey chars = passthrough hotkey prefix
if (c == '|' && !inBitwiseExpr) {
bool prevIsStatementStart = currentTokens.empty() ||
currentTokens.back().type == TokenType::NewLine ||
currentTokens.back().type == TokenType::Semicolon ||
currentTokens.back().type == TokenType::CloseBrace ||
currentTokens.back().type == TokenType::EOF_TOKEN;
if (prevIsStatementStart) {
char next = peek();
if (isAlpha(next) || next == '+' || next == '!' || next == '^' ||
next == '#' || next == '@' || next == '~' || next == '$' ||
next == '*') {
currentTokens.push_back(scanHotkey());
continue;
}
}
}
// Inside (( )), single | is bitwise OR, not pipeline
if (c == '|' && inBitwiseExpr) {
currentTokens.push_back(makeToken("|", TokenType::BitwiseOr));
if (debug_lexer) {
havel::debug("LEX: {}", currentTokens.back().toString());
}
continue;
}
// Context-aware: if previous token suggests expression context, treat |
// as bitwise OR operator rather than pipeline
if (c == '|' && !currentTokens.empty()) {
TokenType prevType = currentTokens.back().type;
// After Assign, check if this is a hotkey binding (|x if => or |x =>)
if (prevType == TokenType::Assign && !isAtEnd() && isHotkeyLookahead()) {
  currentTokens.push_back(scanHotkey());
  continue;
}
if (prevType == TokenType::Number ||
prevType == TokenType::Identifier ||
prevType == TokenType::String ||
prevType == TokenType::InterpolatedString ||
prevType == TokenType::MultilineString ||
prevType == TokenType::RegexString ||
prevType == TokenType::CloseParen ||
prevType == TokenType::CloseBracket ||
prevType == TokenType::Not ||
prevType == TokenType::Or ||
prevType == TokenType::And ||
prevType == TokenType::Assign ||
prevType == TokenType::BitwiseOr ||
prevType == TokenType::BitwiseAnd ||
prevType == TokenType::BitwiseXor ||
prevType == TokenType::ShiftLeft ||
prevType == TokenType::ShiftRight) {
currentTokens.push_back(makeToken("|", TokenType::BitwiseOr));
if (debug_lexer) {
havel::debug("LEX: {}", currentTokens.back().toString());
}
continue;
}
}

  // Handle <= and >=
  if (c == '<' && peek() == '=') {
    advance();
    currentTokens.push_back(makeToken("<=", TokenType::LessEquals));
    if (debug_lexer) {
      havel::debug("LEX: {}", currentTokens.back().toString());
    }
    continue;
  }
  if (c == '>' && peek() == '=') {
    advance();
    currentTokens.push_back(makeToken(">=", TokenType::GreaterEquals));
    if (debug_lexer) {
      havel::debug("LEX: {}", currentTokens.back().toString());
    }
    continue;
  }

// Handle <<= (bitwise left shift assign) - must check before <<
  if (c == '<' && peek() == '<' && position + 1 < source.length() && source[position + 1] == '=') {
    advance(); // consume second '<'
    advance(); // consume '='
    currentTokens.push_back(makeToken("<<=", TokenType::ShiftLeftAssign));
    if (debug_lexer) {
      havel::debug("LEX: {}", currentTokens.back().toString());
    }
    continue;
  }
  // Handle << (bitwise left shift) - must check before single <
  if (c == '<' && peek() == '<') {
    advance(); // consume second '<'
    currentTokens.push_back(makeToken("<<", TokenType::ShiftLeft));
    if (debug_lexer) {
      havel::debug("LEX: {}", currentTokens.back().toString());
    }
    continue;
  }

  // Handle >>= (bitwise right shift assign) - must check before >>
  if (c == '>' && peek() == '>' && position + 1 < source.length() && source[position + 1] == '=') {
    advance(); // consume second '>'
    advance(); // consume '='
    currentTokens.push_back(makeToken(">>=", TokenType::ShiftRightAssign));
    if (debug_lexer) {
      havel::debug("LEX: {}", currentTokens.back().toString());
    }
    continue;
  }
  // Handle >> (config append/get or bitwise right shift) - must check before single >
  if (c == '>' && peek() == '>') {
    advance(); // consume second '>'
    currentTokens.push_back(makeToken(">>", TokenType::ShiftRight));
    if (debug_lexer) {
      havel::debug("LEX: {}", currentTokens.back().toString());
    }
 continue;
 }

 // Handle <- (left arrow / fiber await) - must check before single <
 if (c == '<' && peek() == '-') {
 advance(); // consume '-'
 currentTokens.push_back(makeToken("<-", TokenType::LeftArrow));
 if (debug_lexer) {
 havel::debug("LEX: {}", currentTokens.back().toString());
 }
 continue;
 }

 // Handle single < and >
    if (c == '<') {
      currentTokens.push_back(makeToken("<", TokenType::Less));
      if (debug_lexer) {
        havel::debug("LEX: {}", currentTokens.back().toString());
      }
      continue;
    }
    if (c == '>') {
      currentTokens.push_back(makeToken(">", TokenType::Greater));
      if (debug_lexer) {
        havel::debug("LEX: {}", currentTokens.back().toString());
      }
      continue;
    }

    // Handle single equals (assignment)
    if (c == '=') {
      currentTokens.push_back(makeToken("=", TokenType::Assign));
      if (debug_lexer) {
        havel::debug("LEX: {}", currentTokens.back().toString());
      }
      continue;
    }

    // Handle ... (spread operator) - must check before ..
    // Note: c is already consumed by advance(), so peek() is at position+1
    if (c == '.' && peek() == '.' && peek(1) == '.') {
      advance(); // consume second '.'
      advance(); // consume third '.'
      currentTokens.push_back(makeToken("...", TokenType::Spread));
      continue;
    }

    // Handle ..= (inclusive range pattern) - check before ..
    if (c == '.' && peek() == '.' && peek(1) == '=') {
      advance(); // consume second '.'
      advance(); // consume '='
      currentTokens.push_back(makeToken("..=", TokenType::DotDotEquals));
      continue;
    }

    // Handle .. (range operator)
    if (c == '.' && peek() == '.') {
      advance(); // consume second '.'
      currentTokens.push_back(makeToken("..", TokenType::DotDot));
      continue;
    }

    // Handle shell command prefix: $ command (must be before hotkey handling)
    // But NOT if followed by => (which would make it a hotkey like $Esc =>)
    if (c == '$') {
      // Look ahead: if $ is followed by a hotkey name and then =>,
      // treat it as a hotkey (e.g., $Esc =>) instead of a shell command
      {
        size_t look = position;
        while (look < source.length() && isHotkeyChar(source[look])) look++;
        while (look < source.length() && (source[look] == ' ' || source[look] == '\t')) look++;
        if (look + 1 < source.length() && source[look] == '=' && source[look + 1] == '>') {
          currentTokens.push_back(scanHotkey());
          if (debug_lexer) {
            havel::debug("LEX: {}", currentTokens.back().toString());
          }
          continue;
        }
      }

      // Check for capture mode: $!
      bool captureOutput = false;
      if (!isAtEnd() && peek() == '!') {
        advance(); // consume '!'
        captureOutput = true;
      }

      // Don't skip whitespace - let parser handle it
      // Just emit the token and let parser parse the expression

      currentTokens.push_back(scanShellCommand(captureOutput));
      if (debug_lexer) {
        havel::debug("LEX: {}", currentTokens.back().toString());
      }
      continue;
    }

    // Handle @-> (super call operator) - must be before hotkey handling
    if (c == '@' && peek() == '-' && peek(1) == '>') {
      advance(); // consume '-'
      advance(); // consume '>'
      currentTokens.push_back(makeToken("@->", TokenType::SuperArrow));
      if (debug_lexer) {
        havel::debug("LEX: {}", currentTokens.back().toString());
      }
      continue;
    }

    // Handle @@ (class member marker) - must be before single @ handling
    if (c == '@' && peek() == '@') {
      advance(); // consume second '@'
      currentTokens.push_back(makeToken("@@", TokenType::AtAt));
      if (debug_lexer) {
        havel::debug("LEX: {}", currentTokens.back().toString());
      }
      continue;
    }

    // Handle @ (at/this field access) - must be before hotkey handling
    // @ not followed by alpha/underscore is always self-reference (At token), never a hotkey
    if (c == '@' && !(isAlpha(peek()) || peek() == '_')) {
        currentTokens.push_back(makeToken("@", TokenType::At));
        if (debug_lexer) {
            havel::debug("LEX: {}", currentTokens.back().toString());
        }
        continue;
    }
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
      } else if (look < source.size() && source[look] == '&' && 
                 (look + 1 >= source.size() || source[look + 1] != '&')) {
        isHotkey = true; // @identifier & ... (compound hotkey), but not &&
      } else if (look < source.size() && source[look] == ':') {
        isHotkey = true; // @identifier:timing
      }
      if (isHotkey) {
        // Fall through to scanHotkey
      } else {
        // @identifier with anything else ( = / . / ( / \n / etc ) - field access
        currentTokens.push_back(makeToken("@", TokenType::At));
        if (debug_lexer) {
            havel::debug("LEX: {}", currentTokens.back().toString());
        }
        continue;
      }
    }

    // Handle modifier-based hotkeys starting with special characters like # and
    // combo '&' — but inside (( )), & is bitwise AND
    if (c == '&') {
        if (inBitwiseExpr) {
            currentTokens.push_back(makeToken("&", TokenType::BitwiseAnd));
            if (debug_lexer) {
                havel::debug("LEX: {}", currentTokens.back().toString());
            }
            continue;
        }
        // Context-aware: if previous token suggests expression context, treat as
        // bitwise AND operator rather than hotkey
        if (!currentTokens.empty()) {
            TokenType prevType = currentTokens.back().type;
            if (prevType == TokenType::Number ||
                prevType == TokenType::Identifier ||
                prevType == TokenType::String ||
                prevType == TokenType::InterpolatedString ||
                prevType == TokenType::MultilineString ||
                prevType == TokenType::RegexString ||
                prevType == TokenType::CloseParen ||
                prevType == TokenType::CloseBracket ||
                prevType == TokenType::Not ||
                prevType == TokenType::Or ||
                prevType == TokenType::And ||
                prevType == TokenType::Assign ||
                prevType == TokenType::BitwiseOr ||
                prevType == TokenType::BitwiseXor ||
            prevType == TokenType::BitwiseAnd ||
            prevType == TokenType::ShiftLeft ||
            prevType == TokenType::ShiftRight) {
        currentTokens.push_back(makeToken("&", TokenType::BitwiseAnd));
                if (debug_lexer) {
                    havel::debug("LEX: {}", currentTokens.back().toString());
                }
                continue;
            }
        }
        currentTokens.push_back(scanHotkey());
    continue;
}

// Handle modifier-based hotkeys starting with special characters like ^ + !
    // @ ~ $ — but inside (( )), ^ is bitwise XOR and ~ is bitwise NOT
    if (c == '^' && inBitwiseExpr) {
        currentTokens.push_back(makeToken("^", TokenType::BitwiseXor));
        if (debug_lexer) {
            havel::debug("LEX: {}", currentTokens.back().toString());
        }
        continue;
    }
  if (c == '^' || c == '!' || c == '+' || c == '@' || c == '~' || c == '$' || c == '|') {
 // Special case: !{ for unsorted object literals
 if (c == '!' && peek() == '{') {
 advance(); // consume '{'
 currentTokens.push_back(makeToken("!{", TokenType::BangOpenBrace));
 if (debug_lexer) {
 havel::debug("LEX: {}", currentTokens.back().toString());
 }
 continue;
 }
  // Special case: ! followed by _ or lowercase letter is NOT operator,
  // not a hotkey modifier (e.g., !_m, !x vs !F1, !Esc)
  // UNLESS it's followed by => which indicates a hotkey binding
  bool isHotkeyBinding = false;
  if (c == '!' && (peek() == '_' || (peek() != 0 && std::islower(static_cast<unsigned char>(peek()))))) {
    if (isHotkeyLookahead()) {
      isHotkeyBinding = true;
    }
 }

 if (c == '!' && !isHotkeyBinding && (peek() == '_' || (peek() != 0 && std::islower(static_cast<unsigned char>(peek()))))) {
   // Fall through to SINGLE_CHAR_TOKENS to get Not token
 } else if (c == '~' && inBitwiseExpr) {
   // Fall through to SINGLE_CHAR_TOKENS for Tilde
     } else if ((c == '+' || c == '!' || c == '~' || c == '^' || c == '@' || c == '|') && !currentTokens.empty()) {
       TokenType prevType = currentTokens.back().type;

       // After Assign, check if modifier+key is a hotkey binding
       if (prevType == TokenType::Assign) {
         bool isHotkey = false;
         if (c == '!') {
           isHotkey = isHotkeyBinding;
         } else if ((c == '+' || c == '~' || c == '^' || c == '|') && !isAtEnd() && isHotkeyLookahead()) {
           isHotkey = true;
         }
         if (isHotkey) {
           currentTokens.push_back(scanHotkey());
           if (debug_lexer) {
             havel::debug("LEX: {}", currentTokens.back().toString());
           }
           continue;
         }
       }

       // If previous token suggests expression context, treat as operator
       // Exclude CloseBrace - after } we're at statement level (could be
       // hotkey) Include statement starters that are followed by expressions
       // (if, while, for, etc.)
       // Also: keyword tokens after Dot are property accesses (x.mode + y),
       // so check if token-before-previous is Dot
       bool prevIsKeywordAfterDot = (currentTokens.size() >= 2 &&
         currentTokens[currentTokens.size() - 2].type == TokenType::Dot);
       if (prevType == TokenType::Number ||
           prevType == TokenType::Identifier ||
           prevType == TokenType::String ||
           prevType == TokenType::InterpolatedString ||
           prevType == TokenType::MultilineString ||
           prevType == TokenType::RegexString ||
           prevType == TokenType::CloseParen ||
           prevType == TokenType::OpenParen ||
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
           prevType == TokenType::Tilde ||
           prevType == TokenType::Comma ||
           prevType == TokenType::Dot ||
           prevType == TokenType::BitwiseOr ||
           prevType == TokenType::BitwiseXor ||
           prevType == TokenType::BitwiseAnd ||
           prevType == TokenType::ShiftLeft ||
           prevType == TokenType::ShiftRight ||
           prevType == TokenType::LeftArrow ||
            prevType == TokenType::Minus ||
            prevType == TokenType::Fn ||
            prevType == TokenType::Op ||
            prevType == TokenType::Mode ||
            prevType == TokenType::When ||
            prevType == TokenType::Repeat ||
            prevType == TokenType::On ||
            prevType == TokenType::Off ||
            prevType == TokenType::Match ||
            prevType == TokenType::Case ||
            prevType == TokenType::Default ||
            prevType == TokenType::Config ||
            prevType == TokenType::Devices ||
            prevType == TokenType::Modes ||
            prevType == TokenType::Pool ||
            prevType == TokenType::Struct ||
            prevType == TokenType::Class ||
            prevType == TokenType::Enum ||
            prevType == TokenType::Trait ||
            prevType == TokenType::Prot ||
            prevType == TokenType::Impl ||
            prevType == TokenType::Is ||
            prevType == TokenType::As ||
            prevIsKeywordAfterDot) {
		if (c == '^') {
        // ^ is binary XOR (needs left-hand operand).
        // After Assign case handled above; here it's a real operator.
        currentTokens.push_back(makeToken("^", TokenType::BitwiseXor));
        if (debug_lexer) {
          havel::debug("LEX: {}", currentTokens.back().toString());
        }
        continue;
      }
			if (c == '@') {
				currentTokens.push_back(makeToken("@", TokenType::At));
				if (debug_lexer) {
					havel::debug("LEX: {}", currentTokens.back().toString());
				}
				continue;
			}
			// Fall through to SINGLE_CHAR_TOKENS to get Plus, Not, or Tilde
   } else {
     currentTokens.push_back(scanHotkey());
     continue;
   }
 } else {
   currentTokens.push_back(scanHotkey());
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
                currentTokens.push_back(makeToken(std::string(1, c), singleCharIt->second));
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
    // Check if this might be a hotkey starting with F (F1..F24)
    if (c == 'F' && isDigit(peek())) {
      // Could be F-key hotkey, but check if it's followed by assignment or
      // other non-hotkey syntax If next non-digit char is '=' or
      // whitespace+identifier, treat as identifier
      size_t lookahead = 1;
      while (position + lookahead < source.length() &&
             isDigit(source[position + lookahead])) {
        lookahead++;
      }
      if (position + lookahead < source.length()) {
        char after = source[position + lookahead];
        if (after == '=' || after == ';' || after == ',') {
          currentTokens.push_back(scanIdentifier());
          continue;
        }
        if (after == ' ' || after == '\t') {
          currentTokens.push_back(scanIdentifier());
          continue;
        }
      } else {
        // End of input - treat as identifier (e.g., standalone F1)
        currentTokens.push_back(scanIdentifier());
        continue;
      }
      currentTokens.push_back(scanHotkey());
    } else {
      currentTokens.push_back(scanIdentifier());
    }
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
    currentTokens.push_back(makeToken("EndOfFile", TokenType::EOF_TOKEN));

  return currentTokens;
}

void Lexer::printTokens(const std::vector<Token> &tokens) const {
    havel::debug("=== HAVEL TOKENS ===");
    for (size_t i = 0; i < currentTokens.size(); ++i) {
        havel::debug("[{}] {}", i, tokens[i].toString());
    }
    havel::debug("===================");
}

bool Lexer::isSoftIdentifier(TokenType t) {
  return t == TokenType::Identifier ||
         t == TokenType::On ||
         t == TokenType::Off ||
         t == TokenType::When ||
         t == TokenType::Mode ||
         t == TokenType::Val ||
         t == TokenType::Class ||
         t == TokenType::Struct ||
         t == TokenType::Enum ||
         t == TokenType::Trait ||
         t == TokenType::Prot ||
         t == TokenType::Repeat ||
         t == TokenType::Go ||
         t == TokenType::Thread ||
         t == TokenType::Timeout ||
         t == TokenType::Interval ||
         t == TokenType::Channel ||
         t == TokenType::Sync ||
         t == TokenType::Async ||
         t == TokenType::Wait ||
         t == TokenType::WaitGroup ||
         t == TokenType::Defer ||
         t == TokenType::Co ||
         t == TokenType::Yield ||
         t == TokenType::Pool ||
         t == TokenType::Config ||
         t == TokenType::Devices ||
         t == TokenType::Modes ||
         t == TokenType::Update;
}

} // namespace havel
