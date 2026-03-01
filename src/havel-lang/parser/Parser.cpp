#include "Parser.h"
#include "../utils/Logger.hpp"
#include <sstream>

namespace havel::parser {

void Parser::reportError(const std::string& message) {
  CompilerError err(ErrorSeverity::Error, at().line, at().column, message);
  errors.push_back(err);
}

void Parser::reportErrorAt(const Token& token, const std::string& message) {
  CompilerError err(ErrorSeverity::Error, token.line, token.column, message);
  errors.push_back(err);
}

void Parser::synchronize() {
  // Panic mode recovery - skip tokens until we find a statement boundary
  advance(); // Skip the current token that caused the error

  while (notEOF()) {
    havel::TokenType type = at().type;

    // Statement boundaries
    if (type == havel::TokenType::Semicolon || type == havel::TokenType::NewLine) {
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
  throw havel::parser::ParseError(token.line, token.column, message);
}

havel::Token Parser::at(size_t offset) const {
  size_t pos = position + offset;
  if (pos >= tokens.size()) {
    return havel::Token("EOF", havel::TokenType::EOF_TOKEN, "EOF", 0, 0);
  }
  return tokens[pos];
}

havel::Token Parser::advance() {
  if (position >= tokens.size()) {
    return havel::Token("EOF", havel::TokenType::EOF_TOKEN, "EOF", 0, 0);
  }
  return tokens[position++];
}

bool Parser::notEOF() const { return at().type != havel::TokenType::EOF_TOKEN; }

std::unique_ptr<havel::ast::Program>
Parser::produceAST(const std::string &sourceCode) {
  // Tokenize source code
  havel::Lexer lexer(sourceCode, debug.lexer);
  tokens = lexer.tokenize();
  
  // Collect lexer errors
  for (const auto& err : lexer.getErrors()) {
    errors.push_back(err);
  }
  
  position = 0;

  if (debug.parser) {
    havel::debug("PARSE: Starting to parse program with {} tokens", tokens.size());
  }

  // Create program AST node
  auto program = std::make_unique<havel::ast::Program>();

  // Parse all statements until EOF
  while (notEOF()) {
    // Skip empty lines or statement separators
    if (at().type == havel::TokenType::NewLine ||
        at().type == havel::TokenType::Semicolon) {
      advance();
      continue;
    }

    if (debug.parser) {
      havel::debug("PARSE: Parsing statement at token {}", at().toString());
    }

    // Fail fast on errors - no recovery at top level
    auto stmt = parseStatement();
    if (stmt) {
      program->body.push_back(std::move(stmt));
    }
  }

  if (havel::debugging::debug_ast) {
    printAST(*program);
  }

  return program;
}

std::unique_ptr<havel::ast::Program>
Parser::produceASTStrict(const std::string &sourceCode) {
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
    return std::make_unique<havel::ast::ExpressionStatement>(std::move(expr));
  }
  case havel::TokenType::Let:
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
  case havel::TokenType::Enum:
    return parseEnumDeclaration();
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
  case havel::TokenType::OpenBrace:
    return parseBlockStatement();
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
  case havel::TokenType::Modes:
    if (at(1).type == havel::TokenType::OpenBrace) {
      return parseModesBlock();
    }
    {
      auto expr = parseExpression();
      return std::make_unique<havel::ast::ExpressionStatement>(std::move(expr));
    }
  default: {
    auto expr = parseExpression();

    // Require statement terminator: semicolon or newline
    // This prevents "expr1 expr2" from being parsed as expr1(expr2)
    havel::debug("DEBUG: After parseExpression, token is: {}", at().toString());
    if (at().type != havel::TokenType::Semicolon &&
        at().type != havel::TokenType::NewLine &&
        at().type != havel::TokenType::EOF_TOKEN &&
        at().type != havel::TokenType::CloseBrace) {
      havel::debug("DEBUG: Failing at token: {}", at().toString());
      failAt(at(), "Expected ';' or newline after expression (Havel requires statement terminators)");
    }

    // Consume optional semicolon
    if (at().type == havel::TokenType::Semicolon) {
      advance();
    }

    return std::make_unique<havel::ast::ExpressionStatement>(std::move(expr));
  }
  }
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
  auto name = std::make_unique<havel::ast::Identifier>(advance().value);

  if (at().type != havel::TokenType::OpenParen) {
    failAt(at(), "Expected '(' after function name");
  }
  advance(); // consume '('

  std::vector<std::unique_ptr<havel::ast::Identifier>> params;
  while (notEOF() && at().type != havel::TokenType::CloseParen) {
    while (at().type == havel::TokenType::NewLine) {
      advance();
    }
    if (at().type == havel::TokenType::CloseParen) {
      break;
    }

    if (at().type != havel::TokenType::Identifier) {
      failAt(at(), "Expected identifier in parameter list");
    }
    params.push_back(std::make_unique<havel::ast::Identifier>(advance().value));

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

  auto body = parseBlockStatement();

  return std::make_unique<havel::ast::FunctionDeclaration>(
      std::move(name), std::move(params), std::move(body));
}

std::unique_ptr<havel::ast::Statement> Parser::parseReturnStatement() {
  advance(); // consume "return"
  std::unique_ptr<havel::ast::Expression> value = nullptr;

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
  
  // Parse fields
  auto fields = parseStructFields();
  
  // Parse closing brace
  if (at().type != havel::TokenType::CloseBrace) {
    failAt(at(), "Expected '}' to close struct definition");
  }
  advance(); // consume '}'
  
  // Create struct definition
  ast::StructDefinition def(std::move(fields));
  
  return std::make_unique<ast::StructDeclaration>(structName, std::move(def));
}

std::vector<ast::StructFieldDef> Parser::parseStructFields() {
  std::vector<ast::StructFieldDef> fields;
  
  while (at().type != havel::TokenType::CloseBrace && notEOF()) {
    // Skip newlines and comments
    if (at().type == havel::TokenType::NewLine || 
        at().type == havel::TokenType::Comment) {
      advance();
      continue;
    }
    
    // Parse field name
    if (at().type != havel::TokenType::Identifier) {
      failAt(at(), "Expected field name in struct");
    }
    std::string fieldName = advance().value;
    
    // Optional type annotation
    std::optional<std::unique_ptr<ast::TypeDefinition>> fieldType;
    if (at().type == havel::TokenType::Colon) {
      advance(); // consume ':'
      fieldType = parseTypeDefinition();
    }
    
    fields.emplace_back(fieldName, std::move(fieldType));
    
    // Optional comma
    if (at().type == havel::TokenType::Comma) {
      advance();
    }
  }
  
  return fields;
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
  if (at().type != havel::TokenType::Colon) {
    failAt(at(), "Expected ':' for type annotation");
  }
  advance(); // consume ':'
  
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
        catchVariable = std::make_unique<havel::ast::Identifier>(advance().value);
      }
      if (at().type != havel::TokenType::CloseParen) {
        failAt(at(), "Expected ')' after catch variable");
      }
      advance(); // consume ')'
    } else if (at().type == havel::TokenType::Identifier) {
      // Old syntax without parentheses
      catchVariable = std::make_unique<havel::ast::Identifier>(advance().value);
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

  bool prevAllow = allowBraceCallSugar;
  allowBraceCallSugar = false;
  auto condition = parseExpression();
  allowBraceCallSugar = prevAllow;

  if (at().type != havel::TokenType::OpenBrace) {
    failAt(at(), "Expected '{' after if condition");
  }

  auto consequence = parseBlockStatement();

  std::unique_ptr<havel::ast::Statement> alternative = nullptr;
  if (at().type == havel::TokenType::Else) {
    advance(); // consume "else"

    if (at().type == havel::TokenType::If) {
      alternative = parseIfStatement();
    } else {
      alternative = parseBlockStatement();
    }
  }

  return std::make_unique<havel::ast::IfStatement>(
      std::move(condition), std::move(consequence), std::move(alternative));
}

std::unique_ptr<havel::ast::Statement> Parser::parseWhileStatement() {
  advance(); // consume "while"

  bool prevAllow = allowBraceCallSugar;
  allowBraceCallSugar = false;
  auto condition = parseExpression();
  allowBraceCallSugar = prevAllow;

  if (at().type != havel::TokenType::OpenBrace) {
    failAt(at(), "Expected '{' after while condition");
  }

  auto body = parseBlockStatement();

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
  bool prevAllow = allowBraceCallSugar;
  allowBraceCallSugar = false;
  auto condition = parseExpression();
  allowBraceCallSugar = prevAllow;

  return std::make_unique<havel::ast::DoWhileStatement>(std::move(body),
                                                        std::move(condition));
}

std::unique_ptr<havel::ast::Statement> Parser::parseSwitchStatement() {
  advance(); // consume "switch"

  // Parse the switch expression
  bool prevAllow = allowBraceCallSugar;
  allowBraceCallSugar = false;
  auto expression = parseExpression();
  allowBraceCallSugar = prevAllow;

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
    iterators.push_back(
        std::make_unique<havel::ast::Identifier>(advance().value));

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
      iterators.push_back(
          std::make_unique<havel::ast::Identifier>(advance().value));
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
    iterators.push_back(
        std::make_unique<havel::ast::Identifier>(advance().value));
  }

