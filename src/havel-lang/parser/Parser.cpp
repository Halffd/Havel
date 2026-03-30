#include "Parser.h"
#include "../utils/Logger.hpp"
#include <iostream>
#include <sstream>

namespace havel::parser {

void Parser::reportError(const std::string &message) {
  CompilerError err(ErrorSeverity::Error, at().line, at().column, message);
  errors.push_back(err);
}

void Parser::reportErrorAt(const Token &token, const std::string &message) {
  CompilerError err(ErrorSeverity::Error, token.line, token.column, message);
  errors.push_back(err);
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
    std::cerr << "FATAL: Parser::at() called with invalid offset: " << offset
              << " (position=" << position
              << ", tokens.size()=" << tokens.size() << ")\n";
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
  default:
    // Expression statement (including assignments, function calls, etc.)
    auto expr = parseExpression();
    return std::make_unique<havel::ast::ExpressionStatement>(std::move(expr));
  }
}

std::unique_ptr<havel::ast::Statement> Parser::parseStatement() {
  // Skip leading newlines within statement context
  // This allows multiple newlines between statements
  while (at().type == havel::TokenType::NewLine) {
    advance();
  }

  // If we hit EOF after skipping newlines, return null
  if (at().type == havel::TokenType::EOF_TOKEN) {
    return nullptr;
  }

  // Keywords that should NOT be parsed as statements
  // (they belong to parent constructs like if/else/while)
  if (at().type == havel::TokenType::Else ||
      at().type == havel::TokenType::Catch ||
      at().type == havel::TokenType::Finally) {
    return nullptr;
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
      // This would need to be converted to a proper expression
      // For now, let's parse a simple condition expression
      prefixCondition = parseExpression();
    } else if (at().type == havel::TokenType::If) {
      advance(); // consume 'if'
      prefixCondition = parseExpression();
    }

    if (at().type == havel::TokenType::Arrow) {
      advance(); // consume '=>'

      // Parse the action (block, statement, or expression)
      std::unique_ptr<havel::ast::Statement> action;
      if (at().type == havel::TokenType::OpenBrace) {
        action = parseBlockStatement();
      } else if (at().type == havel::TokenType::Let ||
                 at().type == havel::TokenType::If ||
                 at().type == havel::TokenType::While ||
                 at().type == havel::TokenType::For ||
                 at().type == havel::TokenType::Loop ||
                 at().type == havel::TokenType::Repeat ||
                 at().type == havel::TokenType::Break ||
                 at().type == havel::TokenType::Continue ||
                 at().type == havel::TokenType::Return ||
                 at().type == havel::TokenType::Ret ||
                 at().type == havel::TokenType::When ||
                 at().type == havel::TokenType::On ||
                 at().type == havel::TokenType::Off ||
                 at().type == havel::TokenType::Fn ||
                 at().type == havel::TokenType::Import ||
                 at().type == havel::TokenType::Config ||
                 at().type == havel::TokenType::Devices ||
                 at().type == havel::TokenType::Modes) {
        action = parseStatement();
      } else {
        auto expr = parseExpression();
        action =
            std::make_unique<havel::ast::ExpressionStatement>(std::move(expr));
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
    // Sugar forms:
    //   thread { ... }            -> thread(fn() { ... })
    //   interval <ms> { ... }     -> interval(<ms>, fn() { ... })
    //   timeout <ms> { ... }      -> timeout(<ms>, fn() { ... })
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
        prefixCondition = parseExpression();
      } else if (at().type == havel::TokenType::If) {
        advance(); // consume 'if'
        prefixCondition = parseExpression();
      }

      if (at().type == havel::TokenType::Arrow) {
        advance(); // consume '=>'

        // Parse the action (block, statement, or expression)
        std::unique_ptr<havel::ast::Statement> action;
        if (at().type == havel::TokenType::OpenBrace) {
          action = parseBlockStatement();
        } else if (at().type == havel::TokenType::Let ||
                   at().type == havel::TokenType::If ||
                   at().type == havel::TokenType::While ||
                   at().type == havel::TokenType::For ||
                   at().type == havel::TokenType::Loop ||
                   at().type == havel::TokenType::Break ||
                   at().type == havel::TokenType::Continue ||
                   at().type == havel::TokenType::Return ||
                   at().type == havel::TokenType::Ret ||
                   at().type == havel::TokenType::When ||
                   at().type == havel::TokenType::On ||
                   at().type == havel::TokenType::Off ||
                   at().type == havel::TokenType::Fn ||
                   at().type == havel::TokenType::Import ||
                   at().type == havel::TokenType::Config ||
                   at().type == havel::TokenType::Devices ||
                   at().type == havel::TokenType::Modes) {
          action = parseStatement();
        } else {
          auto expr = parseExpression();
          action = std::make_unique<havel::ast::ExpressionStatement>(
              std::move(expr));
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
    return parseStructDeclaration();
  case havel::TokenType::Class:
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
    // Check if this is a destructuring pattern like {a, b} = obj or {a: b} =
    // obj We need to look ahead to see if the brace contains identifiers
    // followed by =
    if (isDestructuringPattern()) {
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
  case havel::TokenType::Export:
    return parseExportStatement();
  case havel::TokenType::With:
    return parseWithStatement();
  case havel::TokenType::Config:
    if (at(1).type == havel::TokenType::OpenBrace) {
      return parseConfigBlock();
    }
    // Fall through to expression parsing for config.method() calls
    [[fallthrough]];
  case havel::TokenType::Match:
    // match is an expression, not a statement
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
  case havel::TokenType::Colon:
    return parseSleepStatement();
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
          return parseImplicitInputStatement();
        }
      }

      auto expr = parseExpression();

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
    value = parseExpression();
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

// Parse implicit input statement in hotkey blocks
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
    if (at().type == havel::TokenType::Equals) {
      advance(); // consume '='
      defaultValue = parsePrimaryExpression();
    }

    fields.emplace_back(fieldName, std::move(fieldType),
                        std::move(defaultValue));

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
          isOperator));
      continue;
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
    if (at().type == havel::TokenType::Equals) {
      advance(); // consume '='
      defaultValue = parsePrimaryExpression();
    }

    fields.emplace_back(fieldName, std::move(fieldType),
                        std::move(defaultValue));

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
  // Parse type reference (simple type name)
  if (at().type != havel::TokenType::Identifier) {
    failAt(at(), "Expected type name");
  }
  std::string typeName = advance().value;

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

    // Parse first iterator
    if (at().type != havel::TokenType::Identifier) {
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

      if (at().type != havel::TokenType::Identifier) {
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
    // Single iterator: for i in range
    if (at().type != havel::TokenType::Identifier) {
      failAt(at(), "Expected iterator variable after 'for'");
    }
    iterators.push_back(makeIdentifier(advance()));
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

  return std::make_unique<havel::ast::ForStatement>(
      std::move(iterators), std::move(iterable), std::move(body));
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
            std::make_unique<havel::ast::NumberLiteral>(std::stod(at().value));
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
    }
  }

  failAt(at(), "Expected 'mode', 'reload', 'start', 'tap', 'combo', 'keydown', "
               "or 'keyup' after 'on'");
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
    pattern = makeIdentifier(advance());
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

  return std::make_unique<havel::ast::LetDeclaration>(
      std::move(pattern), std::move(value), std::move(typeAnnotation), isConst);
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

  // Consume opening brace
  if (at().type != havel::TokenType::OpenBrace) {
    failAt(at(), "Expected '{'");
  }
  advance();

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

    // Fail fast on unexpected tokens in block context
    // Don't try to recover - this is a hard error
    auto stmt = parseStatement();
    if (stmt) {
      block->body.push_back(std::move(stmt));
    }
  }

  // Restore input context
  context.inInputContext = savedInputContext;

  // Consume closing brace - it might not be there if error recovery happened
  if (at().type != havel::TokenType::CloseBrace) {
    failAt(at(), "Expected '}'");
  }
  advance();

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

  // Syntax 1: use "file.hv" - import script file
  if (at().type == havel::TokenType::String ||
      at().type == havel::TokenType::MultilineString) {
    std::string filePath = advance().value;
    return std::make_unique<havel::ast::UseStatement>(
        filePath, std::vector<std::string>{"*"});
  }

  // Syntax 2: use module or use module.* - import module (Lua-style)
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
        // Wildcard import - flatten all functions into current scope
        return std::make_unique<havel::ast::UseStatement>(
            moduleName, std::vector<std::string>{"*"});
      } else {
        failAt(at(), "Expected '*' after '.' in use statement");
      }
    }

    // Simple module import - flatten into current scope
    return std::make_unique<havel::ast::UseStatement>(
        moduleName, std::vector<std::string>{"*"});
  }

  failAt(at(), "Expected module name or file path after 'use'");
  return nullptr;
}

