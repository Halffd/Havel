#include "Parser.h"
#include "../utils/Logger.hpp"
#include <iostream>
#include <sstream>

using namespace havel;
using enum havel::TokenType;

namespace havel::parser {

static double parseNumberLiteral(const std::string& s) {
    if (s.size() >= 2 && s[0] == '0') {
        if (s[1] == 'x' || s[1] == 'X')
            return static_cast<double>(std::stoll(s, nullptr, 0));
        if (s[1] == 'o' || s[1] == 'O')
            return static_cast<double>(std::stoll(s.substr(2), nullptr, 8));
        if (s[1] == 'b' || s[1] == 'B')
            return static_cast<double>(std::stoll(s.substr(2), nullptr, 2));
    }
    return std::stod(s);
}

static bool hasDecimalPart(const std::string& s) {
    return s.find('.') != std::string::npos ||
           s.find('e') != std::string::npos ||
           s.find('E') != std::string::npos;
}

void Parser::reportError(const std::string &message) {
  CompilerError err(ErrorSeverity::Error, at().line, at().column, message);
  errors.push_back(err);

  // Also report to unified ErrorReporter
  errors::ErrorReporter::instance().errorAt(
      ::havel::errors::ErrorStage::Parser, message,
      at().line, at().column, at().length);
}

void Parser::reportErrorAt(const Token &token, const std::string &message) {
  CompilerError err(ErrorSeverity::Error, token.line, token.column, message);
  errors.push_back(err);

  // Also report to unified ErrorReporter
  errors::ErrorReporter::instance().errorAt(
      ::havel::errors::ErrorStage::Parser, message,
      token.line, token.column, token.length);
}

// ============================================================================
// PARSER SAFETY CHECKS
// ============================================================================

void Parser::checkTokenLimit() {
  if (tokens_consumed_ > MAX_PARSER_TOKENS) {
    throw ParseError(at().line, at().column,
      "parser timeout: exceeded " + std::to_string(MAX_PARSER_TOKENS) + " tokens (possible infinite loop)");
  }
}

void Parser::checkParseLoop(int &counter, const char* context) {
  if (++counter > 100000) {
    size_t line = position < tokens.size() ? tokens[position].line : 0;
    throw ParseError(line, position < tokens.size() ? tokens[position].column : 0,
      std::string("parse loop exceeded 100000 iterations in ") + context +
      " (possible infinite loop)");
  }
}

std::unique_ptr<ast::Identifier> Parser::makeIdentifier(const Token &token) {
  return std::make_unique<ast::Identifier>(token.value, token.line,
                                           token.column);
}

void Parser::synchronize() {
  // Panic mode recovery - skip tokens until we find a statement boundary
  advance(); // Skip the current token that caused the error

  while (notEOF()) {
    havel::TokenType type = at().type;

    // Statement boundaries
    if (type == havel::TokenType::Semicolon ||
        type == havel::TokenType::NewLine) {
      advance();
      return;
    }

    // Statement starters
    if (type == havel::TokenType::Let || type == havel::TokenType::Fn ||
        type == havel::TokenType::If || type == havel::TokenType::While ||
        type == havel::TokenType::For || type == havel::TokenType::Return ||
        type == havel::TokenType::Break || type == havel::TokenType::Continue ||
        type == havel::TokenType::Config || type == havel::TokenType::Devices ||
        type == havel::TokenType::Modes) {
      return;
    }

    // Block boundaries
    if (type == havel::TokenType::CloseBrace) {
      return;
    }

    advance();
  }
}

void Parser::synchronizeTo(havel::TokenType type) {
  // Skip tokens until we find the specified type
  while (notEOF() && at().type != type) {
    advance();
  }
  if (notEOF()) {
    advance(); // Consume the target token
  }
}

[[noreturn]] void Parser::fail(const std::string &message) {
  failAt(at(), message);
}

[[noreturn]] void Parser::failAt(const havel::Token &token,
                                 const std::string &message) {
  throw havel::parser::ParseError(token.line, token.column, message,
                                  token.length == 0 ? 1 : token.length);
}

// Record error without throwing - for error recovery
void Parser::errorAt(const havel::Token &token, const std::string &message) {
  CompilerError err(ErrorSeverity::Error, token.line, token.column, message);
  errors.push_back(err);
}

const havel::Token &Parser::at(size_t offset) const {
  // Sanity check - offset should never be huge (indicates memory corruption)
  if (offset > 10000) {
        havel::fatal("Parser::at() called with invalid offset: {} (position={}, tokens.size()={})",
                     offset, position, tokens.size());
    std::abort();
  }

  size_t pos = position + offset;
  static const havel::Token eofToken("EOF", havel::TokenType::EOF_TOKEN, "EOF",
                                     0, 0);
  if (pos >= tokens.size()) {
    return eofToken;
  }
  return tokens[pos];
}

const havel::Token &Parser::advance() {
  static const havel::Token eofToken("EOF", havel::TokenType::EOF_TOKEN, "EOF",
                                     0, 0);
  if (position >= tokens.size()) {
    return eofToken;
  }
  tokens_consumed_++;
  if (tokens_consumed_ > MAX_PARSER_TOKENS) {
    throw ParseError(tokens[position].line, tokens[position].column,
      "parser timeout: exceeded " + std::to_string(MAX_PARSER_TOKENS) + " tokens (possible infinite loop)");
  }
  return tokens[position++];
}

bool Parser::notEOF() const {
  // Don't call at() to avoid potential recursion - check directly
  return position < tokens.size() &&
         tokens[position].type != havel::TokenType::EOF_TOKEN;
}

// Lookahead helper to detect destructuring patterns like {a, b} = obj or {a: b}
// = obj
bool Parser::isDestructuringPattern() const {
  // Must start with OpenBrace
  if (at().type != havel::TokenType::OpenBrace) {
    return false;
  }

  size_t offset = 1; // Skip past the '{'

  // Look for pattern: { identifier (',' | ':' | '}') ... '='
  while (true) {
    // Check for EOF
    if (position + offset >= tokens.size()) {
      return false;
    }

    const Token &token = tokens[position + offset];

    // End of object literal - check what comes after
    if (token.type == havel::TokenType::CloseBrace) {
      // Look for '=' after the closing brace (skipping newlines)
      size_t afterBrace = offset + 1;
      while (position + afterBrace < tokens.size()) {
        const Token &nextToken = tokens[position + afterBrace];
        if (nextToken.type == havel::TokenType::NewLine) {
          afterBrace++;
          continue;
        }
        if (nextToken.type == havel::TokenType::Assign) {
          return true; // Found { ... } = pattern
        }
        // Any other token means it's not a destructuring assignment
        return false;
      }
      return false;
    }

    // Statement boundary - not a destructuring pattern
    if (token.type == havel::TokenType::Semicolon ||
        token.type == havel::TokenType::EOF_TOKEN) {
      return false;
    }

    // First token inside brace must be an identifier for shorthand
    // or we could have {identifier} or {identifier: ...}
    if (offset == 1) {
      // First element must be an identifier
      if (token.type != havel::TokenType::Identifier) {
        return false;
      }
    } else {
      // After first element, we expect comma, colon, or closing brace
      if (token.type == havel::TokenType::Comma) {
        offset++;
        // After comma, expect another identifier
        if (position + offset >= tokens.size()) {
          return false;
        }
        const Token &nextToken = tokens[position + offset];
        if (nextToken.type != havel::TokenType::Identifier) {
          return false;
        }
      } else if (token.type == havel::TokenType::Colon) {
        // {key: value} pattern - skip the value (could be complex)
        offset++;
        // Skip until we find comma or closing brace
        int depth = 1;
        while (position + offset < tokens.size()) {
          const Token &t = tokens[position + offset];
          if (t.type == havel::TokenType::OpenBrace ||
              t.type == havel::TokenType::OpenParen ||
              t.type == havel::TokenType::OpenBracket) {
            depth++;
          } else if (t.type == havel::TokenType::CloseBrace) {
            depth--;
            if (depth == 0)
              break;
          } else if (t.type == havel::TokenType::CloseParen ||
                     t.type == havel::TokenType::CloseBracket) {
            depth--;
          } else if (t.type == havel::TokenType::Comma && depth == 1) {
            break;
          }
          offset++;
        }
      } else if (token.type == havel::TokenType::CloseBrace) {
        continue; // Will be handled at top of loop
      } else if (token.type != havel::TokenType::Identifier) {
        // Something unexpected - probably not destructuring
        return false;
      }
    }

    offset++;

    // Safety limit to prevent infinite loops
    if (offset > 1000) {
      return false;
    }
  }

  return false;
}

// Lookahead helper to detect object literals vs block statements
// Object literal: {key: value, ...}
// Block statement: { statements }
bool Parser::isObjectLiteral() const {
  // Must start with OpenBrace
  if (at().type != havel::TokenType::OpenBrace) {
    return false;
  }

  size_t offset = 1; // Skip past the '{'

  // Skip newlines to find first significant token
  while (position + offset < tokens.size() &&
         tokens[position + offset].type == havel::TokenType::NewLine) {
    offset++;
  }

  // Empty braces {} is an object literal in expression context
  if (position + offset >= tokens.size()) {
    return true;
  }

  const Token &firstToken = tokens[position + offset];

  // If next token is '}', it's an empty object literal {}
  if (firstToken.type == havel::TokenType::CloseBrace) {
    return true;
  }

  // Object keys can be: identifier, string, number, or certain keywords
  // followed by ':'
  bool couldBeKey = (firstToken.type == havel::TokenType::Identifier ||
                     firstToken.type == havel::TokenType::String ||
                     firstToken.type == havel::TokenType::MultilineString ||
                     firstToken.type == havel::TokenType::Number ||
                     firstToken.type == havel::TokenType::Config ||
                     firstToken.type == havel::TokenType::Devices ||
                     firstToken.type == havel::TokenType::Modes ||
                     firstToken.type == havel::TokenType::Mode);

  if (!couldBeKey) {
    return false;
  }

  // Look ahead to see if there's a ':' after the key
  offset++;
  while (position + offset < tokens.size() &&
         tokens[position + offset].type == havel::TokenType::NewLine) {
    offset++;
  }

  if (position + offset >= tokens.size()) {
    return false;
  }

  // If we see ':' after a potential key, this is an object literal
  return tokens[position + offset].type == havel::TokenType::Colon;
}

// ============================================================================
// PRATT PARSER IMPLEMENTATION (Top-Down Operator Precedence)
// ============================================================================

int Parser::getBindingPower(TokenType type) const {
switch (type) {
// Assignment (right-associative, low precedence)
case TokenType::Assign:
case TokenType::PlusAssign:
case TokenType::MinusAssign:
case TokenType::MultiplyAssign:
case TokenType::DivideAssign:
case TokenType::ModuloAssign:
case TokenType::PowerAssign:
return 10;

// Nullish coalescing
case TokenType::Nullish:
      return 20;

    // Arrow (lambda) - low precedence like assignment
    // Return 0 inside match expressions so Pratt parser exits the loop
    // and lets parseMatchExpression handle the => as a match arm separator
    case TokenType::Arrow:
      if (context.inMatchExpression) {
        if (debug.parser) {
          havel::debug("BP: Arrow in match context, returning 0");
        }
        return 0;
      }
      return 10;

    // Logical OR
    case TokenType::Or:
      return 30;

    // Logical AND
    case TokenType::And:
      return 40;

    // Equality
    case TokenType::Equals:
    case TokenType::NotEquals:
    case TokenType::Is:
      return 50;

    // Membership (in / not in)
    case TokenType::In:
      return 55;

    // 'not' as infix for 'not in' - slightly lower than 'in' so 'not' binds to 'in'
    case TokenType::Not:
      return 52;

    // Comparison
    case TokenType::Less:
    case TokenType::Greater:
    case TokenType::LessEquals:
    case TokenType::GreaterEquals:
      return 60;

    // Range
    case TokenType::DotDot:
      return 70;

// Pipe
case TokenType::Pipe:
return 75;

// Bitwise OR
case TokenType::BitwiseOr:
return 80;

// Bitwise XOR
case TokenType::BitwiseXor:
return 90;

// Bitwise AND
case TokenType::BitwiseAnd:
return 100;

// Shift
case TokenType::ShiftLeft:
case TokenType::ShiftRight:
return 110;

// Additive
    case TokenType::Plus:
    case TokenType::Minus:
      return 70;

    // Multiplicative
    case TokenType::Multiply:
    case TokenType::Divide:
    case TokenType::Modulo:
    case TokenType::Backslash:
      return 130;

    // Power (right-associative)
    case TokenType::Power:
      return 135;

    // Postfix operators
    case TokenType::PlusPlus:
    case TokenType::MinusMinus:
      return 150;

    // Member access, optional chaining, calls, indexing
    case TokenType::Dot:
    case TokenType::QuestionDot:
    case TokenType::OpenParen:
    case TokenType::OpenBracket:
      return 170;

    // Ternary operator (?)
    case TokenType::Question:
      return 15;

    default:
      return 0;
  }
}

int Parser::getRightBindingPower(TokenType type) const {
  // For right-associative operators, return a lower right binding power
  switch (type) {
    case TokenType::Assign:
    case TokenType::PlusAssign:
    case TokenType::MinusAssign:
    case TokenType::MultiplyAssign:
    case TokenType::DivideAssign:
    case TokenType::ModuloAssign:
    case TokenType::PowerAssign:
      return 5;  // Right-associative: 10 - 1 = 9, but use lower for safety

    case TokenType::Arrow:
      return 5;  // Low right binding power for arrow body expressions

    case TokenType::Power:
      return 130;  // Right-associative: 135 - 5 = 130

    case TokenType::Nullish:
      return 15;  // Right-associative

    case TokenType::Question:
      return 10;  // Right-associative (lbp=15, rbp=10)

    default:
      return getBindingPower(type);
  }
}

// Optimized Pratt parser with lookup tables
namespace {
  // Token count - must be >= max TokenType enum value
  constexpr size_t TOKEN_TYPE_COUNT = 256;  // Power of 2 for cache efficiency
  
  // Binding power lookup table - initialized once at startup
  struct BindingPowerTables {
    std::array<uint8_t, TOKEN_TYPE_COUNT> left_bp{};   // Left binding power
    std::array<uint8_t, TOKEN_TYPE_COUNT> right_bp{}; // Right binding power (for right-assoc)
    std::array<bool, TOKEN_TYPE_COUNT> can_start{};    // Can token start expression?
    
    BindingPowerTables() {
      // Initialize all to zero/false
left_bp.fill(0);
right_bp.fill(0);
can_start.fill(false);

// Assignment operators (right-associative, lowest precedence)
setBoth(Assign, 10, 10);
setBoth(PlusAssign, 10, 10);
setBoth(MinusAssign, 10, 10);
setBoth(MultiplyAssign, 10, 10);
setBoth(DivideAssign, 10, 10);
setBoth(ModuloAssign, 10, 10);
setBoth(PowerAssign, 10, 10);

// Nullish coalescing
      setBoth(Nullish, 20, 20);
      
      // Arrow (lambda) - low precedence like assignment
      setBoth(Arrow, 10, 5);
      
      // Logical OR
      setBoth(Or, 30, 30);
      
      // Logical AND
      setBoth(And, 40, 40);
      
      // Equality
      setBoth(Equals, 50, 50);
      setBoth(NotEquals, 50, 50);
      setBoth(Is, 50, 50);  // Identity comparison
      
      // Relational
      setBoth(Less, 60, 60);
      setBoth(Greater, 60, 60);
      setBoth(LessEquals, 60, 60);
      setBoth(GreaterEquals, 60, 60);
      
      // Range
      setBoth(DotDot, 65, 66);  // Right associative
      
      // Additive
      setBoth(Plus, 70, 70);
      setBoth(Minus, 70, 70);
      
      // Multiplicative
      setBoth(Multiply, 80, 80);
      setBoth(Divide, 80, 80);
      setBoth(Modulo, 80, 80);
      
      // Power (right-associative)
      setBoth(Power, 90, 91);
      
      // Prefix operators (handled in nud, not led)
        // Pipeline
        setBoth(Pipe, 35, 35);

        // Bitwise operators (inside (( )))
        setBoth(BitwiseOr, 80, 80);
        setBoth(BitwiseXor, 90, 90);
        setBoth(BitwiseAnd, 100, 100);
        setBoth(ShiftLeft, 110, 110);
        setBoth(ShiftRight, 110, 110);
      
      // Postfix operators (very high left binding power, 0 right)
      left_bp[static_cast<size_t>(PlusPlus)] = 100;
      left_bp[static_cast<size_t>(MinusMinus)] = 100;

      // Member access (highest)
      left_bp[static_cast<size_t>(Dot)] = 110;
      // Optional chaining ?. (same priority as Dot)
      left_bp[static_cast<size_t>(Question)] = 110;

      // Function call and index
      left_bp[static_cast<size_t>(OpenParen)] = 110;
      left_bp[static_cast<size_t>(OpenBracket)] = 110;
      
      // Tokens that can start expressions
      can_start[static_cast<size_t>(Number)] = true;
      can_start[static_cast<size_t>(String)] = true;
      can_start[static_cast<size_t>(MultilineString)] = true;
      can_start[static_cast<size_t>(InterpolatedString)] = true;
  can_start[static_cast<size_t>(InterpolatedBacktick)] = true;
      can_start[static_cast<size_t>(Identifier)] = true;
      can_start[static_cast<size_t>(True)] = true;
      can_start[static_cast<size_t>(False)] = true;
      can_start[static_cast<size_t>(Null)] = true;
      can_start[static_cast<size_t>(OpenParen)] = true;
      can_start[static_cast<size_t>(OpenBracket)] = true;
      can_start[static_cast<size_t>(OpenBrace)] = true;
      can_start[static_cast<size_t>(Fn)] = true;
      can_start[static_cast<size_t>(Match)] = true;
      can_start[static_cast<size_t>(If)] = true;
      can_start[static_cast<size_t>(Not)] = true;
      can_start[static_cast<size_t>(Minus)] = true;
      can_start[static_cast<size_t>(Plus)] = true;
      can_start[static_cast<size_t>(PlusPlus)] = true;
      can_start[static_cast<size_t>(MinusMinus)] = true;
      can_start[static_cast<size_t>(Length)] = true;
      can_start[static_cast<size_t>(Spread)] = true;
        can_start[static_cast<size_t>(Hash)] = true;
        can_start[static_cast<size_t>(Backtick)] = true;
        can_start[static_cast<size_t>(DoubleOpenParen)] = true;
        can_start[static_cast<size_t>(Tilde)] = true;
    }
    
    void setBoth(TokenType t, uint8_t lbp, uint8_t rbp) {
      size_t idx = static_cast<size_t>(t);
      if (idx < TOKEN_TYPE_COUNT) {
        left_bp[idx] = lbp;
        right_bp[idx] = rbp;
      }
    }
  };
  
