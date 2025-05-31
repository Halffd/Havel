// src/havel-lang/parser/havel_parser.cpp
#include "havel_parser.hpp"
#include <iostream>
#include <stdexcept>

namespace havel::parser {

havel::Token Parser::at(size_t offset) const {
    size_t pos = position + offset;
    if (pos >= tokens.size()) {
        return havel::Token("EOF", havel::TokenType::EOF_TOKEN);
    }
    return tokens[pos];
}

havel::Token Parser::advance() {
    if (position >= tokens.size()) {
        return havel::Token("EOF", havel::TokenType::EOF_TOKEN);
    }
    return tokens[position++];
}

bool Parser::notEOF() const {
    return at().type != havel::TokenType::EOF_TOKEN;
}

std::unique_ptr<havel::ast::Program> Parser::produceAST(const std::string& sourceCode) {
    // Tokenize source code (like Tyler's approach)
    havel::HavelLexer lexer(sourceCode);
    tokens = lexer.tokenize();
    position = 0;

    // Create program AST node
    auto program = std::make_unique<havel::ast::Program>();

    // Parse all statements until EOF
    while (notEOF()) {
        auto stmt = parseStatement();
        if (stmt) {
            program->body.push_back(std::move(stmt));
        }
    }

    return program;
}

std::unique_ptr<havel::ast::Statement> Parser::parseStatement() {
    // Check for hotkey bindings (F1 =>, Ctrl+V =>, etc.)
    if (at().type == havel::TokenType::Hotkey) {
        return parseHotkeyBinding();
    }

    // Check for let declarations
    if (at().type == havel::TokenType::Let) {
        // TODO: Implement let declarations
        throw std::runtime_error("Let declarations not implemented yet");
    }

    // Check for block statements
    if (at().type == havel::TokenType::OpenBrace) {
        return parseBlockStatement();
    }

    // Otherwise, parse as expression statement
    auto expr = parseExpression();
    return std::unique_ptr<havel::ast::Statement>(
        static_cast<havel::ast::Statement*>(expr.release())
    );
}

std::unique_ptr<havel::ast::HotkeyBinding> Parser::parseHotkeyBinding() {
    auto binding = std::make_unique<havel::ast::HotkeyBinding>();

    // Parse hotkey (F1, Ctrl+V, etc.)
    auto hotkeyToken = advance();
    binding->hotkey = std::make_unique<havel::ast::HotkeyLiteral>(hotkeyToken.value);

    // Expect arrow operator =>
    if (at().type != havel::TokenType::Arrow) {
        throw std::runtime_error("Expected '=>' after hotkey");
    }
    advance(); // consume '=>'

    // Parse action (expression or block)
    binding->action = parseExpression();

    return binding;
}

std::unique_ptr<havel::ast::BlockStatement> Parser::parseBlockStatement() {
    auto block = std::make_unique<havel::ast::BlockStatement>();

    // Consume opening brace
    if (at().type != havel::TokenType::OpenBrace) {
        throw std::runtime_error("Expected '{'");
    }
    advance();

    // Parse statements until closing brace
    while (notEOF() && at().type != havel::TokenType::CloseBrace) {
        auto stmt = parseStatement();
        if (stmt) {
            block->body.push_back(std::move(stmt));
        }
    }

    // Consume closing brace
    if (at().type != havel::TokenType::CloseBrace) {
        throw std::runtime_error("Expected '}'");
    }
    advance();

    return block;
}

std::unique_ptr<havel::ast::Expression> Parser::parseExpression() {
    return parsePipelineExpression();
}

std::unique_ptr<havel::ast::Expression> Parser::parsePipelineExpression() {
    auto left = parseBinaryExpression();

    // Check for pipeline operator |
    if (at().type == havel::TokenType::Pipe) {
        auto pipeline = std::make_unique<havel::ast::PipelineExpression>();
        pipeline->stages.push_back(std::move(left));

        while (at().type == havel::TokenType::Pipe) {
            advance(); // consume '|'
            auto stage = parseBinaryExpression();
            pipeline->stages.push_back(std::move(stage));
        }

        return std::move(pipeline);
    }

    return left;
}

std::unique_ptr<havel::ast::Expression> Parser::parseBinaryExpression() {
    // For now, just parse primary expressions
    // TODO: Implement proper operator precedence
    return parsePrimaryExpression();
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

        case havel::TokenType::Identifier: {
            advance();
            auto identifier = std::make_unique<havel::ast::Identifier>(tk.value);

            // Check for member access (clipboard.out)
            if (at().type == havel::TokenType::Dot) {
                auto member = std::make_unique<havel::ast::MemberExpression>();
                member->object = std::move(identifier);

                advance(); // consume '.'

                if (at().type != havel::TokenType::Identifier) {
                    throw std::runtime_error("Expected property name after '.'");
                }

                auto property = advance();
                member->property = std::make_unique<havel::ast::Identifier>(property.value);

                return std::move(member);
            }

            return std::move(identifier);
        }

        case havel::TokenType::Hotkey: {
            advance();
            return std::make_unique<havel::ast::HotkeyLiteral>(tk.value);
        }

        case havel::TokenType::OpenParen: {
            advance(); // consume '('
            auto expr = parseExpression();

            if (at().type != havel::TokenType::CloseParen) {
                throw std::runtime_error("Expected ')'");
            }
            advance(); // consume ')'

            return expr;
        }

        default:
            throw std::runtime_error("Unexpected token in expression: " +
