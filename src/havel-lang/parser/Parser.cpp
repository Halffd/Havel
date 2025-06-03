// src/havel-lang/parser/Parser.cpp
#include "Parser.h"
#include <iostream>
#include <stdexcept>

namespace havel::parser {
    havel::Token Parser::at(size_t offset) const {
        size_t pos = position + offset;
        if (pos >= tokens.size()) {
            return havel::Token("EOF", havel::TokenType::EOF_TOKEN, "EOF", 0,
                                0);
        }
        return tokens[pos];
    }

    havel::Token Parser::advance() {
        if (position >= tokens.size()) {
            return havel::Token("EOF", havel::TokenType::EOF_TOKEN, "EOF", 0,
                                0);
        }
        return tokens[position++];
    }

    bool Parser::notEOF() const {
        return at().type != havel::TokenType::EOF_TOKEN;
    }

    std::unique_ptr<havel::ast::Program> Parser::produceAST(
        const std::string &sourceCode) {
        // Tokenize source code
        havel::Lexer lexer(sourceCode);
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
        return std::make_unique<havel::ast::ExpressionStatement>(
            std::move(expr));
    }

    std::unique_ptr<havel::ast::HotkeyBinding> Parser::parseHotkeyBinding() {
        // Create the hotkey binding AST node
        auto binding = std::make_unique<havel::ast::HotkeyBinding>();

        // Parse the hotkey token (F1, Ctrl+V, etc.)
        if (at().type != havel::TokenType::Hotkey) {
            throw std::runtime_error(
                "Expected hotkey token at start of hotkey binding");
        }
        auto hotkeyToken = advance();
        binding->hotkey = std::make_unique<havel::ast::HotkeyLiteral>(
            hotkeyToken.value);

        // Expect and consume the arrow operator '=>'
        if (at().type != havel::TokenType::Arrow) {
            throw std::runtime_error(
                "Expected '=>' after hotkey '" + hotkeyToken.value + "'");
        }
        advance(); // consume the '=>'

        // Parse the action - could be an expression or a block statement
        if (at().type == havel::TokenType::OpenBrace) {
            // It's a block statement - parse it directly as a statement
            binding->action = parseBlockStatement();
        } else {
            // It's an expression - wrap it in an ExpressionStatement
            auto expr = parseExpression();
            if (!expr) {
                throw std::runtime_error(
                    "Failed to parse action expression after '=>'");
            }

            auto exprStmt = std::make_unique<havel::ast::ExpressionStatement>();
            exprStmt->expression = std::move(expr);
            binding->action = std::move(exprStmt);
        }

        // Validate that we successfully created the binding
        if (!binding->hotkey || !binding->action) {
            throw std::runtime_error(
                "Failed to create complete hotkey binding");
        }

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
                auto identTk = advance();
                auto identifier = std::make_unique<havel::ast::Identifier>(
                    identTk.value);

                if (at().type == havel::TokenType::Dot) {
                    auto member = std::make_unique<
                        havel::ast::MemberExpression>();
                    member->object = std::move(identifier);
                    advance(); // consume '.'

                    if (at().type != havel::TokenType::Identifier) {
                        throw std::runtime_error(
                            "Expected property name or method call after '.'");
                    }

                    auto property = advance();
                    member->property = std::make_unique<havel::ast::Identifier>(
                        property.value);

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
                throw std::runtime_error(
                    "Unexpected token in expression: " + tk.value);
        }
    }

    void Parser::printAST(const havel::ast::ASTNode &node, int indent) const {
        std::string padding(indent * 2, ' ');
        std::cout << padding << node.toString() << std::endl;

        // Print children based on node type
        if (node.kind == havel::ast::NodeType::Program) {
            const auto &program = static_cast<const havel::ast::Program &>(
                node);
            for (const auto &stmt: program.body) {
                printAST(*stmt, indent + 1);
            }
        } else if (node.kind == havel::ast::NodeType::BlockStatement) {
            const auto &block = static_cast<const havel::ast::BlockStatement &>(
                node);
            for (const auto &stmt: block.body) {
                printAST(*stmt, indent + 1);
            }
        } else if (node.kind == havel::ast::NodeType::HotkeyBinding) {
            const auto &binding = static_cast<const havel::ast::HotkeyBinding &>
                    (node);
            printAST(*binding.hotkey, indent + 1);
            printAST(*binding.action, indent + 1);
        } else if (node.kind == havel::ast::NodeType::PipelineExpression) {
            const auto &pipeline = static_cast<const
                havel::ast::PipelineExpression &>(node);
            for (const auto &stage: pipeline.stages) {
                printAST(*stage, indent + 1);
            }
        } else if (node.kind == havel::ast::NodeType::BinaryExpression) {
            const auto &binary = static_cast<const havel::ast::BinaryExpression
                &>(node);
            printAST(*binary.left, indent + 1);
            printAST(*binary.right, indent + 1);
        } else if (node.kind == havel::ast::NodeType::MemberExpression) {
            const auto &member = static_cast<const havel::ast::MemberExpression
                &>(node);
            printAST(*member.object, indent + 1);
            printAST(*member.property, indent + 1);
        } else if (node.kind == havel::ast::NodeType::CallExpression) {
            const auto &call = static_cast<const havel::ast::CallExpression &>(
                node);
            printAST(*call.callee, indent + 1);
            for (const auto &arg: call.args) {
                printAST(*arg, indent + 1);
            }
        }
    }
} // namespace havel::parser