  // Static singleton - initialized once at program startup
  const BindingPowerTables BP_TABLES;
}

bool Parser::canStartExpression(TokenType type) const {
  size_t idx = static_cast<size_t>(type);
  return idx < TOKEN_TYPE_COUNT ? BP_TABLES.can_start[idx] : false;
}

bool Parser::isInfixOperator(TokenType type) const {
  return BP_TABLES.left_bp[static_cast<size_t>(type)] > 0;
}

// Note: getBindingPower and getRightBindingPower now use lookup tables (see inline definitions below)

std::unique_ptr<ast::Expression> Parser::parsePrattExpression(int rbp) {
  DepthGuard depth_guard(recursion_depth_);
  if (debug.parser) {
    havel::debug("PRATT: parseExpression START rbp={} at {}", rbp, at().toString());
  }

  // Get the first token (null denotation)
  Token start_token = at();
  Token token = advance();

  std::unique_ptr<ast::Expression> left;
  try {
    left = nud(token);
  } catch (const ParseError &e) {
    reportErrorAt(token, e.what());
    synchronize();
    return nullptr;
  }

  if (!left) {
    return nullptr;
  }

  // Assign source span to the base expression
  if (left->line == 0 || left->line == start_token.line) {
    left->line = start_token.line;
    left->column = start_token.column;
    left->length = start_token.length;
  }

  // While the next token has higher binding power than our right binding power
  // Guard against infinite loops from malformed binding power tables
  int infixIterations = 0;
  while (rbp < getBindingPower(at().type)) {
    infixIterations++;
    if (infixIterations > 10000) {
      throw std::runtime_error("Pratt infix loop exceeded at token " + std::to_string(position) + ": " + at().toString() + " (rbp=" + std::to_string(rbp) + ", bp=" + std::to_string(getBindingPower(at().type)) + ")");
    }
    Token op_token = at();
    try {
      advance(); // consume the operator
      auto next = led(op_token, std::move(left));
      if (!next) {
        return nullptr;
      }
      left = std::move(next);
    } catch (const ParseError &e) {
      reportErrorAt(op_token, e.what());
      synchronize();
      return left; // Return what we have so far
    }
    
    // Update span for the compound expression
    if (left->line == start_token.line) {
       // Single-line expression width
       left->length = (op_token.column + op_token.length) - start_token.column;
    } else {
       // Just fallback to the operator's length if multiline is unsupported here
       left->length = op_token.length;
    }
  }
  
  // Implicit call sugar: expr "string" -> expr("string")
  // This handles function calls without parentheses like: print "hello"
  // Also handles: print a ** 5 -> print(a ** 5)
    if (context.allowBraceSugar &&
        (at().type == TokenType::String ||
         at().type == TokenType::MultilineString ||
         at().type == TokenType::Number ||
         at().type == TokenType::Identifier ||
         at().type == TokenType::InterpolatedString ||
         at().type == TokenType::InterpolatedBacktick)) {
    Token arg_token = at();
    // Parse argument as full expression (with operators), but disable nested implicit calls
    // to prevent infinite recursion and ensure operators like ** bind tighter than the call
    bool prevAllow = context.allowBraceSugar;
    context.allowBraceSugar = false;
    auto arg = parsePrattExpression(0);
    context.allowBraceSugar = prevAllow;

    if (!arg) {
      return nullptr;
    }

    std::vector<std::unique_ptr<ast::Expression>> args;
    args.push_back(std::move(arg));
    
    // Support comma-separated arguments: print a, b, c -> print(a, b, c)
    while (at().type == TokenType::Comma) {
      advance(); // consume comma
      auto nextArg = parsePrattExpression(0);
      if (!nextArg) {
        errorAt(at(), "Expected expression after comma in function call");
        return nullptr;
      }
      args.push_back(std::move(nextArg));
    }
    
    auto call_expr = std::make_unique<ast::CallExpression>(std::move(left), std::move(args));
    call_expr->line = start_token.line;
    call_expr->column = start_token.column;
    call_expr->length = (arg_token.column + arg_token.length) - start_token.column;
    left = std::move(call_expr);
  }

  return left;
}

// Null denotation - parse token at start of expression
std::unique_ptr<ast::Expression> Parser::nud(const Token &token) {
  if (debug.parser) {
    havel::debug("PRATT: nud for {}", token.toString());
  }

  switch (token.type) {
case TokenType::Number:
        return std::make_unique<ast::NumberLiteral>(parseNumberLiteral(token.value), hasDecimalPart(token.value));

    case TokenType::String:
    case TokenType::MultilineString:
      return std::make_unique<ast::StringLiteral>(token.value);

    case TokenType::CharLiteral:
      return std::make_unique<ast::CharLiteral>(token.value[0]);

    case TokenType::InterpolatedString: {
      // Parse interpolated string into segments
      std::vector<ast::InterpolatedStringExpression::Segment> segments;
      const std::string &value = token.value;
      size_t pos = 0;
      std::string currentLiteral;
      
      while (pos < value.length()) {
        // Check for ${...} pattern (regular interpolated strings)
        if (value[pos] == '$' && pos + 1 < value.length() && value[pos + 1] == '{') {
          // Found interpolation start: ${
          if (!currentLiteral.empty()) {
            segments.emplace_back(currentLiteral);
            currentLiteral.clear();
          }
          pos += 2; // skip ${
          
          // Find matching } (accounting for nested braces)
          size_t braceDepth = 1;
          size_t exprStart = pos;
          while (pos < value.length() && braceDepth > 0) {
            if (value[pos] == '{') braceDepth++;
            else if (value[pos] == '}') braceDepth--;
            if (braceDepth > 0) pos++;
          }
          
          if (braceDepth == 0) {
            // Extract expression string (excluding closing })
            std::string exprStr = value.substr(exprStart, pos - exprStart);
            // Parse the expression
            auto expr = parseExpressionFromString(exprStr);
            if (expr) {
              segments.emplace_back(std::move(expr));
            }
            pos++; // skip }
          } else {
            // Unclosed interpolation - treat rest as literal
            currentLiteral += value.substr(exprStart - 2);
            break;
          }
        }
        // Check for {...} pattern (f-strings) - but not ${ which is handled above
        else if (value[pos] == '{' && !(pos > 0 && value[pos-1] == '$')) {
          // Found f-string interpolation start: {
          if (!currentLiteral.empty()) {
            segments.emplace_back(currentLiteral);
            currentLiteral.clear();
          }
          pos += 1; // skip {
          
          // Find matching } (accounting for nested braces)
          size_t braceDepth = 1;
          size_t exprStart = pos;
          while (pos < value.length() && braceDepth > 0) {
            if (value[pos] == '{') braceDepth++;
            else if (value[pos] == '}') braceDepth--;
            if (braceDepth > 0) pos++;
          }
          
          if (braceDepth == 0) {
            // Extract expression string (excluding closing })
            std::string exprStr = value.substr(exprStart, pos - exprStart);
            // Parse the expression
            auto expr = parseExpressionFromString(exprStr);
            if (expr) {
              segments.emplace_back(std::move(expr));
            }
            pos++; // skip }
          } else {
            // Unclosed interpolation - treat rest as literal
            currentLiteral += value.substr(exprStart - 1);
            break;
          }
        } else {
          currentLiteral += value[pos++];
        }
      }
      
      // Add remaining literal
      if (!currentLiteral.empty()) {
        segments.emplace_back(currentLiteral);
      }
      
    return std::make_unique<ast::InterpolatedStringExpression>(std::move(segments));
  }

  case TokenType::InterpolatedBacktick: {
    std::vector<ast::InterpolatedStringExpression::Segment> segments;
    const std::string &value = token.value;
    size_t pos = 0;
    std::string currentLiteral;

    while (pos < value.length()) {
      if (value[pos] == '$' && pos + 1 < value.length() && value[pos + 1] == '{') {
        if (!currentLiteral.empty()) {
          segments.emplace_back(currentLiteral);
          currentLiteral.clear();
        }
        pos += 2;
        size_t braceDepth = 1;
        size_t exprStart = pos;
        while (pos < value.length() && braceDepth > 0) {
          if (value[pos] == '{') braceDepth++;
          else if (value[pos] == '}') braceDepth--;
          if (braceDepth > 0) pos++;
        }
        if (braceDepth == 0) {
          std::string exprStr = value.substr(exprStart, pos - exprStart);
          auto expr = parseExpressionFromString(exprStr);
          if (expr) {
            segments.emplace_back(std::move(expr));
          }
          pos++;
        } else {
          currentLiteral += value.substr(exprStart - 2);
          break;
        }
      } else if (value[pos] == '{' && !(pos > 0 && value[pos-1] == '$')) {
        if (!currentLiteral.empty()) {
          segments.emplace_back(currentLiteral);
          currentLiteral.clear();
        }
        pos += 1;
        size_t braceDepth = 1;
        size_t exprStart = pos;
        while (pos < value.length() && braceDepth > 0) {
          if (value[pos] == '{') braceDepth++;
          else if (value[pos] == '}') braceDepth--;
          if (braceDepth > 0) pos++;
        }
        if (braceDepth == 0) {
          std::string exprStr = value.substr(exprStart, pos - exprStart);
          auto expr = parseExpressionFromString(exprStr);
          if (expr) {
            segments.emplace_back(std::move(expr));
          }
          pos++;
        } else {
          currentLiteral += value.substr(exprStart - 1);
          break;
        }
      } else {
        currentLiteral += value[pos++];
      }
    }

    if (!currentLiteral.empty()) {
      segments.emplace_back(currentLiteral);
    }

    auto interpExpr = std::make_unique<ast::InterpolatedStringExpression>(std::move(segments));
    auto shellExpr = std::make_unique<ast::ShellCommandExpression>(std::move(interpExpr), true);
    return shellExpr;
  }

    case TokenType::Identifier:
    case TokenType::Config:
    case TokenType::Devices:
      return makeIdentifier(token);

    case TokenType::Is: {
      errorAt(token, "'is' cannot start an expression - it must be used as infix operator (e.g., x is y)");
      return nullptr;
    }

    case TokenType::This: {
      // `this` keyword in expression context
      auto thisExpr = std::make_unique<ast::ThisExpression>();
      thisExpr->line = token.line;
      thisExpr->column = token.column;
      // Allow this to have postfix operations like .field access
      return parsePostfixExpression(std::move(thisExpr));
    }

    case TokenType::ColonColon: {
      if (at().type != TokenType::Identifier) {
        failAt(at(), "Expected identifier after '::'");
      }
      auto ident = makeIdentifier(advance());
      ident->isGlobalScope = true;
      return ident;
    }

    case TokenType::Colon: {
      // Fallback for lexers that emit '::' as two ':' tokens.
      if (at().type == TokenType::Colon && at(1).type == TokenType::Identifier) {
        advance(); // consume second ':'
        auto ident = makeIdentifier(advance());
        ident->isGlobalScope = true;
        return ident;
      }
      failAt(token, "Unexpected ':' in expression");
    }

    // Allow certain keywords to be used as identifiers in expression context
    // This enables expressions like: mode == "work" && class == "code"
    case TokenType::Class:
    case TokenType::Struct:
    case TokenType::Enum:
    case TokenType::Mode:
    case TokenType::Const:
    case TokenType::Let:
      return makeIdentifier(token);

    case TokenType::True:
      return std::make_unique<ast::BooleanLiteral>(true);

    case TokenType::False:
      return std::make_unique<ast::BooleanLiteral>(false);

    case TokenType::Null:
      return std::make_unique<ast::NullLiteral>();

    case TokenType::OpenParen:
      return parseParenthesizedExpression();

    case TokenType::OpenBracket:
      // parsePrattExpression already consumed '[', but parseArrayLiteral
      // expects to consume it. Back up one position.
      position--;
      return parseArrayLiteral();

    case TokenType::OpenBrace: {
      // parsePrattExpression already consumed '{', but parseObjectLiteral
      // expects to consume it. Back up one position.
      position--;
      // Now check if it's a set or object literal
      // Look ahead to determine which
      size_t lookahead = 1; // Skip the '{' token
      while (at(lookahead).type == havel::TokenType::NewLine) {
        lookahead++;
      }
      auto nextTok = at(lookahead);
      
      // Empty braces {} = empty object literal
      if (nextTok.type == havel::TokenType::CloseBrace) {
        return parseObjectLiteral();
      }
      
      // Check if it's an object literal (identifier/string followed by ':')
      bool isObject = false;
      auto isObjKeyType = [](havel::TokenType t) {
        return t == havel::TokenType::Identifier ||
               t == havel::TokenType::String ||
               t == havel::TokenType::MultilineString ||
               t == havel::TokenType::Config ||
               t == havel::TokenType::Devices ||
               t == havel::TokenType::Modes ||
               t == havel::TokenType::Mode ||
               t == havel::TokenType::Timeout ||
               t == havel::TokenType::Thread ||
               t == havel::TokenType::Interval ||
               t == havel::TokenType::Channel ||
               t == havel::TokenType::On ||
               t == havel::TokenType::Off ||
               t == havel::TokenType::Go ||
               t == havel::TokenType::When ||
               t == havel::TokenType::Class ||
               t == havel::TokenType::Struct ||
               t == havel::TokenType::Enum ||
               t == havel::TokenType::Fn ||
               t == havel::TokenType::If ||
               t == havel::TokenType::For ||
               t == havel::TokenType::Loop ||
               t == havel::TokenType::While ||
               t == havel::TokenType::Switch ||
               t == havel::TokenType::Do ||
               t == havel::TokenType::Return ||
               t == havel::TokenType::Ret ||
               t == havel::TokenType::Break ||
               t == havel::TokenType::Continue ||
               t == havel::TokenType::Let ||
               t == havel::TokenType::Const ||
               t == havel::TokenType::Try ||
               t == havel::TokenType::Catch ||
               t == havel::TokenType::Finally ||
               t == havel::TokenType::Throw ||
               t == havel::TokenType::Del ||
               t == havel::TokenType::True ||
               t == havel::TokenType::False ||
               t == havel::TokenType::Null ||
               t == havel::TokenType::Repeat;
      };
      if (isObjKeyType(nextTok.type)) {
        size_t colonLookahead = lookahead + 1;
        while (at(colonLookahead).type == havel::TokenType::NewLine) {
          colonLookahead++;
        }
        if (at(colonLookahead).type == havel::TokenType::Colon) {
          isObject = true;
        }
      }
      
      if (isObject) {
        return parseObjectLiteral();
      }
      
      // Check if it looks like a set literal
      bool couldBeSet = (nextTok.type == havel::TokenType::Identifier ||
                    nextTok.type == havel::TokenType::String ||
                    nextTok.type == havel::TokenType::MultilineString ||
                    nextTok.type == havel::TokenType::Number ||
                    nextTok.type == havel::TokenType::OpenBracket ||
                    nextTok.type == havel::TokenType::OpenParen ||
                    nextTok.type == havel::TokenType::OpenBrace ||
                    nextTok.type == havel::TokenType::Minus ||
                    nextTok.type == havel::TokenType::Not ||
                    nextTok.type == havel::TokenType::Plus ||
                    nextTok.type == havel::TokenType::Length ||
                    nextTok.type == havel::TokenType::True ||
                    nextTok.type == havel::TokenType::False ||
                    nextTok.type == havel::TokenType::Null);
      
      if (couldBeSet) {
        // Look ahead for comma
        size_t setLookahead = lookahead + 1;
        while (setLookahead < tokens.size() && 
               at(setLookahead).type != havel::TokenType::Comma &&
               at(setLookahead).type != havel::TokenType::Semicolon &&
               at(setLookahead).type != havel::TokenType::CloseBrace &&
               at(setLookahead).type != havel::TokenType::NewLine) {
          setLookahead++;
        }
        
        if (at(setLookahead).type == havel::TokenType::Comma) {
          // Parse as set literal
          advance(); // consume '{'
          
          std::vector<std::unique_ptr<havel::ast::Expression>> elements;
          
          while (notEOF() && at().type != havel::TokenType::CloseBrace) {
            while (at().type == havel::TokenType::NewLine) {
              advance();
            }
            if (at().type == havel::TokenType::CloseBrace) {
              break;
            }
            
            auto element = parseExpression();
            elements.push_back(std::move(element));
            
            while (at().type == havel::TokenType::NewLine) {
              advance();
            }
            
            if (at().type == havel::TokenType::Comma) {
              advance();
            } else if (at().type != havel::TokenType::CloseBrace) {
              failAt(at(), "Expected ',' or '}' in set literal");
            }
          }
          
          if (at().type != havel::TokenType::CloseBrace) {
            failAt(at(), "Expected '}' after set literal");
          }
          advance(); // consume '}'
          
          return std::make_unique<havel::ast::SetExpression>(std::move(elements));
        }
      }
      
      // Default to object literal
      return parseObjectLiteral();
    }

    case TokenType::Fn:
      return parseLambdaExpression();

    case TokenType::Match:
      return parseMatchExpression();

    case TokenType::If:
      return parseIfExpression();

    case TokenType::Not: {
      // Check if this is !{} for unsorted object
      if (at().type == havel::TokenType::OpenBrace) {
        // Don't consume '{' - parseObjectLiteral expects to consume it
        auto obj = parseObjectLiteral(true); // true = unsorted
        if (!obj) return nullptr;
        return parsePostfixExpression(std::move(obj));
      }
      auto operand = parsePrattExpression(bp(BindingPower::Prefix));
      return std::make_unique<ast::UnaryExpression>(
          ast::UnaryExpression::UnaryOperator::Not, std::move(operand));
    }

    case TokenType::Minus: {
      auto operand = parsePrattExpression(bp(BindingPower::Prefix));
      return std::make_unique<ast::UnaryExpression>(
          ast::UnaryExpression::UnaryOperator::Minus, std::move(operand));
    }

    case TokenType::Plus: {
      // Unary plus - just return the operand
      return parsePrattExpression(bp(BindingPower::Prefix));
    }

    case TokenType::PlusPlus: {
      // Prefix increment
      auto operand = parsePrattExpression(bp(BindingPower::Prefix));
      return std::make_unique<ast::UpdateExpression>(
          std::move(operand), ast::UpdateExpression::Operator::Increment, true);
    }

    case TokenType::MinusMinus: {
      // Prefix decrement
      auto operand = parsePrattExpression(bp(BindingPower::Prefix));
      return std::make_unique<ast::UpdateExpression>(
          std::move(operand), ast::UpdateExpression::Operator::Decrement, true);
    }

    case TokenType::Length: {
      // #length operator
      auto operand = parsePrattExpression(bp(BindingPower::Prefix));
      return std::make_unique<ast::UnaryExpression>(
          ast::UnaryExpression::UnaryOperator::Length, std::move(operand));
    }

    case TokenType::At: {
      // @field - field access within class methods
      if (at().type != TokenType::Identifier) {
        errorAt(at(), "Expected field name after '@'");
        return nullptr;
      }
      auto fieldName = makeIdentifier(advance());
      return std::make_unique<ast::AtExpression>(std::move(fieldName));
    }

    case TokenType::AtAt: {
      // @@field - class field access within class methods
      if (at().type != TokenType::Identifier) {
        errorAt(at(), "Expected field name after '@@'");
        return nullptr;
      }
      auto fieldName = makeIdentifier(advance());
      return std::make_unique<ast::AtAtExpression>(std::move(fieldName));
    }

    case TokenType::SuperArrow: {
      // @->method() - super call for prototype inheritance
      // Parse method name after @->
      if (at().type != TokenType::Identifier) {
        errorAt(at(), "Expected method name after '@->'");
        return nullptr;
      }
      auto methodName = makeIdentifier(advance());
      // Create a special SuperCallExpression (reuse CallExpression with isSuper
      // flag)
      auto call = std::make_unique<havel::ast::CallExpression>(
          std::make_unique<havel::ast::Identifier>("__super__"));
      call->isSuperCall = true;
      call->superMethodName = methodName->symbol;

      // Parse arguments if present
      if (at().type == TokenType::OpenParen) {
        advance(); // consume '('
        while (at().type != TokenType::CloseParen && notEOF()) {
          while (at().type == TokenType::NewLine) advance();
          if (at().type == TokenType::CloseParen) break;
          call->args.push_back(parsePrattExpression(0));
          while (at().type == TokenType::NewLine) advance();
          if (at().type == TokenType::Comma) advance();
          else if (at().type != TokenType::CloseParen) {
            errorAt(at(), "Expected ',' or ')' in super call arguments");
            return nullptr;
          }
        }
        if (at().type != TokenType::CloseParen) {
          errorAt(at(), "Expected ')' after super call arguments");
          return nullptr;
        }
        advance();
      }
      return std::move(call);
    }

    case TokenType::Spread: {
      // Spread expression: ...array
      // Use same binding power as other prefix operators
      auto operand = parsePrattExpression(bp(havel::parser::BindingPower::Prefix));
      return std::make_unique<ast::SpreadExpression>(std::move(operand));
    }

  case TokenType::Backtick:
    return std::make_unique<ast::BacktickExpression>(token.value);

    // Concurrency Primitives
    case TokenType::Thread:
      return parseThreadExpression();

    case TokenType::Interval:
      return parseIntervalExpression();

    case TokenType::Timeout:
      return parseTimeoutExpression();

    // Coroutines
    case TokenType::Yield:
      return parseYieldExpression();

    case TokenType::Channel:
      return parseChannelExpression();

    case TokenType::Go:
      return parseGoExpression();

    case TokenType::Hotkey:
      return parseHotkeyExpression(token);

        case TokenType::Tilde: {
            if (at().type == TokenType::Identifier ||
                at().type == TokenType::Hotkey) {
                return parseTildeHotkeyExpression();
            }
            auto operand = parsePrattExpression(bp(BindingPower::Prefix));
            return std::make_unique<ast::UnaryExpression>(
                ast::UnaryExpression::UnaryOperator::BitwiseNot, std::move(operand));
        }

    case TokenType::DoubleOpenParen: {
        auto inner = parsePrattExpression(bp(BindingPower::Assignment));
    if (at().type != TokenType::DoubleCloseParen) {
        errorAt(at(), "Expected '))' to close bitwise expression");
      } else {
        advance();
      }
      return inner;
    }

    default:
      errorAt(token, "Unexpected token in expression");
      return nullptr;
  }
}

// Parse ~identifier as hotkey with no-grab flag
// Called when nud(Tilde) sees identifier following the tilde
std::unique_ptr<havel::ast::Expression> Parser::parseTildeHotkeyExpression() {
  // We've already consumed '~', now get the identifier/hotkey that follows
  auto nextToken = at();
  advance(); // consume the identifier

  // Build the full hotkey string: ~identifier or ~identifier:timing etc
  std::string combo = "~" + nextToken.value;

  // Check for timing modifier (:100, :up, :down)
  if (at().type == TokenType::Colon) {
    advance(); // consume ':'
    if (at().type == TokenType::Number || at().type == TokenType::Identifier) {
      combo += ":" + at().value;
      advance();
    }
  }

  // Create a synthetic hotkey token and pass to parseHotkeyExpression
  Token syntheticHotkey(combo, TokenType::Hotkey, combo, nextToken.line, nextToken.column);
  return parseHotkeyExpression(syntheticHotkey);
}

// Left denotation - parse infix/postfix operators
std::unique_ptr<ast::Expression> Parser::led(const Token &token,
                                              std::unique_ptr<ast::Expression> left) {
  if (debug.parser) {
    havel::debug("PRATT: led for {} with left operand", token.toString());
  }

switch (token.type) {
    // Assignment operators (right-associative)
    case TokenType::Assign:
    case TokenType::PlusAssign:
    case TokenType::MinusAssign:
    case TokenType::MultiplyAssign:
    case TokenType::DivideAssign:
    case TokenType::ModuloAssign:
case TokenType::PowerAssign: {
        auto right = parsePrattExpression(getRightBindingPower(token.type));
        std::string op = token.value;
        
        // Check if left is an identifier and there might be more targets (comma-separated)
        // This handles: a, b, c = value
        // The comma case will collect targets and then hit '='
        return std::make_unique<ast::AssignmentExpression>(
            std::move(left), std::move(right), op, false);
    }

case TokenType::Plus: {
      auto right = parsePrattExpression(getRightBindingPower(token.type));
      return std::make_unique<ast::BinaryExpression>(
          std::move(left), ast::BinaryOperator::Add, std::move(right));
    }

    case TokenType::Minus: {
      auto right = parsePrattExpression(getRightBindingPower(token.type));
      return std::make_unique<ast::BinaryExpression>(
          std::move(left), ast::BinaryOperator::Sub, std::move(right));
    }

    case TokenType::Multiply: {
      auto right = parsePrattExpression(getRightBindingPower(token.type));
      return std::make_unique<ast::BinaryExpression>(
          std::move(left), ast::BinaryOperator::Mul, std::move(right));
    }

    case TokenType::Divide: {
      auto right = parsePrattExpression(getRightBindingPower(token.type));
      return std::make_unique<ast::BinaryExpression>(
          std::move(left), ast::BinaryOperator::Div, std::move(right));
    }

    case TokenType::Modulo: {
      auto right = parsePrattExpression(getRightBindingPower(token.type));
      return std::make_unique<ast::BinaryExpression>(
          std::move(left), ast::BinaryOperator::Mod, std::move(right));
    }

    case TokenType::Power: {
      auto right = parsePrattExpression(getRightBindingPower(token.type));
      return std::make_unique<ast::BinaryExpression>(
          std::move(left), ast::BinaryOperator::Pow, std::move(right));
    }

    case TokenType::Equals: {
      auto right = parsePrattExpression(getRightBindingPower(token.type));
      return std::make_unique<ast::BinaryExpression>(
          std::move(left), ast::BinaryOperator::Equal, std::move(right));
    }

    case TokenType::NotEquals: {
      auto right = parsePrattExpression(getRightBindingPower(token.type));
      return std::make_unique<ast::BinaryExpression>(
          std::move(left), ast::BinaryOperator::NotEqual, std::move(right));
    }

    case TokenType::Is: {
      auto right = parsePrattExpression(getRightBindingPower(token.type));
      return std::make_unique<ast::BinaryExpression>(
          std::move(left), ast::BinaryOperator::Is, std::move(right));
    }

    case TokenType::In: {
      auto right = parsePrattExpression(getRightBindingPower(token.type));
      return std::make_unique<ast::BinaryExpression>(
          std::move(left), ast::BinaryOperator::In, std::move(right));
    }

    case TokenType::Less: {
      auto right = parsePrattExpression(getRightBindingPower(token.type));
      return std::make_unique<ast::BinaryExpression>(
          std::move(left), ast::BinaryOperator::Less, std::move(right));
    }

    case TokenType::Greater: {
      auto right = parsePrattExpression(getRightBindingPower(token.type));
      return std::make_unique<ast::BinaryExpression>(
          std::move(left), ast::BinaryOperator::Greater, std::move(right));
    }

    case TokenType::LessEquals: {
      auto right = parsePrattExpression(getRightBindingPower(token.type));
      return std::make_unique<ast::BinaryExpression>(
          std::move(left), ast::BinaryOperator::LessEqual, std::move(right));
    }

    case TokenType::GreaterEquals: {
      auto right = parsePrattExpression(getRightBindingPower(token.type));
      return std::make_unique<ast::BinaryExpression>(
          std::move(left), ast::BinaryOperator::GreaterEqual, std::move(right));
    }

    case TokenType::And: {
      auto right = parsePrattExpression(getRightBindingPower(token.type));
      return std::make_unique<ast::BinaryExpression>(
          std::move(left), ast::BinaryOperator::And, std::move(right));
    }

    case TokenType::Or: {
      auto right = parsePrattExpression(getRightBindingPower(token.type));
      return std::make_unique<ast::BinaryExpression>(
          std::move(left), ast::BinaryOperator::Or, std::move(right));
    }

    case TokenType::Not: {
      // Handle 'not in' as a compound infix operator
      if (at().type == TokenType::In) {
        advance(); // consume 'in'
        auto right = parsePrattExpression(getRightBindingPower(TokenType::In));
        return std::make_unique<ast::BinaryExpression>(
            std::move(left), ast::BinaryOperator::NotIn, std::move(right));
      }
      // Regular 'not' as unary - but this shouldn't happen in infix position
      // Just return left as-is (the not prefix is handled in nud)
      return std::move(left);
    }

    case TokenType::Nullish: {
      auto right = parsePrattExpression(getRightBindingPower(token.type));
      return std::make_unique<ast::BinaryExpression>(
          std::move(left), ast::BinaryOperator::Nullish, std::move(right));
    }

    // Optional chaining: obj?.field or obj?.method()
    case TokenType::QuestionDot: {
        // Property names can be identifiers or certain keywords
        if (at().type == TokenType::Identifier ||
            at().type == TokenType::And ||
            at().type == TokenType::Or ||
            at().type == TokenType::Not ||
            at().type == TokenType::Repeat ||
            at().type == TokenType::Loop ||
            at().type == TokenType::If ||
            at().type == TokenType::Else ||
            at().type == TokenType::While ||
            at().type == TokenType::For ||
            at().type == TokenType::Match ||
            at().type == TokenType::Fn) {
          auto property = makeIdentifier(advance());
          auto member = std::make_unique<ast::MemberExpression>(
              std::move(left), std::move(property), true);
          return member;
        }
        failAt(at(), "Expected identifier after '?.'");
    }

    // Ternary operator: cond ? trueExpr : falseExpr
    case TokenType::Question: {
        auto trueValue = parsePrattExpression(0);
        if (at().type != TokenType::Colon) {
            failAt(at(), "Expected ':' after ternary expression condition");
        }
        advance(); // consume ':'
        auto falseValue = parsePrattExpression(getRightBindingPower(TokenType::Question));
        return std::make_unique<ast::TernaryExpression>(
            std::move(left), std::move(trueValue), std::move(falseValue));
    }

    case TokenType::DotDot: {
      // Check if left is already a RangeExpression (e.g., 1..10..2)
      // If so, parse the step and create a single range with step
      if (left && left->kind == ast::NodeType::RangeExpression) {
        auto &existingRange = static_cast<ast::RangeExpression &>(*left);
        // If this range already has a step, it's an error or we just treat it as nested
        // Parse the end value
        auto right = parsePrattExpression(getRightBindingPower(token.type));
        // Treat as a new range: start..end (ignoring the previous range wrapper)
        return std::make_unique<ast::RangeExpression>(
            std::move(left), std::move(right));
      }

      // Check for step: a..b..step - look ahead after parsing end
      auto right = parsePrattExpression(getRightBindingPower(token.type));

      // After parsing the right side, check if next token is another DotDot for step
      if (at().type == TokenType::DotDot) {
        advance(); // consume second ..
        auto step = parsePrattExpression(getRightBindingPower(TokenType::DotDot));
        return std::make_unique<ast::RangeExpression>(
            std::move(left), std::move(right), std::move(step));
      }

      return std::make_unique<ast::RangeExpression>(
          std::move(left), std::move(right));
    }

  // Member access
  case TokenType::Dot: {
    // Property names can be identifiers or certain keywords
      if (at().type == TokenType::Identifier ||
          at().type == TokenType::And ||
          at().type == TokenType::Or ||
          at().type == TokenType::Not ||
          at().type == TokenType::Repeat ||
          at().type == TokenType::Loop ||
          at().type == TokenType::If ||
          at().type == TokenType::Else ||
          at().type == TokenType::While ||
          at().type == TokenType::For ||
          at().type == TokenType::Match ||
          at().type == TokenType::Fn) {
        auto property = makeIdentifier(advance());
        return std::make_unique<ast::MemberExpression>(
            std::move(left), std::move(property));
      }
      failAt(at(), "Expected identifier after '.'");
    }

    // Function call
    case TokenType::OpenParen: {
      std::vector<std::unique_ptr<ast::Expression>> args;
      std::vector<ast::KeywordArg> kwargs;

      // Parse arguments
      while (at().type != TokenType::CloseParen) {
        if (!args.empty() || !kwargs.empty()) {
          if (at().type == TokenType::Comma) {
            advance();
          }
        }

        if (at().type == TokenType::CloseParen) {
          break;
        }

        // Check for keyword argument: name=value
        // Must check BEFORE calling parsePrattExpression to avoid treating
        // '=' as an assignment operator
        if (at().type == TokenType::Identifier &&
            at(1).type == TokenType::Assign) {
          std::string name = advance().value; // consume identifier
          advance();                          // consume '='
          auto value = parsePrattExpression(0);
          kwargs.emplace_back(std::move(name), std::move(value));
        } else {
          // Positional argument (possibly with spread)
          std::unique_ptr<ast::Expression> arg;
          if (at().type == TokenType::Spread) {
            advance(); // consume '...'
            auto target = parsePrattExpression(0);
            arg = std::make_unique<ast::SpreadExpression>(std::move(target));
          } else {
            arg = parsePrattExpression(0);
          }
          args.push_back(std::move(arg));
        }
      }

      if (at().type != TokenType::CloseParen) {
        failAt(at(), "Expected ')' after arguments");
      }
      advance(); // consume ')'

      return std::make_unique<ast::CallExpression>(
          std::move(left), std::move(args), std::move(kwargs));
    }

    // Array/Object index or slice
    case TokenType::OpenBracket: {
      // Check if this is a slice expression: [start:end:step]
      // At this point, '[' has been consumed. Check if current token is Colon or if next token is Colon
      // Also handle ColonColon (::) which can be two colons for slices
      if (at().type == TokenType::Colon || at().type == TokenType::ColonColon ||
          at(1).type == TokenType::Colon || at(1).type == TokenType::ColonColon) {
        // Slice expression - desugar to target.slice(start, end, step)
        std::unique_ptr<ast::Expression> start, end, step;
        
        // Parse optional start (before first colon)
        // Only parse if the current token is NOT a colon or ColonColon
        if (at(0).type != TokenType::Colon && at(0).type != TokenType::ColonColon) {
          start = parsePrattExpression(0);
        }
        
        // Handle ColonColon (::) as two colons
        if (at().type == TokenType::ColonColon) {
          // :: is start::step with no end, or start:end with no step
          // We need to look ahead to determine which
          advance(); // consume ::
          // After ::, if we see a number (possibly negative), it's step
          // If we see ], there's no step or end
          if (at().type != TokenType::CloseBracket) {
            // It's a step value
            step = parsePrattExpression(0);
          }
        } else {
          // Consume first colon
          if (at().type != TokenType::Colon) {
            failAt(at(), "Expected ':' in slice expression, got " + std::to_string(static_cast<int>(at().type)));
          }
          advance();
          
          // Parse optional end (between colons)
          if (at().type != TokenType::Colon && at().type != TokenType::CloseBracket) {
            end = parsePrattExpression(0);
          }
          
          // Parse optional step (after second colon)
          if (at().type == TokenType::Colon) {
            advance();
            if (at().type != TokenType::CloseBracket) {
              step = parsePrattExpression(0);
            }
          }
        }
        
        // Consume closing bracket
        if (at().type != TokenType::CloseBracket) {
          failAt(at(), "Expected ']' after slice expression, got " + std::to_string(static_cast<int>(at().type)));
        }
        advance();
        
        // Build: target.slice(start, end, step)
        auto sliceCall = std::make_unique<ast::CallExpression>(
            std::make_unique<ast::MemberExpression>(
                std::move(left), std::make_unique<ast::Identifier>("slice")),
            std::vector<std::unique_ptr<ast::Expression>>{});
        
        // Add slice arguments (use null for omitted values)
        if (start) {
          sliceCall->args.push_back(std::move(start));
        } else {
          sliceCall->args.push_back(std::make_unique<ast::NullLiteral>());
        }
        
        if (end) {
          sliceCall->args.push_back(std::move(end));
        } else {
          sliceCall->args.push_back(std::make_unique<ast::NullLiteral>());
        }
        
        if (step) {
          sliceCall->args.push_back(std::move(step));
        } else {
          // Always add step as null if not present
          sliceCall->args.push_back(std::make_unique<ast::NullLiteral>());
        }
        
        return sliceCall;
      }
      
      // Regular index expression
      auto index = parsePrattExpression(0);

      if (at().type != TokenType::CloseBracket) {
        failAt(at(), "Expected ']' after index");
      }
      advance(); // consume ']'

      return std::make_unique<ast::IndexExpression>(
          std::move(left), std::move(index));
    }

    // Postfix increment/decrement
    case TokenType::PlusPlus:
      return std::make_unique<ast::UpdateExpression>(
          std::move(left), ast::UpdateExpression::Operator::Increment, false);

    case TokenType::MinusMinus:
      return std::make_unique<ast::UpdateExpression>(
          std::move(left), ast::UpdateExpression::Operator::Decrement, false);

    case TokenType::Pipe: {
      auto right = parsePrattExpression(getRightBindingPower(token.type));

      if (auto *call = dynamic_cast<ast::CallExpression*>(right.get())) {
        call->args.push_back(std::move(left));
        return right;
      }

      std::vector<std::unique_ptr<ast::Expression>> args;
      args.push_back(std::move(left));
      return std::make_unique<ast::CallExpression>(
          std::move(right), std::move(args));
    }

    case TokenType::BitwiseOr: {
      auto right = parsePrattExpression(getRightBindingPower(token.type));
      return std::make_unique<ast::BinaryExpression>(
          std::move(left), ast::BinaryOperator::BitwiseOr, std::move(right));
    }

    case TokenType::BitwiseXor: {
      auto right = parsePrattExpression(getRightBindingPower(token.type));
      return std::make_unique<ast::BinaryExpression>(
          std::move(left), ast::BinaryOperator::BitwiseXor, std::move(right));
    }

    case TokenType::BitwiseAnd: {
      auto right = parsePrattExpression(getRightBindingPower(token.type));
      return std::make_unique<ast::BinaryExpression>(
          std::move(left), ast::BinaryOperator::BitwiseAnd, std::move(right));
    }

    case TokenType::ShiftLeft: {
      auto right = parsePrattExpression(getRightBindingPower(token.type));
      return std::make_unique<ast::BinaryExpression>(
          std::move(left), ast::BinaryOperator::BitwiseShiftLeft, std::move(right));
    }

    case TokenType::ShiftRight: {
      auto right = parsePrattExpression(getRightBindingPower(token.type));
      return std::make_unique<ast::BinaryExpression>(
          std::move(left), ast::BinaryOperator::BitwiseShiftRight, std::move(right));
    }

case TokenType::Arrow: {
      // Arrow function: identifier => body
        havel::debug("Arrow: left={}, at={}, Arrow.type={}",
                     left->toString(), at().toString(), static_cast<int>(TokenType::Arrow));
      if (context.inMatchExpression) {
        return nullptr;
      }
      if (left->kind != ast::NodeType::Identifier) {
        errorAt(token, "Arrow function requires an identifier parameter");
        return nullptr;
      }
      auto ident = std::unique_ptr<ast::Identifier>(static_cast<ast::Identifier*>(left.release()));

      // Parse body - use high rbp to prevent any infix ops after arrow body
      std::unique_ptr<ast::BlockStatement> body;
      if (at().type == TokenType::OpenBrace) {
        body = parseBlockStatement();
      } else {
        // Expression body: wrap in return
        auto bodyExpr = parsePrattExpression(getRightBindingPower(token.type));
        if (!bodyExpr) return nullptr;
        
        body = std::make_unique<ast::BlockStatement>();
        body->body.push_back(std::make_unique<ast::ExpressionStatement>(std::move(bodyExpr)));
      }

      std::vector<std::unique_ptr<ast::FunctionParameter>> params;
      params.push_back(std::make_unique<ast::FunctionParameter>(
          std::move(ident), std::nullopt, std::nullopt, false));
          
      return std::make_unique<ast::LambdaExpression>(std::move(params), std::move(body));
    }

    default:
      errorAt(token, "Unexpected token in infix position");
      return nullptr;
  }
}

// Helper methods for Pratt parser
std::unique_ptr<ast::Expression> Parser::parseParenthesizedExpression() {
  // Check for empty lambda: () => body
  if (at().type == TokenType::CloseParen && at(1).type == TokenType::Arrow) {
    advance(); // consume ')'
    advance(); // consume '=>'
    std::vector<std::unique_ptr<ast::FunctionParameter>> params;
    std::unique_ptr<ast::Statement> body;
    if (at().type == TokenType::OpenBrace) {
      body = parseBlockStatement();
    } else {
      auto bodyExpr = parsePrattExpression(getRightBindingPower(TokenType::Arrow));
      if (!bodyExpr) return nullptr;
      auto block = std::make_unique<ast::BlockStatement>();
      block->body.push_back(std::make_unique<ast::ExpressionStatement>(std::move(bodyExpr)));
      body = std::move(block);
    }
    return std::make_unique<ast::LambdaExpression>(std::move(params), std::move(body));
  }

  // Check if this is a multi-parameter lambda: (a, b, c) => body
  // Peek ahead: look for comma-separated identifiers followed by ) =>
  size_t savedPos = position;
  std::vector<std::unique_ptr<ast::FunctionParameter>> lambdaParams;
  bool isMultiParamLambda = false;

  if (at().type == TokenType::Identifier) {
    // Collect comma-separated identifiers
    while (true) {
      if (at().type != TokenType::Identifier) {
          break;
      }
      auto pattern = makeIdentifier(advance());
      lambdaParams.push_back(std::make_unique<ast::FunctionParameter>(
          std::move(pattern), std::nullopt, std::nullopt, false));
      if (at().type == TokenType::Comma) {
        advance();
      } else {
        break;
      }
    }
    // Check for ) =>
    if (lambdaParams.size() >= 1 && at().type == TokenType::CloseParen && at(1).type == TokenType::Arrow) {
      advance(); // consume ')'
      advance(); // consume '=>'
      isMultiParamLambda = true;
    }
  }

  if (isMultiParamLambda) {
    // Parse lambda body
    std::unique_ptr<ast::Statement> body;
    if (at().type == TokenType::OpenBrace) {
      body = parseBlockStatement();
    } else {
      auto bodyExpr = parsePrattExpression(getRightBindingPower(TokenType::Arrow));
      if (!bodyExpr) return nullptr;
      auto block = std::make_unique<ast::BlockStatement>();
      block->body.push_back(std::make_unique<ast::ExpressionStatement>(std::move(bodyExpr)));
      body = std::move(block);
    }
    return std::make_unique<ast::LambdaExpression>(std::move(lambdaParams), std::move(body));
  }

  // Not a multi-param lambda, restore position and parse normally
  position = savedPos;

  // Parse first expression
  auto expr = parsePrattExpression(0);
  if (!expr) return nullptr;

  // Check if this is a tuple (comma-separated expressions)
  if (at().type == TokenType::Comma) {
    // It's a tuple!
    std::vector<std::unique_ptr<ast::Expression>> elements;
    elements.push_back(std::move(expr));

    while (at().type == TokenType::Comma) {
      advance(); // consume ','
      // Allow trailing comma
      if (at().type == TokenType::CloseParen) break;
      auto next = parsePrattExpression(0);
      elements.push_back(std::move(next));
    }

    if (at().type != TokenType::CloseParen) {
      failAt(at(), "Expected ')' after tuple elements");
    }
    advance(); // consume ')'

    return std::make_unique<ast::TupleExpression>(std::move(elements));
  }

  // Not a tuple, just a grouped expression
  if (at().type != TokenType::CloseParen) {
    failAt(at(), "Expected ')'");
  }
  advance(); // consume ')'

  return expr;
}

std::unique_ptr<ast::Expression> Parser::parseLambdaExpression() {
  // We already consumed 'fn', now parse parameters and body
  std::vector<std::unique_ptr<ast::FunctionParameter>> params;

  if (at().type == TokenType::OpenParen) {
    advance(); // consume '('

    while (at().type != TokenType::CloseParen) {
      if (!params.empty() && at().type == TokenType::Comma) {
        advance();
        continue;
      }

      if (at().type == TokenType::Identifier) {
        auto pattern = makeIdentifier(advance());

        std::optional<std::unique_ptr<ast::TypeAnnotation>> typeAnn;
        if (at().type == TokenType::Colon) {
          typeAnn = parseTypeAnnotation();
        }

        std::optional<std::unique_ptr<ast::Expression>> defaultVal;
        if (at().type == TokenType::Assign) {
          advance(); // consume '='
          defaultVal = parseExpression();
        }

        params.push_back(std::make_unique<ast::FunctionParameter>(
            std::move(pattern), std::move(defaultVal), std::move(typeAnn), false));
      } else if (at().type == TokenType::Spread) {
        advance(); // consume '...'
        auto pattern = makeIdentifier(advance());
        params.push_back(std::make_unique<ast::FunctionParameter>(
            std::move(pattern), std::nullopt, std::nullopt, true));
      } else if (at().type != TokenType::CloseParen) {
        failAt(at(), "Expected parameter name in function definition");
      }
    }

    advance(); // consume ')'
  }
  
  // Parse body
  std::unique_ptr<ast::BlockStatement> body;
  if (at().type == TokenType::OpenBrace) {
    body = parseBlockStatement();
  } else {
    // Expression body: wrap in return
    auto expr = parsePrattExpression(0);
    auto stmt = std::make_unique<ast::ExpressionStatement>(std::move(expr));
    body = std::make_unique<ast::BlockStatement>();
    body->body.push_back(std::move(stmt));
  }
  
  return std::make_unique<ast::LambdaExpression>(std::move(params), std::move(body));
}

// Replace the old parseExpression with a wrapper that calls Pratt parser
std::unique_ptr<ast::Expression> Parser::parseExpression() {
  DepthGuard dg(recursion_depth_);
  return parsePrattExpression(0);
}

std::unique_ptr<ast::Expression> Parser::parseExpressionFromString(const std::string &expr) {
  // Tokenize the expression string
  havel::Lexer lexer(expr, debug.lexer);
  auto savedTokens = tokens;
  auto savedPosition = position;
  
  tokens = lexer.tokenize();
  position = 0;
  
  // Collect lexer errors but don't propagate them
  // (they're for the expression context, not the main parser)
  
  std::unique_ptr<ast::Expression> result = nullptr;
  try {
    if (notEOF()) {
      result = parsePrattExpression(0);
    }
  } catch (...) {
    // Ignore errors - if parsing fails, return nullptr
  }
  
  // Restore parser state
  tokens = std::move(savedTokens);
  position = savedPosition;
  
  return result;
}

std::unique_ptr<havel::ast::Program>
Parser::produceAST(const std::string &sourceCode) {
  // Tokenize source code
  havel::Lexer lexer(sourceCode, debug.lexer);
  tokens = lexer.tokenize();

  // Collect lexer errors
  for (const auto &err : lexer.getErrors()) {
    errors.push_back(err);
  }

  position = 0;

  if (debug.parser) {
    havel::debug("PARSE: Starting to parse program with {} tokens",
                 tokens.size());
  }

  // Create program AST node
  auto program = std::make_unique<havel::ast::Program>();

  // Parse all statements until EOF with error recovery
  size_t iterations = 0;
  while (notEOF()) {
    // Skip empty lines or statement separators
    if (at().type == havel::TokenType::NewLine ||
        at().type == havel::TokenType::Semicolon) {
      advance();
      continue;
    }

    // Error throttle - stop after too many errors
    if (errors.size() > 100) {
      throw std::runtime_error("Too many parse errors, aborting");
    }

    iterations++;
    if (iterations > 50000) {
      throw std::runtime_error("Parse exceeded iteration limit at token " + std::to_string(position) + "/" + std::to_string(tokens.size()));
    }

    if (debug.parser) {
      havel::debug("PARSE: Parsing statement at token {}", at().toString());
    }

    // Track position for forward progress guarantee
    size_t before = position;

    try {
      auto stmt = parseStatement();
      if (stmt) {
        program->body.push_back(std::move(stmt));
      }
    } catch (const ParseError &e) {
      // Convert ParseError to CompilerError
      CompilerError err(ErrorSeverity::Error, e.line, e.column, e.what());
      errors.push_back(err);
      synchronize(); // Recover to next safe point
    }

    // Forward progress guarantee - if we didn't advance, force advance
    if (position == before && notEOF()) {
      advance();
    }
  }

  if (havel::debugging::debug_ast) {
    printAST(*program);
  }

  return program;
}

std::unique_ptr<havel::ast::Program>
Parser::parseStrict(const std::string &sourceCode) {
  havel::Lexer lexer(sourceCode);
  tokens = lexer.tokenize();
  position = 0;

  auto program = std::make_unique<havel::ast::Program>();
  while (notEOF()) {
    if (at().type == havel::TokenType::NewLine ||
        at().type == havel::TokenType::Semicolon) {
      advance();
      continue;
    }

    try {
      auto stmt = parseStatement();
      if (stmt) {
        program->body.push_back(std::move(stmt));
      }
    } catch (const havel::parser::ParseError &) {
      throw;
    } catch (const std::exception &e) {
      fail(e.what());
    }
  }

  return program;
}

// Parse a single statement without skipping leading newlines (for inline forms)
std::unique_ptr<havel::ast::Statement> Parser::parseInlineStatement() {
  // Don't skip newlines - they terminate inline statements
  // If we hit a newline or EOF, return null
  if (at().type == havel::TokenType::NewLine ||
      at().type == havel::TokenType::EOF_TOKEN) {
    return nullptr;
  }

  // Keywords that should NOT be parsed as statements in inline context
  // (they belong to parent constructs like if/else/while)
  if (at().type == havel::TokenType::Else ||
      at().type == havel::TokenType::Catch ||
      at().type == havel::TokenType::Finally) {
    return nullptr;
  }

  // Parse based on current token
  switch (at().type) {
  case havel::TokenType::Colon:
    return parseSleepStatement();
  case havel::TokenType::Let:
  case havel::TokenType::Const:
    return parseLetDeclaration();
  case havel::TokenType::If:
    return parseIfStatement();
  case havel::TokenType::While:
    return parseWhileStatement();
  case havel::TokenType::For:
    return parseForStatement();
  case havel::TokenType::Loop:
    return parseLoopStatement();
  case havel::TokenType::Repeat:
    return parseRepeatStatement();
  case havel::TokenType::Break:
    return parseBreakStatement();
  case havel::TokenType::Continue:
    return parseContinueStatement();
  case havel::TokenType::Return:
  case havel::TokenType::Ret:
    return parseReturnStatement();
  case havel::TokenType::Fn:
    return parseFunctionDeclaration();
  case havel::TokenType::Switch:
    return parseSwitchStatement();
  case havel::TokenType::Try:
    return parseTryStatement();
  case havel::TokenType::Catch:
    failAt(at(), "'catch' can only appear within a 'try' statement");
  case havel::TokenType::Finally:
    failAt(at(), "'finally' can only appear within a 'try' statement");
  case havel::TokenType::Throw:
    return parseThrowStatement();
  case havel::TokenType::Del:
    return parseDelStatement();
  default:
    // Expression statement (including assignments, function calls, etc.)
    auto expr = parseExpression();
    return std::make_unique<havel::ast::ExpressionStatement>(std::move(expr));
  }
}

std::unique_ptr<havel::ast::Statement> Parser::parseStatement() {
  DepthGuard dg(recursion_depth_);
  // Skip leading newlines within statement context
  // This allows multiple newlines between statements
  while (at().type == havel::TokenType::NewLine) {
    advance();
  }

  // If we hit EOF after skipping newlines, return null
  if (at().type == havel::TokenType::EOF_TOKEN) {
    return nullptr;
  }

  if (debug.parser) {
    havel::debug("PARSE: parseStatement at token type={} value={}", static_cast<int>(at().type), at().value);
  }


  // Keywords that should NOT be parsed as statements
  // (they belong to parent constructs like if/else/while)
  if (at().type == havel::TokenType::Else ||
      at().type == havel::TokenType::Catch ||
      at().type == havel::TokenType::Finally) {
    return nullptr;
  }

  // Context-sensitive decorator detection:
  // @ at statement start + next is Identifier + after that is fn/( = decorator
  // @ anywhere else = field access (handled in parsePrattExpression)
  if (at().type == havel::TokenType::At &&
      at(1).type == havel::TokenType::Identifier &&
      (at(2).type == havel::TokenType::Fn ||
       at(2).type == havel::TokenType::OpenParen ||
       at(2).type == havel::TokenType::At ||
       at(2).type == havel::TokenType::NewLine)) {
    return parseDecoratorStatement();
  }

  switch (at().type) {
  case havel::TokenType::Hotkey: {
    // Parse hotkey with potential prefix conditions (when/if before =>)
    auto hotkeyToken = at(); // Store the hotkey token
    advance();               // consume the hotkey

    // Check for prefix condition (before =>)
    std::unique_ptr<havel::ast::Expression> prefixCondition = nullptr;
    if (at().type == havel::TokenType::When) {
      advance(); // consume 'when'
      // Parse legacy when syntax (e.g., "when mode gaming")
      // Stop at => so condition doesn't consume the arrow
      prefixCondition = parsePrattExpression(bp(BindingPower::Assignment));
    } else if (at().type == havel::TokenType::If) {
      advance(); // consume 'if'
      prefixCondition = parsePrattExpression(bp(BindingPower::Assignment));
    }

    if (at().type == havel::TokenType::Arrow) {
      advance(); // consume '=>'

      std::unique_ptr<havel::ast::BlockStatement> action;

      // => can be followed by { } block or a single expression
      if (at().type == havel::TokenType::OpenBrace) {
        action = parseBlockStatement(true); // true = input context
      } else {
        // Single expression wrapped in a block
        auto expr = parseExpression();
        action = std::make_unique<havel::ast::BlockStatement>();
        action->body.push_back(
            std::make_unique<havel::ast::ExpressionStatement>(std::move(expr)));
      }

      // Check for suffix condition (after action)
      std::unique_ptr<havel::ast::Expression> suffixCondition = nullptr;
      if (at().type == havel::TokenType::If) {
        advance(); // consume 'if'
        suffixCondition = parseExpression();
      }

      // Create the base hotkey binding
      auto binding = std::make_unique<havel::ast::HotkeyBinding>();
      binding->hotkeys.push_back(
          std::make_unique<havel::ast::HotkeyLiteral>(hotkeyToken.value));
      binding->action = std::move(action);

      // Combine conditions if needed
      if (prefixCondition || suffixCondition) {
        // Combine prefix and suffix conditions with AND
        auto finalCondition = combineConditions(std::move(prefixCondition),
                                                std::move(suffixCondition));
        return std::make_unique<havel::ast::ConditionalHotkey>(
            std::move(finalCondition), std::move(binding));
      }

      // No conditions, return normal binding
      return binding;
    } else {
      failAt(hotkeyToken, "Expected '=>' after hotkey literal");
    }
  }
case havel::TokenType::Identifier: {
    // Check for multiple assignment: a, b, c = value
    // Look ahead: identifier comma identifier ... = 
    if (at(1).type == havel::TokenType::Comma) {
        std::vector<std::unique_ptr<havel::ast::Expression>> targets;
        targets.push_back(makeIdentifier(advance()));
        
        while (at().type == havel::TokenType::Comma) {
            advance(); // consume comma
            if (at().type != havel::TokenType::Identifier) {
                failAt(at(), "Expected identifier after comma in multiple assignment");
            }
            targets.push_back(makeIdentifier(advance()));
        }
        
        if (at().type != havel::TokenType::Assign) {
            failAt(at(), "Expected '=' after comma-separated identifiers");
        }
        advance(); // consume '='
        
        auto value = parseExpression();
        
        // Require statement terminator
        if (at().type != havel::TokenType::Semicolon &&
            at().type != havel::TokenType::NewLine &&
            at().type != havel::TokenType::EOF_TOKEN &&
            at().type != havel::TokenType::CloseBrace) {
            failAt(at(), "Expected ';' or newline after expression");
        }
        if (at().type == havel::TokenType::Semicolon) {
            advance();
        }
        
        auto multiAssign = std::make_unique<havel::ast::MultipleAssignment>(
            std::move(targets), std::move(value));
        return std::make_unique<havel::ast::ExpressionStatement>(std::move(multiAssign));
    }

    // Sugar forms:
    // thread { ... } -> thread(fn() { ... })
    // interval <ms> { ... } -> interval(<ms>, fn() { ... })
    // timeout <ms> { ... } -> timeout(<ms>, fn() { ... })
    // ui { ... } -> desugared ui.create calls
    if (at().value == "thread" && at(1).type == havel::TokenType::OpenBrace) {
      advance(); // consume "thread"
      auto body = parseBlockStatement();
      auto lambda = std::make_unique<havel::ast::LambdaExpression>();
      lambda->body = std::move(body);
      std::vector<std::unique_ptr<havel::ast::Expression>> args;
      args.push_back(std::move(lambda));
      auto call = std::make_unique<havel::ast::CallExpression>(
          std::make_unique<havel::ast::Identifier>("thread"), std::move(args));
      return std::make_unique<havel::ast::ExpressionStatement>(std::move(call));
    }

    if (at().value == "ui" && at(1).type == havel::TokenType::OpenBrace) {
      return parseUIDeclaration();
    }

    if ((at().value == "interval" || at().value == "timeout") &&
        at(1).type != havel::TokenType::OpenBrace) {
      const std::string async_kind = at().value;
      advance(); // consume "interval" / "timeout"
      auto delay = parseExpression();
      while (at().type == havel::TokenType::NewLine) {
        advance();
      }
      if (at().type != havel::TokenType::OpenBrace) {
        failAt(at(), "Expected block body after " + async_kind + " delay");
      }
      auto body = parseBlockStatement();
      auto lambda = std::make_unique<havel::ast::LambdaExpression>();
      lambda->body = std::move(body);
      std::vector<std::unique_ptr<havel::ast::Expression>> args;
      args.push_back(std::move(delay));
      args.push_back(std::move(lambda));
      auto call = std::make_unique<havel::ast::CallExpression>(
          std::make_unique<havel::ast::Identifier>(async_kind),
          std::move(args));
      return std::make_unique<havel::ast::ExpressionStatement>(std::move(call));
    }

    // TEMPORARILY DISABLED: Check for config section - causes issues with mode
    // definitions Original code checked for identifier [args...] { key = value
    // } pattern but incorrectly triggered inside mode enter/exit blocks

    // Check if this is a hotkey (identifier followed by =>)
    // or if it has prefix conditions like: a when mode == "gaming" => action
    if (at(1).type == havel::TokenType::When ||
        at(1).type == havel::TokenType::If ||
        at(1).type == havel::TokenType::Arrow) {
      auto hotkeyToken = advance(); // identifier as hotkey

      // Check for prefix condition (before =>)
      std::unique_ptr<havel::ast::Expression> prefixCondition = nullptr;
      if (at().type == havel::TokenType::When) {
        advance(); // consume 'when'
        // Stop at => (binding power 10) so condition doesn't consume the arrow
        prefixCondition = parsePrattExpression(bp(BindingPower::Assignment));
      } else if (at().type == havel::TokenType::If) {
        advance(); // consume 'if'
        prefixCondition = parsePrattExpression(bp(BindingPower::Assignment));
      }

      if (at().type == havel::TokenType::Arrow) {
        advance(); // consume '=>'

        std::unique_ptr<havel::ast::BlockStatement> action;

        // => can be followed by { } block or a single expression
        if (at().type == havel::TokenType::OpenBrace) {
          action = parseBlockStatement(true); // true = input context
        } else {
          // Single expression wrapped in a block
          auto expr = parseExpression();
          action = std::make_unique<havel::ast::BlockStatement>();
          action->body.push_back(
              std::make_unique<havel::ast::ExpressionStatement>(std::move(expr)));
        }

        // Check for suffix condition (after action)
        std::unique_ptr<havel::ast::Expression> suffixCondition = nullptr;
        if (at().type == havel::TokenType::If) {
          advance(); // consume 'if'
          suffixCondition = parseExpression();
        }

        // Create the base hotkey binding
        auto binding = std::make_unique<havel::ast::HotkeyBinding>();
        binding->hotkeys.push_back(
            std::make_unique<havel::ast::HotkeyLiteral>(hotkeyToken.value));
        binding->action = std::move(action);

        // Combine conditions if needed
        if (prefixCondition || suffixCondition) {
          // Combine prefix and suffix conditions with AND
          auto finalCondition = combineConditions(std::move(prefixCondition),
                                                  std::move(suffixCondition));
          return std::make_unique<havel::ast::ConditionalHotkey>(
              std::move(finalCondition), std::move(binding));
        }

        // No conditions, return normal binding
        return binding;
      } else {
        failAt(hotkeyToken, "Expected '=>' after identifier hotkey");
      }
    }

    // Not a hotkey binding, parse as expression
    auto expr = parseExpression();

    // Require statement terminator: semicolon or newline
    // Prevents accidental expression chaining across lines:
    //   print("a")
    //   print("b")
    if (at().type != havel::TokenType::Semicolon &&
        at().type != havel::TokenType::NewLine &&
        at().type != havel::TokenType::EOF_TOKEN &&
        at().type != havel::TokenType::CloseBrace) {
      failAt(at(), "Expected ';' or newline after expression (Havel requires "
                   "statement terminators)");
    }

    if (at().type == havel::TokenType::Semicolon) {
      advance();
    }

    return std::make_unique<havel::ast::ExpressionStatement>(std::move(expr));
  }
  case havel::TokenType::Let:
  case havel::TokenType::Const:
    return parseLetDeclaration();
  case havel::TokenType::If:
    return parseIfStatement();
  case havel::TokenType::While:
    return parseWhileStatement();
  case havel::TokenType::Do:
    return parseDoWhileStatement();
  case havel::TokenType::Switch:
    return parseSwitchStatement();
  case havel::TokenType::For:
    return parseForStatement();
  case havel::TokenType::Loop:
    return parseLoopStatement();
  case havel::TokenType::Break:
    return parseBreakStatement();
  case havel::TokenType::Continue:
    return parseContinueStatement();
  case havel::TokenType::On:
    return parseOnStatement();
  case havel::TokenType::Off:
    return parseOffModeStatement();
  case havel::TokenType::Fn:
    return parseFunctionDeclaration();
  case havel::TokenType::Struct:
    // struct.define(...) is a method call, struct Name { } is a declaration
    if (at(1).type == havel::TokenType::Dot) {
      auto expr = parseExpression();
      if (at().type == havel::TokenType::Semicolon) advance();
      return std::make_unique<havel::ast::ExpressionStatement>(std::move(expr));
    }
    return parseStructDeclaration();
  case havel::TokenType::Class:
    // class.define(...) is a method call, class Name { } is a declaration
    if (at(1).type == havel::TokenType::Dot) {
      auto expr = parseExpression();
      if (at().type == havel::TokenType::Semicolon) advance();
      return std::make_unique<havel::ast::ExpressionStatement>(std::move(expr));
    }
    return parseClassDeclaration();
  case havel::TokenType::Enum:
    return parseEnumDeclaration();
  case havel::TokenType::Trait:
    return parseTraitDeclaration();
  case havel::TokenType::Impl:
    return parseImplDeclaration();
  case havel::TokenType::Return:
  case havel::TokenType::Ret:
    return parseReturnStatement();
  case havel::TokenType::Throw:
    return parseThrowStatement();
  case havel::TokenType::Del:
    return parseDelStatement();
  case havel::TokenType::Go:
    return parseGoStatement();
  case havel::TokenType::Try:
    return parseTryStatement();
  case havel::TokenType::Catch:
    // Catch should only appear within try statements
    failAt(at(), "'catch' can only appear within a 'try' statement");
  case havel::TokenType::Finally:
    // Finally should only appear within try statements
    failAt(at(), "'finally' can only appear within a 'try' statement");
  case havel::TokenType::When:
    return parseWhenBlock();
  case havel::TokenType::Repeat:
    return parseRepeatStatement();
  case havel::TokenType::OpenBrace: {
    // Check if this is an object literal or destructuring pattern
    // Object literal: {key: value, ...}
    // Destructuring: {a, b} = obj or {a: b} = obj
    
    // Look ahead to determine the context
    if (isObjectLiteral()) {
      // Parse as expression statement with object literal
      auto expr = parseExpression();
      
      // Consume optional semicolon
      if (at().type == havel::TokenType::Semicolon) {
        advance();
      }
      
      return std::make_unique<havel::ast::ExpressionStatement>(std::move(expr));
    } else if (isDestructuringPattern()) {
      // Parse as expression statement with object pattern (destructuring)
      auto expr = parseExpression();
      
      // Consume optional semicolon
      if (at().type == havel::TokenType::Semicolon) {
        advance();
      }
      
      return std::make_unique<havel::ast::ExpressionStatement>(std::move(expr));
    }
    return parseBlockStatement();
  }
  case havel::TokenType::Import:
    return parseImportStatement();
  case havel::TokenType::Use:
    return parseUseStatement();
  case havel::TokenType::With:
    return parseWithStatement();
  case havel::TokenType::Config:
    if (at(1).type == havel::TokenType::OpenBrace) {
      return parseConfigBlock();
    }
    // Fall through to expression parsing for config.method() calls
    [[fallthrough]];
  case havel::TokenType::Match:
    // match is an expression, not a statement - parse it directly
    // Match expressions are self-contained and don't need terminators
    {
      auto expr = parseExpression();
      return std::make_unique<havel::ast::ExpressionStatement>(std::move(expr));
    }
  case havel::TokenType::Devices:
    if (at(1).type == havel::TokenType::OpenBrace) {
      return parseDevicesBlock();
    }
    {
      auto expr = parseExpression();
      return std::make_unique<havel::ast::ExpressionStatement>(std::move(expr));
    }
  case havel::TokenType::Mode:
    if (at(1).type == havel::TokenType::Identifier) {
      // Check if this is a simple mode block or full mode definition
      // Full definition: mode name [priority N] { condition/enter/exit/on ... }
      // Simple block: mode name { statements }
      size_t savedPos = position;
      advance();
      advance(); // skip mode, name

      // Skip optional priority clause
      while (at().type == havel::TokenType::NewLine)
        advance();
      if (at().type == havel::TokenType::Identifier &&
          at().value == "priority") {
        advance(); // skip priority
        while (at().type == havel::TokenType::NewLine)
          advance();
        if (at().type == havel::TokenType::Number) {
          advance(); // skip number
        }
      }

      // Now check for opening brace
      while (at().type == havel::TokenType::NewLine)
        advance();
      if (at().type == havel::TokenType::OpenBrace) {
        advance(); // skip {
        while (at().type == havel::TokenType::NewLine)
          advance();

        // Check if this is a full definition (starts with
        // condition/enter/exit/on)
        bool isFullDefinition =
            (at().type == havel::TokenType::Identifier &&
             (at().value == "condition" || at().value == "enter" ||
              at().value == "exit" || at().value == "on"));
        position = savedPos; // restore position

        if (isFullDefinition) {
          return parseModeDefinition();
        } else {
          return parseModeBlock();
        }
      } else {
        position = savedPos; // restore position
      }
    }
    {
      auto expr = parseExpression();
      return std::make_unique<havel::ast::ExpressionStatement>(std::move(expr));
    }
  case havel::TokenType::Modes:
    if (at(1).type == havel::TokenType::OpenBrace) {
      return parseModesBlock();
    }
    {
      auto expr = parseExpression();
      return std::make_unique<havel::ast::ExpressionStatement>(std::move(expr));
    }
  case havel::TokenType::Signal:
    return parseSignalDefinition();
  case havel::TokenType::Group:
    return parseGroupDefinition();
  case havel::TokenType::Colon: {
    return parseSleepStatement();
  }
  case havel::TokenType::ShellCommand:
  case havel::TokenType::ShellCommandCapture: {
    bool captureOutput = (at().type == havel::TokenType::ShellCommandCapture);
    advance(); // consume '$' or '$!'
    // Skip newlines (spaces already skipped by lexer)
    while (at().type == havel::TokenType::NewLine) {
      advance();
    }
    // Expression forms: $ firefox, $! ls, $! (cmd), $! [array], $! var, $!
    // "string"
    std::unique_ptr<havel::ast::Expression> cmdExpr;
    if (at().type == havel::TokenType::OpenParen ||
        at().type == havel::TokenType::OpenBracket) {
      cmdExpr = parseExpression();
    } else if (at().type == havel::TokenType::Identifier) {
      // Just an identifier - parse as primary expression (e.g., $ firefox)
      cmdExpr = parsePrimaryExpression();
    } else if (at().type == havel::TokenType::String ||
               at().type == havel::TokenType::MultilineString) {
      // String literal for shell command
      cmdExpr = parsePrimaryExpression();
    } else {
      failAt(at(), "Shell command requires expression: $ firefox, $ (cmd), $! "
                   "[array], $! var, or $! \"string\"");
    }

    auto stmt = std::make_unique<havel::ast::ShellCommandStatement>(
        std::move(cmdExpr), captureOutput);

    // Check for pipe chain: $! cmd1 | cmd2 | cmd3
    while (at().type == havel::TokenType::Pipe) {
      advance(); // consume '|'
      // Skip whitespace after pipe
      while (at().type == havel::TokenType::NewLine) {
        advance();
      }

      // Parse next command in chain
      std::unique_ptr<havel::ast::Expression> nextCmdExpr;
      if (at().type == havel::TokenType::OpenParen ||
          at().type == havel::TokenType::OpenBracket) {
        nextCmdExpr = parseExpression();
      } else if (at().type == havel::TokenType::Identifier) {
        nextCmdExpr = parsePrimaryExpression();
      } else if (at().type == havel::TokenType::String ||
                 at().type == havel::TokenType::MultilineString) {
        nextCmdExpr = parsePrimaryExpression();
      } else {
        failAt(at(), "Pipe requires valid shell command");
      }

      // Create next statement and link it
      auto nextStmt = std::make_unique<havel::ast::ShellCommandStatement>(
          std::move(nextCmdExpr), false);
      stmt->next = std::move(nextStmt);
      stmt = std::move(stmt->next);
    }

    return stmt;
  }
  case havel::TokenType::Greater:
    return parseInputStatement();
  case havel::TokenType::Dsl:
    advance(); // consume 'dsl'
    if (at().type != havel::TokenType::OpenBrace) {
      failAt(at(), "Expected '{' after 'dsl'");
    }
    return parseBlockStatement(true); // Always input context
  case havel::TokenType::Less:
    if (context.inInputContext) {
      auto expr = parseGetInputExpression();
      return std::make_unique<havel::ast::ExpressionStatement>(std::move(expr));
    }
    {
      auto expr = parseExpression();
      return std::make_unique<havel::ast::ExpressionStatement>(std::move(expr));
    }
  case havel::TokenType::Question:
    if (context.inInputContext) {
      return parseIfStatement(); // Mapping ? to if in DSL
    }
    {
      auto expr = parseExpression();
      return std::make_unique<havel::ast::ExpressionStatement>(std::move(expr));
    }
  case havel::TokenType::Multiply:
    if (context.inInputContext) {
      return parseRepeatStatement(); // Mapping * to repeat in DSL
    }
    {
      auto expr = parseExpression();
      return std::make_unique<havel::ast::ExpressionStatement>(std::move(expr));
    }
  default:
    // Expression statement (function calls, assignments, etc.)
    // This handles: config.set(...), print("hello"), x = 5, etc.
    {
      // In input context (hotkey blocks), check for implicit input commands
      // first
      if (context.inInputContext) {
        if (at().type == havel::TokenType::String ||
            at().type == havel::TokenType::MultilineString ||
            at().type == havel::TokenType::Number ||
            at().type == havel::TokenType::OpenBrace ||
            (at().type == havel::TokenType::Identifier &&
             (at().value == "lmb" || at().value == "rmb" || at().value == "m" ||
              at().value == "r" || at().value == "w"))) {
          
          // Special case for 'w': check if it's wait (w expr) or wheel (w(args))
          if (at().type == havel::TokenType::Identifier && at().value == "w") {
            if (at(1).type != havel::TokenType::OpenParen) {
              return parseWaitStatement();
            }
          }
          return parseImplicitInputStatement();
        }
      }

      auto expr = parseExpression();

      // In DSL input context, if expression is followed by DSL command tokens,
      // treat the expression as an implicit input command and continue chaining
      if (expr && context.inInputContext) {
        bool isDslNext =
            at().type == havel::TokenType::Colon ||           // :100 sleep
            at().type == havel::TokenType::Less ||            // < get
            at().type == havel::TokenType::Greater ||         // > send
            at().type == havel::TokenType::Multiply ||        // * repeat
            at().type == havel::TokenType::Question ||        // ? condition
            (at().type == havel::TokenType::Identifier &&
             (at().value == "lmb" || at().value == "rmb" || at().value == "mmb" ||
              at().value == "m" || at().value == "r" || at().value == "w" ||
              at().value == "c" || at().value == "type" ||
              at().value == "click" || at().value == "move" || at().value == "scroll" ||
              at().value == "key" || at().value == "keys" || at().value == "send")) ||
            at().type == havel::TokenType::String ||
            at().type == havel::TokenType::MultilineString ||
            at().type == havel::TokenType::OpenBrace;         // {Key}
        
        if (isDslNext) {
          // Convert expression to implicit input statement
          return buildImplicitInputStatement(std::move(expr));
        }
      }

      // Require statement terminator for expression statements
      // Skip this check for match expressions since they're self-contained
      bool isMatchExpr = expr && expr->kind == ast::NodeType::MatchExpression;
      // In DSL input context, allow input commands to chain without separators
      bool isInputChainable = context.inInputContext &&
          (at().type == havel::TokenType::Colon ||           // :100 sleep
           at().type == havel::TokenType::Less ||            // < input get
           at().type == havel::TokenType::Greater ||         // > send
           at().type == havel::TokenType::Multiply ||        // * repeat
           at().type == havel::TokenType::Question ||        // ? if
           (at().type == havel::TokenType::Identifier &&
            (at().value == "lmb" || at().value == "rmb" || at().value == "mmb" ||
             at().value == "m" || at().value == "r" || at().value == "w" ||
             at().value == "c" || at().value == "type" ||
             at().value == "click" || at().value == "move" || at().value == "scroll" ||
             at().value == "key" || at().value == "keys" || at().value == "send")));
      if (!isMatchExpr && !isInputChainable &&
          at().type != havel::TokenType::NewLine &&
          at().type != havel::TokenType::Semicolon &&
          at().type != havel::TokenType::CloseBrace &&
          at().type != havel::TokenType::EOF_TOKEN) {
        failAt(at(), "Expected ';' or newline after expression (Havel requires statement terminators)");
      }

      // Consume optional semicolon
      if (at().type == havel::TokenType::Semicolon) {
        advance();
      }

      return std::make_unique<havel::ast::ExpressionStatement>(std::move(expr));
    }
  }

  // Should never reach here
  return nullptr;
}

std::unique_ptr<havel::ast::Statement> Parser::parseFunctionDeclaration() {
  advance(); // consume "fn"

  if (at().type != havel::TokenType::Identifier) {
    if (havel::Lexer::KEYWORDS.count(at().value)) {
      failAt(at(), "Cannot use reserved keyword '" + at().value +
                       "' as function name");
    }
    failAt(at(), "Expected function name after 'fn'");
  }

  // Parse function name (lexer already includes ? suffix if present)
  auto name = makeIdentifier(advance());

  if (at().type != havel::TokenType::OpenParen) {
    failAt(at(), "Expected '(' after function name");
  }
  advance(); // consume '('

  std::vector<std::unique_ptr<havel::ast::FunctionParameter>> params;
  while (notEOF() && at().type != havel::TokenType::CloseParen) {
    while (at().type == havel::TokenType::NewLine) {
      advance();
    }
    if (at().type == havel::TokenType::CloseParen) {
      break;
    }

    // Check for variadic parameter: ...args
    bool isVariadic = false;
    std::unique_ptr<havel::ast::Expression> pattern;

    if (at().type == havel::TokenType::Spread) {
      advance(); // consume '...'
      if (at().type != havel::TokenType::Identifier) {
        failAt(at(), "Expected identifier after '...' in variadic parameter");
      }
      pattern = makeIdentifier(advance());
      isVariadic = true;
    } else if (at().type == havel::TokenType::Identifier) {
      pattern = makeIdentifier(advance());
    } else {
      failAt(at(), "Expected identifier or '...' in parameter list");
    }

    // Check for type annotation (paramName: Type)
    std::optional<std::unique_ptr<havel::ast::TypeAnnotation>> typeAnnotation;
    if (at().type == havel::TokenType::Colon) {
      advance(); // consume ':'
      typeAnnotation = parseTypeAnnotation();
    }

    // Check for default value
    std::optional<std::unique_ptr<havel::ast::Expression>> defaultValue;
    if (at().type == havel::TokenType::Assign) {
      advance(); // consume '='
      defaultValue = parseExpression();
    }

    params.push_back(std::make_unique<havel::ast::FunctionParameter>(
        std::move(pattern), std::move(defaultValue), std::move(typeAnnotation),
        isVariadic));

    while (at().type == havel::TokenType::NewLine) {
      advance();
    }

    if (at().type == havel::TokenType::Comma) {
      advance();
    } else if (at().type != havel::TokenType::CloseParen) {
      failAt(at(), "Expected ',' or ')' in parameter list");
    }
  }

  if (at().type != havel::TokenType::CloseParen) {
    failAt(at(), "Expected ')' after parameter list");
  }
  advance(); // consume ')'

  // Check for return type annotation (-> Type)
  std::optional<std::unique_ptr<havel::ast::TypeAnnotation>> returnType;
  while (at().type == havel::TokenType::NewLine) {
    advance();
  }
  if (at().type == havel::TokenType::ReturnType) {
    advance(); // consume '->'
    returnType = parseTypeAnnotation();
  }

  auto body = parseBlockStatement();

  return std::make_unique<havel::ast::FunctionDeclaration>(
      std::move(name), std::move(params), std::move(body),
      std::move(returnType));
}

std::unique_ptr<havel::ast::Statement> Parser::parseReturnStatement() {
  advance(); // consume "return"
  std::unique_ptr<havel::ast::Expression> value = nullptr;

  // Skip newlines before checking for return value
  while (at().type == havel::TokenType::NewLine) {
    advance();
  }

  // Return value is optional
  if (at().type != havel::TokenType::Semicolon &&
      at().type != havel::TokenType::CloseBrace &&
      at().type != havel::TokenType::EOF_TOKEN) {
    // Special case: "return @" means "return this" (builder pattern)
    if ((at().type == havel::TokenType::At ||
         at().type == havel::TokenType::Hotkey) &&
        at(1).type != havel::TokenType::Identifier &&
        at(1).type != havel::TokenType::Arrow) {
      advance(); // consume '@'
      value = std::make_unique<havel::ast::ThisExpression>();
    } else {
      value = parseExpression();
    }
  }

  // Optional semicolon
  if (at().type == havel::TokenType::Semicolon) {
    advance();
  }

  return std::make_unique<havel::ast::ReturnStatement>(std::move(value));
}

// Parse sleep statement: :1500 or :1h30m or :3:10:25
std::unique_ptr<havel::ast::Statement> Parser::parseSleepStatement() {
  advance(); // consume ':'

  // Parse duration - can be number, string, or time format
  std::string duration;
  if (at().type == havel::TokenType::Number ||
      at().type == havel::TokenType::String ||
      at().type == havel::TokenType::MultilineString) {
    duration = advance().value;
  } else {
    // Try to parse as identifier (for unquoted strings like :1h30m)
    // For now, just consume tokens until newline/semicolon
    while (at().type != havel::TokenType::NewLine &&
           at().type != havel::TokenType::Semicolon &&
           at().type != havel::TokenType::EOF_TOKEN &&
           at().type != havel::TokenType::CloseBrace) {
      duration += at().value;
      advance();
    }
  }

  return std::make_unique<havel::ast::SleepStatement>(duration);
}

// Parse input statement: > "text" or > {Enter} or > lmb or > m(100, 200)
std::unique_ptr<havel::ast::Statement> Parser::parseInputStatement() {
  advance(); // consume '>'

  std::vector<havel::ast::InputCommand> commands;

  // Parse sequence of input commands until newline or end of block
  while (at().type != havel::TokenType::NewLine &&
         at().type != havel::TokenType::Semicolon &&
         at().type != havel::TokenType::EOF_TOKEN &&
         at().type != havel::TokenType::CloseBrace) {

    havel::ast::InputCommand cmd;

    // Check for sleep inline: :500
    if (at().type == havel::TokenType::Colon) {
      advance(); // consume ':'
      cmd.type = havel::ast::InputCommand::Sleep;
      if (at().type == havel::TokenType::Number) {
        cmd.duration = advance().value;
      }
      commands.push_back(cmd);
      continue;
    }

    // Check for string: "text"
    if (at().type == havel::TokenType::String ||
        at().type == havel::TokenType::MultilineString) {
      cmd.type = havel::ast::InputCommand::SendText;
      cmd.text = advance().value;
      commands.push_back(cmd);
      continue;
    }

    // Check for key: {Enter}
    if (at().type == havel::TokenType::OpenBrace) {
      advance(); // consume '{'
      if (at().type == havel::TokenType::Identifier) {
        cmd.type = havel::ast::InputCommand::SendKey;
        cmd.key = advance().value;
        commands.push_back(cmd);
      }
      if (at().type == havel::TokenType::CloseBrace) {
        advance(); // consume '}'
      }
      continue;
    }

    // Check for identifier: lmb, rmb, m, r, w
    if (at().type == havel::TokenType::Identifier) {
      std::string ident = at().value;

      if (ident == "lmb") {
        advance();
        cmd.type = havel::ast::InputCommand::MouseClick;
        cmd.text = "left";
        commands.push_back(cmd);
        continue;
      } else if (ident == "rmb") {
        advance();
        cmd.type = havel::ast::InputCommand::MouseClick;
        cmd.text = "right";
        commands.push_back(cmd);
        continue;
      } else if (ident == "m" || ident == "r" || ident == "w") {
        advance(); // consume identifier

        // Parse function call: m(x, y)
        if (at().type == havel::TokenType::OpenParen) {
          advance(); // consume '('

          // Parse x argument - just capture the token value for now
          if (at().type != havel::TokenType::CloseParen) {
            cmd.xExprStr = at().value;
            advance();
          }

          // Parse optional y argument
          if (at().type == havel::TokenType::Comma) {
            advance(); // consume ','
            if (at().type != havel::TokenType::CloseParen) {
              cmd.yExprStr = at().value;
              advance();
            }
          }

          if (at().type == havel::TokenType::CloseParen) {
            advance(); // consume ')'
          }

          if (ident == "m") {
            cmd.type = havel::ast::InputCommand::MouseMove;
          } else if (ident == "r") {
            cmd.type = havel::ast::InputCommand::MouseRelative;
          } else if (ident == "w") {
            cmd.type = havel::ast::InputCommand::MouseWheel;
          }
          commands.push_back(cmd);
        }
        continue;
      }
    }

    // Skip unknown token
    advance();
  }

  return std::make_unique<havel::ast::InputStatement>(commands);
}

// Parse get input expression: < clipboard or < in("...")
std::unique_ptr<havel::ast::Expression> Parser::parseGetInputExpression() {
  advance(); // consume '<'

  std::string source;
  std::unique_ptr<havel::ast::Expression> prompt = nullptr;

  if (at().type == havel::TokenType::Identifier) {
    source = advance().value;

    // Special case for in("...")
    if (source == "in" && at().type == havel::TokenType::OpenParen) {
      advance(); // consume '('
      if (at().type != havel::TokenType::CloseParen) {
        prompt = parseExpression();
      }
      if (at().type == havel::TokenType::CloseParen) {
        advance(); // consume ')'
      }
    }
  } else {
    failAt(at(), "Expected identifier after '<'");
  }

  return std::make_unique<havel::ast::GetInputExpression>(source, std::move(prompt));
}

// Parse wait statement: w window.title == "Chrome"
std::unique_ptr<havel::ast::Statement> Parser::parseWaitStatement() {
  if (at().value == "w") {
    advance(); // consume 'w'
  } else {
    failAt(at(), "Expected 'w' for wait statement");
  }

  auto condition = parseExpression();
  return std::make_unique<havel::ast::WaitStatement>(std::move(condition));
}

// Parse implicit input statement in hotkey blocks
// Build an input statement starting from an already-parsed expression.
// This handles cases like `m(0) :100` where the first command was parsed
// as a regular expression before we realized we're in DSL context.
std::unique_ptr<havel::ast::Statement> Parser::buildImplicitInputStatement(
    std::unique_ptr<ast::Expression> leadingExpr) {
  std::vector<havel::ast::InputCommand> commands;

  // Convert the leading expression to an input command
  if (leadingExpr->kind == ast::NodeType::CallExpression) {
    const auto &call = static_cast<const ast::CallExpression &>(*leadingExpr);
    if (call.callee && call.callee->kind == ast::NodeType::Identifier) {
      const auto &ident = static_cast<const ast::Identifier &>(*call.callee);
      havel::ast::InputCommand cmd;

      if (ident.symbol == "m") {
        cmd.type = havel::ast::InputCommand::MouseMove;
      } else if (ident.symbol == "r") {
        cmd.type = havel::ast::InputCommand::MouseRelative;
      } else if (ident.symbol == "w") {
        cmd.type = havel::ast::InputCommand::MouseWheel;
      } else if (ident.symbol == "c") {
        cmd.type = havel::ast::InputCommand::MouseClickAt;
      } else {
        // Unknown function call - treat as generic expression
        cmd.type = havel::ast::InputCommand::SendText;
        cmd.text = call.toString();
        commands.push_back(cmd);
        // Continue parsing more commands
        return parseMoreInputCommands(std::move(commands));
      }

      // Extract arguments from call args
      for (size_t i = 0; i < call.args.size(); i++) {
        std::string val = call.args[i]->toString();
        if (i == 0) cmd.xExprStr = val;
        else if (i == 1) cmd.yExprStr = val;
        else if (i == 2) cmd.speedExprStr = val;
        else if (i == 3) cmd.accelExprStr = val;
      }
      commands.push_back(cmd);
    } else {
      // Non-identifier callee - treat as generic expression
      havel::ast::InputCommand cmd;
      cmd.type = havel::ast::InputCommand::SendText;
      cmd.text = leadingExpr->toString();
      commands.push_back(cmd);
      return parseMoreInputCommands(std::move(commands));
    }
  } else if (leadingExpr->kind == ast::NodeType::StringLiteral) {
    havel::ast::InputCommand cmd;
    cmd.type = havel::ast::InputCommand::SendText;
    cmd.text = static_cast<const ast::StringLiteral &>(*leadingExpr).value;
    commands.push_back(cmd);
  } else {
    // Generic expression - convert to text
    havel::ast::InputCommand cmd;
    cmd.type = havel::ast::InputCommand::SendText;
    cmd.text = leadingExpr->toString();
    commands.push_back(cmd);
  }

  return parseMoreInputCommands(std::move(commands));
}

// Continue parsing input commands after the first one has been added
std::unique_ptr<havel::ast::Statement> Parser::parseMoreInputCommands(
    std::vector<havel::ast::InputCommand> commands) {
  while (notEOF() && at().type != havel::TokenType::NewLine &&
         at().type != havel::TokenType::Semicolon &&
         at().type != havel::TokenType::EOF_TOKEN &&
         at().type != havel::TokenType::CloseBrace) {

    havel::ast::InputCommand cmd;

    // Check for sleep inline: :500
    if (at().type == havel::TokenType::Colon) {
      advance(); // consume ':'
      cmd.type = havel::ast::InputCommand::Sleep;
      if (at().type == havel::TokenType::Number) {
        cmd.duration = advance().value;
      }
      commands.push_back(cmd);
      continue;
    }

    // Check for string: "text"
    if (at().type == havel::TokenType::String ||
        at().type == havel::TokenType::MultilineString) {
      cmd.type = havel::ast::InputCommand::SendText;
      cmd.text = advance().value;
      commands.push_back(cmd);
      continue;
    }

    // Check for key: {Enter}
    if (at().type == havel::TokenType::OpenBrace) {
      advance(); // consume '{'
      if (at().type == havel::TokenType::Identifier) {
        cmd.type = havel::ast::InputCommand::SendKey;
        cmd.key = advance().value;
        commands.push_back(cmd);
      }
      if (at().type == havel::TokenType::CloseBrace) {
        advance(); // consume '}'
      }
      continue;
    }

    // Check for identifier: lmb, rmb, m, r, w
    if (at().type == havel::TokenType::Identifier) {
      std::string ident = at().value;

      if (ident == "lmb") {
        advance();
        cmd.type = havel::ast::InputCommand::MouseClick;
        cmd.text = "left";
        commands.push_back(cmd);
        continue;
      } else if (ident == "rmb") {
        advance();
        cmd.type = havel::ast::InputCommand::MouseClick;
        cmd.text = "right";
        commands.push_back(cmd);
        continue;
      } else if (ident == "m" || ident == "r" || ident == "w") {
        advance(); // consume identifier

        if (at().type == havel::TokenType::OpenParen) {
          advance(); // consume '('
          if (at().type != havel::TokenType::CloseParen) {
            cmd.xExprStr = at().value;
            advance();
          }
          if (at().type == havel::TokenType::Comma) {
            advance();
            if (at().type != havel::TokenType::CloseParen) {
              cmd.yExprStr = at().value;
              advance();
            }
          }
          if (at().type == havel::TokenType::CloseParen) {
            advance(); // consume ')'
          }
          if (ident == "m") cmd.type = havel::ast::InputCommand::MouseMove;
          else if (ident == "r") cmd.type = havel::ast::InputCommand::MouseRelative;
          else if (ident == "w") cmd.type = havel::ast::InputCommand::MouseWheel;
          commands.push_back(cmd);
        }
        continue;
      }
    }

    // Skip unknown token
    advance();
  }

  return std::make_unique<havel::ast::InputStatement>(commands);
}

// Handles: "text", {Key}, lmb, rmb, mmb, side1, side2, btn4, btn5,
// m(x,y,speed,accel), r(x,y,speed,accel), w(x,y,speed,accel),
// c(x,y,btn,speed,accel), :500
std::unique_ptr<havel::ast::Statement> Parser::parseImplicitInputStatement() {
  std::vector<havel::ast::InputCommand> commands;

  while (notEOF() && at().type != havel::TokenType::NewLine &&
         at().type != havel::TokenType::Semicolon &&
         at().type != havel::TokenType::EOF_TOKEN &&
         at().type != havel::TokenType::CloseBrace) {

    havel::ast::InputCommand cmd;

    // Check for sleep inline: :500
    if (at().type == havel::TokenType::Colon) {
      advance(); // consume ':'
      cmd.type = havel::ast::InputCommand::Sleep;
      if (at().type == havel::TokenType::Number) {
        cmd.duration = advance().value;
      }
      commands.push_back(cmd);
      continue;
    }

    // Check for string: "text"
    if (at().type == havel::TokenType::String ||
        at().type == havel::TokenType::MultilineString) {
      cmd.type = havel::ast::InputCommand::SendText;
      cmd.text = advance().value;
      commands.push_back(cmd);
      continue;
    }

    // Check for key: {Enter}
    if (at().type == havel::TokenType::OpenBrace) {
      advance(); // consume '{'
      if (at().type == havel::TokenType::Identifier) {
        cmd.type = havel::ast::InputCommand::SendKey;
        cmd.key = advance().value;
        commands.push_back(cmd);
      }
      if (at().type == havel::TokenType::CloseBrace) {
        advance(); // consume '}'
      }
      continue;
    }

    // Check for identifier: lmb, rmb, mmb, side1, side2, btn4, btn5, m, r, w, c
    if (at().type == havel::TokenType::Identifier) {
      std::string ident = at().value;

      // Mouse button shortcuts
      if (ident == "lmb") {
        advance();
        cmd.type = havel::ast::InputCommand::MouseClick;
        cmd.text = "left";
        commands.push_back(cmd);
        continue;
      } else if (ident == "rmb") {
        advance();
        cmd.type = havel::ast::InputCommand::MouseClick;
        cmd.text = "right";
        commands.push_back(cmd);
        continue;
      } else if (ident == "mmb") {
        advance();
        cmd.type = havel::ast::InputCommand::MouseClick;
        cmd.text = "middle";
        commands.push_back(cmd);
        continue;
      } else if (ident == "side1" || ident == "btn4") {
        advance();
        cmd.type = havel::ast::InputCommand::MouseClick;
        cmd.text = "side1";
        commands.push_back(cmd);
        continue;
      } else if (ident == "side2" || ident == "btn5") {
        advance();
        cmd.type = havel::ast::InputCommand::MouseClick;
        cmd.text = "side2";
        commands.push_back(cmd);
        continue;
      } else if (ident == "m" || ident == "r" || ident == "w" || ident == "c") {
        advance(); // consume identifier

        // Parse function call: m(x, y, speed, accel) or c(x, y, button, speed,
        // accel)
        if (at().type == havel::TokenType::OpenParen) {
          advance(); // consume '('

          // Helper to parse comma-separated arguments
          auto parseArgs = [this](std::string &x, std::string &y,
                                  std::string &speed, std::string &accel,
                                  std::string &button = *(new std::string())) {
            // Parse x argument
            if (at().type != havel::TokenType::CloseParen) {
              x = at().value;
              advance();
            }

            // Parse y argument
            if (at().type == havel::TokenType::Comma) {
              advance();
              if (at().type != havel::TokenType::CloseParen) {
                y = at().value;
                advance();
              }
            }

            // Parse speed argument
            if (at().type == havel::TokenType::Comma) {
              advance();
              if (at().type != havel::TokenType::CloseParen) {
                speed = at().value;
                advance();
              }
            }

            // Parse accel argument (or button for c())
            if (at().type == havel::TokenType::Comma) {
              advance();
              if (at().type != havel::TokenType::CloseParen) {
                accel = at().value;
                advance();
              }
            }
          };

          if (ident == "c") {
            // c(x, y, button, speed, accel)
            std::string button;
            parseArgs(cmd.xExprStr, cmd.yExprStr, cmd.speedExprStr,
                      cmd.accelExprStr, button);
            cmd.buttonExprStr = button;
            cmd.type = havel::ast::InputCommand::MouseClickAt;
          } else {
            // m(x, y, speed, accel), r(x, y, speed, accel), w(x, y, speed,
            // accel)
            parseArgs(cmd.xExprStr, cmd.yExprStr, cmd.speedExprStr,
                      cmd.accelExprStr);

            if (ident == "m") {
              cmd.type = havel::ast::InputCommand::MouseMove;
            } else if (ident == "r") {
              cmd.type = havel::ast::InputCommand::MouseRelative;
            } else if (ident == "w") {
              cmd.type = havel::ast::InputCommand::MouseWheel;
            }
          }

          if (at().type == havel::TokenType::CloseParen) {
            advance(); // consume ')'
          }
          commands.push_back(cmd);
        }
        continue;
      }
    }

    // Skip unknown token
    advance();
  }

  return std::make_unique<havel::ast::InputStatement>(commands);
}

std::unique_ptr<havel::ast::Statement> Parser::parseStructDeclaration() {
  advance(); // consume 'struct'

  // Parse struct name
  if (at().type != havel::TokenType::Identifier) {
    failAt(at(), "Expected struct name after 'struct'");
  }
  std::string structName = advance().value;

  // Parse opening brace
  if (at().type != havel::TokenType::OpenBrace) {
    failAt(at(), "Expected '{' after struct name");
  }
  advance(); // consume '{'

  // Parse fields and methods
  auto [fields, methods] = parseStructMembers();

  // Parse closing brace
  if (at().type != havel::TokenType::CloseBrace) {
    failAt(at(), "Expected '}' to close struct definition");
  }
  advance(); // consume '}'

  // Create struct definition with fields and methods
  ast::StructDefinition def(std::move(fields), std::move(methods));

  return std::make_unique<ast::StructDeclaration>(structName, std::move(def));
}

std::unique_ptr<havel::ast::Statement> Parser::parseClassDeclaration() {
  advance(); // consume 'class'

  // Parse class name
  if (at().type != havel::TokenType::Identifier) {
    failAt(at(), "Expected class name after 'class'");
  }
  std::string className = advance().value;

  // Check for inheritance syntax: class X : ParentClass
  std::string parentName;
  if (at().type == havel::TokenType::Colon) {
    advance(); // consume ':'
    if (at().type != havel::TokenType::Identifier) {
      failAt(at(), "Expected parent class name after ':'");
    }
    parentName = advance().value;
  }

  // Parse opening brace
  if (at().type != havel::TokenType::OpenBrace) {
    failAt(at(), "Expected '{' after class name");
  }
  advance(); // consume '{'

  // Parse fields and methods
  auto [fields, methods] = parseClassMembers();

  // Parse closing brace
  if (at().type != havel::TokenType::CloseBrace) {
    failAt(at(), "Expected '}' to close class definition");
  }
  advance(); // consume '}'

  // Create class definition with fields and methods
  ast::ClassDefinition def(std::move(fields), std::move(methods));

  return std::make_unique<ast::ClassDeclaration>(className, std::move(def),
                                                 parentName);
}

// Parse struct members (fields and methods)
std::pair<std::vector<ast::StructFieldDef>,
          std::vector<std::unique_ptr<ast::StructMethodDef>>>
Parser::parseStructMembers() {
  std::vector<ast::StructFieldDef> fields;
  std::vector<std::unique_ptr<ast::StructMethodDef>> methods;

  while (at().type != havel::TokenType::CloseBrace && notEOF()) {
    // Skip newlines and comments
    if (at().type == havel::TokenType::NewLine ||
        at().type == havel::TokenType::Comment) {
      advance();
      continue;
    }

    // Check for method definition (fn keyword) or operator (op keyword)
    if (at().type == havel::TokenType::Fn ||
        at().type == havel::TokenType::Op) {
      bool isOperator = (at().type == havel::TokenType::Op);
      advance(); // consume 'fn' or 'op'

      // Skip whitespace/newlines after fn/op
      while ((at().type == havel::TokenType::NewLine ||
              at().type == havel::TokenType::Comment) &&
             notEOF()) {
        advance();
      }

      std::string methodName;

      if (isOperator) {
        // For operators, accept operator symbols (+, -, *, /, etc.)
        // Check token value directly for reliability
        const std::string &tokenVal = at().value;
        if (tokenVal == "+") {
          methodName = "op_add";
          advance();
        } else if (tokenVal == "-") {
          methodName = "op_sub";
          advance();
        } else if (tokenVal == "*") {
          methodName = "op_mul";
          advance();
        } else if (tokenVal == "/") {
          methodName = "op_div";
          advance();
        } else if (tokenVal == "%") {
          methodName = "op_mod";
          advance();
        } else if (tokenVal == "==") {
          methodName = "op_eq";
          advance();
        } else if (tokenVal == "!=") {
          methodName = "op_ne";
          advance();
        } else if (tokenVal == "<") {
          methodName = "op_lt";
          advance();
        } else if (tokenVal == ">") {
          methodName = "op_gt";
          advance();
        } else if (tokenVal == "<=") {
          methodName = "op_le";
          advance();
        } else if (tokenVal == ">=") {
          methodName = "op_ge";
          advance();
        } else if (tokenVal == "[") {
          methodName = "op_index";
          advance();
        } else if (tokenVal == "(") {
          methodName = "op_call";
          advance();
        } else {
          failAt(at(),
                 "Expected operator symbol (+, -, *, /, ==, etc.) after 'op'");
        }
      } else {
        // Regular method - expect identifier
        if (at().type != havel::TokenType::Identifier) {
          failAt(at(), "Expected method name after 'fn'");
        }
        methodName = advance().value;
      }

      // Check if this is a constructor (named 'init')
      bool isConstructor = (methodName == "init" || methodName == "op_init");

      // Parse parameters
      if (at().type != havel::TokenType::OpenParen) {
        failAt(at(), "Expected '(' after method name");
      }
      advance(); // consume '('

      std::vector<std::unique_ptr<ast::FunctionParameter>> params;
      while (at().type != havel::TokenType::CloseParen && notEOF()) {
        if (at().type == havel::TokenType::Identifier) {
          auto paramName = makeIdentifier(advance());
          params.push_back(
              std::make_unique<ast::FunctionParameter>(std::move(paramName)));
        }
        if (at().type == havel::TokenType::Comma) {
          advance();
        } else if (at().type != havel::TokenType::CloseParen) {
          failAt(at(), "Expected ',' or ')' in parameter list");
        }
      }
      if (at().type != havel::TokenType::CloseParen) {
        failAt(at(), "Expected ')' after parameters");
      }
      advance(); // consume ')'

      // Parse body
      auto body = parseBlockStatement();

      methods.push_back(std::make_unique<ast::StructMethodDef>(
          methodName, std::move(params), std::move(body), isConstructor,
          isOperator));
      continue;
    }

    // Parse field name
    if (at().type != havel::TokenType::Identifier) {
      failAt(at(), "Expected field name or 'fn' in struct");
    }
    std::string fieldName = advance().value;

    // Optional type annotation
    std::optional<std::unique_ptr<ast::TypeDefinition>> fieldType;
    if (at().type == havel::TokenType::Colon) {
      advance(); // consume ':'
      fieldType = parseTypeDefinition();
    }

    // Optional default value
    std::optional<std::unique_ptr<ast::Expression>> defaultValue;
    if (at().type == havel::TokenType::Assign) {
      advance(); // consume '='
      defaultValue = parsePrimaryExpression();
    }

    fields.emplace_back(fieldName, std::move(fieldType),
                        std::move(defaultValue)); // structs don't have class fields

    // Optional comma
    if (at().type == havel::TokenType::Comma) {
      advance();
    }
  }

  return {std::move(fields), std::move(methods)};
}

// Parse class members (fields and methods)
std::pair<std::vector<ast::ClassFieldDef>,
          std::vector<std::unique_ptr<ast::ClassMethodDef>>>
Parser::parseClassMembers() {
  std::vector<ast::ClassFieldDef> fields;
  std::vector<std::unique_ptr<ast::ClassMethodDef>> methods;

  while (at().type != havel::TokenType::CloseBrace && notEOF()) {
    // Skip newlines and comments
    if (at().type == havel::TokenType::NewLine ||
        at().type == havel::TokenType::Comment) {
      advance();
      continue;
    }

    // Check for class member marker (@@) or instance member marker (@)
    bool isClassMember = false;
    if (at().type == havel::TokenType::AtAt) {
      advance(); // consume '@@'
      isClassMember = true;
    } else if (at().type == havel::TokenType::At) {
      advance(); // consume '@' (optional for instance methods, required for methods with @fn)
    }

    // Check for method definition (fn keyword) or operator (op keyword)
    if (at().type == havel::TokenType::Fn ||
        at().type == havel::TokenType::Op) {
      bool isOperator = (at().type == havel::TokenType::Op);
      advance(); // consume 'fn' or 'op'

      // Skip whitespace/newlines after fn/op
      while ((at().type == havel::TokenType::NewLine ||
              at().type == havel::TokenType::Comment) &&
             notEOF()) {
        advance();
      }

      std::string methodName;

      if (isOperator) {
        // For operators, accept operator symbols (+, -, *, /, etc.)
        const std::string &tokenVal = at().value;
        if (tokenVal == "+") {
          methodName = "op_add";
          advance();
        } else if (tokenVal == "-") {
          methodName = "op_sub";
          advance();
        } else if (tokenVal == "*") {
          methodName = "op_mul";
          advance();
        } else if (tokenVal == "/") {
          methodName = "op_div";
          advance();
        } else if (tokenVal == "%") {
          methodName = "op_mod";
          advance();
        } else if (tokenVal == "==") {
          methodName = "op_eq";
          advance();
        } else if (tokenVal == "!=") {
          methodName = "op_ne";
          advance();
        } else if (tokenVal == "<") {
          methodName = "op_lt";
          advance();
        } else if (tokenVal == ">") {
          methodName = "op_gt";
          advance();
        } else if (tokenVal == "<=") {
          methodName = "op_le";
          advance();
        } else if (tokenVal == ">=") {
          methodName = "op_ge";
          advance();
        } else if (tokenVal == "[") {
          methodName = "op_index";
          advance();
        } else if (tokenVal == "(") {
          methodName = "op_call";
          advance();
        } else {
          failAt(at(),
                 "Expected operator symbol (+, -, *, /, ==, etc.) after 'op'");
        }
      } else {
        // Regular method - expect identifier
        if (at().type != havel::TokenType::Identifier) {
          failAt(at(), "Expected method name after 'fn'");
        }
        methodName = advance().value;
      }

      // Parse parameters
      if (at().type != havel::TokenType::OpenParen) {
        failAt(at(), "Expected '(' after method name");
      }
      advance(); // consume '('

      std::vector<std::unique_ptr<ast::FunctionParameter>> params;
      while (at().type != havel::TokenType::CloseParen && notEOF()) {
        // Skip newlines and comments
        if (at().type == havel::TokenType::NewLine ||
            at().type == havel::TokenType::Comment) {
          advance();
          continue;
        }

        if (at().type == havel::TokenType::Identifier) {
          auto paramName = makeIdentifier(advance());
          params.push_back(
              std::make_unique<ast::FunctionParameter>(std::move(paramName)));
        }
        if (at().type == havel::TokenType::Comma) {
          advance();
        } else if (at().type != havel::TokenType::CloseParen) {
          failAt(at(), "Expected ',' or ')' in parameter list");
        }
      }

      if (at().type != havel::TokenType::CloseParen) {
        failAt(at(), "Expected ')' after parameters");
      }
      advance(); // consume ')'

      // Parse body
      auto body = parseBlockStatement();

      // Check if this is a constructor (method name is "init")
      bool isConstructor = (methodName == "init");

      methods.push_back(std::make_unique<ast::ClassMethodDef>(
          methodName, std::move(params), std::move(body), isConstructor,
          isOperator, isClassMember));
      continue;
    }

    // Parse field definition
    // Note: isClassMember is already set from @@ or @ above

    // Support optional val/const/let prefix for fields
    bool isConst = false;
    if (at().type == havel::TokenType::Const ||
        at().type == havel::TokenType::Let) {
      isConst = (at().type == havel::TokenType::Const);
      advance(); // consume val/const/let
    }

    // Parse field name
    if (at().type != havel::TokenType::Identifier) {
      failAt(at(), "Expected field name or 'fn' in class");
    }
    std::string fieldName = advance().value;

    // Optional type annotation
    std::optional<std::unique_ptr<ast::TypeDefinition>> fieldType;
    if (at().type == havel::TokenType::Colon) {
      advance(); // consume ':'
      fieldType = parseTypeDefinition();
    }

    // Optional default value
    std::optional<std::unique_ptr<ast::Expression>> defaultValue;
    if (at().type == havel::TokenType::Assign) {
      advance(); // consume '='
      defaultValue = parsePrimaryExpression();
    }

    fields.emplace_back(fieldName, std::move(fieldType),
                        std::move(defaultValue), isClassMember);

    // Optional comma
    if (at().type == havel::TokenType::Comma) {
      advance();
    }
  }

  return {std::move(fields), std::move(methods)};
}

std::unique_ptr<havel::ast::Statement> Parser::parseEnumDeclaration() {
  advance(); // consume 'enum'

  // Parse enum name
  if (at().type != havel::TokenType::Identifier) {
    failAt(at(), "Expected enum name after 'enum'");
  }
  std::string enumName = advance().value;

  // Parse opening brace
  if (at().type != havel::TokenType::OpenBrace) {
    failAt(at(), "Expected '{' after enum name");
  }
  advance(); // consume '{'

  // Parse variants
  auto variants = parseEnumVariants();

  // Parse closing brace
  if (at().type != havel::TokenType::CloseBrace) {
    failAt(at(), "Expected '}' to close enum definition");
  }
  advance(); // consume '}'

  // Create enum definition
  ast::EnumDefinition def(std::move(variants));

  return std::make_unique<ast::EnumDeclaration>(enumName, std::move(def));
}

std::vector<ast::EnumVariantDef> Parser::parseEnumVariants() {
  std::vector<ast::EnumVariantDef> variants;

  while (at().type != havel::TokenType::CloseBrace && notEOF()) {
    // Skip newlines and comments
    if (at().type == havel::TokenType::NewLine ||
        at().type == havel::TokenType::Comment) {
      advance();
      continue;
    }

    // Parse variant name
    if (at().type != havel::TokenType::Identifier) {
      failAt(at(), "Expected variant name in enum");
    }
    std::string variantName = advance().value;

    // Optional payload type
    std::optional<std::unique_ptr<ast::TypeDefinition>> payloadType;
    if (at().type == havel::TokenType::OpenParen) {
      advance(); // consume '('
      payloadType = parseTypeDefinition();
      if (at().type != havel::TokenType::CloseParen) {
        failAt(at(), "Expected ')' after payload type");
      }
      advance(); // consume ')'
    }

    variants.emplace_back(variantName, std::move(payloadType));

    // Optional comma
    if (at().type == havel::TokenType::Comma) {
      advance();
    }
  }

  return variants;
}

// Parse trait declaration: trait Name { fn method1(); fn method2() { default
// impl } }
std::unique_ptr<havel::ast::Statement> Parser::parseTraitDeclaration() {
  advance(); // consume 'trait'

  // Parse trait name
  if (at().type != havel::TokenType::Identifier) {
    failAt(at(), "Expected trait name after 'trait'");
  }
  auto traitName = makeIdentifier(advance());

  // Parse opening brace
  if (at().type != havel::TokenType::OpenBrace) {
    failAt(at(), "Expected '{' after trait name");
  }
  advance(); // consume '{'

  // Parse trait methods
  std::vector<std::unique_ptr<havel::ast::TraitMethod>> methods;

  while (at().type != havel::TokenType::CloseBrace && notEOF()) {
    // Skip newlines and comments
    if (at().type == havel::TokenType::NewLine ||
        at().type == havel::TokenType::Comment) {
      advance();
      continue;
    }

    // Expect 'fn' keyword
    if (at().type != havel::TokenType::Fn) {
      failAt(at(), "Expected 'fn' in trait body");
    }
    advance(); // consume 'fn'

    // Parse method name
    if (at().type != havel::TokenType::Identifier) {
      failAt(at(), "Expected method name");
    }
    auto methodName = makeIdentifier(advance());

    // Parse parameters
    if (at().type != havel::TokenType::OpenParen) {
      failAt(at(), "Expected '(' after method name");
    }
    advance(); // consume '('

    std::vector<std::unique_ptr<havel::ast::FunctionParameter>> params;
    while (at().type != havel::TokenType::CloseParen && notEOF()) {
      if (at().type == havel::TokenType::Identifier) {
        auto paramName = makeIdentifier(advance());
        params.push_back(std::make_unique<havel::ast::FunctionParameter>(
            std::move(paramName)));
      }
      if (at().type == havel::TokenType::Comma) {
        advance();
      } else if (at().type != havel::TokenType::CloseParen) {
        failAt(at(), "Expected ',' or ')' in parameter list");
      }
    }
    if (at().type != havel::TokenType::CloseParen) {
      failAt(at(), "Expected ')' after parameters");
    }
    advance(); // consume ')'

    // Check for default implementation
    std::unique_ptr<havel::ast::BlockStatement> defaultBody;
    if (at().type == havel::TokenType::OpenBrace) {
      defaultBody = parseBlockStatement();
    }

    methods.push_back(std::make_unique<havel::ast::TraitMethod>(
        std::move(methodName), std::move(params), std::move(defaultBody)));
  }

  if (at().type != havel::TokenType::CloseBrace) {
    failAt(at(), "Expected '}' to close trait definition");
  }
  advance(); // consume '}'

  return std::make_unique<havel::ast::TraitDeclaration>(std::move(traitName),
                                                        std::move(methods));
}

// Parse impl declaration: impl Trait for Type { fn method() { ... } }
std::unique_ptr<havel::ast::Statement> Parser::parseImplDeclaration() {
  advance(); // consume 'impl'

  // Parse trait name
  if (at().type != havel::TokenType::Identifier) {
    failAt(at(), "Expected trait name after 'impl'");
  }
  auto traitName = makeIdentifier(advance());

  // Expect 'for' keyword
  if (at().type != havel::TokenType::For) {
    failAt(at(), "Expected 'for' after trait name in impl declaration");
  }
  advance(); // consume 'for'

  // Parse type name
  if (at().type != havel::TokenType::Identifier) {
    failAt(at(), "Expected type name after 'for'");
  }
  auto typeName = makeIdentifier(advance());

  // Parse opening brace
  if (at().type != havel::TokenType::OpenBrace) {
    failAt(at(), "Expected '{' after type name in impl declaration");
  }
  advance(); // consume '{'

  // Parse method implementations
  std::vector<std::unique_ptr<havel::ast::FunctionDeclaration>> methods;

  while (at().type != havel::TokenType::CloseBrace && notEOF()) {
    // Skip newlines and comments
    if (at().type == havel::TokenType::NewLine ||
        at().type == havel::TokenType::Comment) {
      advance();
      continue;
    }

    // Parse function declaration
    if (at().type != havel::TokenType::Fn) {
      failAt(at(), "Expected 'fn' in impl body");
    }
    auto funcStmt = parseFunctionDeclaration();
    methods.push_back(std::unique_ptr<havel::ast::FunctionDeclaration>(
        static_cast<havel::ast::FunctionDeclaration *>(funcStmt.release())));
  }

  if (at().type != havel::TokenType::CloseBrace) {
    failAt(at(), "Expected '}' to close impl declaration");
  }
  advance(); // consume '}'

  return std::make_unique<havel::ast::ImplDeclaration>(
      std::move(traitName), std::move(typeName), std::move(methods));
}

std::unique_ptr<ast::TypeDefinition> Parser::parseTypeDefinition() {
  std::string typeName;
  switch (at().type) {
  case havel::TokenType::Identifier:
    typeName = advance().value;
    break;
  case havel::TokenType::Fn:
    typeName = "fn";
    advance();
    break;
  case havel::TokenType::Struct:
    typeName = "struct";
    advance();
    break;
  case havel::TokenType::Class:
    typeName = "class";
    advance();
    break;
  default:
    failAt(at(), "Expected type name");
  }

  // Parse zero or more array suffixes: T[], T[][], ...
  while (at().type == havel::TokenType::OpenBracket) {
    advance(); // consume '['
    if (at().type != havel::TokenType::CloseBracket) {
      failAt(at(), "Expected ']' in array type annotation");
    }
    advance(); // consume ']'
    typeName += "[]";
  }

  // For now, just create a type reference
  // Could be extended to parse generic types like List(Int)
  return std::make_unique<ast::TypeReference>(typeName);
}

std::unique_ptr<ast::TypeAnnotation> Parser::parseTypeAnnotation() {
  // Note: ':' should already be consumed by caller for parameter annotations
  // For standalone type annotations, we consume the ':' here
  if (at().type == havel::TokenType::Colon) {
    advance(); // consume ':'
  }

  auto type = parseTypeDefinition();
  return std::make_unique<ast::TypeAnnotation>(std::move(type));
}

std::unique_ptr<havel::ast::Statement> Parser::parseThrowStatement() {
  advance(); // consume 'throw'
  auto value = parseExpression();
  return std::make_unique<havel::ast::ThrowStatement>(std::move(value));
}

// Parse del statement: del expression
// Supports: del variable, del obj.field, del arr[index]
// Also handles del(...) as a function call if followed by (
std::unique_ptr<havel::ast::Statement> Parser::parseDelStatement() {
  // If followed by '(', treat as function call: del(args)
  if (at(1).type == havel::TokenType::OpenParen) {
    // Parse as function call expression: del(args)
    auto call = std::make_unique<havel::ast::CallExpression>(
        std::make_unique<havel::ast::Identifier>("del"));
    advance(); // consume 'del'
    advance(); // consume '('
    while (at().type != havel::TokenType::CloseParen && notEOF()) {
      call->args.push_back(parseExpression());
      if (at().type == havel::TokenType::Comma) advance();
    }
    if (at().type == havel::TokenType::CloseParen) advance();
    return std::make_unique<havel::ast::ExpressionStatement>(std::move(call));
  }
  advance(); // consume 'del'
  auto target = parseExpression();
  return std::make_unique<havel::ast::DelStatement>(std::move(target));
}

// Parse UI declarative block: ui { window "Title" { ... } }
// Desugars to imperative API calls
std::unique_ptr<havel::ast::Statement> Parser::parseUIDeclaration() {
  advance(); // consume "ui"

  if (at().type != havel::TokenType::OpenBrace) {
    failAt(at(), "Expected '{' after 'ui'");
  }
  advance(); // consume '{'

  // Create a block to hold all the desugared statements
  auto block = std::make_unique<havel::ast::BlockStatement>();

  // Parse UI element declarations
  while (notEOF() && at().type != havel::TokenType::CloseBrace) {
    // Skip newlines
    while (at().type == havel::TokenType::NewLine) {
      advance();
    }

    if (at().type == havel::TokenType::CloseBrace) {
      break;
    }

    // Parse each top-level UI element (like window, modal, etc.)
    if (at().type == havel::TokenType::Identifier) {
      parseUIElementDeclaration("", false, block->body);
    } else {
      // Unexpected token, skip it
      advance();
    }

    // Skip newlines
    while (at().type == havel::TokenType::NewLine) {
      advance();
    }
  }

  if (at().type != havel::TokenType::CloseBrace) {
    failAt(at(), "Expected '}' to close ui block");
  }
  advance(); // consume '}'

  return block;
}

// Parse a single UI element declaration and its children
// Returns statements to add to parent block
void Parser::parseUIElementDeclaration(
    const std::string &parentVar, bool addToParent,
    std::vector<std::unique_ptr<ast::Statement>> &statements) {

  if (at().type != havel::TokenType::Identifier) {
    return;
  }

  std::string elemType = advance().value; // e.g., "window", "button", "column"

  // Generate a unique variable name for this element
  static int elemCounter = 0;
  std::string varName =
      "__ui_" + elemType + "_" + std::to_string(elemCounter++);

  // Parse element arguments (title, label, etc.)
  std::vector<std::unique_ptr<havel::ast::Expression>> args;

  // First argument is usually a string (title, label, placeholder)
  if (at().type == havel::TokenType::String ||
      at().type == havel::TokenType::MultilineString) {
    args.push_back(
        std::make_unique<havel::ast::StringLiteral>(advance().value));
  }

  // Create the constructor call: ui.window("Title")
  auto uiMember = std::make_unique<havel::ast::MemberExpression>(
      std::make_unique<havel::ast::Identifier>("ui"),
      std::make_unique<havel::ast::Identifier>(elemType));
  auto constructorCall = std::make_unique<havel::ast::CallExpression>(
      std::move(uiMember), std::move(args));

  // Create the let declaration: let varName = ui.elemType(...)
  auto letDecl = std::make_unique<havel::ast::LetDeclaration>(
      std::make_unique<havel::ast::Identifier>(varName),
      std::move(constructorCall), std::nullopt, false);
  statements.push_back(std::move(letDecl));

  // Parse children if there's a block
  if (at().type == havel::TokenType::OpenBrace) {
    advance(); // consume '{'

    // Parse child elements and add them
    while (notEOF() && at().type != havel::TokenType::CloseBrace) {
      // Skip newlines
      while (at().type == havel::TokenType::NewLine) {
        advance();
      }

      if (at().type == havel::TokenType::CloseBrace) {
        break;
      }

      // Check for event handlers: onClick => { ... }
      if (at().type == havel::TokenType::Identifier &&
          at().value.rfind("on", 0) == 0 && // starts with "on"
          at(1).type == havel::TokenType::Arrow) {
        std::string eventName = advance().value; // e.g., "onClick"
        advance();                               // consume '=>'

        // Parse the handler (lambda or expression)
        std::unique_ptr<havel::ast::Expression> handler;
        if (at().type == havel::TokenType::OpenBrace) {
          // Block handler: create lambda
          auto body = parseBlockStatement();
          auto lambda = std::make_unique<havel::ast::LambdaExpression>();
          lambda->body = std::move(body);
          handler = std::move(lambda);
        } else {
          // Expression handler
          handler = parseExpression();
        }

        // Create: varName.onClick(handler)
        std::vector<std::unique_ptr<havel::ast::Expression>> handlerArgs;
        handlerArgs.push_back(std::move(handler));
        auto eventMember = std::make_unique<havel::ast::MemberExpression>(
            std::make_unique<havel::ast::Identifier>(varName),
            std::make_unique<havel::ast::Identifier>(eventName));
        auto eventCall = std::make_unique<havel::ast::CallExpression>(
            std::move(eventMember), std::move(handlerArgs));

        statements.push_back(std::make_unique<havel::ast::ExpressionStatement>(
            std::move(eventCall)));
      }
      // Check for style method calls: pad(10), bg("#000"), etc.
      else if (at().type == havel::TokenType::Identifier &&
               at(1).type == havel::TokenType::OpenParen) {
        std::string methodName = advance().value;
        advance(); // consume '('

        std::vector<std::unique_ptr<havel::ast::Expression>> methodArgs;
        while (at().type != havel::TokenType::CloseParen) {
          methodArgs.push_back(parseExpression());
          if (at().type == havel::TokenType::Comma) {
            advance();
          }
        }
        advance(); // consume ')'

        // Create: varName.methodName(args)
        auto methodMember = std::make_unique<havel::ast::MemberExpression>(
            std::make_unique<havel::ast::Identifier>(varName),
            std::make_unique<havel::ast::Identifier>(methodName));
        auto methodCall = std::make_unique<havel::ast::CallExpression>(
            std::move(methodMember), std::move(methodArgs));

        statements.push_back(std::make_unique<havel::ast::ExpressionStatement>(
            std::move(methodCall)));
      }
      // Regular child element
      else if (at().type == havel::TokenType::Identifier) {
        parseUIElementDeclaration(varName, true, statements);
      } else {
        advance(); // skip unexpected token
      }

      // Skip newlines
      while (at().type == havel::TokenType::NewLine) {
        advance();
      }
    }

    if (at().type != havel::TokenType::CloseBrace) {
      failAt(at(), "Expected '}' to close element block");
    }
    advance(); // consume '}'
  }

  // If we have a parent, add the .add() call
  if (addToParent && !parentVar.empty()) {
    std::vector<std::unique_ptr<havel::ast::Expression>> addArgs;
    addArgs.push_back(std::make_unique<havel::ast::Identifier>(varName));
    auto addMember = std::make_unique<havel::ast::MemberExpression>(
        std::make_unique<havel::ast::Identifier>(parentVar),
        std::make_unique<havel::ast::Identifier>("add"));
    auto addCall = std::make_unique<havel::ast::CallExpression>(
        std::move(addMember), std::move(addArgs));

    statements.push_back(
        std::make_unique<havel::ast::ExpressionStatement>(std::move(addCall)));
  }
}

std::unique_ptr<havel::ast::Statement> Parser::parseTryStatement() {
  advance(); // consume 'try'

  // Parse try body
  if (at().type != havel::TokenType::OpenBrace) {
    failAt(at(), "Expected '{' after 'try'");
  }
  auto tryBody = parseBlockStatement();

  std::unique_ptr<havel::ast::Identifier> catchVariable = nullptr;
  std::unique_ptr<havel::ast::Statement> catchBody = nullptr;

  // Parse optional catch
  if (at().type == havel::TokenType::Catch) {
    advance(); // consume 'catch'

    // Support both syntaxes: catch e { and catch (e) {
    if (at().type == havel::TokenType::OpenParen) {
      advance(); // consume '('
      if (at().type == havel::TokenType::Identifier) {
        catchVariable = makeIdentifier(advance());
      }
      if (at().type != havel::TokenType::CloseParen) {
        failAt(at(), "Expected ')' after catch variable");
      }
      advance(); // consume ')'
    } else if (at().type == havel::TokenType::Identifier) {
      // Old syntax without parentheses
      catchVariable = makeIdentifier(advance());
    }

    // Parse catch body
    if (at().type != havel::TokenType::OpenBrace) {
      failAt(at(), "Expected '{' after catch");
    }
    catchBody = parseBlockStatement();
  }

  // Parse optional finally
  std::unique_ptr<havel::ast::Statement> finallyBlock = nullptr;
  if (at().type == havel::TokenType::Finally) {
    advance(); // consume 'finally'

    if (at().type != havel::TokenType::OpenBrace) {
      failAt(at(), "Expected '{' after 'finally'");
    }
    finallyBlock = parseBlockStatement();
  }

  return std::make_unique<havel::ast::TryExpression>(
      std::move(tryBody), std::move(catchVariable), std::move(catchBody),
      std::move(finallyBlock));
}

std::unique_ptr<havel::ast::Statement> Parser::parseIfStatement() {
  advance(); // consume "if"

  bool prevAllow = context.allowBraceSugar;
  context.allowBraceSugar = false;
  auto condition = parseExpression();
  context.allowBraceSugar = prevAllow;

  std::unique_ptr<havel::ast::Statement> consequence;

  // Block form or inline form
  if (at().type == havel::TokenType::OpenBrace) {
    consequence = parseBlockStatement();
  } else {
    // Inline form - single statement (don't skip newlines)
    consequence = parseInlineStatement();
  }

  // Skip newlines before checking for else
  while (at().type == havel::TokenType::NewLine) {
    advance();
  }

  std::unique_ptr<havel::ast::Statement> alternative = nullptr;
  if (at().type == havel::TokenType::Else) {
    advance(); // consume "else"

    if (at().type == havel::TokenType::If) {
      alternative = parseIfStatement();
    } else if (at().type == havel::TokenType::OpenBrace) {
      alternative = parseBlockStatement();
    } else {
      // Inline else - single statement (don't skip newlines)
      alternative = parseInlineStatement();
    }
  }

  return std::make_unique<havel::ast::IfStatement>(
      std::move(condition), std::move(consequence), std::move(alternative));
}

std::unique_ptr<havel::ast::Statement> Parser::parseWhileStatement() {
  advance(); // consume "while"

  bool prevAllow = context.allowBraceSugar;
  context.allowBraceSugar = false;
  auto condition = parseExpression();
  context.allowBraceSugar = prevAllow;

  std::unique_ptr<havel::ast::Statement> body;

  // Block form or inline form
  if (at().type == havel::TokenType::OpenBrace) {
    body = parseBlockStatement();
  } else {
    // Inline form - single statement (don't skip newlines)
    body = parseInlineStatement();
  }

  return std::make_unique<havel::ast::WhileStatement>(std::move(condition),
                                                      std::move(body));
}

std::unique_ptr<havel::ast::Statement> Parser::parseDoWhileStatement() {
  advance(); // consume "do"

  // Parse the body block
  std::unique_ptr<havel::ast::Statement> body;
  if (at().type == havel::TokenType::OpenBrace) {
    body = parseBlockStatement();
  } else {
    failAt(at(), "Expected '{' after 'do'");
  }

  // Expect "while" keyword
  if (at().type != havel::TokenType::While) {
    failAt(at(), "Expected 'while' after do-while body");
  }
  advance(); // consume "while"

  // Parse the condition
  bool prevAllow = context.allowBraceSugar;
  context.allowBraceSugar = false;
  auto condition = parseExpression();
  context.allowBraceSugar = prevAllow;

  return std::make_unique<havel::ast::DoWhileStatement>(std::move(body),
                                                        std::move(condition));
}

std::unique_ptr<havel::ast::Statement> Parser::parseSwitchStatement() {
  advance(); // consume "switch"

  // Parse the switch expression
  bool prevAllow = context.allowBraceSugar;
  context.allowBraceSugar = false;
  auto expression = parseExpression();
  context.allowBraceSugar = prevAllow;

  // Expect opening brace
  if (at().type != havel::TokenType::OpenBrace) {
    failAt(at(), "Expected '{' after switch expression");
  }
  advance(); // consume "{"

  std::vector<std::unique_ptr<havel::ast::SwitchCase>> cases;

  // Parse switch cases
  while (notEOF() && at().type != havel::TokenType::CloseBrace) {
    // Skip newlines
    while (at().type == havel::TokenType::NewLine) {
      advance();
    }

    if (at().type == havel::TokenType::CloseBrace) {
      break;
    }

    // Parse case test expression or 'else'
    std::unique_ptr<havel::ast::Expression> test = nullptr;

    if (at().type == havel::TokenType::Else) {
      advance(); // consume "else"
    } else {
      // Parse case test expression
      test = parseExpression();
    }

    // Expect '=>'
    if (at().type != havel::TokenType::Arrow) {
      failAt(at(), "Expected '=>' after switch case test");
    }
    advance(); // consume "=>"

    // Parse case body
    std::unique_ptr<havel::ast::Statement> caseBody;
    if (at().type == havel::TokenType::OpenBrace) {
      caseBody = parseBlockStatement();
    } else {
      // Single expression statement
      auto expr = parseExpression();
      caseBody =
          std::make_unique<havel::ast::ExpressionStatement>(std::move(expr));
    }

    cases.push_back(std::make_unique<havel::ast::SwitchCase>(
        std::move(test), std::move(caseBody)));

    // Skip newlines after case
    while (at().type == havel::TokenType::NewLine) {
      advance();
    }
  }

  // Expect closing brace
  if (at().type != havel::TokenType::CloseBrace) {
    failAt(at(), "Expected '}' to close switch statement");
  }
  advance(); // consume "}"

  return std::make_unique<havel::ast::SwitchStatement>(std::move(expression),
                                                       std::move(cases));
}

std::unique_ptr<havel::ast::Statement> Parser::parseForStatement() {
  advance(); // consume "for"

  std::vector<std::unique_ptr<havel::ast::Identifier>> iterators;

  // Check for multiple iterators in parentheses: for (key, value) in dict
  if (at().type == havel::TokenType::OpenParen) {
    advance(); // consume "("

    // Parse first iterator - allow keywords as variable names
    if (at().type != havel::TokenType::Identifier &&
        at().type != havel::TokenType::Let &&
        at().type != havel::TokenType::Const &&
        at().type != havel::TokenType::Const &&
        at().type != havel::TokenType::If &&
        at().type != havel::TokenType::For &&
        at().type != havel::TokenType::While &&
        at().type != havel::TokenType::Match) {
      failAt(at(), "Expected first iterator variable in parentheses");
    }
    iterators.push_back(makeIdentifier(advance()));

    // Parse additional iterators separated by commas
    while (at().type == havel::TokenType::Comma) {
      advance(); // consume ","

      // Skip newlines after comma
      while (at().type == havel::TokenType::NewLine) {
        advance();
      }

      // Allow keywords as iterator names
      if (at().type != havel::TokenType::Identifier &&
          at().type != havel::TokenType::Let &&
          at().type != havel::TokenType::Const &&
          at().type != havel::TokenType::Const &&
          at().type != havel::TokenType::If &&
          at().type != havel::TokenType::For &&
          at().type != havel::TokenType::While &&
          at().type != havel::TokenType::Match) {
        failAt(at(), "Expected iterator variable after comma");
      }
      iterators.push_back(makeIdentifier(advance()));
    }

    // Skip newlines before closing paren or 'in'
    while (at().type == havel::TokenType::NewLine) {
      advance();
    }

    // Check for closing paren (for multiple iterators) or 'in' (for single
    // iterator)
    if (at().type == havel::TokenType::CloseParen) {
      advance(); // consume ")"
    } else if (at().type != havel::TokenType::In) {
      failAt(at(), "Expected ')' or 'in' after iterator variable(s)");
    }
  } else {
    // Single or multiple iterators: for i, j in range (without parentheses)
    // Allow keywords as iterator names
    if (at().type != havel::TokenType::Identifier &&
        at().type != havel::TokenType::Let &&
        at().type != havel::TokenType::Const &&
        at().type != havel::TokenType::Const &&
        at().type != havel::TokenType::If &&
        at().type != havel::TokenType::For &&
        at().type != havel::TokenType::While &&
        at().type != havel::TokenType::Match) {
      failAt(at(), "Expected iterator variable after 'for'");
    }
    iterators.push_back(makeIdentifier(advance()));

    // Parse additional iterators separated by commas
    while (at().type == havel::TokenType::Comma) {
      advance(); // consume ","
      // Skip newlines after comma
      while (at().type == havel::TokenType::NewLine) {
        advance();
      }
      // Allow keywords as iterator names
      if (at().type != havel::TokenType::Identifier &&
          at().type != havel::TokenType::Let &&
          at().type != havel::TokenType::Const &&
          at().type != havel::TokenType::Const &&
          at().type != havel::TokenType::If &&
          at().type != havel::TokenType::For &&
          at().type != havel::TokenType::While &&
          at().type != havel::TokenType::Match) {
        failAt(at(), "Expected iterator variable after comma");
      }
      iterators.push_back(makeIdentifier(advance()));
    }
  }

  if (at().type != havel::TokenType::In) {
    failAt(at(), "Expected 'in' after iterator variable(s)");
  }
  advance(); // consume "in"

  // Disable brace call sugar to prevent for loop body { from being consumed
  bool prevAllow = context.allowBraceSugar;
  context.allowBraceSugar = false;
  auto iterable = parseExpression();
  context.allowBraceSugar = prevAllow;

  // Skip newlines before body
  while (at().type == havel::TokenType::NewLine) {
    advance();
  }

  std::unique_ptr<havel::ast::Statement> body;

  // Block form or inline form
  if (at().type == havel::TokenType::OpenBrace) {
    body = parseBlockStatement();
  } else {
    // Inline form - single statement (don't skip newlines)
    body = parseInlineStatement();
  }

  auto stmt = std::make_unique<havel::ast::ForStatement>(
      std::move(iterators), std::move(iterable), std::move(body));
  return stmt;
}

std::unique_ptr<havel::ast::Statement> Parser::parseLoopStatement() {
  advance(); // consume "loop"

  // Check for optional count or "while condition"
  std::unique_ptr<havel::ast::Expression> countExpr;
  std::unique_ptr<havel::ast::Expression> condition;

  // Skip newlines
  while (at().type == havel::TokenType::NewLine) {
    advance();
  }

  // Check for loop count: loop 5 { ... } or loop n { ... }
  // We need to peek ahead to see if the next token is a number or identifier
  // followed by an opening brace (not an operator)
  if ((at().type == havel::TokenType::Number ||
       at().type == havel::TokenType::Identifier) &&
      notEOF()) {
    // Peek ahead to check if this is followed by { or an operator
    size_t savedPos = position;

    // Skip the count expression
    if (at().type == havel::TokenType::Number ||
        at().type == havel::TokenType::Identifier) {
      advance();
    }

    // Skip newlines
    while (at().type == havel::TokenType::NewLine) {
      advance();
    }

    // If followed by {, this is a count-based loop
    if (at().type == havel::TokenType::OpenBrace) {
      position = savedPos; // Restore position
      // Parse just the count value, not a full expression that might consume
// the brace
        if (at().type == havel::TokenType::Number) {
            countExpr =
                std::make_unique<havel::ast::NumberLiteral>(parseNumberLiteral(at().value), hasDecimalPart(at().value));
            advance();
      } else if (at().type == havel::TokenType::Identifier) {
        countExpr = std::make_unique<havel::ast::Identifier>(
            at().value, at().line, at().column);
        advance();
      }

      // Skip newlines before body
      while (at().type == havel::TokenType::NewLine) {
        advance();
      }
    } else {
      position = savedPos; // Restore position, not a count-based loop
    }
  }

  // Check for "while condition"
  if (at().type == havel::TokenType::While) {
    advance(); // consume "while"

    // Parse condition expression
    bool prevAllow = context.allowBraceSugar;
    context.allowBraceSugar = false;
    condition = parseExpression();
    context.allowBraceSugar = prevAllow;

    // Skip newlines before body
    while (at().type == havel::TokenType::NewLine) {
      advance();
    }
  }

  std::unique_ptr<havel::ast::Statement> body;

  // Block form or inline form
  if (at().type == havel::TokenType::OpenBrace) {
    body = parseBlockStatement();
  } else {
    // Inline form - single statement (don't skip newlines)
    body = parseInlineStatement();
  }

  auto loopStmt = std::make_unique<havel::ast::LoopStatement>(
      std::move(body), std::move(condition));
  loopStmt->countExpr = std::move(countExpr);
  return loopStmt;
}

std::unique_ptr<havel::ast::Statement> Parser::parseBreakStatement() {
  advance(); // consume "break"
  return std::make_unique<havel::ast::BreakStatement>();
}

std::unique_ptr<havel::ast::Statement> Parser::parseContinueStatement() {
  advance(); // consume "continue"
  return std::make_unique<havel::ast::ContinueStatement>();
}

std::unique_ptr<havel::ast::Statement> Parser::parseOnStatement() {
  advance(); // consume "on"

  // Check what follows "on"
  if (at().type == havel::TokenType::Mode) {
    // on mode {name} { ... }
    return parseOnModeStatementBody();
  } else if (at().type == havel::TokenType::Identifier) {
    std::string keyword = at().value;
    if (keyword == "reload") {
      advance(); // consume "reload"
      return parseOnReloadStatement();
    } else if (keyword == "start") {
      advance(); // consume "start"
      return parseOnStartStatement();
    } else if (keyword == "tap" || keyword == "combo") {
      // on tap(key) => { ... } or on combo(key) => { ... }
      return parseOnTapOrComboStatement();
    } else if (keyword == "keydown" || keyword == "keyup") {
      // on keyDown { ... } or on keyDown(keys...) { ... }
      return parseOnKeyDownOrKeyUpStatement();
    } else {
      // Generic message handler: on <identifier> { ... }
      // This creates a message handler in the current scope
      std::string msgVar = advance().value; // consume identifier
      
      // Expect block
      if (at().type != havel::TokenType::OpenBrace) {
        failAt(at(), "Expected '{' after message variable");
        return nullptr;
      }
      
      auto body = parseBlockStatement();
      
      // Create a special OnMessageStatement
      // For now, treat as a let declaration with a message binding
      // This will create a variable available in the handler block
      auto stmt = std::make_unique<havel::ast::OnMessageStatement>(msgVar, std::move(body));
      stmt->line = at().line;
      stmt->column = at().column;
      return stmt;
    }
  }

  failAt(at(), "Expected 'mode', 'reload', 'start', 'tap', 'combo', 'keydown', "
               "'keyup', or message variable after 'on'");
  return nullptr;
}

std::unique_ptr<havel::ast::Statement> Parser::parseOnModeStatementBody() {
  // Expect "mode" keyword (already consumed "on")
  advance(); // consume "mode"

  // Get mode name
  if (at().type != havel::TokenType::Identifier) {
    failAt(at(), "Expected mode name after 'on mode'");
  }
  std::string modeName = advance().value;

  // Skip newlines
  while (at().type == havel::TokenType::NewLine) {
    advance();
  }

  // Parse body block
  if (at().type != havel::TokenType::OpenBrace) {
    failAt(at(), "Expected '{' after mode name");
  }
  auto body = parseBlockStatement();

  // Check for else block
  std::unique_ptr<havel::ast::Statement> alternative = nullptr;
  if (at().type == havel::TokenType::Else) {
    advance(); // consume "else"

    // Skip newlines
    while (at().type == havel::TokenType::NewLine) {
      advance();
    }

    if (at().type == havel::TokenType::OpenBrace) {
      alternative = parseBlockStatement();
    } else {
      failAt(at(), "Expected '{' after else");
    }
  }

  return std::make_unique<havel::ast::OnModeStatement>(
      modeName, std::move(body), std::move(alternative));
}

std::unique_ptr<havel::ast::Statement> Parser::parseOnModeStatement() {
  advance(); // consume "on"

  // Expect "mode" keyword
  if (at().type != havel::TokenType::Mode) {
    failAt(at(), "Expected 'mode' after 'on'");
  }
  advance(); // consume "mode"

  // Get mode name
  if (at().type != havel::TokenType::Identifier) {
    failAt(at(), "Expected mode name after 'on mode'");
  }
  std::string modeName = advance().value;

  // Skip newlines
  while (at().type == havel::TokenType::NewLine) {
    advance();
  }

  // Parse body block
  if (at().type != havel::TokenType::OpenBrace) {
    failAt(at(), "Expected '{' after mode name");
  }
  auto body = parseBlockStatement();

  // Check for else block
  std::unique_ptr<havel::ast::Statement> alternative = nullptr;
  if (at().type == havel::TokenType::Else) {
    advance(); // consume "else"

    // Skip newlines
    while (at().type == havel::TokenType::NewLine) {
      advance();
    }

    if (at().type == havel::TokenType::OpenBrace) {
      alternative = parseBlockStatement();
    } else {
      failAt(at(), "Expected '{' after else");
    }
  }

  return std::make_unique<havel::ast::OnModeStatement>(
      modeName, std::move(body), std::move(alternative));
}

std::unique_ptr<havel::ast::Statement> Parser::parseOffModeStatement() {
  advance(); // consume "off"

  // Expect "mode" keyword
  if (at().type != havel::TokenType::Mode) {
    failAt(at(), "Expected 'mode' after 'off'");
  }
  advance(); // consume "mode"

  // Get mode name
  if (at().type != havel::TokenType::Identifier) {
    failAt(at(), "Expected mode name after 'off mode'");
  }
  std::string modeName = advance().value;

  // Skip newlines
  while (at().type == havel::TokenType::NewLine) {
    advance();
  }

  // Parse body block
  if (at().type != havel::TokenType::OpenBrace) {
    failAt(at(), "Expected '{' after mode name");
  }
  auto body = parseBlockStatement();

  return std::make_unique<havel::ast::OffModeStatement>(modeName,
                                                        std::move(body));
}

std::unique_ptr<havel::ast::Statement> Parser::parseOnReloadStatement() {
  // Skip newlines
  while (at().type == havel::TokenType::NewLine) {
    advance();
  }

  // Parse body block
  if (at().type != havel::TokenType::OpenBrace) {
    failAt(at(), "Expected '{' after 'on reload'");
  }
  auto body = parseBlockStatement();

  return std::make_unique<havel::ast::OnReloadStatement>(std::move(body));
}

std::unique_ptr<havel::ast::Statement> Parser::parseOnStartStatement() {
  // Skip newlines
  while (at().type == havel::TokenType::NewLine) {
    advance();
  }

  // Parse body block
  if (at().type != havel::TokenType::OpenBrace) {
    failAt(at(), "Expected '{' after 'on start'");
  }
  auto body = parseBlockStatement();

  return std::make_unique<havel::ast::OnStartStatement>(std::move(body));
}

std::unique_ptr<havel::ast::Statement> Parser::parseOnTapOrComboStatement() {
  // We've consumed "on" and identified "tap" or "combo"
  bool isTap = (at().value == "tap");
  advance(); // consume "tap" or "combo"

  // Expect opening parenthesis
  if (at().type != havel::TokenType::OpenParen) {
    failAt(at(), "Expected '(' after 'on " +
                     std::string(isTap ? "tap" : "combo") + "'");
  }
  advance(); // consume '('

  // Parse key (Hotkey token or Identifier)
  std::string keyName;
  if (at().type == havel::TokenType::Hotkey) {
    keyName = advance().value;
  } else if (at().type == havel::TokenType::Identifier) {
    keyName = advance().value;
  } else {
    failAt(at(), "Expected key name (e.g., 'lwin', 'f1', 'escape')");
  }

  // Expect closing parenthesis
  if (at().type != havel::TokenType::CloseParen) {
    failAt(at(), "Expected ')' after key name");
  }
  advance(); // consume ')'

  // Skip newlines
  while (at().type == havel::TokenType::NewLine) {
    advance();
  }

  // Expect arrow operator
  if (at().type != havel::TokenType::Arrow) {
    failAt(at(), "Expected '=>' after key specification");
  }
  advance(); // consume '=>'

  // Skip newlines
  while (at().type == havel::TokenType::NewLine) {
    advance();
  }

  // Parse action block
  if (at().type != havel::TokenType::OpenBrace) {
    failAt(at(), "Expected '{' to start action block");
  }
  auto actionBlock = parseBlockStatement();

  // Create the statement
  if (isTap) {
    return std::make_unique<havel::ast::OnTapStatement>(keyName,
                                                        std::move(actionBlock));
  } else {
    return std::make_unique<havel::ast::OnComboStatement>(
        keyName, std::move(actionBlock));
  }
}

std::unique_ptr<havel::ast::Statement>
Parser::parseOnKeyDownOrKeyUpStatement() {
  // We've consumed "on" and identified "keydown" or "keyup"
  bool isKeyDown = (at().value == "keydown");
  advance(); // consume "keydown" or "keyup"

  // Skip newlines
  while (at().type == havel::TokenType::NewLine) {
    advance();
  }

  // Check if there's a key list in parentheses
  std::vector<std::string> keys;
  if (at().type == havel::TokenType::OpenParen) {
    advance(); // consume '('

    // Parse comma-separated key names
    while (true) {
      // Skip newlines
      while (at().type == havel::TokenType::NewLine) {
        advance();
      }

      // Parse key (Hotkey token or Identifier)
      if (at().type == havel::TokenType::Hotkey) {
        keys.push_back(advance().value);
      } else if (at().type == havel::TokenType::Identifier) {
        keys.push_back(advance().value);
      } else {
        failAt(at(), "Expected key name in key list");
      }

      // Skip newlines
      while (at().type == havel::TokenType::NewLine) {
        advance();
      }

      // Check for comma or closing paren
      if (at().type == havel::TokenType::CloseParen) {
        advance(); // consume ')'
        break;
      } else if (at().type == havel::TokenType::Comma) {
        advance(); // consume ','
      } else {
        failAt(at(), "Expected ',' or ')' in key list");
      }
    }
  }

  // Skip newlines
  while (at().type == havel::TokenType::NewLine) {
    advance();
  }

  // Expect action block
  if (at().type != havel::TokenType::OpenBrace) {
    failAt(at(), "Expected '{' to start action block");
  }
  auto actionBlock = parseBlockStatement();

  // Create the statement
  if (isKeyDown) {
    return std::make_unique<havel::ast::OnKeyDownStatement>(
        keys, std::move(actionBlock));
  } else {
    return std::make_unique<havel::ast::OnKeyUpStatement>(
        keys, std::move(actionBlock));
  }
}

std::unique_ptr<havel::ast::Statement> Parser::parseLetDeclaration() {
    bool isConst = false;

    // Check if this is 'const' or 'let'
    if (at().type == havel::TokenType::Const) {
        isConst = true;
    }
    advance(); // consume "let" or "const"

    std::unique_ptr<havel::ast::Expression> pattern;

    // Check if we have a destructuring pattern
    if (at().type == havel::TokenType::OpenBracket) {
        // Array destructuring: let [a, b] = arr
        pattern = parseArrayPattern();
    } else if (at().type == havel::TokenType::OpenParen) {
        // Tuple destructuring: let (a, b) = tuple
        advance(); // consume '('
        std::vector<std::unique_ptr<havel::ast::Expression>> elements;
        while (notEOF() && at().type != havel::TokenType::CloseParen) {
            while (at().type == havel::TokenType::NewLine) {
                advance();
            }
            if (at().type == havel::TokenType::CloseParen) {
                break;
            }
            if (at().type != havel::TokenType::Identifier) {
                failAt(at(), "Tuple destructuring expects identifiers");
            }
            elements.push_back(makeIdentifier(advance()));
            while (at().type == havel::TokenType::NewLine) {
                advance();
            }
            if (at().type == havel::TokenType::Comma) {
                advance();
            } else if (at().type != havel::TokenType::CloseParen) {
                failAt(at(), "Expected ',' or ')' in tuple destructuring");
            }
        }
        if (at().type != havel::TokenType::CloseParen) {
            failAt(at(), "Expected ')' to close tuple destructuring");
        }
        advance(); // consume ')'
        pattern = std::make_unique<havel::ast::ArrayPattern>(std::move(elements));
    } else if (at().type == havel::TokenType::OpenBrace) {
        // Object destructuring: let {x, y} = obj
        pattern = parseObjectPattern();
    } else if (at().type == havel::TokenType::Identifier) {
        // Regular variable: let x = value
        // Or comma-separated: let a, b, c = value
        pattern = makeIdentifier(advance());
        
        // Check for comma-separated identifiers: let a, b, c = value
        if (at().type == havel::TokenType::Comma) {
            std::vector<std::unique_ptr<havel::ast::Expression>> elements;
            elements.push_back(std::move(pattern));
            
            while (at().type == havel::TokenType::Comma) {
                advance(); // consume comma
                if (at().type != havel::TokenType::Identifier) {
                    failAt(at(), "Expected identifier in comma-separated declaration");
                }
                elements.push_back(makeIdentifier(advance()));
            }
            
            // Create an ArrayPattern to hold the multiple identifiers
            pattern = std::make_unique<havel::ast::ArrayPattern>(std::move(elements));
        }
    } else {
        failAt(at(), "Expected identifier, '[' or '{' after '" +
        std::string(isConst ? "const" : "let") + "'");
    }

    // Check for type annotation (let x: int = 5)
    std::optional<std::unique_ptr<havel::ast::TypeAnnotation>> typeAnnotation;
    if (at().type == havel::TokenType::Colon) {
        typeAnnotation = parseTypeAnnotation();
    }

    if (at().type != havel::TokenType::Assign) {
        // Allow declarations without assignment, e.g., `let x;`
        if (dynamic_cast<havel::ast::Identifier *>(pattern.get())) {
            return std::make_unique<havel::ast::LetDeclaration>(
                std::move(pattern), nullptr, std::move(typeAnnotation), isConst);
        } else {
            failAt(at(), "Destructuring patterns require initialization");
        }
    }
    advance(); // consume "="

    auto value = parseExpression();

    auto result = std::make_unique<havel::ast::LetDeclaration>(
        std::move(pattern), std::move(value), std::move(typeAnnotation), isConst);
    return result;
}

// Parse hotkey as expression (for assignment: hk = ^t => { ... })
// Returns HotkeyExpression (wraps HotkeyBinding as expression)
std::unique_ptr<havel::ast::Expression> Parser::parseHotkeyExpression(const Token &hotkeyToken) {
  // hotkeyToken was already consumed by the Pratt parser's nud()

  // Check for prefix condition (before =>)
  std::unique_ptr<havel::ast::Expression> prefixCondition = nullptr;
  if (at().type == havel::TokenType::When) {
    advance(); // consume 'when'
    prefixCondition = parsePrattExpression(bp(BindingPower::Assignment));
  } else if (at().type == havel::TokenType::If) {
    advance(); // consume 'if'
    prefixCondition = parsePrattExpression(bp(BindingPower::Assignment));
  }

  if (at().type != havel::TokenType::Arrow) {
    failAt(hotkeyToken, "Expected '=>' after hotkey literal");
    return nullptr;
  }
  advance(); // consume '=>'

  std::unique_ptr<havel::ast::BlockStatement> action;

  // => can be followed by { } block or a single expression
  if (at().type == havel::TokenType::OpenBrace) {
    action = parseBlockStatement(true); // true = input context
  } else {
    // Single expression wrapped in a block
    auto expr = parseExpression();
    action = std::make_unique<havel::ast::BlockStatement>();
    action->body.push_back(
        std::make_unique<havel::ast::ExpressionStatement>(std::move(expr)));
  }

  // Check for suffix condition (after action)
  std::unique_ptr<havel::ast::Expression> suffixCondition = nullptr;
  if (at().type == havel::TokenType::If) {
    advance(); // consume 'if'
    suffixCondition = parseExpression();
  }

  // Create the base hotkey binding
  auto binding = std::make_unique<havel::ast::HotkeyBinding>();
  binding->hotkeys.push_back(
      std::make_unique<havel::ast::HotkeyLiteral>(hotkeyToken.value));
  binding->action = std::move(action);

  // Combine conditions if needed
  if (prefixCondition || suffixCondition) {
    auto finalCondition = combineConditions(std::move(prefixCondition),
                                            std::move(suffixCondition));
    // Set condition on the binding
    binding->conditionExpr = std::move(finalCondition);
  }

  // Return HotkeyExpression (condition is on binding->conditionExpr)
  auto hkExpr = std::make_unique<havel::ast::HotkeyExpression>(std::move(binding));
  hkExpr->line = hotkeyToken.line;
  hkExpr->column = hotkeyToken.column;
  return hkExpr;
}

std::unique_ptr<havel::ast::HotkeyBinding> Parser::parseHotkeyBinding() {
  // Create the hotkey binding AST node
  auto binding = std::make_unique<havel::ast::HotkeyBinding>();

  // Parse the hotkey token (F1, Ctrl+V, etc.)
  if (at().type != havel::TokenType::Hotkey) {
    failAt(at(), "Expected hotkey token at start of hotkey binding");
  }
  auto hotkeyToken = advance();
  binding->hotkeys.push_back(
      std::make_unique<havel::ast::HotkeyLiteral>(hotkeyToken.value));

  // Check for conditional 'when' or 'if' clause
  if (at().type == havel::TokenType::When) {
    advance(); // consume 'when'

    // Parse conditions (mode X && title Y)
    while (true) {
      // Parse condition type (mode, title, class, etc.)
      if (at().type == havel::TokenType::Mode) {
        advance(); // consume 'mode'
        if (at().type == havel::TokenType::Identifier) {
          binding->conditions.push_back("mode " + advance().value);
        }
      } else if (at().type == havel::TokenType::Identifier) {
        std::string condType = advance().value;
        if (condType == "title" || condType == "class" ||
            condType == "process") {
          if (at().type == havel::TokenType::String ||
              at().type == havel::TokenType::MultilineString ||
              at().type == havel::TokenType::Identifier) {
            binding->conditions.push_back(condType + " " + advance().value);
          }
        }
      }

      // Check for && (AND operator)
      if (at().type == havel::TokenType::And) {
        advance(); // consume '&&'
        continue;
      }

      break;
    }
  } else if (at().type == havel::TokenType::If) {
    // New: Complex condition expression
    advance(); // consume 'if'
    binding->conditionExpr = parseExpression();
  }

  // Expect and consume the arrow operator '=>'
  if (at().type != havel::TokenType::Arrow) {
    failAt(at(), "Expected '=>' after hotkey '" + hotkeyToken.value + "'");
  }
  advance(); // consume the '=>'

  // Check for direct key mapping (e.g., Left => A)
  if (at().type == havel::TokenType::Identifier ||
      at().type == havel::TokenType::Hotkey) {
    // Peek ahead to see if this is a simple key mapping
    if (at(1).type == havel::TokenType::NewLine ||
        at(1).type == havel::TokenType::Semicolon ||
        at(1).type == havel::TokenType::EOF_TOKEN) {
      // Direct key mapping
      binding->isKeyMapping = true;
      binding->mappedKey = advance().value;

      // Create a simple send action
      auto sendCallee = std::make_unique<havel::ast::Identifier>("send");
      std::vector<std::unique_ptr<havel::ast::Expression>> args;
      args.push_back(
          std::make_unique<havel::ast::StringLiteral>(binding->mappedKey));
      auto sendExpr = std::make_unique<havel::ast::CallExpression>(
          std::move(sendCallee), std::move(args));

      auto exprStmt = std::make_unique<havel::ast::ExpressionStatement>();
      exprStmt->expression = std::move(sendExpr);
      binding->action = std::move(exprStmt);

      return binding;
    }
  }

  // Parse the action - could be an expression or a block statement
  if (at().type == havel::TokenType::OpenBrace) {
    // It's a block statement - parse it with input context enabled
    binding->action = parseBlockStatement(true);
  } else {
    // It's an expression - wrap it in an ExpressionStatement
    auto expr = parseExpression();
    if (!expr) {
      failAt(at(), "Failed to parse action expression after '=>'");
    }

    auto exprStmt = std::make_unique<havel::ast::ExpressionStatement>();
    exprStmt->expression = std::move(expr);
    binding->action = std::move(exprStmt);
  }

  // Validate that we successfully created the binding
  if (binding->hotkeys.empty() || !binding->action) {
    failAt(at(), "Failed to create complete hotkey binding");
  }

  return binding;
}

std::unique_ptr<ast::Expression>
Parser::combineConditions(std::unique_ptr<ast::Expression> left,
                          std::unique_ptr<ast::Expression> right) {
  if (!left)
    return right;
  if (!right)
    return left;

  // Create an AND expression combining both conditions
  return std::make_unique<ast::BinaryExpression>(
      std::move(left), ast::BinaryOperator::And, std::move(right));
}

std::unique_ptr<havel::ast::Statement> Parser::parseWhenBlock() {
  advance(); // consume 'when'

  // Parse the condition
  bool prevAllow = context.allowBraceSugar;
  context.allowBraceSugar = false;
  auto condition = parseExpression();
  context.allowBraceSugar = prevAllow;

  if (at().type != havel::TokenType::OpenBrace) {
    failAt(at(), "Expected '{' after when condition");
  }

  advance(); // consume '{'

  std::vector<std::unique_ptr<ast::Statement>> statements;

  // Parse statements until closing brace
  while (notEOF() && at().type != havel::TokenType::CloseBrace) {
    // Skip newlines and semicolons (empty statements)
    if (at().type == havel::TokenType::NewLine ||
        at().type == havel::TokenType::Semicolon) {
      advance();
      continue;
    }

    try {
      auto stmt = parseStatement();
      if (stmt) {
        // Simple version without condition inheritance for now
        statements.push_back(std::move(stmt));
      }
    } catch (const std::exception &e) {
      if (havel::debugging::debug_parser) {
        havel::error("Parse error in when block: {} at position {}", e.what(),
                     position);
      }

      // Synchronize to recover from the error
      synchronize();
      if (notEOF() == false) {
        break; // Can't synchronize, so break out of the block
      }
    }
  }

  if (at().type != havel::TokenType::CloseBrace) {
    failAt(at(), "Expected '}' to close when block");
  }
  advance(); // consume '}'

  return std::make_unique<havel::ast::WhenBlock>(std::move(condition),
                                                 std::move(statements));
}

// Parse repeat statement: repeat count { body } or repeat count statement
std::unique_ptr<havel::ast::Statement> Parser::parseRepeatStatement() {
  advance(); // consume 'repeat'

  // Parse count expression - disable brace call sugar to avoid n {...} being
  // parsed as call
  bool prevAllowBraceCallSugar = context.allowBraceSugar;
  context.allowBraceSugar = false;
  auto countExpr = parseExpression();
  context.allowBraceSugar = prevAllowBraceCallSugar;

  // Skip newlines before body
  while (at().type == havel::TokenType::NewLine) {
    advance();
  }

  std::unique_ptr<havel::ast::Statement> body;

  // Block form or inline form
  if (at().type == havel::TokenType::OpenBrace) {
    body = parseBlockStatement();
  } else {
    // Inline form - single statement (don't skip newlines)
    body = parseInlineStatement();
  }

  return std::make_unique<ast::RepeatStatement>(std::move(countExpr),
                                                std::move(body));
}

std::unique_ptr<havel::ast::BlockStatement>
Parser::parseBlockStatement(bool inputContext) {
  auto block = std::make_unique<havel::ast::BlockStatement>();

  // New grammar: support : (indented block), :: (hotkey block), and { } (brace block)
  if (at().type == havel::TokenType::Colon) {
    // Colon block: consume ':' and parse indented statements
    size_t colonColumn = at().column; // Track colon's column for dedent detection
    advance(); // consume ':'
    
    // Skip newline after colon
    while (at().type == havel::TokenType::NewLine) {
      advance();
    }
    
    // Save and set input context
    bool savedInputContext = context.inInputContext;
    context.inInputContext = inputContext;
    
    // Track the first statement's column as base indentation for dedent detection
    // Skip any leading newlines first
    while (at().type == havel::TokenType::NewLine) {
      advance();
    }
    size_t baseIndentation = at().column;
    
    // Parse statements until we hit a dedent (token at lower column than base)
    while (notEOF()) {
      // Skip empty lines
      if (at().type == havel::TokenType::NewLine) {
        advance();
        continue;
      }

      // Check for end of block conditions
      if (at().type == havel::TokenType::CloseBrace ||
          at().type == havel::TokenType::EOF_TOKEN) {
        break;
      }

      // Check if we're back at base indentation or lower (dedent)
      // Note: we use < not <= because statements at same column as base are still in the block
      // Only strictly lower column indicates dedent
      if (at().column < baseIndentation) {
        break;
      }

      size_t beforePos = position;
      auto stmt = parseStatement();
      if (stmt) {
        block->body.push_back(std::move(stmt));
      }
      if (position == beforePos && notEOF() && at().column >= baseIndentation &&
          at().type != havel::TokenType::CloseBrace && at().type != havel::TokenType::EOF_TOKEN &&
          at().type != havel::TokenType::NewLine) {
        advance();
      }
    }
    
    // Restore input context
    context.inInputContext = savedInputContext;
    
  } else if (at().type == havel::TokenType::ColonColon) {
    // Double colon block: consume '::' and parse indented statements (hotkey style)
    advance(); // consume '::'
    
    // Skip newline after ::
    while (at().type == havel::TokenType::NewLine) {
      advance();
    }
    
    // Save and set input context (always true for :: blocks)
    bool savedInputContext = context.inInputContext;
    context.inInputContext = true; // Hotkey blocks are always in input context
    
    // Parse statements until end of block
    while (notEOF()) {
      // Skip empty lines
      if (at().type == havel::TokenType::NewLine) {
        advance();
        continue;
      }

      // Check for end of block
      if (at().type == havel::TokenType::CloseBrace ||
          at().type == havel::TokenType::EOF_TOKEN) {
        break;
      }

      size_t beforePos = position;
      auto stmt = parseStatement();
      if (stmt) {
        block->body.push_back(std::move(stmt));
      }
      // Forward progress guarantee
      if (position == beforePos && notEOF() &&
          at().type != havel::TokenType::CloseBrace && at().type != havel::TokenType::EOF_TOKEN) {
        advance();
      }
    }
    
    // Restore input context
    context.inInputContext = savedInputContext;
    
  } else if (at().type == havel::TokenType::OpenBrace) {
    // Brace block: original behavior
    advance(); // consume '{'
    
    // Save and set input context
    bool savedInputContext = context.inInputContext;
    context.inInputContext = inputContext;
    
    // Parse statements until closing brace
    while (notEOF() && at().type != havel::TokenType::CloseBrace) {
      // Skip newlines and semicolons (empty statements)
      if (at().type == havel::TokenType::NewLine ||
          at().type == havel::TokenType::Semicolon) {
        advance();
        continue;
      }

      size_t beforePos = position;
      auto stmt = parseStatement();
      if (stmt) {
        block->body.push_back(std::move(stmt));
      }
      // Forward progress guarantee
      if (position == beforePos && notEOF() && at().type != havel::TokenType::CloseBrace) {
        advance();
      }
    }

    // Restore input context
    context.inInputContext = savedInputContext;
    
    // Consume closing brace
    if (at().type != havel::TokenType::CloseBrace) {
      failAt(at(), "Expected '}'");
    }
    advance();
    
  } else {
    failAt(at(), "Expected ':', '::', or '{' to start block");
  }

  return block;
}
std::unique_ptr<havel::ast::Statement> Parser::parseImportStatement() {
  advance(); // consume 'import'

  std::vector<std::pair<std::string, std::string>> items;

  // Handle `import *` for wildcard imports
  if (at().type == havel::TokenType::Multiply) {
    advance(); // consume '*'
    items.push_back({"*", "*"});
  }
  // Handle comma-separated identifiers: `import a, b, c from "module"`
  else if (at().type == havel::TokenType::Identifier) {
    while (notEOF() && at().type == havel::TokenType::Identifier) {
      std::string name = advance().value;
      items.push_back({name, name});

      if (at().type == havel::TokenType::Comma) {
        advance();
      } else {
        break;
      }
    }
  }
  // `import { item1, item2 as alias } from "path"`
  else if (at().type == havel::TokenType::OpenBrace) {
    advance(); // consume '{'
    while (notEOF() && at().type != havel::TokenType::CloseBrace) {
      if (at().type != havel::TokenType::Identifier) {
        failAt(at(), "Expected identifier in import list");
      }
      std::string originalName = advance().value;
      std::string alias = originalName;

      if (at().type == havel::TokenType::As) {
        advance(); // consume 'as'
        if (at().type != havel::TokenType::Identifier) {
          failAt(at(), "Expected alias name after 'as'");
        }
        alias = advance().value;
      }
      items.push_back({originalName, alias});

      if (at().type == havel::TokenType::Comma) {
        advance();
      } else if (at().type != havel::TokenType::CloseBrace) {
        failAt(at(), "Expected ',' or '}' in import list");
      }
    }
    if (at().type != havel::TokenType::CloseBrace)
      failAt(at(), "Expected '}'");
    advance(); // consume '}'
  }

  // Optional 'from' clause
  if (at().type == havel::TokenType::From) {
    advance(); // consume 'from'
    if (at().type != havel::TokenType::String &&
        at().type != havel::TokenType::Identifier) {
      failAt(at(), "Expected module path after 'from'");
    }
    std::string path = advance().value;
    return std::make_unique<havel::ast::ImportStatement>(path, items);
  }
  // No 'from': treat as importing built-in modules by name
  return std::make_unique<havel::ast::ImportStatement>(std::string(""), items);
}

std::unique_ptr<havel::ast::Statement> Parser::parseUseStatement() {
  advance(); // consume 'use'

  // Skip newlines
  while (at().type == havel::TokenType::NewLine) {
    advance();
  }

  if (at().type == havel::TokenType::Identifier) {
    std::string moduleName = advance().value;

    while (at().type == havel::TokenType::NewLine) advance();
    std::string alias = "";
    if (at().type == havel::TokenType::As) {
      advance();
      while (at().type == havel::TokenType::NewLine) advance();
      if (at().type != havel::TokenType::Identifier) {
        failAt(at(), "Expected alias after 'as'");
        return nullptr;
      }
      alias = advance().value;
    }

    while (at().type == havel::TokenType::NewLine) advance();
    if (at().type == havel::TokenType::From) {
      advance();
      while (at().type == havel::TokenType::NewLine) advance();
      std::string source;
      if (at().type == havel::TokenType::String ||
          at().type == havel::TokenType::MultilineString) {
        source = advance().value;
      } else if (at().type == havel::TokenType::Identifier) {
        source = advance().value;
      } else {
        failAt(at(), "Expected module name or file path after 'from'");
        return nullptr;
      }
        auto stmt = std::make_unique<havel::ast::UseStatement>(source, moduleName);
        stmt->isFileImport = true;
        if (!alias.empty()) {
            stmt->alias = alias;
        }
        return stmt;
    }

        auto stmt = std::make_unique<havel::ast::UseStatement>(
            std::vector<std::string>{moduleName});
        stmt->alias = alias;
        return stmt;
  }

  if (at().type == havel::TokenType::OpenBrace) {
    advance(); // consume '{'
    
    std::vector<std::pair<std::string, std::string>> importNames; // name, alias
    
    while (at().type != havel::TokenType::CloseBrace) {
      while (at().type == havel::TokenType::NewLine) advance();
      
      if (at().type != havel::TokenType::Identifier) {
        failAt(at(), "Expected identifier in import list");
        return nullptr;
      }
      
      std::string name = advance().value;
      std::string alias = name; // default alias is same as name
      
      // Check for "as alias"
      while (at().type == havel::TokenType::NewLine) advance();
      if (at().type == havel::TokenType::As) {
        advance(); // consume 'as'
        while (at().type == havel::TokenType::NewLine) advance();
        if (at().type != havel::TokenType::Identifier) {
          failAt(at(), "Expected alias after 'as'");
          return nullptr;
        }
        alias = advance().value;
      }
      
      importNames.emplace_back(name, alias);
      
      // Skip comma
      while (at().type == havel::TokenType::NewLine) advance();
      if (at().type == havel::TokenType::Comma) {
        advance(); // consume ','
        while (at().type == havel::TokenType::NewLine) advance();
      }
    }
    
    advance(); // consume '}'
    
    // Expect "from"
    while (at().type == havel::TokenType::NewLine) advance();
    if (at().type != havel::TokenType::From) {
      failAt(at(), "Expected 'from' after import list");
      return nullptr;
    }
    advance(); // consume 'from'
    
    // Get source
    while (at().type == havel::TokenType::NewLine) advance();
    std::string source;
    if (at().type == havel::TokenType::String || 
        at().type == havel::TokenType::MultilineString) {
      source = advance().value;
    } else if (at().type == havel::TokenType::Identifier) {
      source = advance().value; // module name
    } else {
      failAt(at(), "Expected module name or file path after 'from'");
      return nullptr;
    }
    
    auto stmt = std::make_unique<havel::ast::UseStatement>(source, std::vector<std::string>{});
    stmt->isNamedImport = true;
    for (auto& [name, alias] : importNames) {
      stmt->importNames.push_back(name);
    }
    stmt->alias = importNames.empty() ? "" : importNames[0].second;
    return stmt;
  }

  // =========================================================================
  // Syntax: use * from "path" or use * from module
  // =========================================================================
  if (at().type == havel::TokenType::Multiply) {
    advance(); // consume '*'
    
    // Expect "from"
    while (at().type == havel::TokenType::NewLine) advance();
    if (at().type != havel::TokenType::From) {
      failAt(at(), "Expected 'from' after '*'");
      return nullptr;
    }
    advance(); // consume 'from'
    
    while (at().type == havel::TokenType::NewLine) advance();
    std::string source;
    if (at().type == havel::TokenType::String || 
        at().type == havel::TokenType::MultilineString) {
      source = advance().value;
    } else if (at().type == havel::TokenType::Identifier) {
      source = advance().value;
    } else {
      failAt(at(), "Expected module name or file path after 'from'");
      return nullptr;
    }
    
    auto stmt = std::make_unique<havel::ast::UseStatement>(source, std::vector<std::string>{"*"});
    stmt->isWildcard = true;
    return stmt;
  }

  // =========================================================================
  // Syntax: use "file.hv" or use "file.hv" as alias
  // =========================================================================
  if (at().type == havel::TokenType::String ||
      at().type == havel::TokenType::MultilineString) {
    std::string filePath = advance().value;
    
    // Check for "as alias"
    while (at().type == havel::TokenType::NewLine) advance();
    std::string alias = "";
    if (at().type == havel::TokenType::As) {
      advance(); // consume 'as'
      while (at().type == havel::TokenType::NewLine) advance();
      if (at().type != havel::TokenType::Identifier) {
        failAt(at(), "Expected alias after 'as'");
        return nullptr;
      }
      alias = advance().value;
    }
    
    if (!alias.empty()) {
      auto stmt = std::make_unique<havel::ast::UseStatement>(filePath, alias);
      stmt->isFileImport = true;
      return stmt;
    }
    return std::make_unique<havel::ast::UseStatement>(
        filePath, std::vector<std::string>{"*"});
  }

  // =========================================================================
  // Syntax: use module or use module.* - import module (Lua-style)
  // =========================================================================
  if (at().type == havel::TokenType::Identifier) {
    std::string moduleName = advance().value;

    // Skip newlines
    while (at().type == havel::TokenType::NewLine) {
      advance();
    }

    // Check for wildcard: module.*
    if (at().type == havel::TokenType::Dot) {
      advance(); // consume '.'

      // Skip newlines
      while (at().type == havel::TokenType::NewLine) {
        advance();
      }

      if (at().type == havel::TokenType::Multiply) {
        advance(); // consume '*'
        auto stmt = std::make_unique<havel::ast::UseStatement>(
            std::vector<std::string>{moduleName});
        stmt->isWildcard = true;
        return stmt;
      } else {
        failAt(at(), "Expected '*' after '.' in use statement");
        return nullptr;
      }
    }

    // Simple module import - flatten into current scope
    return std::make_unique<havel::ast::UseStatement>(
        std::vector<std::string>{moduleName});
  }

  failAt(at(), "Expected module name or file path after 'use'");
  return nullptr;
}

std::unique_ptr<havel::ast::Statement> Parser::parseDecoratorStatement() {
  std::vector<std::unique_ptr<havel::ast::Expression>> decorators;

  while (at().type == havel::TokenType::At) {
    advance(); // consume '@'

    if (at().type != havel::TokenType::Identifier) {
      failAt(at(), "Expected decorator name after '@'");
      return nullptr;
    }
    auto decoName = makeIdentifier(advance());

    std::unique_ptr<havel::ast::Expression> decoExpr = std::move(decoName);

    if (at().type == havel::TokenType::OpenParen) {
      advance(); // consume '('
      std::vector<std::unique_ptr<havel::ast::Expression>> args;
      while (notEOF() && at().type != havel::TokenType::CloseParen) {
        while (at().type == havel::TokenType::NewLine) advance();
        if (at().type == havel::TokenType::CloseParen) break;
        args.push_back(parsePrattExpression(bp(BindingPower::Assignment)));
        while (at().type == havel::TokenType::NewLine) advance();
        if (at().type == havel::TokenType::Comma) advance();
        else if (at().type != havel::TokenType::CloseParen) {
          failAt(at(), "Expected ',' or ')' in decorator arguments");
          return nullptr;
        }
      }
      if (at().type != havel::TokenType::CloseParen) {
        failAt(at(), "Expected ')' after decorator arguments");
        return nullptr;
      }
      advance(); // consume ')'
      decoExpr = std::make_unique<havel::ast::CallExpression>(
          std::move(decoExpr), std::move(args));
    }

    decorators.push_back(std::move(decoExpr));

    while (at().type == havel::TokenType::NewLine) advance();
  }

  auto target = parseStatement();
  if (!target) {
    failAt(at(), "Expected declaration after decorator");
    return nullptr;
  }

  return std::make_unique<havel::ast::DecoratorStatement>(
      std::move(decorators), std::move(target));
}

std::unique_ptr<havel::ast::Statement> Parser::parseWithStatement() {
  advance(); // consume 'with'

  if (at().type != havel::TokenType::Identifier) {
    failAt(at(), "Expected object name after 'with'");
  }

  std::string objectName = advance().value;

  if (at().type != havel::TokenType::OpenBrace) {
    failAt(at(), "Expected '{' after with object name");
  }

  advance(); // consume '{'

  std::vector<std::unique_ptr<havel::ast::Statement>> body;

  // Parse block statements
  while (notEOF() && at().type != havel::TokenType::CloseBrace) {
    auto stmt = parseStatement();
    if (stmt) {
      body.push_back(std::move(stmt));
    }

    // Skip newlines
    while (at().type == havel::TokenType::NewLine) {
      advance();
    }
  }

  if (at().type != havel::TokenType::CloseBrace) {
    failAt(at(), "Expected '}' to close with block");
  }

  advance(); // consume '}'

  return std::make_unique<havel::ast::WithStatement>(objectName,
                                                     std::move(body));
}

// Parse LINQ-style query expression and desugar to pipeline
// from x in numbers where x > 2 select x * 2
// desugars to: numbers | filter(x => x > 2) | map(x => x * 2)
std::unique_ptr<havel::ast::Expression> Parser::parseQueryExpression() {
  advance(); // consume 'from'

  // Parse variable name
  if (at().type != havel::TokenType::Identifier) {
    failAt(at(), "Expected variable name after 'from'");
  }
  std::string varName = advance().value;

  // Expect 'in'
  if (at().type != havel::TokenType::In) {
    failAt(at(), "Expected 'in' after variable name in query expression");
  }
  advance(); // consume 'in'

  // Parse the source expression
  auto source = parseAssignmentExpression();

  // Build pipeline stages
  std::vector<std::unique_ptr<havel::ast::Expression>> stages;
  stages.push_back(std::move(source));

  // Parse optional where clause(s)
  while (at().type == havel::TokenType::Where) {
    advance(); // consume 'where'
    auto condition = parseAssignmentExpression();

    // Create filter call: filter(varName => condition)
    auto filterCall = std::make_unique<havel::ast::CallExpression>(
        std::make_unique<havel::ast::Identifier>("filter"));

    // Build lambda: varName => condition
    auto param = std::make_unique<havel::ast::FunctionParameter>(
        std::make_unique<havel::ast::Identifier>(varName));
    std::vector<std::unique_ptr<havel::ast::FunctionParameter>> params;
    params.push_back(std::move(param));

    // Wrap the condition expression in an ExpressionStatement
    auto body =
        std::make_unique<havel::ast::ExpressionStatement>(std::move(condition));
    auto lambda = std::make_unique<havel::ast::LambdaExpression>(
        std::move(params), std::move(body));

    filterCall->args.push_back(std::move(lambda));
    stages.push_back(std::move(filterCall));
  }

  // Parse optional select clause
  if (at().type == havel::TokenType::Select) {
    advance(); // consume 'select'
    auto transform = parseAssignmentExpression();

    // Create map call: map(varName => transform)
    auto mapCall = std::make_unique<havel::ast::CallExpression>(
        std::make_unique<havel::ast::Identifier>("map"));

    // Build lambda: varName => transform
    auto param = std::make_unique<havel::ast::FunctionParameter>(
        std::make_unique<havel::ast::Identifier>(varName));
    std::vector<std::unique_ptr<havel::ast::FunctionParameter>> params;
    params.push_back(std::move(param));

    // Wrap the transform expression in an ExpressionStatement
    auto body =
        std::make_unique<havel::ast::ExpressionStatement>(std::move(transform));
    auto lambda = std::make_unique<havel::ast::LambdaExpression>(
        std::move(params), std::move(body));

    mapCall->args.push_back(std::move(lambda));
    stages.push_back(std::move(mapCall));
  }

  // Create pipeline expression
  auto pipeline = std::make_unique<havel::ast::PipelineExpression>();
  pipeline->stages = std::move(stages);

  return std::move(pipeline);
}

std::unique_ptr<havel::ast::Expression> Parser::parseAssignmentExpression() {
    auto left = parseTernaryExpression();

    // Check for comma-separated targets: a, b, c = value
    // But NOT when in match expression - the comma is the match arm separator
    std::vector<std::unique_ptr<havel::ast::Expression>> targets;
    bool hasComma = false;
    
    if (at().type == havel::TokenType::Comma && !context.inMatchExpression) {
        hasComma = true;
        targets.push_back(std::move(left));
        
        while (at().type == havel::TokenType::Comma) {
            advance(); // consume comma
            targets.push_back(parseTernaryExpression());
        }
    }

    // Check for assignment operators
    if (at().type == havel::TokenType::Assign ||
        at().type == havel::TokenType::PlusAssign ||
        at().type == havel::TokenType::MinusAssign ||
        at().type == havel::TokenType::MultiplyAssign ||
        at().type == havel::TokenType::DivideAssign ||
        at().type == havel::TokenType::ModuloAssign ||
        at().type == havel::TokenType::PowerAssign) {
        auto opTok = advance(); // consume the operator

        // Right-associative: a = b = c means a = (b = c)
        auto value = parseAssignmentExpression();

        if (hasComma) {
            // Multiple assignment: a, b, c = value
            if (opTok.value != "=") {
                failAt(opTok, "Compound assignment operators not supported for multiple targets");
            }
            auto multiAssign = std::make_unique<havel::ast::MultipleAssignment>(
                std::move(targets), std::move(value));
            multiAssign->line = opTok.line;
            multiAssign->column = opTok.column;
            return multiAssign;
        }

        // Check if target is global scope (::identifier)
        bool isGlobalScope = false;
        if (left && left->kind == havel::ast::NodeType::Identifier) {
            auto &ident = static_cast<havel::ast::Identifier &>(*left);
            isGlobalScope = ident.isGlobalScope;
        }

        auto assign = std::make_unique<havel::ast::AssignmentExpression>(
            std::move(left), std::move(value), opTok.value, isGlobalScope);
        assign->line = opTok.line;
        assign->column = opTok.column;
        return assign;
    }

    // If we had comma but no assignment, that's an error
    if (hasComma) {
        failAt(at(), "Expected '=' after comma-separated targets");
    }

    return left;
}

// Parse cast expression: expr as Type
std::unique_ptr<havel::ast::Expression> Parser::parseCastExpression() {
  auto left = parseMatchExpression();

  // Check for 'as' keyword
  if (at().type == havel::TokenType::As) {
    advance(); // consume 'as'

    if (at().type != havel::TokenType::Identifier) {
      failAt(at(), "Expected type name after 'as'");
    }

    std::string targetType = advance().value;
    return std::make_unique<havel::ast::CastExpression>(std::move(left),
                                                        targetType);
  }

  return left;
}

// Parse match expression: match value { pattern => expr, _ => default }
std::unique_ptr<havel::ast::Expression> Parser::parseMatchExpression() {
  // Note: the 'match' token has already been consumed by nud() before calling this.
  // Do NOT check for Match token or advance here.

  // Save and set inMatchExpression flag for the ENTIRE match block.
  // This prevents => from being parsed as arrow functions in match arms.
  bool savedInMatchExpression = context.inMatchExpression;
  context.inMatchExpression = true;

  // Parse comma-separated discriminants
  std::vector<std::unique_ptr<havel::ast::Expression>> discriminants;

  // Temporarily disable brace sugar to prevent { from being consumed as a lambda
  bool savedBraceSugar = context.allowBraceSugar;
  context.allowBraceSugar = false;

  discriminants.push_back(parseBinaryExpression());

  // Parse additional discriminants separated by commas
  while (at().type == havel::TokenType::Comma) {
    advance(); // consume ','
    discriminants.push_back(parseBinaryExpression());
  }

  auto match = std::make_unique<havel::ast::MatchExpression>(std::move(discriminants));

  // Expect opening brace
  if (at().type != havel::TokenType::OpenBrace) {
    failAt(at(), "Expected '{' after match value(s)");
  }
  advance(); // consume '{'

  // Parse cases
  while (at().type != havel::TokenType::CloseBrace && notEOF()) {
    // Skip newlines
    while (at().type == havel::TokenType::NewLine) {
      advance();
    }

    if (at().type == havel::TokenType::CloseBrace) {
      break;
    }

    // Parse comma-separated patterns using the new pattern parser
    std::vector<std::unique_ptr<havel::ast::Expression>> patterns;
    std::unique_ptr<havel::ast::Expression> guard;
    bool isDefault = true;

    // Parse first pattern
    auto firstPat = parsePattern();
    isDefault = (firstPat && firstPat->kind == ast::NodeType::WildcardPattern);
    patterns.push_back(std::move(firstPat));

    // Parse additional patterns separated by commas
    while (at().type == havel::TokenType::Comma) {
      // Look ahead: if comma is followed by 'if', it's a guard, not another pattern
      if (at(1).type == havel::TokenType::If) {
        break; // Guard follows, stop consuming patterns
      }
      advance(); // consume ','
      auto pat = parsePattern();
      if (pat && pat->kind != ast::NodeType::WildcardPattern) {
        isDefault = false;
      }
      patterns.push_back(std::move(pat));
    }

    // Check for optional guard condition: pattern if guard => result
    if (at().type == havel::TokenType::If) {
      advance(); // consume 'if'
      // Use rbp=11 to prevent Arrow (bp=10) from being parsed as lambda
      guard = parsePrattExpression(11);
    }

    // Skip newlines before checking for =>
    while (at().type == havel::TokenType::NewLine) {
      advance();
    }

    // Expect =>
    if (at().type != havel::TokenType::Arrow) {
      failAt(at(), "Expected '=>' after pattern(s)");
    }
    advance(); // consume '=>'

    // Parse result expression (use parseAssignmentExpression to handle assignments like x = true)
    auto result = parseAssignmentExpression();

    if (isDefault) {
      match->defaultCase = std::move(result);
    } else {
      ast::MatchExpression::MatchArm arm;
      arm.patterns = std::move(patterns);
      arm.guard = std::move(guard);
      arm.result = std::move(result);
      match->cases.push_back(std::move(arm));
    }

    // Skip optional comma
    if (at().type == havel::TokenType::Comma) {
      advance();
    }

    // Skip newlines and semicolons (case separators)
    while (at().type == havel::TokenType::NewLine || at().type == havel::TokenType::Semicolon) {
      advance();
    }
  }

  // Expect closing brace
  if (at().type != havel::TokenType::CloseBrace) {
    failAt(at(), "Expected '}' to close match expression");
  }
  advance(); // consume '}'

  // Restore context
  context.allowBraceSugar = savedBraceSugar;
  context.inMatchExpression = savedInMatchExpression;

  return match;
}

std::unique_ptr<havel::ast::Expression> Parser::parsePipelineExpression() {
  auto left = parseAssignmentExpression();

  // Check for pipeline operator |
  if (at().type == havel::TokenType::Pipe) {
    auto pipeline = std::make_unique<havel::ast::PipelineExpression>();
    pipeline->stages.push_back(std::move(left));

    while (at().type == havel::TokenType::Pipe) {
      advance(); // consume '|'
      auto stage = parseAssignmentExpression();
      pipeline->stages.push_back(std::move(stage));
    }

    return std::move(pipeline);
  }

  // Check for config append operator >>
    if (at().type == havel::TokenType::ShiftRight) {
      auto opTok = at();
      advance();
      auto right = parseLogicalOr();
      auto bin = std::make_unique<havel::ast::BinaryExpression>(
          std::move(left), havel::ast::BinaryOperator::ConfigAppend, std::move(right));
    bin->line = opTok.line;
    bin->column = opTok.column;
    return std::move(bin);
  }

  return left;
}

std::unique_ptr<havel::ast::Expression> Parser::parseTernaryExpression() {
  auto condition = parseNullishCoalescing();

  // Check for ternary operator ?
  if (at().type == havel::TokenType::Question) {
    advance(); // consume '?'

    auto trueValue = parseBinaryExpression();

    if (at().type != havel::TokenType::Colon) {
      failAt(at(), "Expected ':' in ternary expression");
    }
    advance(); // consume ':'

    auto falseValue = parseTernaryExpression(); // Right-associative

    return std::make_unique<havel::ast::TernaryExpression>(
        std::move(condition), std::move(trueValue), std::move(falseValue));
  }

  return condition;
}

std::unique_ptr<havel::ast::Expression> Parser::parseBinaryExpression() {
  return parseLogicalOr();
}

// Add these new methods for operator precedence
std::unique_ptr<havel::ast::Expression> Parser::parseLogicalOr() {
  auto left = parseLogicalAnd();

  while (at().type == havel::TokenType::Or) {
    auto opTok = at(); // Save operator token location
    auto op = tokenToBinaryOperator(at().type);
    advance();
    auto right = parseLogicalAnd();
    auto bin = std::make_unique<havel::ast::BinaryExpression>(
        std::move(left), op, std::move(right));
    bin->line = opTok.line;
    bin->column = opTok.column;
    left = std::move(bin);
  }

  return left;
}

std::unique_ptr<havel::ast::Expression> Parser::parseNullishCoalescing() {
  // Use logical or parsing for left side so binary operators like == work
  // E.g., a == b ?? default, obj.field == value ?? default
  auto left = parseLogicalOr();

  while (at().type == havel::TokenType::Nullish) {
    auto opTok = at(); // Save operator token location
    advance();         // consume ??
    auto right = parseLogicalOr();
    auto bin = std::make_unique<havel::ast::BinaryExpression>(
        std::move(left), havel::ast::BinaryOperator::Nullish, std::move(right));
    bin->line = opTok.line;
    bin->column = opTok.column;
    left = std::move(bin);
  }

  return left;
}

std::unique_ptr<havel::ast::Expression> Parser::parseLogicalAnd() {
  auto left = parseEquality();

  while (at().type == havel::TokenType::And) {
    auto opTok = at(); // Save operator token location
    auto op = tokenToBinaryOperator(at().type);
    advance();
    auto right = parseEquality();
    auto bin = std::make_unique<havel::ast::BinaryExpression>(
        std::move(left), op, std::move(right));
    bin->line = opTok.line;
    bin->column = opTok.column;
    left = std::move(bin);
  }

  return left;
}

std::unique_ptr<havel::ast::Expression> Parser::parseEquality() {
  auto left = parseComparison();

  // Regex match: ~ /pattern/ or ~ "string"
  if (at().type == havel::TokenType::Tilde) {
    auto tildeTok = at();
    advance();
    auto right = parseRange();
    auto bin = std::make_unique<havel::ast::BinaryExpression>(
        std::move(left), havel::ast::BinaryOperator::Tilde, std::move(right));
    bin->line = tildeTok.line;
    bin->column = tildeTok.column;
    left = std::move(bin);
  }

  while (at().type == havel::TokenType::Equals ||
         at().type == havel::TokenType::NotEquals) {
    auto opTok = at(); // Save operator token location
    auto op = tokenToBinaryOperator(at().type);
    advance();
    auto right = parseComparison();
    auto bin = std::make_unique<havel::ast::BinaryExpression>(
        std::move(left), op, std::move(right));
    bin->line = opTok.line;
    bin->column = opTok.column;
    left = std::move(bin);
  }

  return left;
}

std::unique_ptr<havel::ast::Expression> Parser::parseComparison() {
  auto left = parseRange();

  // Membership operators: in, not in
  // Check for "not in" first (two-token operator)
  if (at().type == havel::TokenType::Not) {
    if (at(1).type == havel::TokenType::In) {
      auto notTok = at(); // Save location of 'not'
      advance();          // consume 'not'
      advance();          // consume 'in'
      auto right = parseRange();
      auto bin = std::make_unique<havel::ast::BinaryExpression>(
          std::move(left), havel::ast::BinaryOperator::NotIn, std::move(right));
      bin->line = notTok.line;
      bin->column = notTok.column;
      left = std::move(bin);
    }
  }

  // Check for "in" operator
  if (at().type == havel::TokenType::In) {
    auto inTok = at(); // Save location of 'in'
    advance();         // consume 'in'
    auto right = parseRange();
    auto bin = std::make_unique<havel::ast::BinaryExpression>(
        std::move(left), havel::ast::BinaryOperator::In, std::move(right));
    bin->line = inTok.line;
    bin->column = inTok.column;
    left = std::move(bin);
  }

  // Regex match operator: matches or ~
  if (at().type == havel::TokenType::Matches ||
      at().type == havel::TokenType::Tilde) {
    auto matchesTok = at(); // Save location
    advance();              // consume 'matches' or '~'
    auto right = parseRange();
    auto bin = std::make_unique<havel::ast::BinaryExpression>(
        std::move(left), havel::ast::BinaryOperator::Matches, std::move(right));
    bin->line = matchesTok.line;
    bin->column = matchesTok.column;
    left = std::move(bin);
  }

  // Comparison operators: < > <= >=
  // Left-associative: a < b < c parses as ((a < b) < c)
  // Note: Python-style chaining (a < b && b < c) is NOT supported.
  // For Python semantics, use explicit: (a < b) && (b < c)
  while (at().type == havel::TokenType::Less ||
         at().type == havel::TokenType::Greater ||
         at().type == havel::TokenType::LessEquals ||
         at().type == havel::TokenType::GreaterEquals) {
    auto opTok = at(); // Save operator token location
    auto op = tokenToBinaryOperator(at().type);
    advance();
    auto right = parseRange();
    auto bin = std::make_unique<havel::ast::BinaryExpression>(
        std::move(left), op, std::move(right));
    bin->line = opTok.line;
    bin->column = opTok.column;
    left = std::move(bin);
  }

  return left;
}

std::unique_ptr<havel::ast::Expression> Parser::parseRange() {
  auto left = parseAdditive();

  // Range operator: ..
  // Supports: start .. end  OR  start .. end .. step
  if (at().type == havel::TokenType::DotDot) {
    auto opTok = at(); // Save operator token location
    advance();         // consume '..'
    auto right = parseAdditive();

    // Check for optional step value
    std::unique_ptr<havel::ast::Expression> step;
    if (at().type == havel::TokenType::DotDot) {
      advance(); // consume second '..'
      step = parseAdditive();
    }

    auto range = std::make_unique<havel::ast::RangeExpression>(
        std::move(left), std::move(right), std::move(step));
    range->line = opTok.line;
    range->column = opTok.column;

    return range;
  }

  return left;
}

std::unique_ptr<havel::ast::Expression> Parser::parseAdditive() {
  auto left = parseMultiplicative();

  while (at().type == havel::TokenType::Plus ||
         at().type == havel::TokenType::Minus) {
    auto opTok = at(); // Save operator token location
    auto op = tokenToBinaryOperator(at().type);
    advance();
    auto right = parseMultiplicative();
    auto bin = std::make_unique<havel::ast::BinaryExpression>(
        std::move(left), op, std::move(right));
    bin->line = opTok.line;
    bin->column = opTok.column;
    left = std::move(bin);
  }

  return left;
}

std::unique_ptr<havel::ast::Expression> Parser::parseMultiplicative() {
  auto left = parseUnary();

  while (at().type == havel::TokenType::Multiply ||
         at().type == havel::TokenType::Divide ||
         at().type == havel::TokenType::Modulo ||
         at().type == havel::TokenType::Power ||
         at().type == havel::TokenType::Backslash) {
    auto opTok = at(); // Save operator token location
    auto op = tokenToBinaryOperator(at().type);
    advance();
    auto right = parseUnary();
    auto bin = std::make_unique<havel::ast::BinaryExpression>(
        std::move(left), op, std::move(right));
    bin->line = opTok.line;
    bin->column = opTok.column;
    left = std::move(bin);
  }

  return left;
}
std::unique_ptr<havel::ast::Expression> Parser::parseUnary() {
  if (at().type == havel::TokenType::Identifier && at().value == "await") {
    auto awaitTok = advance();
    auto argument = parseUnary();
    auto awaitExpr =
        std::make_unique<havel::ast::AwaitExpression>(std::move(argument));
    awaitExpr->line = awaitTok.line;
    awaitExpr->column = awaitTok.column;
    return awaitExpr;
  }

  if (at().type == havel::TokenType::PlusPlus ||
      at().type == havel::TokenType::MinusMinus) {
    auto op = (at().type == havel::TokenType::PlusPlus)
                  ? havel::ast::UpdateExpression::Operator::Increment
                  : havel::ast::UpdateExpression::Operator::Decrement;
    advance();
    auto operand = parseUnary();
    return std::make_unique<havel::ast::UpdateExpression>(std::move(operand),
                                                          op, true);
  }

  if (at().type == havel::TokenType::Not) {
    // Check if this is !{} for unsorted object
    if (at(1).type == havel::TokenType::OpenBrace) {
      // !{...} - unsorted object literal
      advance();                       // consume '!'
      return parseObjectLiteral(true); // true = unsorted
    }
  }

  if (at().type == havel::TokenType::Not ||
      at().type == havel::TokenType::Minus ||
      at().type == havel::TokenType::Plus ||
      at().type == havel::TokenType::Length) {

    // Convert TokenType to UnaryOperator
    havel::ast::UnaryExpression::UnaryOperator unaryOp;

    switch (at().type) {
    case havel::TokenType::Not:
      unaryOp = havel::ast::UnaryExpression::UnaryOperator::Not;
      break;
    case havel::TokenType::Minus:
      unaryOp = havel::ast::UnaryExpression::UnaryOperator::Minus;
      break;
    case havel::TokenType::Plus:
      unaryOp = havel::ast::UnaryExpression::UnaryOperator::Plus;
      break;
    case havel::TokenType::Length:
      unaryOp = havel::ast::UnaryExpression::UnaryOperator::Length;
      break;
    default:
      failAt(at(), "Invalid unary operator");
    }

    advance(); // Consume the unary operator
    auto operand =
        parseUnary(); // Right associative - handle nested unary operators

    return std::make_unique<havel::ast::UnaryExpression>(unaryOp,
                                                         std::move(operand));
  }

  return parsePostfixExpression(parsePrimaryExpression());
}
havel::TokenType Parser::getBinaryOperatorToken(ast::BinaryOperator op) {
  switch (op) {
  case ast::BinaryOperator::Add:
    return TokenType::Plus;
  case ast::BinaryOperator::Sub:
    return TokenType::Minus;
  case ast::BinaryOperator::Mul:
    return TokenType::Multiply;
  case ast::BinaryOperator::Div:
    return TokenType::Divide;
  case ast::BinaryOperator::Equal:
    return TokenType::Equals;
  case ast::BinaryOperator::NotEqual:
    return TokenType::NotEquals;
  case ast::BinaryOperator::Is:
    return TokenType::Is;
  case ast::BinaryOperator::Less:
    return TokenType::Less;
  case ast::BinaryOperator::Greater:
    return TokenType::Greater;
  case ast::BinaryOperator::And:
    return TokenType::And;
      case ast::BinaryOperator::Or:
        return TokenType::Or;
      case ast::BinaryOperator::BitwiseAnd:
        return TokenType::BitwiseAnd;
      case ast::BinaryOperator::BitwiseOr:
        return TokenType::BitwiseOr;
      case ast::BinaryOperator::BitwiseXor:
        return TokenType::BitwiseXor;
      case ast::BinaryOperator::BitwiseShiftLeft:
        return TokenType::ShiftLeft;
      case ast::BinaryOperator::BitwiseShiftRight:
        return TokenType::ShiftRight;
  default:
    fail("Unknown binary operator");
  }
}
// Add the helper method implementation
havel::ast::BinaryOperator Parser::tokenToBinaryOperator(TokenType tokenType) {
  switch (tokenType) {
  case TokenType::Plus:
    return havel::ast::BinaryOperator::Add;
  case TokenType::Minus:
    return havel::ast::BinaryOperator::Sub;
  case TokenType::Multiply:
    return havel::ast::BinaryOperator::Mul;
  case TokenType::Divide:
    return havel::ast::BinaryOperator::Div;
  case TokenType::Modulo:
    return havel::ast::BinaryOperator::Mod;
  case TokenType::Power:
    return havel::ast::BinaryOperator::Pow;
  case TokenType::Backslash:
    return havel::ast::BinaryOperator::IntDiv;
  case TokenType::Equals:
    return havel::ast::BinaryOperator::Equal;
  case TokenType::NotEquals:
    return havel::ast::BinaryOperator::NotEqual;
  case TokenType::Is:
    return havel::ast::BinaryOperator::Is;
  case TokenType::Less:
    return havel::ast::BinaryOperator::Less;
  case TokenType::Greater:
    return havel::ast::BinaryOperator::Greater;
  case TokenType::LessEquals:
    return havel::ast::BinaryOperator::LessEqual;
  case TokenType::GreaterEquals:
    return havel::ast::BinaryOperator::GreaterEqual;
    case TokenType::And:
      return havel::ast::BinaryOperator::And;
    case TokenType::Or:
      return havel::ast::BinaryOperator::Or;
    case TokenType::BitwiseAnd:
      return havel::ast::BinaryOperator::BitwiseAnd;
    case TokenType::BitwiseOr:
      return havel::ast::BinaryOperator::BitwiseOr;
    case TokenType::BitwiseXor:
      return havel::ast::BinaryOperator::BitwiseXor;
    case TokenType::ShiftLeft:
      return havel::ast::BinaryOperator::BitwiseShiftLeft;
    case TokenType::ShiftRight:
      return havel::ast::BinaryOperator::BitwiseShiftRight;
    case TokenType::Matches:
    case TokenType::Tilde:
      return havel::ast::BinaryOperator::Matches;
  default:
    fail("Invalid binary operator token: " +
         std::to_string(static_cast<int>(tokenType)));
  }
}
std::unique_ptr<havel::ast::Expression> Parser::parsePrimaryExpression() {
  havel::Token tk = at();

  switch (tk.type) {
case havel::TokenType::Number: {
            advance();
        double value = parseNumberLiteral(tk.value);
        return std::make_unique<havel::ast::NumberLiteral>(value, hasDecimalPart(tk.value));
        }

  case havel::TokenType::String: {
    advance();
    auto strLit = std::make_unique<havel::ast::StringLiteral>(tk.value);
    // Allow string literals to have postfix operations like indexing/slicing
    return parsePostfixExpression(std::move(strLit));
  }

  case havel::TokenType::MultilineString: {
    advance();
    auto strLit = std::make_unique<havel::ast::StringLiteral>(tk.value);
    // Allow string literals to have postfix operations like indexing/slicing
    return parsePostfixExpression(std::move(strLit));
  }

  case havel::TokenType::Backtick: {
    advance();
    return std::make_unique<havel::ast::BacktickExpression>(tk.value);
  }

  case havel::TokenType::RegexLiteral: {
    advance();
    return std::make_unique<havel::ast::StringLiteral>(
        tk.value); // Store regex as string for now
  }

  case havel::TokenType::ShellCommand:
  case havel::TokenType::ShellCommandCapture: {
    // Shell command in expression context: $! ["date"] or $ (cmd) or $! var
    bool captureOutput = (tk.type == havel::TokenType::ShellCommandCapture);
    advance(); // consume '$' or '$!'
    // Skip whitespace
    while (at().type == havel::TokenType::NewLine) {
      advance();
    }
    // Expression forms: $! var, $! (expr), $! [array]
    if (at().type == havel::TokenType::OpenParen ||
        at().type == havel::TokenType::OpenBracket) {
      auto cmdExpr = parseExpression();
      return std::make_unique<havel::ast::ShellCommandExpression>(
          std::move(cmdExpr), captureOutput);
    } else if (at().type == havel::TokenType::Identifier) {
      // Just an identifier - parse as primary expression
      auto cmdExpr = parsePrimaryExpression();
      return std::make_unique<havel::ast::ShellCommandExpression>(
          std::move(cmdExpr), captureOutput);
    } else {
      failAt(
          tk,
          "Shell command requires expression: $ (cmd), $! [array], or $! var");
    }
  }

  case havel::TokenType::This: {
    advance();
    auto thisExpr = std::make_unique<havel::ast::ThisExpression>();
    // Allow this to have postfix operations like .field access
    return parsePostfixExpression(std::move(thisExpr));
  }

  case havel::TokenType::At: {
    advance(); // consume '@'
    // Parse field name after @
    if (at().type != havel::TokenType::Identifier) {
      failAt(at(), "Expected field name after '@'");
    }
    auto fieldName = makeIdentifier(advance());
    return std::make_unique<havel::ast::AtExpression>(std::move(fieldName));
  }

  case havel::TokenType::SuperArrow: {
    advance(); // consume '@->'
    // Parse method name after @->
    if (at().type != havel::TokenType::Identifier) {
      failAt(at(), "Expected method name after '@->'");
    }
    auto methodName = makeIdentifier(advance());
    // Create a special SuperCallExpression (reuse CallExpression with isSuper
    // flag)
    auto call = std::make_unique<havel::ast::CallExpression>(
        std::make_unique<havel::ast::Identifier>("__super__"));
    call->isSuperCall = true;
    call->superMethodName = methodName->symbol;

    // Parse arguments if present
    if (at().type == havel::TokenType::OpenParen) {
      advance(); // consume '('
      while (at().type != havel::TokenType::CloseParen && notEOF()) {
        // Skip newlines
        while (at().type == havel::TokenType::NewLine) {
          advance();
        }
        if (at().type == havel::TokenType::CloseParen) {
          break;
        }

        auto arg = parseExpression();
        call->args.push_back(std::move(arg));

        while (at().type == havel::TokenType::NewLine) {
          advance();
        }

        if (at().type == havel::TokenType::Comma) {
          advance();
        } else if (at().type != havel::TokenType::CloseParen) {
          failAt(at(), "Expected ',' or ')' in super call arguments");
        }
      }

      if (at().type != havel::TokenType::CloseParen) {
        failAt(at(), "Expected ')' after super call arguments");
      }
      advance(); // consume ')'
    }

    return call;
  }

  case havel::TokenType::InterpolatedString: {
    advance();
    // Parse the interpolated string: "text ${expr} more text"
    std::vector<havel::ast::InterpolatedStringExpression::Segment> segments;
    std::string str = tk.value;
    size_t pos = 0;

    while (pos < str.length()) {
      // Find next ${
      size_t start = str.find("${", pos);

      if (start == std::string::npos) {
        // No more interpolations, add rest as string
        if (pos < str.length()) {
          segments.push_back(havel::ast::InterpolatedStringExpression::Segment(
              str.substr(pos)));
        }
        break;
      }

      // Add text before ${ as string segment
      if (start > pos) {
        segments.push_back(havel::ast::InterpolatedStringExpression::Segment(
            str.substr(pos, start - pos)));
      }

      // Find matching }
      size_t end = str.find('}', start + 2);
      if (end == std::string::npos) {
        failAt(tk, "Unclosed interpolation in string");
      }

      // Parse expression between ${ and }
      std::string exprCode = str.substr(start + 2, end - start - 2);

      // Create a mini-parser for the expression
      havel::Lexer exprLexer(exprCode);
      auto exprTokens = exprLexer.tokenize();

      // Save current parser state
      auto savedTokens = tokens;
      auto savedPos = position;

      // Parse the expression
      tokens = exprTokens;
      position = 0;
      auto expr = parseExpression();

      // Restore parser state
      tokens = savedTokens;
      position = savedPos;

      segments.push_back(
          havel::ast::InterpolatedStringExpression::Segment(std::move(expr)));

      pos = end + 1;
    }

        return std::make_unique<havel::ast::InterpolatedStringExpression>(
            std::move(segments));
        }

        case havel::TokenType::InterpolatedBacktick: {
            advance();
            std::vector<havel::ast::InterpolatedStringExpression::Segment> segments;
            std::string str = tk.value;
            size_t pos = 0;

            while (pos < str.length()) {
                // Check for ${expr} pattern
                size_t dollarBrace = str.find("${", pos);
                // Check for {expr} pattern (bare brace, not preceded by $)
                size_t bareBrace = std::string::npos;
                for (size_t i = pos; i < str.length(); i++) {
                    if (str[i] == '{' && (i == 0 || str[i-1] != '$')) {
                        // Make sure this isn't inside a ${} we already found
                        if (dollarBrace == std::string::npos || i < dollarBrace) {
                            bareBrace = i;
                            break;
                        }
                    }
                }

                // Determine which comes first
                size_t nextInterp = std::string::npos;
                bool isDollarBrace = false;
                if (dollarBrace != std::string::npos && (bareBrace == std::string::npos || dollarBrace <= bareBrace)) {
                    nextInterp = dollarBrace;
                    isDollarBrace = true;
                } else if (bareBrace != std::string::npos) {
                    nextInterp = bareBrace;
                    isDollarBrace = false;
                }

                if (nextInterp == std::string::npos) {
                    if (pos < str.length()) {
                        segments.push_back(havel::ast::InterpolatedStringExpression::Segment(
                            str.substr(pos)));
                    }
                    break;
                }

                // Add text before interpolation as literal
                if (nextInterp > pos) {
                    segments.push_back(havel::ast::InterpolatedStringExpression::Segment(
                        str.substr(pos, nextInterp - pos)));
                }

                // Skip ${ or {
                size_t exprStart = isDollarBrace ? nextInterp + 2 : nextInterp + 1;

                // Find matching } with brace depth tracking
                size_t braceDepth = 1;
                size_t i = exprStart;
                while (i < str.length() && braceDepth > 0) {
                    if (str[i] == '{') braceDepth++;
                    else if (str[i] == '}') braceDepth--;
                    if (braceDepth > 0) i++;
                }

                if (braceDepth == 0) {
                    std::string exprCode = str.substr(exprStart, i - exprStart);
                    havel::Lexer exprLexer(exprCode);
                    auto exprTokens = exprLexer.tokenize();
                    auto savedTokens = tokens;
                    auto savedPos = position;
                    tokens = exprTokens;
                    position = 0;
                    auto expr = parseExpression();
                    tokens = savedTokens;
                    position = savedPos;
                    segments.push_back(
                        havel::ast::InterpolatedStringExpression::Segment(std::move(expr)));
                    pos = i + 1;
                } else {
                    failAt(tk, "Unclosed interpolation in backtick string");
                }
            }

            auto interpExpr = std::make_unique<havel::ast::InterpolatedStringExpression>(
                std::move(segments));
            return std::make_unique<havel::ast::ShellCommandExpression>(
                std::move(interpExpr), true);
        }

        case havel::TokenType::True: {
    advance();
    return std::make_unique<havel::ast::BooleanLiteral>(true);
  }

  case havel::TokenType::False: {
    advance();
    return std::make_unique<havel::ast::BooleanLiteral>(false);
  }

  case havel::TokenType::Null: {
    advance();
    return std::make_unique<havel::ast::NullLiteral>();
  }

  case havel::TokenType::ColonColon: {
    // Explicit global-scope identifier expression: ::name
    advance(); // consume '::'
    if (at().type != havel::TokenType::Identifier) {
      failAt(at(), "Expected identifier after '::'");
    }
    auto identTk = advance();
    auto ident = makeIdentifier(identTk);
    ident->isGlobalScope = true;
    ident->line = identTk.line;
    ident->column = identTk.column;
    std::unique_ptr<havel::ast::Expression> expr = std::move(ident);
    return parsePostfixExpression(std::move(expr));
  }

  case havel::TokenType::Colon: {
    // Fallback support for lexers that emit '::' as two ':' tokens.
    if (at(1).type == havel::TokenType::Colon &&
        at(2).type == havel::TokenType::Identifier) {
      advance(); // first ':'
      advance(); // second ':'
      auto identTk = advance();
      auto ident = makeIdentifier(identTk);
      ident->isGlobalScope = true;
      ident->line = identTk.line;
      ident->column = identTk.column;
      std::unique_ptr<havel::ast::Expression> expr = std::move(ident);
      return parsePostfixExpression(std::move(expr));
    }
    failAt(at(), "Unexpected ':' in expression");
  }

  case havel::TokenType::Mode:
  case havel::TokenType::Config:
  case havel::TokenType::Devices:
  case havel::TokenType::Modes:
  case havel::TokenType::Identifier: {
    auto identTk = at();
    // Arrow lambda with single parameter: x => expr
    if (at(1).type == havel::TokenType::Arrow) {
      // single identifier parameter
      advance(); // consume identifier
      advance(); // consume '=>'
      std::vector<std::unique_ptr<havel::ast::FunctionParameter>> params;
      auto paramName = makeIdentifier(identTk);
      paramName->line = identTk.line;
      paramName->column = identTk.column;
      params.push_back(std::make_unique<havel::ast::FunctionParameter>(
          std::move(paramName)));
      return parseLambdaFromParams(std::move(params));
    }
    // Otherwise it's a normal identifier expression
    identTk = advance();
    std::unique_ptr<havel::ast::Expression> expr = makeIdentifier(identTk);
    expr->line = identTk.line;
    expr->column = identTk.column;

    // Handle postfix operations in a loop to support chaining: arr[0].prop()
    // etc. This is moved to a separate function to handle all expression types
    return parsePostfixExpression(std::move(expr));
  }

  case havel::TokenType::Hotkey: {
    advance();
    return std::make_unique<havel::ast::HotkeyLiteral>(tk.value);
  }

  case havel::TokenType::Fn: {
    advance(); // consume 'fn'

    // Parse parameter list
    std::vector<std::unique_ptr<havel::ast::FunctionParameter>> params;

    if (at().type != havel::TokenType::OpenParen) {
      failAt(at(), "Expected '(' after 'fn' for function expression");
    }
    advance(); // consume '('

    // Handle empty parameter list or parameters
    while (notEOF() && at().type != havel::TokenType::CloseParen) {
      while (at().type == havel::TokenType::NewLine) {
        advance();
      }
      if (at().type == havel::TokenType::CloseParen) {
        break;
      }

      // Parse parameter pattern: identifier, { pattern }, [ pattern ], or
      // ...args
      std::unique_ptr<havel::ast::Expression> pattern;
      bool isVariadic = false;

      // Check for variadic parameter: ...args
      if (at().type == havel::TokenType::Spread) {
        advance(); // consume '...'
        if (at().type != havel::TokenType::Identifier) {
          failAt(at(), "Expected identifier after '...' in variadic parameter");
        }
        pattern = makeIdentifier(advance());
        isVariadic = true;
      } else if (at().type == havel::TokenType::Identifier) {
        pattern = makeIdentifier(advance());
      } else if (at().type == havel::TokenType::OpenBrace) {
        pattern = parseObjectPattern();
      } else if (at().type == havel::TokenType::OpenBracket) {
        pattern = parseArrayPattern();
      } else {
        failAt(at(), "Expected identifier, '{', '[', or '...' in function "
                     "parameter list");
      }

      // Check for default value
      std::optional<std::unique_ptr<havel::ast::Expression>> defaultValue;
      if (at().type == havel::TokenType::Assign) {
        advance(); // consume '='
        defaultValue = parseExpression();
      }

      params.push_back(std::make_unique<havel::ast::FunctionParameter>(
          std::move(pattern), std::move(defaultValue), std::nullopt,
          isVariadic));

      while (at().type == havel::TokenType::NewLine) {
        advance();
      }

      if (at().type == havel::TokenType::Comma) {
        advance();
      } else if (at().type != havel::TokenType::CloseParen) {
        failAt(at(), "Expected ',' or ')' in function parameter list");
      }
    }

    if (at().type != havel::TokenType::CloseParen) {
      failAt(at(), "Expected ')' after function parameter list");
    }
    advance(); // consume ')'

    // Parse function body
    if (at().type != havel::TokenType::OpenBrace) {
      failAt(at(), "Expected '{' for function body");
    }
    auto body = parseBlockStatement();

    return std::make_unique<havel::ast::LambdaExpression>(std::move(params),
                                                          std::move(body));
  }

  case havel::TokenType::OpenParen: {
    // Delegate to parseParenthesizedExpression which handles:
    // - Multi-param lambdas: (a, b) => body
    // - Empty lambdas: () => body
    // - Tuples: (1, 2, 3)
    // - Grouped expressions: (1 + 2)
    return parseParenthesizedExpression();
  }

  case havel::TokenType::OpenBracket: {
    auto array = parseArrayLiteral();
    // Handle postfix operations for array literals (moved to common function)
    return parsePostfixExpression(std::move(array));
  }

  case havel::TokenType::OpenBrace: {
    // Could be:
    // 1. Object literal {key: value}
    // 2. Set literal {1, 2, 3} (Python-style)
    // 3. Block expression {stmt; expr}
    // Look ahead to determine which, skipping newlines
    // Note: We haven't consumed '{' yet, so lookahead starts at next token
    size_t savePos = position;
    size_t lookahead = 1; // Skip the '{' token
    // Skip newlines to find first significant token
    while (at(lookahead).type == havel::TokenType::NewLine) {
      lookahead++;
    }
    auto nextTok = at(lookahead);

    // Empty braces {} in expression context = empty object literal
    if (nextTok.type == havel::TokenType::CloseBrace) {
      auto obj = parseObjectLiteral();
      return parsePostfixExpression(std::move(obj));
    }

    // Object keys can be identifiers, strings, or keywords (like 'config')
    // followed by ':'
    bool isObject = false;
    auto isObjKeyType2 = [](havel::TokenType t) {
      return t == havel::TokenType::Identifier ||
             t == havel::TokenType::String ||
             t == havel::TokenType::MultilineString ||
             t == havel::TokenType::Config ||
             t == havel::TokenType::Devices ||
             t == havel::TokenType::Modes ||
             t == havel::TokenType::Mode ||
             t == havel::TokenType::Timeout ||
             t == havel::TokenType::Thread ||
             t == havel::TokenType::Interval ||
             t == havel::TokenType::Channel ||
             t == havel::TokenType::On ||
             t == havel::TokenType::Off ||
             t == havel::TokenType::Go ||
             t == havel::TokenType::When ||
             t == havel::TokenType::Class ||
             t == havel::TokenType::Struct ||
             t == havel::TokenType::Enum ||
             t == havel::TokenType::Fn ||
             t == havel::TokenType::If ||
             t == havel::TokenType::For ||
             t == havel::TokenType::Loop ||
             t == havel::TokenType::While ||
             t == havel::TokenType::Switch ||
             t == havel::TokenType::Do ||
             t == havel::TokenType::Return ||
             t == havel::TokenType::Ret ||
             t == havel::TokenType::Break ||
             t == havel::TokenType::Continue ||
             t == havel::TokenType::Let ||
             t == havel::TokenType::Const ||
             t == havel::TokenType::Try ||
             t == havel::TokenType::Catch ||
             t == havel::TokenType::Finally ||
             t == havel::TokenType::Throw ||
             t == havel::TokenType::Del ||
             t == havel::TokenType::True ||
             t == havel::TokenType::False ||
             t == havel::TokenType::Null ||
             t == havel::TokenType::Repeat;
    };
    if (isObjKeyType2(nextTok.type)) {
      // Look for colon, skipping newlines
      size_t colonLookahead = lookahead + 1;
      while (at(colonLookahead).type == havel::TokenType::NewLine) {
        colonLookahead++;
      }
      if (at(colonLookahead).type == havel::TokenType::Colon) {
        isObject = true;
      }
    }
    
    if (isObject) {
      auto obj = parseObjectLiteral();
      return parsePostfixExpression(std::move(obj));
    }

    // Not an object literal - could be set or block
    // Check if this looks like a set literal: {expr, expr, ...}
    // Sets contain expressions separated by commas
    bool couldBeSet = (nextTok.type == havel::TokenType::Identifier ||
                  nextTok.type == havel::TokenType::String ||
                  nextTok.type == havel::TokenType::MultilineString ||
                  nextTok.type == havel::TokenType::Number ||
                  nextTok.type == havel::TokenType::OpenBracket ||
                  nextTok.type == havel::TokenType::OpenParen ||
                  nextTok.type == havel::TokenType::OpenBrace ||
                  nextTok.type == havel::TokenType::Minus ||
                  nextTok.type == havel::TokenType::Not ||
                  nextTok.type == havel::TokenType::Plus ||
                  nextTok.type == havel::TokenType::Length ||
                  nextTok.type == havel::TokenType::True ||
                  nextTok.type == havel::TokenType::False ||
                  nextTok.type == havel::TokenType::Null);
    
    if (couldBeSet) {
      // Try to parse as set literal
      // Look ahead to see if there's a comma (indicating set) or semicolon/newline (indicating block)
      size_t setLookahead = lookahead + 1;
      // Skip the first expression tokens to find separator
      // For simplicity, just check if there's a comma before any semicolon or newline
      while (setLookahead < tokens.size() && 
             at(setLookahead).type != havel::TokenType::Comma &&
             at(setLookahead).type != havel::TokenType::Semicolon &&
             at(setLookahead).type != havel::TokenType::CloseBrace &&
             at(setLookahead).type != havel::TokenType::NewLine) {
        setLookahead++;
      }
      
      // If we found a comma, it's likely a set
      if (at(setLookahead).type == havel::TokenType::Comma) {
        // Parse as set literal
        advance(); // consume '{'
        
        std::vector<std::unique_ptr<havel::ast::Expression>> elements;
        
        while (notEOF() && at().type != havel::TokenType::CloseBrace) {
          while (at().type == havel::TokenType::NewLine) {
            advance();
          }
          if (at().type == havel::TokenType::CloseBrace) {
            break;
          }
          
          auto element = parseExpression();
          elements.push_back(std::move(element));
          
          while (at().type == havel::TokenType::NewLine) {
            advance();
          }
          
          if (at().type == havel::TokenType::Comma) {
            advance();
          } else if (at().type != havel::TokenType::CloseBrace) {
            failAt(at(), "Expected ',' or '}' in set literal");
          }
        }
        
        if (at().type != havel::TokenType::CloseBrace) {
          failAt(at(), "Expected '}' after set literal");
        }
        advance(); // consume '}'
        
        return std::make_unique<havel::ast::SetExpression>(std::move(elements));
      }
    }
    
    // Block expression: { stmt; stmt; expr }
    return parseBlockExpression();
  }

  case havel::TokenType::If: {
    // If expression: if condition { expr } else { expr }
    return parseIfExpression();
  }

  default:
    failAt(tk, "Unexpected token in expression: " + tk.value);
  }
}
// Add these method declarations to Parser.h first, then implement in Parser.cpp

std::unique_ptr<havel::ast::Expression>
Parser::parseCallExpression(std::unique_ptr<havel::ast::Expression> callee) {

  auto openParen = at(); // Save '(' token location
  advance();             // consume '('

  auto call = std::make_unique<havel::ast::CallExpression>(std::move(callee));
  call->line = openParen.line;
  call->column = openParen.column;

  // Parse arguments
  while (notEOF() && at().type != havel::TokenType::CloseParen) {
    // Skip newlines before argument
    while (at().type == havel::TokenType::NewLine) {
      advance();
    }
    if (at().type == havel::TokenType::CloseParen) {
      break;
    }

    // Check for keyword argument: name=value
    if (at().type == havel::TokenType::Identifier &&
        at(1).type == havel::TokenType::Assign) {
      // This is a keyword argument
      std::string name = advance().value; // consume identifier
      advance();                          // consume '='
      auto value = parseExpression();
      call->kwargs.push_back(havel::ast::KeywordArg(name, std::move(value)));
    } else {
      // Positional argument (possibly with spread)
      std::unique_ptr<havel::ast::Expression> arg;
      if (at().type == havel::TokenType::Spread) {
        advance(); // consume '...'
        auto target = parseExpression();
        arg = std::make_unique<havel::ast::SpreadExpression>(std::move(target));
      } else {
        arg = parseExpression();
      }
      call->args.push_back(std::move(arg));
    }

    // Skip newlines after argument
    while (at().type == havel::TokenType::NewLine) {
      advance();
    }

    if (at().type == havel::TokenType::Comma) {
      advance(); // consume ','
    } else if (at().type != havel::TokenType::CloseParen) {
      failAt(at(), "Expected ',' or ')' in function call");
    }
  }

  if (at().type != havel::TokenType::CloseParen) {
    failAt(at(), "Expected ')' after function arguments");
  }
  advance(); // consume ')'

  return std::move(call);
}
std::unique_ptr<havel::ast::Expression>
Parser::parseMemberExpression(std::unique_ptr<havel::ast::Expression> object) {

  // Special case: if current token is '.' but next is '??', this is not a member access
  // Let the nullish coalescing handler deal with it
  if (at().type == havel::TokenType::Dot && at(1).type == havel::TokenType::Nullish) {
    return std::move(object);
  }

  auto dotTok = at(); // Save token location before consuming
  advance();          // consume '.'

  // Special case: if next token is ??, this is not a member access
  // Let the nullish coalescing handler deal with it
  if (at().type == havel::TokenType::Nullish) {
    return std::move(object);
  }

  // Property names can be identifiers or certain keywords
  // Many keywords should be allowed as property names (e.g., clipboard.in,
  // clipboard.out)
  if (at().type != havel::TokenType::Identifier &&
      at().type != havel::TokenType::Config &&
      at().type != havel::TokenType::Devices &&
      at().type != havel::TokenType::Modes &&
      at().type != havel::TokenType::Mode &&
      at().type != havel::TokenType::In && at().type != havel::TokenType::On &&
      at().type != havel::TokenType::Off &&
      at().type != havel::TokenType::When &&
      at().type != havel::TokenType::Loop &&
      at().type != havel::TokenType::For &&
      at().type != havel::TokenType::While &&
      at().type != havel::TokenType::If &&
      at().type != havel::TokenType::Else &&
      at().type != havel::TokenType::Return &&
      at().type != havel::TokenType::Ret && at().type != havel::TokenType::Fn &&
      at().type != havel::TokenType::Let &&
      at().type != havel::TokenType::Break &&
      at().type != havel::TokenType::Continue &&
      at().type != havel::TokenType::Switch &&
      at().type != havel::TokenType::Case &&
      at().type != havel::TokenType::Default &&
      at().type != havel::TokenType::Match &&
      at().type != havel::TokenType::Struct &&
      at().type != havel::TokenType::Enum &&
      at().type != havel::TokenType::Try &&
      at().type != havel::TokenType::Catch &&
      at().type != havel::TokenType::Finally &&
      at().type != havel::TokenType::Throw &&
      at().type != havel::TokenType::And &&
      at().type != havel::TokenType::Or &&
      at().type != havel::TokenType::Not) {
    failAt(at(), "Expected property name after '.'");
  }

  auto property = advance();

  auto member = std::make_unique<havel::ast::MemberExpression>();
  member->object = std::move(object);
  member->property = makeIdentifier(property);
  member->line = dotTok.line;
  member->column = dotTok.column;

  return std::move(member);
}

std::unique_ptr<havel::ast::Expression>
Parser::parseIndexExpression(std::unique_ptr<havel::ast::Expression> object) {

  advance(); // consume '['

  // Check for slice syntax: [start:end] or [start:] or [:end] or [:]
  // We need to look ahead to see if there's a ':' in the index expression
  bool isSlice = false;
  size_t savedPos = position;
  int bracketDepth = 1;

  // Look ahead to find if there's a ':' at the top level
  size_t lookahead = 0;
  while (position + lookahead < tokens.size()) {
    const Token &tok = tokens[position + lookahead];
    if (tok.type == havel::TokenType::OpenBracket) {
      bracketDepth++;
    } else if (tok.type == havel::TokenType::CloseBracket) {
      bracketDepth--;
      if (bracketDepth == 0)
        break;
    } else if (tok.type == havel::TokenType::Colon && bracketDepth == 1) {
      // Found a ':' at the top level - this is a slice
      isSlice = true;
      break;
    } else if (tok.type == havel::TokenType::EOF_TOKEN) {
      break;
    }
    lookahead++;
  }

  // Restore position
  position = savedPos;

  std::unique_ptr<havel::ast::Expression> index;

  if (isSlice) {
    // Parse slice: [start:end]
    std::unique_ptr<havel::ast::Expression> start;
    std::unique_ptr<havel::ast::Expression> end;

    // Check if slice starts with ':' (implicit start = 0)
    if (at().type == havel::TokenType::Colon) {
      // [:end] form - start is null (implicit 0)
      advance(); // consume ':'
      if (at().type != havel::TokenType::CloseBracket) {
        end = parseExpression();
      }
      // If end is null, it's an open-ended slice [:]
    } else {
      // [start:end] or [start:] form
      start = parseExpression();
      if (at().type == havel::TokenType::Colon) {
        advance(); // consume ':'
        if (at().type != havel::TokenType::CloseBracket) {
          end = parseExpression();
        }
        // If no expression after ':', it's [start:] (open-ended)
      } else {
        // No colon found after parsing start - this shouldn't happen given
        // lookahead But handle it gracefully as simple index
        index = std::move(start);
      }
    }

    // Create a range expression for the slice
    if (!index) {
      index = std::make_unique<havel::ast::RangeExpression>(std::move(start),
                                                            std::move(end));
    }
  } else {
    // Simple index: [expr]
    index = parseExpression();
  }

  if (at().type != havel::TokenType::CloseBracket) {
    failAt(at(), "Expected ']' after array index or slice");
  }
  advance(); // consume ']'

  return std::make_unique<havel::ast::IndexExpression>(std::move(object),
                                                       std::move(index));
}

std::unique_ptr<havel::ast::Expression> Parser::parseArrayLiteral() {
  std::vector<std::unique_ptr<havel::ast::Expression>> elements;

  advance(); // consume '['

  // Parse array elements
  int loopCounter = 0;
  while (notEOF() && at().type != havel::TokenType::CloseBracket) {
    checkParseLoop(loopCounter, "parseArrayLiteral");
    // Skip newlines before element
    while (at().type == havel::TokenType::NewLine) {
      advance();
    }

    // Handle trailing comma or closing bracket
    if (at().type == havel::TokenType::CloseBracket) {
      break;
    }

    // Check for spread operator
    std::unique_ptr<havel::ast::Expression> element;
    if (at().type == havel::TokenType::Spread) {
      advance(); // consume '...'
      auto target = parseExpression();
      element =
          std::make_unique<havel::ast::SpreadExpression>(std::move(target));
    } else {
      element = parseExpression();
    }
    elements.push_back(std::move(element));

    // Skip newlines before checking for comma or closing bracket
    while (at().type == havel::TokenType::NewLine) {
      advance();
    }

    if (at().type == havel::TokenType::Comma) {
      advance(); // consume ','
    } else if (at().type != havel::TokenType::CloseBracket) {
      // If we don't have a comma and we're not at the closing bracket,
      // consume the unexpected token to avoid infinite loops
      auto errTok = at();
      advance();
      failAt(errTok, "Expected ',' or ']' in array literal");
    }
  }

  if (at().type != havel::TokenType::CloseBracket) {
    failAt(at(), "Expected ']' to close array literal");
  }
  advance(); // consume ']'

  return std::make_unique<havel::ast::ArrayLiteral>(std::move(elements));
}

std::unique_ptr<havel::ast::Expression>
Parser::parseObjectLiteral(bool unsorted) {
  std::vector<havel::ast::ObjectLiteral::PairEntry> pairs;

  advance(); // consume '{'

  // Parse entries: key:value, positional value, [expr]:value, or ...spread
  int loopCounter = 0;
  while (notEOF() && at().type != havel::TokenType::CloseBrace) {
    checkParseLoop(loopCounter, "parseObjectLiteral");
    size_t prevPos = position;
    // Skip newlines/semicolons between entries
    while (notEOF() && (at().type == havel::TokenType::NewLine ||
                        at().type == havel::TokenType::Semicolon)) {
      advance();
    }
    if (at().type == havel::TokenType::CloseBrace) {
      break;
    }

    // Check for spread operator
    if (at().type == havel::TokenType::Spread) {
      advance(); // consume '...'
      auto target = parseExpression();
      auto spreadExpr =
          std::make_unique<havel::ast::SpreadExpression>(std::move(target));
      havel::ast::ObjectLiteral::PairEntry entry;
      entry.key = "__spread__";
      entry.value = std::move(spreadExpr);
      pairs.push_back(std::move(entry));

      // Allow comma, newline, or semicolon as separators
      if (at().type == havel::TokenType::Comma ||
          at().type == havel::TokenType::NewLine ||
          at().type == havel::TokenType::Semicolon) {
        advance();
      }
      continue;
    }

    // Check for computed key: [expr]:
    if (at().type == havel::TokenType::OpenBracket) {
      advance(); // consume '['
      auto keyExpr = parseExpression();
      if (at().type != havel::TokenType::CloseBracket) {
        failAt(at(), "Expected ']' after computed key expression");
      }
      advance(); // consume ']'
      if (at().type != havel::TokenType::Colon) {
        failAt(at(), "Expected ':' after computed key");
      }
      advance(); // consume ':'
      auto value = parseExpression();
      havel::ast::ObjectLiteral::PairEntry entry;
      entry.isComputedKey = true;
      entry.keyExpr = std::move(keyExpr);
      entry.value = std::move(value);
      pairs.push_back(std::move(entry));

      // Allow comma, newline, or semicolon as separators
      if (at().type == havel::TokenType::Comma ||
          at().type == havel::TokenType::NewLine ||
          at().type == havel::TokenType::Semicolon) {
        advance();
      }
      continue;
    }

    // Try to parse as key:value pair
    // Check if next token after potential key is a colon
    size_t savedPos = position;
    bool hasColon = false;
    std::string key;
    bool validKey = false;

    auto isKeyToken = [](havel::TokenType t) {
      return t == havel::TokenType::Identifier ||
             t == havel::TokenType::Config || t == havel::TokenType::Devices ||
             t == havel::TokenType::Modes || t == havel::TokenType::Mode ||
             t == havel::TokenType::Timeout || t == havel::TokenType::Thread ||
             t == havel::TokenType::Interval || t == havel::TokenType::Channel ||
             t == havel::TokenType::On || t == havel::TokenType::Off ||
             t == havel::TokenType::Go || t == havel::TokenType::When ||
             t == havel::TokenType::Class || t == havel::TokenType::Struct ||
             t == havel::TokenType::Enum || t == havel::TokenType::Fn ||
             t == havel::TokenType::If || t == havel::TokenType::For ||
             t == havel::TokenType::Loop || t == havel::TokenType::While ||
             t == havel::TokenType::Switch || t == havel::TokenType::Do ||
             t == havel::TokenType::Return || t == havel::TokenType::Ret ||
             t == havel::TokenType::Break || t == havel::TokenType::Continue ||
             t == havel::TokenType::Let || t == havel::TokenType::Const ||
             t == havel::TokenType::Try || t == havel::TokenType::Catch ||
             t == havel::TokenType::Finally || t == havel::TokenType::Throw ||
             t == havel::TokenType::Del ||
             t == havel::TokenType::True || t == havel::TokenType::False ||
             t == havel::TokenType::Null || t == havel::TokenType::Repeat ||
             t == havel::TokenType::String || t == havel::TokenType::MultilineString ||
             t == havel::TokenType::Number;
    };

    if (isKeyToken(at().type)) {
      key = advance().value;
      validKey = true;
      // Skip newlines to find colon
      size_t lookPos = position;
      while (lookPos < tokens.size() && tokens[lookPos].type == havel::TokenType::NewLine) {
        lookPos++;
      }
      if (lookPos < tokens.size() && tokens[lookPos].type == havel::TokenType::Colon) {
        hasColon = true;
      }
    }

    if (validKey && hasColon) {
      // It's a key:value pair
      advance(); // consume ':'
      auto value = parseExpression();
      havel::ast::ObjectLiteral::PairEntry entry;
      entry.key = std::move(key);
      entry.value = std::move(value);
      pairs.push_back(std::move(entry));
    } else {
      // Restore position - it's a positional element, not a key
      position = savedPos;
      // Parse as positional element
      auto value = parseExpression();
      havel::ast::ObjectLiteral::PairEntry entry;
      // key is empty = positional element
      entry.value = std::move(value);
      pairs.push_back(std::move(entry));
    }

    // Allow comma, newline, or semicolon as separators
    if (at().type == havel::TokenType::Comma ||
        at().type == havel::TokenType::NewLine ||
        at().type == havel::TokenType::Semicolon) {
      advance();
    } else if (at().type != havel::TokenType::CloseBrace) {
      auto errTok = at();
      advance();
      failAt(errTok, "Expected ',', newline, or '}' in collection literal");
    }

    // Progress guard
    if (position == prevPos) {
      failAt(at(), "Parser made no progress in collection literal");
    }
  }

  if (at().type != havel::TokenType::CloseBrace) {
    failAt(at(), "Expected '}' to close collection literal");
  }
  advance(); // consume '}'

  return std::make_unique<havel::ast::ObjectLiteral>(std::move(pairs),
                                                     unsorted);
}

std::unique_ptr<havel::ast::Expression> Parser::parseBlockExpression() {
  auto blockExpr = std::make_unique<havel::ast::BlockExpression>();
  blockExpr->line = at().line;
  blockExpr->column = at().column;

  advance(); // consume '{'

  // Parse statements until we hit an expression (last one becomes value)
  // or closing brace
  int loopCounter = 0;
  while (notEOF() && at().type != havel::TokenType::CloseBrace) {
    checkParseLoop(loopCounter, "parseBlockExpression");
    // Skip newlines and semicolons
    if (at().type == havel::TokenType::NewLine ||
        at().type == havel::TokenType::Semicolon) {
      advance();
      continue;
    }

    // Check if this is the final expression (no statement starter)
    // Statement starters: let, fn, if, while, for, return, etc.
    bool isStatementStarter = at().type == havel::TokenType::Let ||
                              at().type == havel::TokenType::Fn ||
                              at().type == havel::TokenType::If ||
                              at().type == havel::TokenType::While ||
                              at().type == havel::TokenType::For ||
                              at().type == havel::TokenType::Loop ||
                              at().type == havel::TokenType::Return ||
                              at().type == havel::TokenType::Break ||
                              at().type == havel::TokenType::Continue ||
                              at().type == havel::TokenType::Switch ||
                              at().type == havel::TokenType::Try ||
                              at().type == havel::TokenType::Del ||
                              at().type == havel::TokenType::Throw;

    if (isStatementStarter) {
      auto stmt = parseStatement();
      if (stmt) {
        blockExpr->body.push_back(std::move(stmt));
      }
    } else {
      // This is the final expression
      auto expr = parseExpression();
      blockExpr->value = std::move(expr);
      break;
    }
  }

  // Skip trailing newlines/semicolons before closing brace
  while (at().type == havel::TokenType::NewLine ||
         at().type == havel::TokenType::Semicolon) {
    advance();
  }

  if (at().type != havel::TokenType::CloseBrace) {
    if (at().type == havel::TokenType::EOF_TOKEN) {
      failAt(at(), "Unexpected end of file in block expression");
    }
    failAt(at(), "Expected '}' to close block expression");
  }
  advance(); // consume '}'

  return blockExpr;
}

std::unique_ptr<havel::ast::Expression> Parser::parseIfExpression() {
  advance(); // consume 'if'

  // Disable brace call sugar to prevent { from being parsed as part of
  // condition
  bool prevAllow = context.allowBraceSugar;
  context.allowBraceSugar = false;
  auto condition = parseExpression();

  // Expect then branch (block or expression)
  std::unique_ptr<havel::ast::Expression> thenBranch;
  if (at().type == havel::TokenType::OpenBrace) {
    thenBranch = parseBlockExpression();
  } else {
    thenBranch = parseExpression();
  }

  // Optional else branch
  std::unique_ptr<havel::ast::Expression> elseBranch;
  // Skip newlines before checking for else
  while (at().type == havel::TokenType::NewLine ||
         at().type == havel::TokenType::Semicolon) {
    advance();
  }
  if (at().type == havel::TokenType::Else) {
    advance(); // consume 'else'
    if (at().type == havel::TokenType::OpenBrace) {
      elseBranch = parseBlockExpression();
    } else if (at().type == havel::TokenType::If) {
      // else if chain
      elseBranch = parseIfExpression();
    } else {
      elseBranch = parseExpression();
    }
  }

  // Save location before moving
  size_t line = condition->line;
  size_t column = condition->column;

  auto ifExpr = std::make_unique<havel::ast::IfExpression>(
      std::move(condition), std::move(thenBranch), std::move(elseBranch));
  ifExpr->line = line;
  ifExpr->column = column;

  return ifExpr;
}

std::unique_ptr<havel::ast::Expression> Parser::parseArrayPattern() {
  std::vector<std::unique_ptr<havel::ast::Expression>> elements;

  advance(); // consume '['

  // Parse pattern elements
  while (notEOF() && at().type != havel::TokenType::CloseBracket) {
    // Skip newlines
    while (at().type == havel::TokenType::NewLine) {
      advance();
    }
    if (at().type == havel::TokenType::CloseBracket) {
      break;
    }

    // Pattern elements can be identifiers, rest patterns, nested patterns, or
    // patterns with defaults
    std::unique_ptr<havel::ast::Expression> element;

    if (at().type == havel::TokenType::Spread) {
      // Rest pattern: ...rest
      advance(); // consume '...'
      if (at().type != havel::TokenType::Identifier) {
        failAt(at(), "Expected identifier after '...' in rest pattern");
      }
      auto restIdent = makeIdentifier(advance());
      element =
          std::make_unique<havel::ast::SpreadExpression>(std::move(restIdent));
    } else if (at().type == havel::TokenType::Identifier) {
      auto ident = makeIdentifier(advance());
      // Check for default value: a = 10
      if (at().type == havel::TokenType::Assign) {
        advance(); // consume '='
        auto defaultValue = parseExpression();
        // Wrap in AssignmentExpression to represent default
        element = std::make_unique<havel::ast::AssignmentExpression>(
            std::move(ident), std::move(defaultValue), "=", false);
      } else {
        element = std::move(ident);
      }
    } else if (at().type == havel::TokenType::OpenBracket) {
      element = parseArrayPattern(); // Nested array pattern
      // Check for default value after nested pattern
      if (at().type == havel::TokenType::Assign) {
        advance(); // consume '='
        auto defaultValue = parseExpression();
        element = std::make_unique<havel::ast::AssignmentExpression>(
            std::move(element), std::move(defaultValue), "=", false);
      }
    } else if (at().type == havel::TokenType::OpenBrace) {
      element = parseObjectPattern(); // Nested object pattern
      // Check for default value after nested pattern
      if (at().type == havel::TokenType::Assign) {
        advance(); // consume '='
        auto defaultValue = parseExpression();
        element = std::make_unique<havel::ast::AssignmentExpression>(
            std::move(element), std::move(defaultValue), "=", false);
      }
    } else {
      failAt(at(), "Expected identifier, rest pattern, or nested pattern in "
                   "array pattern");
    }

    elements.push_back(std::move(element));

    // Skip newlines
    while (at().type == havel::TokenType::NewLine) {
      advance();
    }

    if (at().type == havel::TokenType::Comma) {
      advance(); // consume ','
    } else if (at().type != havel::TokenType::CloseBracket) {
      failAt(at(), "Expected ',' or ']' in array pattern");
    }
  }

  if (at().type != havel::TokenType::CloseBracket) {
    failAt(at(), "Expected ']' to close array pattern");
  }
  advance(); // consume ']'

  return std::make_unique<havel::ast::ArrayPattern>(std::move(elements));
}

std::unique_ptr<havel::ast::Expression> Parser::parseObjectPattern() {
  std::vector<std::pair<std::string, std::unique_ptr<havel::ast::Expression>>>
      properties;

  advance(); // consume '{'

  // Parse object pattern properties
  while (notEOF() && at().type != havel::TokenType::CloseBrace) {
    // Skip newlines
    while (notEOF() && (at().type == havel::TokenType::NewLine ||
                        at().type == havel::TokenType::Semicolon)) {
      advance();
    }
    if (at().type == havel::TokenType::CloseBrace) {
      break;
    }

    // Check for rest pattern: ...rest
    if (at().type == havel::TokenType::Spread) {
      advance(); // consume '...'
      if (at().type != havel::TokenType::Identifier) {
        failAt(at(), "Expected identifier after '...' in object rest pattern");
      }
      auto restIdent = makeIdentifier(advance());
      auto restExpr =
          std::make_unique<havel::ast::SpreadExpression>(std::move(restIdent));
      // Use special key "..." to indicate rest pattern
      properties.push_back({"...", std::move(restExpr)});

      // Rest pattern must be last
      while (at().type == havel::TokenType::NewLine ||
             at().type == havel::TokenType::Semicolon) {
        advance();
      }
      if (at().type == havel::TokenType::Comma) {
        advance(); // consume optional comma
      }
      // Skip any trailing newlines
      while (at().type == havel::TokenType::NewLine ||
             at().type == havel::TokenType::Semicolon) {
        advance();
      }
      break; // Rest pattern must be last
    }

    // Parse property key
    std::string key;
    Token keyToken = at(); // Copy current token
    if (at().type == havel::TokenType::Identifier) {
      key = advance().value;
    } else {
      failAt(at(), "Expected identifier as object pattern key");
    }

    // Check for renamed pattern: { originalName: newName }
    // or shorthand with default: { key = defaultValue }
    std::unique_ptr<havel::ast::Expression> pattern;
    if (at().type == havel::TokenType::Colon) {
      advance(); // consume ':'

      // Parse the pattern for this property
      if (at().type == havel::TokenType::Identifier) {
        pattern = makeIdentifier(advance());
      } else if (at().type == havel::TokenType::OpenBracket) {
        pattern = parseArrayPattern(); // Nested array pattern
      } else if (at().type == havel::TokenType::OpenBrace) {
        pattern = parseObjectPattern(); // Nested object pattern
      } else {
        failAt(at(),
               "Expected identifier or pattern after ':' in object pattern");
      }

      // Check for default value after renamed pattern: { key: pattern = default
      // }
      if (at().type == havel::TokenType::Assign) {
        advance(); // consume '='
        auto defaultValue = parseExpression();
        pattern = std::make_unique<havel::ast::AssignmentExpression>(
            std::move(pattern), std::move(defaultValue), "=", false);
      }
    } else if (at().type == havel::TokenType::Assign) {
      // Shorthand with default: { key = defaultValue }
      advance(); // consume '='
      auto defaultValue = parseExpression();
      auto ident = makeIdentifier(keyToken);
      // Wrap in AssignmentExpression to represent pattern with default
      pattern = std::make_unique<havel::ast::AssignmentExpression>(
          std::move(ident), std::move(defaultValue), "=", false);
    } else {
      // Default: property name becomes variable name
      pattern = makeIdentifier(keyToken);
    }

    properties.push_back({key, std::move(pattern)});

    // Skip newlines
    while (at().type == havel::TokenType::NewLine ||
           at().type == havel::TokenType::Semicolon) {
      advance();
    }

    if (at().type == havel::TokenType::Comma) {
      advance(); // consume ','
    } else if (at().type != havel::TokenType::CloseBrace) {
      failAt(at(), "Expected ',' or '}' in object pattern");
    }
  }

  if (at().type != havel::TokenType::CloseBrace) {
    failAt(at(), "Expected '}' to close object pattern");
  }
  advance(); // consume '}'

  return std::make_unique<havel::ast::ObjectPattern>(std::move(properties));
}

// Parse a match pattern: literal | identifier | _ | { ... } | [ ... ] | pat | pat
std::unique_ptr<havel::ast::Expression> Parser::parsePattern() {
  // Or patterns: try to parse first alternative, then check for |
  std::vector<std::unique_ptr<havel::ast::Expression>> alternatives;
  
  auto first = parsePatternAtom();
  if (!first) return nullptr;
  alternatives.push_back(std::move(first));
  
  while (at().type == havel::TokenType::Pipe || at().type == havel::TokenType::Or) {
    advance(); // consume '|' or '||'
    auto next = parsePatternAtom();
    if (!next) {
      failAt(at(), "Expected pattern after '|' or '||'");
      return nullptr;
    }
    alternatives.push_back(std::move(next));
  }
  
  if (alternatives.size() == 1) {
    return std::move(alternatives[0]);
  }
  return std::make_unique<havel::ast::OrPattern>(std::move(alternatives));
}

// Parse a single pattern atom (no | handling)
std::unique_ptr<havel::ast::Expression> Parser::parsePatternAtom() {
  // First, try to parse a literal expression
  std::unique_ptr<havel::ast::Expression> literal;
  
  // Wildcard
  if (at().type == havel::TokenType::Underscore) {
    advance();
    return std::make_unique<havel::ast::WildcardPattern>();
  }
  
  // Array pattern
  if (at().type == havel::TokenType::OpenBracket) {
    return parseArrayPatternForMatch();
  }
  
  // Object pattern
  if (at().type == havel::TokenType::OpenBrace) {
    return parseObjectPatternForMatch();
  }
  
  // Boolean literals
  if (at().type == havel::TokenType::True) {
    advance();
    literal = std::make_unique<havel::ast::BooleanLiteral>(true);
  } else if (at().type == havel::TokenType::False) {
    advance();
    literal = std::make_unique<havel::ast::BooleanLiteral>(false);
  }
  // Null literal
  else if (at().type == havel::TokenType::Null) {
    advance();
    literal = std::make_unique<havel::ast::NullLiteral>();
  }
  // Number literal
  else if (at().type == havel::TokenType::Number) {
    auto tok = advance();
try {
        literal = std::make_unique<havel::ast::NumberLiteral>(parseNumberLiteral(tok.value), hasDecimalPart(tok.value));
        } catch (...) {
            failAt(tok, "Invalid number literal");
            return nullptr;
        }
  }
  // String literal
  else if (at().type == havel::TokenType::String) {
    auto tok = advance();
    literal = std::make_unique<havel::ast::StringLiteral>(tok.value);
  }
  // Char literal
  else if (at().type == havel::TokenType::CharLiteral) {
    auto tok = advance();
    literal = std::make_unique<havel::ast::CharLiteral>(tok.value[0]);
  }
  // Identifier (variable binding or reference)
  else if (at().type == havel::TokenType::Identifier) {
    literal = makeIdentifier(advance());
  }
  // Wildcard pattern _
  else if (at().type == havel::TokenType::Underscore) {
    advance(); // consume '_'
    literal = std::make_unique<havel::ast::WildcardPattern>();
  }

  if (!literal) {
    failAt(at(), "Expected pattern");
    return nullptr;
  }

  // Check for range pattern: literal..=literal
  if (at().type == havel::TokenType::DotDotEquals) {
    advance(); // consume '..='
    auto endLit = parsePatternAtom();
    if (!endLit) {
      failAt(at(), "Expected pattern after '..='");
      return nullptr;
    }
    return std::make_unique<havel::ast::RangePattern>(std::move(literal), std::move(endLit));
  }
  
  return literal;
}

// Parse array pattern for match (supports [x, y], [x, ..rest])
std::unique_ptr<havel::ast::Expression> Parser::parseArrayPatternForMatch() {
  std::vector<std::unique_ptr<havel::ast::Expression>> elements;
  std::unique_ptr<havel::ast::Expression> rest;
  
  advance(); // consume '['
  
  while (notEOF() && at().type != havel::TokenType::CloseBracket) {
    while (at().type == havel::TokenType::NewLine) {
      advance();
    }
    if (at().type == havel::TokenType::CloseBracket) break;
    
    // Check for spread/rest: ..rest
    if (at().type == havel::TokenType::DotDot) {
      advance(); // consume '..'
      if (at().type == havel::TokenType::Identifier) {
        rest = makeIdentifier(advance());
      } else {
        // Anonymous rest: ..
        rest = std::make_unique<havel::ast::Identifier>("_");
      }
      break;
    }
    
    elements.push_back(parsePattern());
    
    while (at().type == havel::TokenType::NewLine) advance();
    if (at().type == havel::TokenType::Comma) {
      advance();
    }
  }
  
  if (at().type != havel::TokenType::CloseBracket) {
    failAt(at(), "Expected ']' to close array pattern");
  }
  advance(); // consume ']'
  
  return std::make_unique<havel::ast::ArrayPattern>(std::move(elements), std::move(rest));
}

// Parse object pattern for match (supports {x, y: pat})
std::unique_ptr<havel::ast::Expression> Parser::parseObjectPatternForMatch() {
  std::vector<std::pair<std::string, std::unique_ptr<havel::ast::Expression>>> properties;
  
  advance(); // consume '{'
  
  while (notEOF() && at().type != havel::TokenType::CloseBrace) {
    while (at().type == havel::TokenType::NewLine) advance();
    if (at().type == havel::TokenType::CloseBrace) break;
    
    if (at().type != havel::TokenType::Identifier) {
      failAt(at(), "Expected identifier in object pattern");
      return nullptr;
    }
    std::string key = advance().value;
    
    std::unique_ptr<havel::ast::Expression> pattern;
    if (at().type == havel::TokenType::Colon) {
      advance(); // consume ':'
      pattern = parsePattern();
    } else {
      // Shorthand: {x} binds x to x
      pattern = std::make_unique<havel::ast::Identifier>(key);
    }
    
    properties.push_back({key, std::move(pattern)});
    
    while (at().type == havel::TokenType::NewLine) advance();
    if (at().type == havel::TokenType::Comma) advance();
  }
  
  if (at().type != havel::TokenType::CloseBrace) {
    failAt(at(), "Expected '}' to close object pattern");
  }
  advance(); // consume '}'
  
  return std::make_unique<havel::ast::ObjectPattern>(std::move(properties));
}

std::unique_ptr<havel::ast::Expression> Parser::parseLambdaFromParams(
    std::vector<std::unique_ptr<havel::ast::FunctionParameter>> params) {
  // Body can be block or expression
  if (at().type == havel::TokenType::OpenBrace) {
    auto block = parseBlockStatement();
    return std::make_unique<havel::ast::LambdaExpression>(std::move(params),
                                                          std::move(block));
  }
  // Expression body: wrap in Return inside a block
  auto expr = parseExpression();
  auto exprStmt =
      std::make_unique<havel::ast::ExpressionStatement>(std::move(expr));
  auto block = std::make_unique<havel::ast::BlockStatement>();
  block->body.push_back(std::move(exprStmt));
  return std::make_unique<havel::ast::LambdaExpression>(std::move(params),
                                                        std::move(block));
}

std::unique_ptr<havel::ast::Expression>
Parser::parsePostfixExpression(std::unique_ptr<ast::Expression> expr) {
  // Handle postfix operations in a loop to support chaining: arr[0].prop() etc.
  // This handles chaining for all expression types (variables, function calls,
  // arrays, etc.)
  while (true) {
    if (at().type == havel::TokenType::OpenParen) {
      expr = parseCallExpression(std::move(expr));
      // Trailing block as last argument: func(...){ ... }
      // Only allow this if brace call sugar is enabled
      if (at().type == havel::TokenType::OpenBrace && context.allowBraceSugar) {
        // Decide if this is object-literal or block lambda by lookahead
        size_t savePos = position;
        auto next = at(1);
        bool isObject = (next.type == havel::TokenType::Identifier ||
                         next.type == havel::TokenType::String ||
                         next.type == havel::TokenType::MultilineString) &&
                        at(2).type == havel::TokenType::Colon;
        position = savePos; // restore
        if (!isObject) {
          auto block = parseBlockStatement();
          std::vector<std::unique_ptr<havel::ast::FunctionParameter>> noParams;
          auto lambda = std::make_unique<havel::ast::LambdaExpression>(
              std::move(noParams), std::move(block));
          // Append lambda to existing call args
          if (auto *callPtr =
                  dynamic_cast<havel::ast::CallExpression *>(expr.get())) {
            callPtr->args.push_back(std::move(lambda));
          }
        }
      }
    } else if (at().type == havel::TokenType::Dot) {
      // Look ahead: if next token after '.' is an operator, don't parse as member access
      // This handles cases like expr ?? default where we want ?? to bind properly
      auto nextAfterDot = at(1).type;
      bool isOperatorAfterDot = (nextAfterDot == havel::TokenType::Nullish ||
                                  nextAfterDot == havel::TokenType::Question ||
                                  nextAfterDot == havel::TokenType::Or ||
                                  nextAfterDot == havel::TokenType::And ||
                                  nextAfterDot == havel::TokenType::Equals ||
                                  nextAfterDot == havel::TokenType::NotEquals ||
                                  nextAfterDot == havel::TokenType::Less ||
                                  nextAfterDot == havel::TokenType::Greater ||
                                  nextAfterDot == havel::TokenType::LessEquals ||
                                  nextAfterDot == havel::TokenType::GreaterEquals ||
                                  nextAfterDot == havel::TokenType::Plus ||
                                  nextAfterDot == havel::TokenType::Minus ||
                                  nextAfterDot == havel::TokenType::Multiply ||
                                  nextAfterDot == havel::TokenType::Divide ||
                                  nextAfterDot == havel::TokenType::OpenParen ||
                                  nextAfterDot == havel::TokenType::OpenBracket ||
                                  nextAfterDot == havel::TokenType::Assign ||
                                  nextAfterDot == havel::TokenType::Semicolon ||
                                  nextAfterDot == havel::TokenType::NewLine ||
                                  nextAfterDot == havel::TokenType::Comma ||
                                  nextAfterDot == havel::TokenType::Colon ||
                                  nextAfterDot == havel::TokenType::CloseBrace ||
                                  nextAfterDot == havel::TokenType::CloseParen ||
                                  nextAfterDot == havel::TokenType::CloseBracket ||
                                  nextAfterDot == havel::TokenType::Return ||
                                  nextAfterDot == havel::TokenType::Ret);
      if (!isOperatorAfterDot) {
        expr = parseMemberExpression(std::move(expr));
      } else {
        break; // Stop parsing postfix, let parent handle the operator
      }
    } else if (at().type == havel::TokenType::OpenBracket) {
      expr = parseIndexExpression(std::move(expr));
    } else if (at().type == havel::TokenType::OpenBrace) {
      if (!context.allowBraceSugar) {
        break;
      }
      // Sugar: expr { ... } -> expr({ ... }) or expr(() => { ... })
      // Lookahead to determine object vs block
      size_t savePos = position;
      auto next = at(1);
      bool isObject = (next.type == havel::TokenType::Identifier ||
                       next.type == havel::TokenType::String ||
                       next.type == havel::TokenType::MultilineString) &&
                      at(2).type == havel::TokenType::Colon;
      position = savePos; // restore
      std::vector<std::unique_ptr<havel::ast::Expression>> args;
      if (isObject) {
        auto obj = parseObjectLiteral();
        args.push_back(std::move(obj));
      } else {
        auto block = parseBlockStatement();
        std::vector<std::unique_ptr<havel::ast::FunctionParameter>> noParams;
        auto lambda = std::make_unique<havel::ast::LambdaExpression>(
            std::move(noParams), std::move(block));
        args.push_back(std::move(lambda));
      }
      expr = std::make_unique<havel::ast::CallExpression>(std::move(expr),
                                                          std::move(args));
    } else if ((at().type == havel::TokenType::String ||
                at().type == havel::TokenType::MultilineString ||
                at().type == havel::TokenType::Number ||
                at().type == havel::TokenType::Identifier ||
                at().type == havel::TokenType::InterpolatedString) &&
               context.allowBraceSugar) {
      // Implicit call: expression followed by a literal (e.g., variable
      // "Hello")
      // Only allowed when brace call sugar is enabled
      auto arg = parsePrimaryExpression();
      std::vector<std::unique_ptr<havel::ast::Expression>> args;
      args.push_back(std::move(arg));
      expr = std::make_unique<havel::ast::CallExpression>(std::move(expr),
                                                          std::move(args));
    } else if (at().type == havel::TokenType::PlusPlus ||
               at().type == havel::TokenType::MinusMinus) {
      // Postfix increment/decrement: x++ or x--
      auto op = (at().type == havel::TokenType::PlusPlus)
                    ? havel::ast::UpdateExpression::Operator::Increment
                    : havel::ast::UpdateExpression::Operator::Decrement;
      advance();
      expr = std::make_unique<havel::ast::UpdateExpression>(std::move(expr), op,
                                                            false);
    } else {
      break;
    }
  }

  return expr;
}

std::unique_ptr<havel::ast::Statement> Parser::parseConfigBlock() {
  advance(); // consume 'config'
  return std::make_unique<havel::ast::ConfigBlock>(parseKeyValueBlock());
}

std::unique_ptr<havel::ast::Statement> Parser::parseDevicesBlock() {
  advance(); // consume 'devices'
  return std::make_unique<havel::ast::DevicesBlock>(parseKeyValueBlock());
}

// Parse single mode definition: mode name [priority N] { condition = ...; enter
// { ... }; exit { ... } }
std::unique_ptr<havel::ast::Statement> Parser::parseModeDefinition() {
  advance(); // consume 'mode'

  // Parse mode name
  if (at().type != havel::TokenType::Identifier) {
    failAt(at(), "Expected mode name after 'mode'");
  }
  std::string modeName = at().value;
  advance();

  // Parse optional priority
  int priority = 0;
  if (at().type == havel::TokenType::Identifier && at().value == "priority") {
    advance(); // consume 'priority'
    if (at().type != havel::TokenType::Number) {
      failAt(at(), "Expected number after 'priority'");
    }
    priority = std::stoi(at().value);
    advance();
  }

  // Parse mode block { condition = ...; enter { ... }; exit { ... } }
  if (at().type != havel::TokenType::OpenBrace) {
    failAt(at(), "Expected '{' after mode name");
  }
  advance(); // consume '{'

  std::unique_ptr<havel::ast::Expression> condition;
  std::unique_ptr<havel::ast::BlockStatement> enterBlock;
  std::unique_ptr<havel::ast::BlockStatement> exitBlock;
  std::unique_ptr<havel::ast::BlockStatement> onEnterFromBlock;
  std::unique_ptr<havel::ast::BlockStatement> onExitToBlock;
  std::unique_ptr<havel::ast::BlockStatement> onCloseBlock;
  std::unique_ptr<havel::ast::BlockStatement> onMinimizeBlock;
  std::unique_ptr<havel::ast::BlockStatement> onMaximizeBlock;
  std::unique_ptr<havel::ast::BlockStatement> onOpenBlock;
  std::string onEnterFromMode;
  std::string onExitToMode;

  // Parse condition, enter, exit, and transition hooks
  while (notEOF() && at().type != havel::TokenType::CloseBrace) {
    if (at().type == havel::TokenType::NewLine ||
        at().type == havel::TokenType::Semicolon) {
      advance();
      continue;
    }

    if (at().type != havel::TokenType::Identifier) {
      break;
    }

    std::string keyword = at().value;
    advance();

    if (keyword == "condition") {
      if (at().type != havel::TokenType::Assign &&
          at().type != havel::TokenType::Colon) {
        failAt(at(), "Expected '=' or ':' after 'condition'");
      }
      advance(); // consume '=' or ':'
      condition = parseExpression();
    } else if (keyword == "enter") {
      enterBlock = parseBlockStatement();
    } else if (keyword == "exit") {
      exitBlock = parseBlockStatement();
    } else if (keyword == "on") {
      // Parse transition hooks: on enter from "mode" { ... } or on exit to
      // "mode" { ... } Or window events: on close { ... }, on minimize { ... },
      // etc.
      if (at().type != havel::TokenType::Identifier) {
        failAt(at(), "Expected 'enter', 'exit', 'close', 'minimize', "
                     "'maximize', or 'open' after 'on'");
      }
      std::string eventType = at().value;
      advance();

      if (eventType == "enter") {
        if (at().type != havel::TokenType::Identifier || at().value != "from") {
          failAt(at(), "Expected 'from' after 'on enter'");
        }
        advance(); // consume 'from'
        if (at().type != havel::TokenType::String) {
          failAt(at(), "Expected mode name string after 'from'");
        }
        onEnterFromMode = at().value;
        advance();
        onEnterFromBlock = parseBlockStatement();
      } else if (eventType == "exit") {
        if (at().type != havel::TokenType::Identifier || at().value != "to") {
          failAt(at(), "Expected 'to' after 'on exit'");
        }
        advance(); // consume 'to'
        if (at().type != havel::TokenType::String) {
          failAt(at(), "Expected mode name string after 'to'");
        }
        onExitToMode = at().value;
        advance();
        onExitToBlock = parseBlockStatement();
      } else if (eventType == "close") {
        onCloseBlock = parseBlockStatement();
      } else if (eventType == "minimize") {
        onMinimizeBlock = parseBlockStatement();
      } else if (eventType == "maximize") {
        onMaximizeBlock = parseBlockStatement();
      } else if (eventType == "open") {
        onOpenBlock = parseBlockStatement();
      } else {
        failAt(at(), "Unknown event type: " + eventType);
      }
    } else {
      failAt(at(), "Unknown keyword in mode definition: " + keyword);
    }
  }

  if (at().type != havel::TokenType::CloseBrace) {
    failAt(at(), "Expected '}' to close mode definition");
  }
  advance(); // consume '}'

  std::vector<havel::ast::ModeDefinition> modes;
  havel::ast::ModeDefinition modeDef(modeName, std::move(condition),
                                     std::move(enterBlock),
                                     std::move(exitBlock));
  modeDef.priority = priority;
  modeDef.onEnterFrom = onEnterFromMode;
  modeDef.onExitTo = onExitToMode;
  modeDef.onEnterFromBlock = std::move(onEnterFromBlock);
  modeDef.onExitToBlock = std::move(onExitToBlock);
  modeDef.onCloseBlock = std::move(onCloseBlock);
  modeDef.onMinimizeBlock = std::move(onMinimizeBlock);
  modeDef.onMaximizeBlock = std::move(onMaximizeBlock);
  modeDef.onOpenBlock = std::move(onOpenBlock);
  modes.push_back(std::move(modeDef));
  return std::make_unique<havel::ast::ModesBlock>(std::move(modes));
}

// Parse simple mode block: mode name { statements }
// Shorthand for: when mode == "name" { statements }
std::unique_ptr<havel::ast::Statement> Parser::parseModeBlock() {
  advance(); // consume 'mode'

  // Parse mode name
  if (at().type != havel::TokenType::Identifier) {
    failAt(at(), "Expected mode name after 'mode'");
  }
  std::string modeName = at().value;
  advance();

  // Parse opening brace
  if (at().type != havel::TokenType::OpenBrace) {
    failAt(at(), "Expected '{' after mode name");
  }
  advance(); // consume '{'

  // Parse statements until closing brace
  std::vector<std::unique_ptr<havel::ast::Statement>> statements;
  while (notEOF() && at().type != havel::TokenType::CloseBrace) {
    if (at().type == havel::TokenType::NewLine ||
        at().type == havel::TokenType::Semicolon) {
      advance();
      continue;
    }

    try {
      auto stmt = parseStatement();
      if (stmt) {
        statements.push_back(std::move(stmt));
      }
    } catch (const std::exception &e) {
      if (havel::debugging::debug_parser) {
        havel::error("Parse error in mode block: {} at position {}", e.what(),
                     position);
      }
      synchronize();
      if (notEOF() == false) {
        break;
      }
    }
  }

  if (at().type != havel::TokenType::CloseBrace) {
    failAt(at(), "Expected '}' to close mode block");
  }
  advance(); // consume '}'

  return std::make_unique<havel::ast::ModeBlock>(modeName,
                                                 std::move(statements));
}

// Parse signal definition: signal name = expression
std::unique_ptr<havel::ast::Statement> Parser::parseSignalDefinition() {
  advance(); // consume 'signal'

  // Parse signal name
  if (at().type != havel::TokenType::Identifier) {
    failAt(at(), "Expected signal name after 'signal'");
  }
  std::string signalName = at().value;
  advance();

  // Parse '='
  if (at().type != havel::TokenType::Assign &&
      at().type != havel::TokenType::Colon) {
    failAt(at(), "Expected '=' or ':' after signal name");
  }
  advance(); // consume '=' or ':'

  // Parse condition expression
  auto condition = parseExpression();

  return std::make_unique<havel::ast::SignalDefinition>(signalName,
                                                        std::move(condition));
}

// Parse group definition: group name { modes: [...] }
std::unique_ptr<havel::ast::Statement> Parser::parseGroupDefinition() {
  advance(); // consume 'group'

  // Parse group name
  if (at().type != havel::TokenType::Identifier) {
    failAt(at(), "Expected group name after 'group'");
  }
  std::string groupName = at().value;
  advance();

  // Parse group block { modes: [...] }
  if (at().type != havel::TokenType::OpenBrace) {
    failAt(at(), "Expected '{' after group name");
  }
  advance(); // consume '{'

  std::vector<std::string> modeNames;

  // Parse group contents
  while (notEOF() && at().type != havel::TokenType::CloseBrace) {
    if (at().type == havel::TokenType::NewLine ||
        at().type == havel::TokenType::Semicolon) {
      advance();
      continue;
    }

    if (at().type != havel::TokenType::Identifier) {
      break;
    }

    std::string keyword = at().value;
    advance();

    if (keyword == "modes") {
      if (at().type != havel::TokenType::Colon) {
        failAt(at(), "Expected ':' after 'modes'");
      }
      advance(); // consume ':'

      // Parse array of mode names
      if (at().type != havel::TokenType::OpenBracket) {
        failAt(at(), "Expected '[' after 'modes:'");
      }
      advance(); // consume '['

      while (at().type != havel::TokenType::CloseBracket) {
        if (at().type == havel::TokenType::Comma) {
          advance();
          continue;
        }
        if (at().type != havel::TokenType::Identifier &&
            at().type != havel::TokenType::String) {
          failAt(at(), "Expected mode name in group");
        }
        modeNames.push_back(at().value);
        advance();
      }
      advance(); // consume ']'
    } else {
      failAt(at(), "Unknown keyword in group definition: " + keyword);
    }
  }

  if (at().type != havel::TokenType::CloseBrace) {
    failAt(at(), "Expected '}' to close group definition");
  }
  advance(); // consume '}'

  return std::make_unique<havel::ast::GroupDefinition>(groupName, modeNames);
}

// Parse modes block (legacy): modes { name { ... } }
std::unique_ptr<havel::ast::Statement> Parser::parseModesBlock() {
  advance(); // consume 'modes'

  if (at().type != havel::TokenType::OpenBrace) {
    failAt(at(), "Expected '{' after 'modes'");
  }
  advance(); // consume '{'

  std::vector<havel::ast::ModeDefinition> modes;

  // Parse mode definitions
  while (notEOF() && at().type != havel::TokenType::CloseBrace) {
    // Skip newlines
    if (at().type == havel::TokenType::NewLine) {
      advance();
      continue;
    }

    // Parse mode name
    if (at().type != havel::TokenType::Identifier) {
      failAt(at(), "Expected mode name");
    }
    std::string modeName = at().value;
    advance();

    // Parse mode block { condition = ...; enter { ... }; exit { ... } }
    if (at().type != havel::TokenType::OpenBrace) {
      failAt(at(), "Expected '{' after mode name");
    }
    advance(); // consume '{'

    std::unique_ptr<havel::ast::Expression> condition;
    std::unique_ptr<havel::ast::BlockStatement> enterBlock;
    std::unique_ptr<havel::ast::BlockStatement> exitBlock;

    // Parse condition, enter, exit
    while (notEOF() && at().type != havel::TokenType::CloseBrace) {
      if (at().type == havel::TokenType::NewLine ||
          at().type == havel::TokenType::Semicolon) {
        advance();
        continue;
      }

      if (at().type != havel::TokenType::Identifier) {
        break;
      }

      std::string keyword = at().value;
      advance();

      if (keyword == "condition") {
        if (at().type != havel::TokenType::Assign &&
            at().type != havel::TokenType::Colon) {
          failAt(at(), "Expected '=' or ':' after 'condition'");
        }
        advance(); // consume '=' or ':'
        condition = parseExpression();
      } else if (keyword == "enter") {
        enterBlock = parseBlockStatement();
      } else if (keyword == "exit") {
        exitBlock = parseBlockStatement();
      } else {
        failAt(at(), "Unknown keyword in mode definition: " + keyword);
      }
    }

    if (at().type != havel::TokenType::CloseBrace) {
      failAt(at(), "Expected '}' to close mode definition");
    }
    advance(); // consume '}'

    modes.emplace_back(modeName, std::move(condition), std::move(enterBlock),
                       std::move(exitBlock));
  }

  if (at().type != havel::TokenType::CloseBrace) {
    failAt(at(), "Expected '}' to close modes block");
  }
  advance(); // consume '}'

  return std::make_unique<havel::ast::ModesBlock>(std::move(modes));
}

// Parse generic config section: identifier [args...] { key = value }
std::unique_ptr<havel::ast::Statement> Parser::parseConfigSection() {
  std::string sectionName = at().value;
  advance(); // consume identifier

  // Parse optional arguments (Hyprland-style: monitor HDMI-0 { ... })
  std::vector<std::string> args;
  while (notEOF() && at().type != havel::TokenType::OpenBrace &&
         at().type != havel::TokenType::NewLine) {
    if (at().type == havel::TokenType::Identifier ||
        at().type == havel::TokenType::String ||
        at().type == havel::TokenType::MultilineString ||
        at().type == havel::TokenType::Number) {
      args.push_back(at().value);
      advance();
    } else {
      break;
    }
  }

  return std::make_unique<havel::ast::ConfigSection>(
      sectionName, parseKeyValueBlock(), args);
}

std::vector<std::pair<std::string, std::unique_ptr<havel::ast::Expression>>>
Parser::parseKeyValueBlock() {
  // Skip optional newlines
  while (notEOF() && at().type == havel::TokenType::NewLine) {
    advance();
  }

  if (at().type != havel::TokenType::OpenBrace) {
    failAt(at(), "Expected '{' after block keyword");
  }

  std::vector<std::pair<std::string, std::unique_ptr<havel::ast::Expression>>>
      pairs;

  advance(); // consume '{'

  // Parse key-value pairs
  while (notEOF() && at().type != havel::TokenType::CloseBrace) {
    // Skip newlines
    if (at().type == havel::TokenType::NewLine ||
        at().type == havel::TokenType::Semicolon) {
      advance();
      continue;
    }

    // Handle closing brace
    if (at().type == havel::TokenType::CloseBrace) {
      break;
    }

    // Parse key - can be identifier, keyword, or string
    // Keywords are allowed as keys (e.g., "default = HDMI-0")
    std::string key;
    if (at().type == havel::TokenType::Identifier ||
        at().type == havel::TokenType::Default ||
        at().type == havel::TokenType::For ||
        at().type == havel::TokenType::While ||
        at().type == havel::TokenType::If ||
        at().type == havel::TokenType::Else ||
        at().type == havel::TokenType::Match ||
        at().type == havel::TokenType::Case ||
        at().type == havel::TokenType::Switch ||
        at().type == havel::TokenType::Try ||
        at().type == havel::TokenType::Catch ||
        at().type == havel::TokenType::Finally ||
        at().type == havel::TokenType::Throw ||
        at().type == havel::TokenType::Del ||
        at().type == havel::TokenType::Return ||
        at().type == havel::TokenType::Ret ||
        at().type == havel::TokenType::Break ||
        at().type == havel::TokenType::Continue ||
        at().type == havel::TokenType::Fn ||
        at().type == havel::TokenType::Struct ||
        at().type == havel::TokenType::Enum ||
        at().type == havel::TokenType::Trait ||
        at().type == havel::TokenType::Impl ||
        at().type == havel::TokenType::Let ||
        at().type == havel::TokenType::Const ||
        at().type == havel::TokenType::In ||
        at().type == havel::TokenType::Loop ||
        at().type == havel::TokenType::When ||
        at().type == havel::TokenType::Mode ||
        at().type == havel::TokenType::On ||
        at().type == havel::TokenType::Off ||
        at().type == havel::TokenType::Config ||
        at().type == havel::TokenType::Devices ||
        at().type == havel::TokenType::Modes) {
      key = advance().value;
    } else if (at().type == havel::TokenType::String ||
               at().type == havel::TokenType::MultilineString) {
      key = advance().value;
    } else {
      failAt(at(), "Expected identifier, keyword, or string as key");
    }

    // Expect '=' or ':' (support both for compatibility)
    if (at().type != havel::TokenType::Assign &&
        at().type != havel::TokenType::Colon) {
      failAt(at(), "Expected '=' or ':' after key");
    }
    advance(); // consume '=' or ':'

    // Check if value is a nested block
    if (at().type == havel::TokenType::OpenBrace) {
      // Nested block - parse recursively and wrap in an ObjectLiteral
      auto nestedPairs = parseKeyValueBlock();

      // Convert old pair style to new PairEntry
      std::vector<havel::ast::ObjectLiteral::PairEntry> convertedEntries;
      convertedEntries.reserve(nestedPairs.size());
      for (auto &p : nestedPairs) {
        havel::ast::ObjectLiteral::PairEntry e;
        e.key = std::move(p.first);
        e.value = std::move(p.second);
        convertedEntries.push_back(std::move(e));
      }
      auto nestedObj =
          std::make_unique<havel::ast::ObjectLiteral>(std::move(convertedEntries));
      pairs.push_back({std::move(key), std::move(nestedObj)});
    } else {
      // Parse value expression
      auto value = parseExpression();
      pairs.push_back({std::move(key), std::move(value)});
    }

    // Handle comma or newline as separator
    if (at().type == havel::TokenType::Comma ||
        at().type == havel::TokenType::NewLine ||
        at().type == havel::TokenType::Semicolon) {
      advance();
    } else if (at().type != havel::TokenType::CloseBrace) {
      // If no separator is found and not at closing brace, advance to avoid
      // infinite loop
      auto errTok = at();
      advance(); // Skip whatever unexpected token we encountered
      failAt(errTok, "Expected ',', newline, or '}' in block");
    }
  }

  if (at().type != havel::TokenType::CloseBrace) {
    failAt(at(), "Expected '}' to close block");
  }
  advance(); // consume '}'

  return pairs;
}

// ============================================================================
// CONCURRENCY & COROUTINE PARSING
// ============================================================================

std::unique_ptr<havel::ast::Expression> Parser::parseThreadExpression() {
  auto threadToken = at();
  // Note: parsePrattExpression already consumed 'thread' via advance()

  if (at().type != havel::TokenType::OpenBrace) {
    failAt(at(), "Expected '{' after 'thread'");
  }
  
  auto body = parseBlockStatement();
  auto expr = std::make_unique<havel::ast::ThreadExpression>(
    std::unique_ptr<havel::ast::BlockStatement>(
      static_cast<havel::ast::BlockStatement*>(body.release())
    )
  );
  expr->line = threadToken.line;
  expr->column = threadToken.column;
  return expr;
}
std::unique_ptr<ast::Expression> Parser::parseIntervalExpression() {
    // Note: parsePrattExpression already consumed 'interval' via advance()
    // So at() is already the duration token

    // Disable brace sugar so it doesn't consume the duration expression
    auto savedBraceSugar = context.allowBraceSugar;
    context.allowBraceSugar = false;

    std::unique_ptr<ast::Expression> intervalMs;

    // Parse duration: can be number, parenthesized expression, or any expression
    if (at().type == TokenType::OpenParen) {
        advance(); // consume '('
        intervalMs = parseExpression();
        if (at().type != TokenType::CloseParen) {
            failAt(at(), "Expected ')' after interval duration");
        }
        advance(); // consume ')'
    } else if (at().type == TokenType::OpenBrace) {
        failAt(at(), "Expected number or expression for interval duration");
    } else {
        intervalMs = parseExpression();
    }

    context.allowBraceSugar = savedBraceSugar;

    if (at().type != TokenType::OpenBrace) {
        failAt(at(), "Expected '{' after interval duration");
    }

    auto body = parseBlockStatement();

    return std::make_unique<ast::IntervalExpression>(std::move(intervalMs), std::move(body));
}

std::unique_ptr<havel::ast::Expression> Parser::parseTimeoutExpression() {
  // Note: parsePrattExpression already consumed 'timeout' via advance()
  // So at() is already the duration token

  // Disable brace sugar so it doesn't consume the duration expression
  auto savedBraceSugar = context.allowBraceSugar;
  context.allowBraceSugar = false;

  std::unique_ptr<ast::Expression> timeoutMs;

  // Parse duration: can be number, parenthesized expression, or any expression
  if (at().type == TokenType::OpenParen) {
    advance(); // consume '('
    timeoutMs = parseExpression();
    if (at().type != TokenType::CloseParen) {
      failAt(at(), "Expected ')' after timeout duration");
    }
    advance(); // consume ')'
  } else if (at().type == TokenType::OpenBrace) {
    failAt(at(), "Expected number or expression for timeout duration");
  } else {
    // Parse any expression (number literal, variable, function call, etc.)
    timeoutMs = parseExpression();
  }

  context.allowBraceSugar = savedBraceSugar;

  if (at().type != TokenType::OpenBrace) {
    failAt(at(), "Expected '{' after timeout duration");
  }
  
  auto body = parseBlockStatement();
  
  return std::make_unique<ast::TimeoutExpression>(std::move(timeoutMs), std::move(body));
}

std::unique_ptr<havel::ast::Expression> Parser::parseYieldExpression() {
  auto yieldToken = at();

  // Check if yield has an argument (value or delay in ms)
  // Note: 'yield' token was already consumed by parsePrattExpression
  std::unique_ptr<havel::ast::Expression> value;
  if (at().type != havel::TokenType::Semicolon &&
      at().type != havel::TokenType::NewLine &&
      at().type != havel::TokenType::CloseBrace &&
      at().type != havel::TokenType::CloseParen) {
    value = parsePrattExpression(0);
  }

  auto expr = std::make_unique<havel::ast::YieldExpression>(std::move(value));
  expr->line = yieldToken.line;
  expr->column = yieldToken.column;
  return expr;
}

std::unique_ptr<havel::ast::Statement> Parser::parseGoStatement() {
  auto goToken = at();
  advance(); // consume 'go'

  std::unique_ptr<ast::Expression> call;

  // Check for block syntax: go { ... }
  if (at().type == havel::TokenType::OpenBrace) {
    advance(); // consume '{'

    auto blockStmt = std::make_unique<ast::BlockStatement>();
    while (at().type != havel::TokenType::CloseBrace && notEOF()) {
      if (at().type == havel::TokenType::NewLine) {
        advance();
        continue;
      }
      blockStmt->body.push_back(parseStatement());
    }

    if (at().type != havel::TokenType::CloseBrace) {
      failAt(at(), "Expected '}' after go block");
      return nullptr;
    }
    advance(); // consume '}'

    // Create lambda expression with no parameters and the block as body
    call = std::make_unique<havel::ast::LambdaExpression>(
        std::vector<std::unique_ptr<ast::FunctionParameter>>(),
        std::move(blockStmt)
    );
  } else {
    // Parse the function call expression: go fn()
    call = parseExpression();
  }

  auto stmt = std::make_unique<havel::ast::GoStatement>(std::move(call));
  stmt->line = goToken.line;
  stmt->column = goToken.column;
  return stmt;
}

std::unique_ptr<havel::ast::Expression> Parser::parseGoExpression() {
  auto goToken = at();
  // Note: parsePrattExpression already consumed 'go' via advance()
  // So at() is already the next token after 'go'

  // Parse the function call or block expression
  std::unique_ptr<ast::Expression> call;

  if (at().type == havel::TokenType::OpenBrace) {
    // go { ... } - treat as anonymous lambda function
    advance(); // consume '{'
    
    auto blockStmt = std::make_unique<ast::BlockStatement>();
    while (at().type != havel::TokenType::CloseBrace && notEOF()) {
      if (at().type == havel::TokenType::NewLine) {
        advance();
        continue;
      }
      blockStmt->body.push_back(parseStatement());
    }
    
    if (at().type != havel::TokenType::CloseBrace) {
      failAt(at(), "Expected '}' after block");
      return nullptr;
    }
    advance(); // consume '}'
    
    // Create lambda expression with no parameters and the block as body
    call = std::make_unique<havel::ast::LambdaExpression>(
        std::vector<std::unique_ptr<ast::FunctionParameter>>(),
        std::move(blockStmt)
    );
    
  } else {
    // go func() or go identifier - function call
    call = parseExpression();
  }
  
  // Wrap in GoExpression node
  auto expr = std::make_unique<havel::ast::GoExpression>(std::move(call));
  expr->line = goToken.line;
  expr->column = goToken.column;
  return expr;
}

std::unique_ptr<havel::ast::Expression> Parser::parseChannelExpression() {
  auto channelToken = at();
  // Note: parsePrattExpression already consumed 'channel' via advance()
  // So at() is already the next token after 'channel'

  // Check for parentheses: channel()
  if (at().type == havel::TokenType::OpenParen) {
    advance(); // consume '('
    if (at().type != havel::TokenType::CloseParen) {
      failAt(at(), "Expected '()' after channel");
    }
    advance(); // consume ')'
  }
  
  auto expr = std::make_unique<havel::ast::ChannelExpression>();
  expr->line = channelToken.line;
  expr->column = channelToken.column;
  return expr;
}

void Parser::printAST(const havel::ast::ASTNode &node, int indent) const {
  std::string padding(indent * 2, ' ');
  havel::info("{}{}", padding, node.toString());

  // Print children based on node type
  if (node.kind == havel::ast::NodeType::Program) {
    const auto &program = static_cast<const havel::ast::Program &>(node);
    for (const auto &stmt : program.body) {
      printAST(*stmt, indent + 1);
    }
  } else if (node.kind == havel::ast::NodeType::BlockStatement) {
    const auto &block = static_cast<const havel::ast::BlockStatement &>(node);
    for (const auto &stmt : block.body) {
      printAST(*stmt, indent + 1);
    }
  } else if (node.kind == havel::ast::NodeType::HotkeyBinding) {
    const auto &binding = static_cast<const havel::ast::HotkeyBinding &>(node);
    for (size_t i = 0; i < binding.hotkeys.size(); ++i) {
      havel::info("{}Hotkey[{}]: ", padding, i);
      printAST(*binding.hotkeys[i], indent + 1);
    }
    printAST(*binding.action, indent + 1);
  } else if (node.kind == havel::ast::NodeType::PipelineExpression) {
    const auto &pipeline =
        static_cast<const havel::ast::PipelineExpression &>(node);
    for (const auto &stage : pipeline.stages) {
      printAST(*stage, indent + 1);
    }
  } else if (node.kind == havel::ast::NodeType::BinaryExpression) {
    const auto &binary =
        static_cast<const havel::ast::BinaryExpression &>(node);
    printAST(*binary.left, indent + 1);
    printAST(*binary.right, indent + 1);
  } else if (node.kind == havel::ast::NodeType::MemberExpression) {
    const auto &member =
        static_cast<const havel::ast::MemberExpression &>(node);
    printAST(*member.object, indent + 1);
    printAST(*member.property, indent + 1);
  } else if (node.kind == havel::ast::NodeType::CallExpression) {
    const auto &call = static_cast<const havel::ast::CallExpression &>(node);
    printAST(*call.callee, indent + 1);
    for (const auto &arg : call.args) {
      printAST(*arg, indent + 1);
    }
  }
}

bool Parser::atStatementStart() {
  // Common statement start tokens
  switch (at().type) {
  case havel::TokenType::Let:
  case havel::TokenType::Const:
  case havel::TokenType::If:
  case havel::TokenType::While:
  case havel::TokenType::For:
  case havel::TokenType::Loop:
  case havel::TokenType::Break:
  case havel::TokenType::Continue:
  case havel::TokenType::On:
  case havel::TokenType::Off:
  case havel::TokenType::Fn:
  case havel::TokenType::Return:
  case havel::TokenType::Throw:
  case havel::TokenType::Try:
  case havel::TokenType::OpenBrace:
  case havel::TokenType::Import:
  case havel::TokenType::Config:
  case havel::TokenType::Devices:
  case havel::TokenType::Modes:
  case havel::TokenType::Hotkey:     // Added the hotkey token
  case havel::TokenType::Identifier: // Could be variable assignment or function
                                     // call
    return true;
  default:
    return false;
  }
}

bool Parser::isAtEndOfBlock() {
  // Check if we're at the end of a block
  return at().type == havel::TokenType::CloseBrace ||
         at().type == havel::TokenType::EOF_TOKEN;
}

} // namespace havel::parser
