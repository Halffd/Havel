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
            return parseLetDeclaration();
        }

        // Check for if statements
        if (at().type == havel::TokenType::If) {
            return parseIfStatement();
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

    std::unique_ptr<havel::ast::Statement> Parser::parseIfStatement() {
        advance(); // consume "if"

        auto condition = parseExpression();

        if (at().type != havel::TokenType::OpenBrace) {
            throw std::runtime_error("Expected '{' after if condition");
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

        return std::make_unique<havel::ast::IfStatement>(std::move(condition), std::move(consequence), std::move(alternative));
    }

    std::unique_ptr<havel::ast::Statement> Parser::parseLetDeclaration() {
        advance(); // consume "let"

        if (at().type != havel::TokenType::Identifier) {
            throw std::runtime_error("Expected identifier after 'let'");
        }
        auto name = std::make_unique<havel::ast::Identifier>(advance().value);

        if (at().type != havel::TokenType::Equals) {
            // Allow declarations without assignment, e.g., `let x;`
            return std::make_unique<havel::ast::LetDeclaration>(std::move(name));
        }
        advance(); // consume "="

        auto value = parseExpression();

        return std::make_unique<havel::ast::LetDeclaration>(std::move(name), std::move(value));
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
        return parseLogicalOr();
    }
    
    // Add these new methods for operator precedence
    std::unique_ptr<havel::ast::Expression> Parser::parseLogicalOr() {
        auto left = parseLogicalAnd();
        
        while (at().type == havel::TokenType::Or) {
            auto op = tokenToBinaryOperator(at().type);
            advance();
            auto right = parseLogicalAnd();
            left = std::make_unique<havel::ast::BinaryExpression>(std::move(left), op, std::move(right));
        }
        
        return left;
    }
    
    std::unique_ptr<havel::ast::Expression> Parser::parseLogicalAnd() {
        auto left = parseEquality();
        
        while (at().type == havel::TokenType::And) {
            auto op = tokenToBinaryOperator(at().type);
            advance();
            auto right = parseEquality();
            left = std::make_unique<havel::ast::BinaryExpression>(std::move(left), op, std::move(right));
        }
        
        return left;
    }
    
    std::unique_ptr<havel::ast::Expression> Parser::parseEquality() {
        auto left = parseComparison();
        
        while (at().type == havel::TokenType::Equals || 
               at().type == havel::TokenType::NotEquals) {
            auto op = tokenToBinaryOperator(at().type);
            advance();
            auto right = parseComparison();
            left = std::make_unique<havel::ast::BinaryExpression>(std::move(left), op, std::move(right));
        }
        
        return left;
    }
    
    std::unique_ptr<havel::ast::Expression> Parser::parseComparison() {
        auto left = parseAdditive();
        
        while (at().type == havel::TokenType::Less || 
               at().type == havel::TokenType::Greater ||
               at().type == havel::TokenType::LessEquals ||
               at().type == havel::TokenType::GreaterEquals) {
            auto op = tokenToBinaryOperator(at().type);
            advance();
            auto right = parseAdditive();
            left = std::make_unique<havel::ast::BinaryExpression>(std::move(left), op, std::move(right));
        }
        
        return left;
    }
    
    std::unique_ptr<havel::ast::Expression> Parser::parseAdditive() {
        auto left = parseMultiplicative();
        
        while (at().type == havel::TokenType::Plus || 
               at().type == havel::TokenType::Minus) {
            auto op = tokenToBinaryOperator(at().type);
            advance();
            auto right = parseMultiplicative();
            left = std::make_unique<havel::ast::BinaryExpression>(std::move(left), op, std::move(right));
        }
        
        return left;
    }
    
    std::unique_ptr<havel::ast::Expression> Parser::parseMultiplicative() {
        auto left = parseUnary();
        
        while (at().type == havel::TokenType::Multiply || 
               at().type == havel::TokenType::Divide ||
               at().type == havel::TokenType::Modulo) {
            auto op = tokenToBinaryOperator(at().type);
            advance();
            auto right = parseUnary();
            left = std::make_unique<havel::ast::BinaryExpression>(std::move(left), op, std::move(right));
        }
        
        return left;
    }
    std::unique_ptr<havel::ast::Expression> Parser::parseUnary() {
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
                    throw std::runtime_error("Invalid unary operator");
            }
            
            advance(); // Consume the unary operator
            auto operand = parseUnary(); // Right associative - handle nested unary operators
            
            return std::make_unique<havel::ast::UnaryExpression>(unaryOp, std::move(operand));
        }
        
        return parsePrimaryExpression();
    }
    
    // Add the helper method implementation
    havel::ast::BinaryOperator Parser::tokenToBinaryOperator(TokenType tokenType) {
        switch (tokenType) {
            case TokenType::Plus: return havel::ast::BinaryOperator::Add;
            case TokenType::Minus: return havel::ast::BinaryOperator::Sub;
            case TokenType::Multiply: return havel::ast::BinaryOperator::Mul;
            case TokenType::Divide: return havel::ast::BinaryOperator::Div;
            case TokenType::Modulo: return havel::ast::BinaryOperator::Mod;
            case TokenType::Equals: return havel::ast::BinaryOperator::Equal;
            case TokenType::NotEquals: return havel::ast::BinaryOperator::NotEqual;
            case TokenType::Less: return havel::ast::BinaryOperator::Less;
            case TokenType::Greater: return havel::ast::BinaryOperator::Greater;
            case TokenType::LessEquals: return havel::ast::BinaryOperator::LessEqual;
            case TokenType::GreaterEquals: return havel::ast::BinaryOperator::GreaterEqual;
            case TokenType::And: return havel::ast::BinaryOperator::And;
            case TokenType::Or: return havel::ast::BinaryOperator::Or;
            default:
                throw std::runtime_error("Invalid binary operator token: " + std::to_string(static_cast<int>(tokenType)));
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