  if (at().type != havel::TokenType::In) {
    failAt(at(), "Expected 'in' after iterator variable(s)");
  }
  advance(); // consume "in"

  // Disable brace call sugar to prevent for loop body { from being consumed
  bool prevAllow = allowBraceCallSugar;
  allowBraceCallSugar = false;
  auto iterable = parseExpression();
  allowBraceCallSugar = prevAllow;

  // Skip newlines before opening brace
  while (at().type == havel::TokenType::NewLine) {
    advance();
  }

  if (at().type != havel::TokenType::OpenBrace) {
    failAt(at(), "Expected '{' after for iterable");
  }

  auto body = parseBlockStatement();

  return std::make_unique<havel::ast::ForStatement>(
      std::move(iterators), std::move(iterable), std::move(body));
}

std::unique_ptr<havel::ast::Statement> Parser::parseLoopStatement() {
  advance(); // consume "loop"

  // Skip newlines before opening brace
  while (at().type == havel::TokenType::NewLine) {
    advance();
  }

  if (at().type != havel::TokenType::OpenBrace) {
    failAt(at(), "Expected '{' after 'loop'");
  }

  auto body = parseBlockStatement();

  return std::make_unique<havel::ast::LoopStatement>(std::move(body));
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
    }
  }
  
  failAt(at(), "Expected 'mode', 'reload', or 'start' after 'on'");
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