std::unique_ptr<havel::ast::Statement> Parser::parseExportStatement() {
  advance(); // consume 'export'

  // Skip newlines
  while (at().type == havel::TokenType::NewLine) {
    advance();
  }

  // Parse the declaration being exported (fn, let, const, class, etc.)
  // For now, support: export fn name() {}, export let x = 5, export class Foo
  // {}
  auto exported = parseStatement();
  if (!exported) {
    failAt(at(), "Expected declaration after 'export'");
    return nullptr;
  }

  return std::make_unique<havel::ast::ExportStatement>(std::move(exported));
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

std::unique_ptr<havel::ast::Expression> Parser::parseExpression() {
  // Check for LINQ-style query expression: from x in expr where cond select
  // transform
  if (at().type == havel::TokenType::From) {
    return parseQueryExpression();
  }
  return parsePipelineExpression();
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
  if (at().type != havel::TokenType::Match) {
    return parseBinaryExpression();
  }

  advance(); // consume 'match'

  // Parse the value to match on
  auto value = parseBinaryExpression();
  auto match = std::make_unique<havel::ast::MatchExpression>(std::move(value));

  // Expect opening brace
  if (at().type != havel::TokenType::OpenBrace) {
    failAt(at(), "Expected '{' after match value");
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

    // Parse pattern (for now, just literals or _ for default)
    std::unique_ptr<havel::ast::Expression> pattern;
    bool isDefault = false;

    if (at().type == havel::TokenType::Underscore) {
      // Default case: _ => expr
      isDefault = true;
      advance(); // consume '_'
    } else {
      // Pattern is an expression (literal, identifier, etc.)
      pattern = parseBinaryExpression();
    }

    // Expect =>
    if (at().type != havel::TokenType::Arrow) {
      failAt(at(), "Expected '=>' after pattern");
    }
    advance(); // consume '=>'

    // Parse result expression
    auto result = parseBinaryExpression();

    if (isDefault) {
      match->defaultCase = std::move(result);
    } else {
      match->cases.push_back(
          std::make_pair(std::move(pattern), std::move(result)));
    }

    // Skip optional comma
    if (at().type == havel::TokenType::Comma) {
      advance();
    }

    // Skip newlines
    while (at().type == havel::TokenType::NewLine) {
      advance();
    }
  }

  // Expect closing brace
  if (at().type != havel::TokenType::CloseBrace) {
    failAt(at(), "Expected '}' to close match expression");
  }
  advance(); // consume '}'

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
    auto op = tokenToBinaryOperator(at().type);
    advance(); // consume '>>'
    auto right = parseLogicalOr();
    auto bin = std::make_unique<havel::ast::BinaryExpression>(
        std::move(left), op, std::move(right));
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

  if (at().type == havel::TokenType::Not ||
      at().type == havel::TokenType::Minus ||
      at().type == havel::TokenType::Plus) {

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
  case ast::BinaryOperator::Less:
    return TokenType::Less;
  case ast::BinaryOperator::Greater:
    return TokenType::Greater;
  case ast::BinaryOperator::And:
    return TokenType::And;
  case ast::BinaryOperator::Or:
    return TokenType::Or;
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
  case TokenType::Matches:
  case TokenType::Tilde: // ~ is shorthand for matches
    return havel::ast::BinaryOperator::Matches;
  case TokenType::ShiftRight:
    return havel::ast::BinaryOperator::ConfigAppend;
  default:
    fail("Invalid binary operator token: " +
         std::to_string(static_cast<int>(tokenType)));
  }
}
std::unique_ptr<havel::ast::Expression> Parser::parsePrimaryExpression() {
  havel::Token tk = at();

  // Handle global scope assignment: ::identifier = value
  if (tk.type == havel::TokenType::GlobalScope) {
    advance(); // consume '::'
    if (at().type != havel::TokenType::Identifier) {
      failAt(at(), "Expected identifier after '::'");
    }
    auto ident = makeIdentifier(advance());
    // Return identifier marked for global scope assignment
    // The actual assignment handling is in parseAssignmentExpression
    ident->isGlobalScope = true;
    return ident;
  }

  switch (tk.type) {
  case havel::TokenType::Number: {
    advance();
    double value = std::stod(tk.value);
    return std::make_unique<havel::ast::NumberLiteral>(value);
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
    return std::make_unique<havel::ast::ThisExpression>();
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

  case havel::TokenType::Hash: {
    advance(); // consume '#'

    if (at().type != havel::TokenType::OpenBrace) {
      failAt(at(), "Expected '{' after '#' for set literal");
    }
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
    // Save position to enable backtracking for lambda detection
    size_t savePos = position;
    advance(); // consume '('

    // Check for lambda: () => or (params) =>
    std::vector<std::unique_ptr<havel::ast::FunctionParameter>> params;
    bool mightBeLambda = false;

    if (at().type == havel::TokenType::CloseParen) {
      // Empty parens () - might be lambda () =>
      mightBeLambda = true;
    } else if (at().type == havel::TokenType::Identifier ||
               at().type == havel::TokenType::OpenBrace ||
               at().type == havel::TokenType::OpenBracket) {
      // Might be (a) => or ({a, b}) => or ([a, b]) => lambda
      // Try to parse as comma-separated patterns
      // Save position before attempting to parse params so we can restore if it
      // fails
      size_t paramSavePos = position;
      bool validParamList = true;

      while (at().type == havel::TokenType::Identifier ||
             at().type == havel::TokenType::OpenBrace ||
             at().type == havel::TokenType::OpenBracket) {
        // Parse parameter pattern
        std::unique_ptr<havel::ast::Expression> pattern;
        if (at().type == havel::TokenType::Identifier) {
          pattern = makeIdentifier(advance());
        } else if (at().type == havel::TokenType::OpenBrace) {
          pattern = parseObjectPattern();
        } else if (at().type == havel::TokenType::OpenBracket) {
          pattern = parseArrayPattern();
        }

        params.push_back(std::make_unique<havel::ast::FunctionParameter>(
            std::move(pattern)));

        if (at().type == havel::TokenType::Comma) {
          advance();
          if (at().type != havel::TokenType::Identifier &&
              at().type != havel::TokenType::OpenBrace &&
              at().type != havel::TokenType::OpenBracket) {
            validParamList = false;
            break;
          }
        } else if (at().type == havel::TokenType::CloseParen) {
          break;
        } else {
          // Saw pattern start but not followed by , or ) - not a valid param
          // list
          validParamList = false;
          break;
        }
      }

      if (validParamList && at().type == havel::TokenType::CloseParen) {
        mightBeLambda = true;
      } else {
        // Not a valid parameter list, restore position to re-parse as
        // expression
        position = paramSavePos;
        params.clear();
      }
    }

    if (mightBeLambda) {
      // We have () or (params), consume ) and check for =>
      advance(); // consume ')'
      if (at().type == havel::TokenType::Arrow) {
        advance(); // consume '=>'
        return parseLambdaFromParams(std::move(params));
      }
      // Has ) but no =>, so it's a grouped expression
      // Restore position and re-parse as grouped expression
      position = savePos;
    }

    // Parse as grouped expression
    // If mightBeLambda was true, position was restored, so we need to consume
    // '(' If mightBeLambda was false, '(' was already consumed at the start
    if (mightBeLambda) {
      advance(); // consume '(' since position was restored
    }
    // else: '(' already consumed, don't consume again

    // Check if this is a tuple (comma-separated expressions)
    // First, peek ahead to see if there's a comma after the first expression
    size_t checkPos = position;
    bool mightBeTuple = false;

    // Skip the first expression
    while (at(checkPos).type == havel::TokenType::NewLine)
      checkPos++;

    // Try to parse expressions and look for commas
    int exprCount = 0;
    size_t tempPos = checkPos;
    while (tempPos < tokens.size()) {
      auto &tok = tokens[tempPos];
      if (tok.type == havel::TokenType::Comma) {
        mightBeTuple = true;
        break;
      } else if (tok.type == havel::TokenType::CloseParen) {
        break;
      } else if (tok.type != havel::TokenType::NewLine) {
        exprCount++;
      }
      tempPos++;
    }

    if (mightBeTuple && exprCount >= 1) {
      // Parse as tuple
      std::vector<std::unique_ptr<havel::ast::Expression>> elements;

      while (notEOF() && at().type != havel::TokenType::CloseParen) {
        while (at().type == havel::TokenType::NewLine) {
          advance();
        }
        if (at().type == havel::TokenType::CloseParen) {
          break;
        }

        elements.push_back(parseExpression());

        while (at().type == havel::TokenType::NewLine) {
          advance();
        }

        if (at().type == havel::TokenType::Comma) {
          advance();
        } else if (at().type != havel::TokenType::CloseParen) {
          failAt(at(), "Expected ',' or ')' in tuple");
        }
      }

      if (at().type != havel::TokenType::CloseParen) {
        failAt(at(), "Expected ')' after tuple elements");
      }
      advance(); // consume ')'

      return std::make_unique<havel::ast::TupleExpression>(std::move(elements));
    }

    // Parse as grouped expression
    auto expr = parseExpression();
    if (at().type != havel::TokenType::CloseParen) {
      auto errTok = at();
      advance();
      failAt(errTok, "Expected ')'");
    }
    advance(); // consume ')'

    // Handle postfix operations for grouped expressions as well
    return parsePostfixExpression(std::move(expr));
  }

  case havel::TokenType::OpenBracket: {
    auto array = parseArrayLiteral();
    // Handle postfix operations for array literals (moved to common function)
    return parsePostfixExpression(std::move(array));
  }

  case havel::TokenType::OpenBrace: {
    // Could be object literal {key: value} or block expression {stmt; expr}
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
    bool isObject = (nextTok.type == havel::TokenType::Identifier ||
                     nextTok.type == havel::TokenType::String ||
                     nextTok.type == havel::TokenType::MultilineString ||
                     nextTok.type == havel::TokenType::Config ||
                     nextTok.type == havel::TokenType::Devices ||
                     nextTok.type == havel::TokenType::Modes ||
                     nextTok.type == havel::TokenType::Mode) &&
                    at(lookahead + 1).type == havel::TokenType::Colon;

    if (isObject) {
      auto obj = parseObjectLiteral();
      return parsePostfixExpression(std::move(obj));
    } else {
      // Block expression: { stmt; stmt; expr }
      return parseBlockExpression();
    }
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

  auto dotTok = at(); // Save token location before consuming
  advance();          // consume '.'

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
      at().type != havel::TokenType::Throw) {
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
  while (notEOF() && at().type != havel::TokenType::CloseBracket) {
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

std::unique_ptr<havel::ast::Expression> Parser::parseObjectLiteral() {
  std::vector<std::pair<std::string, std::unique_ptr<havel::ast::Expression>>>
      pairs;

  advance(); // consume '{'

  // Parse object key-value pairs
  while (notEOF() && at().type != havel::TokenType::CloseBrace) {
    // Skip newlines/semicolons between pairs
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
      // For object spread, we use a special key "__spread__" to mark it
      auto target = parseExpression();
      auto spreadExpr =
          std::make_unique<havel::ast::SpreadExpression>(std::move(target));
      pairs.push_back({"__spread__", std::move(spreadExpr)});

      // Allow comma, newline, or semicolon as separators
      if (at().type == havel::TokenType::Comma ||
          at().type == havel::TokenType::NewLine ||
          at().type == havel::TokenType::Semicolon) {
        advance();
      }
      continue;
    }

    // Parse key - can be identifier, string, or certain keywords
    std::string key;
    if (at().type == havel::TokenType::Identifier ||
        at().type == havel::TokenType::Config ||
        at().type == havel::TokenType::Devices ||
        at().type == havel::TokenType::Modes ||
        at().type == havel::TokenType::Mode) {
      key = advance().value;
    } else if (at().type == havel::TokenType::String ||
               at().type == havel::TokenType::MultilineString) {
      key = advance().value;
    } else {
      failAt(at(), "Expected identifier, string, or spread for object literal");
    }

    // Expect colon
    if (at().type != havel::TokenType::Colon) {
      failAt(at(), "Expected ':' after object key");
    }
    advance(); // consume ':'

    // Parse value
    auto value = parseExpression();
    pairs.push_back({key, std::move(value)});

    // Allow comma, newline, or semicolon as separators
    if (at().type == havel::TokenType::Comma ||
        at().type == havel::TokenType::NewLine ||
        at().type == havel::TokenType::Semicolon) {
      advance();
    } else if (at().type != havel::TokenType::CloseBrace) {
      // Consume unexpected token to prevent infinite loops
      auto errTok = at();
      advance();
      failAt(errTok, "Expected ',', newline, or '}' in object literal");
    }
  }

  if (at().type != havel::TokenType::CloseBrace) {
    failAt(at(), "Expected '}' to close object literal");
  }
  advance(); // consume '}'

  return std::make_unique<havel::ast::ObjectLiteral>(std::move(pairs));
}

std::unique_ptr<havel::ast::Expression> Parser::parseBlockExpression() {
  auto blockExpr = std::make_unique<havel::ast::BlockExpression>();
  blockExpr->line = at().line;
  blockExpr->column = at().column;

  advance(); // consume '{'

  // Parse statements until we hit an expression (last one becomes value)
  // or closing brace
  while (notEOF() && at().type != havel::TokenType::CloseBrace) {
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
      expr = parseMemberExpression(std::move(expr));
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

      // Create an object literal to hold nested pairs
      auto nestedObj =
          std::make_unique<havel::ast::ObjectLiteral>(std::move(nestedPairs));
      pairs.push_back({key, std::move(nestedObj)});
    } else {
      // Parse value expression
      auto value = parseExpression();
      pairs.push_back({key, std::move(value)});
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