std::unique_ptr<havel::ast::Statement> Parser::parseLetDeclaration() {
  advance(); // consume "let"

  std::unique_ptr<havel::ast::Expression> pattern;

  // Check if we have a destructuring pattern
  if (at().type == havel::TokenType::OpenBracket) {
    // Array destructuring: let [a, b] = arr
    pattern = parseArrayPattern();
  } else if (at().type == havel::TokenType::OpenBrace) {
    // Object destructuring: let {x, y} = obj
    pattern = parseObjectPattern();
  } else if (at().type == havel::TokenType::Identifier) {
    // Regular variable: let x = value
    pattern = std::make_unique<havel::ast::Identifier>(advance().value);
  } else {
    failAt(at(), "Expected identifier, '[' or '{' after 'let'");
  }

  if (at().type != havel::TokenType::Assign) {
    // Allow declarations without assignment, e.g., `let x;`
    if (dynamic_cast<havel::ast::Identifier *>(pattern.get())) {
      return std::make_unique<havel::ast::LetDeclaration>(std::move(pattern));
    } else {
      failAt(at(), "Destructuring patterns require initialization");
    }
  }
  advance(); // consume "="

  auto value = parseExpression();

  return std::make_unique<havel::ast::LetDeclaration>(std::move(pattern),
                                                      std::move(value));
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

  // Check for conditional 'when' clause
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
    // It's a block statement - parse it directly as a statement
    binding->action = parseBlockStatement();
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
  bool prevAllow = allowBraceCallSugar;
  allowBraceCallSugar = false;
  auto condition = parseExpression();
  allowBraceCallSugar = prevAllow;

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
        havel::error("Parse error in when block: {} at position {}", e.what(), position);
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

std::unique_ptr<havel::ast::BlockStatement> Parser::parseBlockStatement() {
  auto block = std::make_unique<havel::ast::BlockStatement>();

  // Consume opening brace
  if (at().type != havel::TokenType::OpenBrace) {
    failAt(at(), "Expected '{'");
  }
  advance();

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

  std::vector<std::string> moduleNames;

  // Parse module names separated by commas
  while (notEOF() && at().type != havel::TokenType::NewLine) {
    if (at().type != havel::TokenType::Identifier) {
      failAt(at(), "Expected module name after 'use'");
    }

    moduleNames.push_back(advance().value);

    // Check for comma
    if (at().type == havel::TokenType::Comma) {
      advance(); // consume ','
    } else {
      break; // No more modules
    }
  }

  if (moduleNames.empty()) {
    failAt(at(), "Expected at least one module name after 'use'");
  }

  return std::make_unique<havel::ast::UseStatement>(std::move(moduleNames));
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
  return parseAssignmentExpression();
}

std::unique_ptr<havel::ast::Expression> Parser::parseAssignmentExpression() {
  auto left = parsePipelineExpression();

  // Check for assignment operators
  if (at().type == havel::TokenType::Assign ||
      at().type == havel::TokenType::PlusAssign ||
      at().type == havel::TokenType::MinusAssign ||
      at().type == havel::TokenType::MultiplyAssign ||
      at().type == havel::TokenType::DivideAssign) {
    auto opTok = advance(); // consume the operator

    // Right-associative: a = b = c means a = (b = c)
    auto value = parseAssignmentExpression();

    auto assign = std::make_unique<havel::ast::AssignmentExpression>(
        std::move(left), std::move(value), opTok.value);
    assign->line = opTok.line;
    assign->column = opTok.column;
    return assign;
  }

  return left;
}

std::unique_ptr<havel::ast::Expression> Parser::parsePipelineExpression() {
  auto left = parseTernaryExpression();

  // Check for pipeline operator |
  if (at().type == havel::TokenType::Pipe) {
    auto pipeline = std::make_unique<havel::ast::PipelineExpression>();
    pipeline->stages.push_back(std::move(left));

    while (at().type == havel::TokenType::Pipe) {
      advance(); // consume '|'
      auto stage = parseTernaryExpression();
      pipeline->stages.push_back(std::move(stage));
    }

    return std::move(pipeline);
  }

  return left;
}

std::unique_ptr<havel::ast::Expression> Parser::parseTernaryExpression() {
  auto condition = parseBinaryExpression();

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
    auto opTok = at();  // Save operator token location
    auto op = tokenToBinaryOperator(at().type);
    advance();
    auto right = parseLogicalAnd();
    auto bin = std::make_unique<havel::ast::BinaryExpression>(std::move(left), op,
                                                          std::move(right));
    bin->line = opTok.line;
    bin->column = opTok.column;
    left = std::move(bin);
  }

  return left;
}

std::unique_ptr<havel::ast::Expression> Parser::parseLogicalAnd() {
  auto left = parseEquality();

  while (at().type == havel::TokenType::And) {
    auto opTok = at();  // Save operator token location
    auto op = tokenToBinaryOperator(at().type);
    advance();
    auto right = parseEquality();
    auto bin = std::make_unique<havel::ast::BinaryExpression>(std::move(left), op,
                                                          std::move(right));
    bin->line = opTok.line;
    bin->column = opTok.column;
    left = std::move(bin);
  }

  return left;
}

std::unique_ptr<havel::ast::Expression> Parser::parseEquality() {
  auto left = parseComparison();

  while (at().type == havel::TokenType::Equals ||
         at().type == havel::TokenType::NotEquals) {
    auto opTok = at();  // Save operator token location
    auto op = tokenToBinaryOperator(at().type);
    advance();
    auto right = parseComparison();
    auto bin = std::make_unique<havel::ast::BinaryExpression>(std::move(left), op,
                                                          std::move(right));
    bin->line = opTok.line;
    bin->column = opTok.column;
    left = std::move(bin);
  }

  return left;
}

std::unique_ptr<havel::ast::Expression> Parser::parseComparison() {
  auto left = parseRange();

  // Comparison operators: < > <= >=
  // Left-associative: a < b < c parses as ((a < b) < c)
  // Note: Python-style chaining (a < b && b < c) is NOT supported.
  // For Python semantics, use explicit: (a < b) && (b < c)
  while (at().type == havel::TokenType::Less ||
         at().type == havel::TokenType::Greater ||
         at().type == havel::TokenType::LessEquals ||
         at().type == havel::TokenType::GreaterEquals) {
    auto opTok = at();  // Save operator token location
    auto op = tokenToBinaryOperator(at().type);
    advance();
    auto right = parseRange();
    auto bin = std::make_unique<havel::ast::BinaryExpression>(std::move(left), op,
                                                          std::move(right));
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
    auto opTok = at();  // Save operator token location
    advance(); // consume '..'
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
    auto opTok = at();  // Save operator token location
    auto op = tokenToBinaryOperator(at().type);
    advance();
    auto right = parseMultiplicative();
    auto bin = std::make_unique<havel::ast::BinaryExpression>(std::move(left), op,
                                                          std::move(right));
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
         at().type == havel::TokenType::Modulo) {
    auto opTok = at();  // Save operator token location
    auto op = tokenToBinaryOperator(at().type);
    advance();
    auto right = parseUnary();
    auto bin = std::make_unique<havel::ast::BinaryExpression>(std::move(left), op,
                                                          std::move(right));
    bin->line = opTok.line;
    bin->column = opTok.column;
    left = std::move(bin);
  }

  return left;
}
std::unique_ptr<havel::ast::Expression> Parser::parseUnary() {
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

  return parsePrimaryExpression();
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
    double value = std::stod(tk.value);
    return std::make_unique<havel::ast::NumberLiteral>(value);
  }

  case havel::TokenType::String: {
    advance();
    return std::make_unique<havel::ast::StringLiteral>(tk.value);
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
      std::vector<std::unique_ptr<havel::ast::Identifier>> params;
      auto param = std::make_unique<havel::ast::Identifier>(identTk.value);
      param->line = identTk.line;
      param->column = identTk.column;
      params.push_back(std::move(param));
      return parseLambdaFromParams(std::move(params));
    }
    // Otherwise it's a normal identifier expression
    identTk = advance();
    std::unique_ptr<havel::ast::Expression> expr =
        std::make_unique<havel::ast::Identifier>(identTk.value);
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
    std::vector<std::unique_ptr<havel::ast::Identifier>> params;

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

      if (at().type != havel::TokenType::Identifier) {
        failAt(at(), "Expected identifier in function parameter list");
      }
      params.push_back(
          std::make_unique<havel::ast::Identifier>(advance().value));

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
    std::vector<std::unique_ptr<havel::ast::Identifier>> params;
    bool mightBeLambda = false;
    
    if (at().type == havel::TokenType::CloseParen) {
      // Empty parens () - might be lambda () =>
      mightBeLambda = true;
    } else if (at().type == havel::TokenType::Identifier) {
      // Might be (a) => or (a, b) => lambda
      // Try to parse as comma-separated identifiers
      bool validParamList = true;
      while (at().type == havel::TokenType::Identifier) {
        params.push_back(std::make_unique<havel::ast::Identifier>(advance().value));
        if (at().type == havel::TokenType::Comma) {
          advance();
          if (at().type != havel::TokenType::Identifier) {
            validParamList = false;
            break;
          }
        } else if (at().type == havel::TokenType::CloseParen) {
          break;
        } else {
          // Saw identifier but not followed by , or ) - not a valid param list
          validParamList = false;
          break;
        }
      }
      
      if (validParamList && at().type == havel::TokenType::CloseParen) {
        mightBeLambda = true;
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
    
    // Parse as grouped expression (either not a lambda, or lambda check failed)
    advance(); // consume '('
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
    size_t savePos = position;
    size_t lookahead = 1;
    // Skip newlines to find first significant token
    while (at(lookahead).type == havel::TokenType::NewLine) {
      lookahead++;
    }
    auto nextTok = at(lookahead);
    bool isObject = (nextTok.type == havel::TokenType::Identifier ||
                     nextTok.type == havel::TokenType::String) &&
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

  auto openParen = at();  // Save '(' token location
  advance(); // consume '('

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
    
    auto arg = parseExpression();
    call->args.push_back(std::move(arg));

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

  auto dotTok = at();  // Save token location before consuming
  advance(); // consume '.'

  if (at().type != havel::TokenType::Identifier) {
    failAt(at(), "Expected property name after '.'");
  }

  auto property = advance();
  
  auto member = std::make_unique<havel::ast::MemberExpression>();
  member->object = std::move(object);
  member->property = std::make_unique<havel::ast::Identifier>(property.value);
  member->line = dotTok.line;
  member->column = dotTok.column;

  return std::move(member);
}

std::unique_ptr<havel::ast::Expression>
Parser::parseIndexExpression(std::unique_ptr<havel::ast::Expression> object) {

  advance(); // consume '['

  auto index = parseExpression();

  if (at().type != havel::TokenType::CloseBracket) {
    failAt(at(), "Expected ']' after array index");
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
    // Handle trailing comma
    if (at().type == havel::TokenType::CloseBracket) {
      break;
    }

    auto element = parseExpression();
    elements.push_back(std::move(element));

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

    // Parse key - can be identifier or string
    std::string key;
    if (at().type == havel::TokenType::Identifier) {
      key = advance().value;
    } else if (at().type == havel::TokenType::String) {
      key = advance().value;
    } else {
      failAt(at(), "Expected identifier or string as object key");
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
    bool isStatementStarter =
        at().type == havel::TokenType::Let ||
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

  // Disable brace call sugar to prevent { from being parsed as part of condition
  bool prevAllow = allowBraceCallSugar;
  allowBraceCallSugar = false;
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

    // Pattern elements can be identifiers, rest patterns, or nested patterns
    std::unique_ptr<havel::ast::Expression> element;

    if (at().type == havel::TokenType::Identifier) {
      element = std::make_unique<havel::ast::Identifier>(advance().value);
    } else if (at().type == havel::TokenType::OpenBracket) {
      element = parseArrayPattern(); // Nested array pattern
    } else if (at().type == havel::TokenType::OpenBrace) {
      element = parseObjectPattern(); // Nested object pattern
    } else {
      failAt(at(), "Expected identifier or pattern in array pattern");
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

    // Parse property key
    std::string key;
    if (at().type == havel::TokenType::Identifier) {
      key = advance().value;
    } else {
      failAt(at(), "Expected identifier as object pattern key");
    }

    // Check for renamed pattern: { originalName: newName }
    std::unique_ptr<havel::ast::Expression> pattern;
    if (at().type == havel::TokenType::Colon) {
      advance(); // consume ':'

      // Parse the pattern for this property
      if (at().type == havel::TokenType::Identifier) {
        pattern = std::make_unique<havel::ast::Identifier>(advance().value);
      } else if (at().type == havel::TokenType::OpenBracket) {
        pattern = parseArrayPattern(); // Nested array pattern
      } else if (at().type == havel::TokenType::OpenBrace) {
        pattern = parseObjectPattern(); // Nested object pattern
      } else {
        failAt(at(),
               "Expected identifier or pattern after ':' in object pattern");
      }
    } else {
      // Default: property name becomes variable name
      pattern = std::make_unique<havel::ast::Identifier>(key);
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
    std::vector<std::unique_ptr<havel::ast::Identifier>> params) {
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
      if (at().type == havel::TokenType::OpenBrace && allowBraceCallSugar) {
        // Decide if this is object-literal or block lambda by lookahead
        size_t savePos = position;
        auto next = at(1);
        bool isObject = (next.type == havel::TokenType::Identifier ||
                         next.type == havel::TokenType::String) &&
                        at(2).type == havel::TokenType::Colon;
        position = savePos; // restore
        if (!isObject) {
          auto block = parseBlockStatement();
          std::vector<std::unique_ptr<havel::ast::Identifier>> noParams;
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
      if (!allowBraceCallSugar) {
        break;
      }
      // Sugar: expr { ... } -> expr({ ... }) or expr(() => { ... })
      // Lookahead to determine object vs block
      size_t savePos = position;
      auto next = at(1);
      bool isObject = (next.type == havel::TokenType::Identifier ||
                       next.type == havel::TokenType::String) &&
                      at(2).type == havel::TokenType::Colon;
      position = savePos; // restore
      std::vector<std::unique_ptr<havel::ast::Expression>> args;
      if (isObject) {
        auto obj = parseObjectLiteral();
        args.push_back(std::move(obj));
      } else {
        auto block = parseBlockStatement();
        std::vector<std::unique_ptr<havel::ast::Identifier>> noParams;
        auto lambda = std::make_unique<havel::ast::LambdaExpression>(
            std::move(noParams), std::move(block));
        args.push_back(std::move(lambda));
      }
      expr = std::make_unique<havel::ast::CallExpression>(std::move(expr),
                                                          std::move(args));
    } else if ((at().type == havel::TokenType::String ||
               at().type == havel::TokenType::Number ||
               at().type == havel::TokenType::Identifier ||
               at().type == havel::TokenType::InterpolatedString) &&
               allowBraceCallSugar) {
      // Implicit call: expression followed by a literal (e.g., variable
      // "Hello")
      // Only allowed when brace call sugar is enabled
      auto arg = parsePrimaryExpression();
      std::vector<std::unique_ptr<havel::ast::Expression>> args;
      args.push_back(std::move(arg));
      expr = std::make_unique<havel::ast::CallExpression>(std::move(expr),
                                                          std::move(args));
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

std::unique_ptr<havel::ast::Statement> Parser::parseModesBlock() {
  advance(); // consume 'modes'
  return std::make_unique<havel::ast::ModesBlock>(parseKeyValueBlock());
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

    // Parse key - can be identifier or string
    std::string key;
    if (at().type == havel::TokenType::Identifier) {
      key = advance().value;
    } else if (at().type == havel::TokenType::String) {
      key = advance().value;
    } else {
      failAt(at(), "Expected identifier or string as key");
    }

    // Expect '=' or ':' (support both for compatibility)
    if (at().type != havel::TokenType::Assign && at().type != havel::TokenType::Colon) {
      failAt(at(), "Expected '=' or ':' after key");
    }
    advance(); // consume '=' or ':'

    // Check if value is a nested block
    if (at().type == havel::TokenType::OpenBrace) {
      // Nested block - parse recursively and wrap in an ObjectLiteral
      auto nestedPairs = parseKeyValueBlock();
      
      // Create an object literal to hold nested pairs
      auto nestedObj = std::make_unique<havel::ast::ObjectLiteral>(std::move(nestedPairs));
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