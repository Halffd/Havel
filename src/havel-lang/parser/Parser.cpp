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
            // Skip empty lines or statement separators
            if (at().type == havel::TokenType::NewLine || at().type == havel::TokenType::Semicolon) {
                advance();
                continue;
            }
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

std::unique_ptr<havel::ast::Statement> Parser::parseStatement() {
        switch (at().type) {
            case havel::TokenType::Hotkey:
                return parseHotkeyBinding();
            case havel::TokenType::Identifier: {
                // Support bare-letter hotkeys: e.g., a => { ... }
                if (at(1).type == havel::TokenType::Arrow) {
                    auto binding = std::make_unique<havel::ast::HotkeyBinding>();
                    auto hotkeyToken = advance(); // identifier as hotkey
                    advance(); // consume '=>'
                    binding->hotkey = std::make_unique<havel::ast::HotkeyLiteral>(hotkeyToken.value);
                    if (at().type == havel::TokenType::OpenBrace) {
                        binding->action = parseBlockStatement();
                    } else {
                        auto expr = parseExpression();
                        auto exprStmt = std::make_unique<havel::ast::ExpressionStatement>();
                        exprStmt->expression = std::move(expr);
                        binding->action = std::move(exprStmt);
                    }
                    return binding;
                }
                // Fallthrough to expression if not a hotkey binding
                [[fallthrough]];
            }
            case havel::TokenType::Let:
                return parseLetDeclaration();
            case havel::TokenType::If:
                return parseIfStatement();
            case havel::TokenType::While:
                return parseWhileStatement();
            case havel::TokenType::For:
                return parseForStatement();
            case havel::TokenType::Loop:
                return parseLoopStatement();
            case havel::TokenType::Break:
                return parseBreakStatement();
            case havel::TokenType::Continue:
                return parseContinueStatement();
            case havel::TokenType::On:
                return parseOnModeStatement();
            case havel::TokenType::Off:
                return parseOffModeStatement();
            case havel::TokenType::Fn:
                return parseFunctionDeclaration();
            case havel::TokenType::Return:
                return parseReturnStatement();
            case havel::TokenType::OpenBrace:
                return parseBlockStatement();
            case havel::TokenType::Import:
                return parseImportStatement();
            case havel::TokenType::Config:
                return parseConfigBlock();
            case havel::TokenType::Devices:
                return parseDevicesBlock();
            case havel::TokenType::Modes:
                return parseModesBlock();
            default: {
                auto expr = parseExpression();
                return std::make_unique<havel::ast::ExpressionStatement>(std::move(expr));
            }
        }
    }
    
    std::unique_ptr<havel::ast::Statement> Parser::parseFunctionDeclaration() {
        advance(); // consume "fn"

        if (at().type != havel::TokenType::Identifier) {
            if (havel::Lexer::KEYWORDS.count(at().value)) {
                throw std::runtime_error("Cannot use reserved keyword '" + at().value + "' as function name");
            }
            throw std::runtime_error("Expected function name after 'fn'");
        }
        auto name = std::make_unique<havel::ast::Identifier>(advance().value);

        if (at().type != havel::TokenType::OpenParen) {
            throw std::runtime_error("Expected '(' after function name");
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
                throw std::runtime_error("Expected identifier in parameter list");
            }
            params.push_back(std::make_unique<havel::ast::Identifier>(advance().value));

            while (at().type == havel::TokenType::NewLine) {
                advance();
            }

            if (at().type == havel::TokenType::Comma) {
                advance();
            } else if (at().type != havel::TokenType::CloseParen) {
                throw std::runtime_error("Expected ',' or ')' in parameter list");
            }
        }

        if (at().type != havel::TokenType::CloseParen) {
            throw std::runtime_error("Expected ')' after parameter list");
        }
        advance(); // consume ')'

        auto body = parseBlockStatement();

        return std::make_unique<havel::ast::FunctionDeclaration>(std::move(name), std::move(params), std::move(body));
    }

    std::unique_ptr<havel::ast::Statement> Parser::parseReturnStatement() {
        advance(); // consume "return"
        std::unique_ptr<havel::ast::Expression> value = nullptr;
        
        // Return value is optional
        if (at().type != havel::TokenType::Semicolon && at().type != havel::TokenType::CloseBrace && at().type != havel::TokenType::EOF_TOKEN) {
            value = parseExpression();
        }

        // Optional semicolon
        if (at().type == havel::TokenType::Semicolon) {
            advance();
        }

        return std::make_unique<havel::ast::ReturnStatement>(std::move(value));
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

    std::unique_ptr<havel::ast::Statement> Parser::parseWhileStatement() {
        advance(); // consume "while"

        auto condition = parseExpression();

        if (at().type != havel::TokenType::OpenBrace) {
            throw std::runtime_error("Expected '{' after while condition");
        }

        auto body = parseBlockStatement();

        return std::make_unique<havel::ast::WhileStatement>(std::move(condition), std::move(body));
    }

    std::unique_ptr<havel::ast::Statement> Parser::parseForStatement() {
        advance(); // consume "for"

        if (at().type != havel::TokenType::Identifier) {
            throw std::runtime_error("Expected iterator variable after 'for'");
        }
        auto iterator = std::make_unique<havel::ast::Identifier>(advance().value);

        if (at().type != havel::TokenType::In) {
            throw std::runtime_error("Expected 'in' after iterator variable");
        }
        advance(); // consume "in"

        auto iterable = parseExpression();

        // Skip newlines before opening brace
        while (at().type == havel::TokenType::NewLine) {
            advance();
        }

        if (at().type != havel::TokenType::OpenBrace) {
            throw std::runtime_error("Expected '{' after for iterable");
        }

        auto body = parseBlockStatement();

        return std::make_unique<havel::ast::ForStatement>(std::move(iterator), std::move(iterable), std::move(body));
    }

    std::unique_ptr<havel::ast::Statement> Parser::parseLoopStatement() {
        advance(); // consume "loop"

        // Skip newlines before opening brace
        while (at().type == havel::TokenType::NewLine) {
            advance();
        }

        if (at().type != havel::TokenType::OpenBrace) {
            throw std::runtime_error("Expected '{' after 'loop'");
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
    
    std::unique_ptr<havel::ast::Statement> Parser::parseOnModeStatement() {
        advance(); // consume "on"
        
        // Expect "mode" keyword
        if (at().type != havel::TokenType::Mode) {
            throw std::runtime_error("Expected 'mode' after 'on'");
        }
        advance(); // consume "mode"
        
        // Get mode name
        if (at().type != havel::TokenType::Identifier) {
            throw std::runtime_error("Expected mode name after 'on mode'");
        }
        std::string modeName = advance().value;
        
        // Skip newlines
        while (at().type == havel::TokenType::NewLine) {
            advance();
        }
        
        // Parse body block
        if (at().type != havel::TokenType::OpenBrace) {
            throw std::runtime_error("Expected '{' after mode name");
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
                throw std::runtime_error("Expected '{' after else");
            }
        }
        
        return std::make_unique<havel::ast::OnModeStatement>(modeName, std::move(body), std::move(alternative));
    }
    
    std::unique_ptr<havel::ast::Statement> Parser::parseOffModeStatement() {
        advance(); // consume "off"
        
        // Expect "mode" keyword
        if (at().type != havel::TokenType::Mode) {
            throw std::runtime_error("Expected 'mode' after 'off'");
        }
        advance(); // consume "mode"
        
        // Get mode name
        if (at().type != havel::TokenType::Identifier) {
            throw std::runtime_error("Expected mode name after 'off mode'");
        }
        std::string modeName = advance().value;
        
        // Skip newlines
        while (at().type == havel::TokenType::NewLine) {
            advance();
        }
        
        // Parse body block
        if (at().type != havel::TokenType::OpenBrace) {
            throw std::runtime_error("Expected '{' after mode name");
        }
        auto body = parseBlockStatement();
        
        return std::make_unique<havel::ast::OffModeStatement>(modeName, std::move(body));
    }

    std::unique_ptr<havel::ast::Statement> Parser::parseLetDeclaration() {
        advance(); // consume "let"

        if (at().type != havel::TokenType::Identifier) {
            throw std::runtime_error("Expected identifier after 'let'");
        }
        auto name = std::make_unique<havel::ast::Identifier>(advance().value);

        if (at().type != havel::TokenType::Assign) {
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
            // Skip newlines and semicolons (empty statements)
            if (at().type == havel::TokenType::NewLine || at().type == havel::TokenType::Semicolon) {
                advance();
                continue;
            }
            
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
            while(notEOF() && at().type != havel::TokenType::CloseBrace) {
                if (at().type != havel::TokenType::Identifier) {
                    throw std::runtime_error("Expected identifier in import list");
                }
                std::string originalName = advance().value;
                std::string alias = originalName;

                if (at().type == havel::TokenType::As) {
                    advance(); // consume 'as'
                    if (at().type != havel::TokenType::Identifier) {
                        throw std::runtime_error("Expected alias name after 'as'");
                    }
                    alias = advance().value;
                }
                items.push_back({originalName, alias});

                if (at().type == havel::TokenType::Comma) {
                    advance();
                } else if (at().type != havel::TokenType::CloseBrace) {
                    throw std::runtime_error("Expected ',' or '}' in import list");
                }
            }
            if(at().type != havel::TokenType::CloseBrace) throw std::runtime_error("Expected '}'");
            advance(); // consume '}'
        }

        // Optional 'from' clause
        if (at().type == havel::TokenType::From) {
            advance(); // consume 'from'
            if (at().type != havel::TokenType::String && at().type != havel::TokenType::Identifier) {
                throw std::runtime_error("Expected module path after 'from'");
            }
            std::string path = advance().value;
            return std::make_unique<havel::ast::ImportStatement>(path, items);
        }
        // No 'from': treat as importing built-in modules by name
        return std::make_unique<havel::ast::ImportStatement>(std::string(""), items);
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
            
            return std::make_unique<havel::ast::AssignmentExpression>(
                std::move(left),
                std::move(value),
                opTok.value
            );
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
                throw std::runtime_error("Expected ':' in ternary expression");
            }
            advance(); // consume ':'
            
            auto falseValue = parseTernaryExpression(); // Right-associative
            
            return std::make_unique<havel::ast::TernaryExpression>(
                std::move(condition), std::move(trueValue), std::move(falseValue)
            );
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
        auto left = parseRange();
        
        while (at().type == havel::TokenType::Less || 
               at().type == havel::TokenType::Greater ||
               at().type == havel::TokenType::LessEquals ||
               at().type == havel::TokenType::GreaterEquals) {
            auto op = tokenToBinaryOperator(at().type);
            advance();
            auto right = parseRange();
            left = std::make_unique<havel::ast::BinaryExpression>(std::move(left), op, std::move(right));
        }
        
        return left;
    }

    std::unique_ptr<havel::ast::Expression> Parser::parseRange() {
        auto left = parseAdditive();
        
        if (at().type == havel::TokenType::DotDot) {
            advance(); // consume '..'
            auto right = parseAdditive();
            return std::make_unique<havel::ast::RangeExpression>(std::move(left), std::move(right));
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
    havel::TokenType Parser::getBinaryOperatorToken(ast::BinaryOperator op) {
        switch (op) {
            case ast::BinaryOperator::Add: return TokenType::Plus;
            case ast::BinaryOperator::Sub: return TokenType::Minus;
            case ast::BinaryOperator::Mul: return TokenType::Multiply;
            case ast::BinaryOperator::Div: return TokenType::Divide;
            case ast::BinaryOperator::Equal: return TokenType::Equals;
            case ast::BinaryOperator::NotEqual: return TokenType::NotEquals;
            case ast::BinaryOperator::Less: return TokenType::Less;
            case ast::BinaryOperator::Greater: return TokenType::Greater;
            case ast::BinaryOperator::And: return TokenType::And;
            case ast::BinaryOperator::Or: return TokenType::Or;
            default:
                throw std::runtime_error("Unknown binary operator");
        }
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
                            segments.push_back(havel::ast::InterpolatedStringExpression::Segment(str.substr(pos)));
                        }
                        break;
                    }
                    
                    // Add text before ${ as string segment
                    if (start > pos) {
                        segments.push_back(havel::ast::InterpolatedStringExpression::Segment(str.substr(pos, start - pos)));
                    }
                    
                    // Find matching }
                    size_t end = str.find('}', start + 2);
                    if (end == std::string::npos) {
                        throw std::runtime_error("Unclosed interpolation in string");
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
                    
                    segments.push_back(havel::ast::InterpolatedStringExpression::Segment(std::move(expr)));
                    
                    pos = end + 1;
                }
                
                return std::make_unique<havel::ast::InterpolatedStringExpression>(std::move(segments));
            }
    
            case havel::TokenType::Identifier: {
                auto identTk = at();
                // Arrow lambda with single parameter: x => expr
                if (at(1).type == havel::TokenType::Arrow) {
                    // single identifier parameter
                    advance(); // consume identifier
                    advance(); // consume '=>'
                    std::vector<std::unique_ptr<havel::ast::Identifier>> params;
                    params.push_back(std::make_unique<havel::ast::Identifier>(identTk.value));
                    return parseLambdaFromParams(std::move(params));
                }
                // Otherwise it's a normal identifier expression
                identTk = advance();
                std::unique_ptr<havel::ast::Expression> expr = std::make_unique<havel::ast::Identifier>(identTk.value);
    
                // Handle postfix operations in a loop to support chaining: arr[0].prop() etc.
                while (true) {
                    if (at().type == havel::TokenType::OpenParen) {
                        expr = parseCallExpression(std::move(expr));
                        // Trailing block as last argument: func(...){ ... }
                        if (at().type == havel::TokenType::OpenBrace) {
                            // Decide if this is object-literal or block lambda by lookahead
                            size_t savePos = position;
                            auto next = at(1);
                            bool isObject = (next.type == havel::TokenType::Identifier || next.type == havel::TokenType::String) && at(2).type == havel::TokenType::Colon;
                            position = savePos; // restore
                            if (!isObject) {
                                auto block = parseBlockStatement();
                                std::vector<std::unique_ptr<havel::ast::Identifier>> noParams;
                                auto lambda = std::make_unique<havel::ast::LambdaExpression>(std::move(noParams), std::move(block));
                                // Append lambda to existing call args
                                if (auto* callPtr = dynamic_cast<havel::ast::CallExpression*>(expr.get())) {
                                    callPtr->args.push_back(std::move(lambda));
                                }
                            }
                        }
                    } else if (at().type == havel::TokenType::Dot) {
                        expr = parseMemberExpression(std::move(expr));
                    } else if (at().type == havel::TokenType::OpenBracket) {
                        expr = parseIndexExpression(std::move(expr));
                    } else if (at().type == havel::TokenType::OpenBrace) {
                        // Sugar: ident { ... } -> ident({ ... }) or ident(() => { ... })
                        // Lookahead to determine object vs block
                        size_t savePos = position;
                        auto next = at(1);
                        bool isObject = (next.type == havel::TokenType::Identifier || next.type == havel::TokenType::String) && at(2).type == havel::TokenType::Colon;
                        position = savePos; // restore
                        std::vector<std::unique_ptr<havel::ast::Expression>> args;
                        if (isObject) {
                            auto obj = parseObjectLiteral();
                            args.push_back(std::move(obj));
                        } else {
                            auto block = parseBlockStatement();
                            std::vector<std::unique_ptr<havel::ast::Identifier>> noParams;
                            auto lambda = std::make_unique<havel::ast::LambdaExpression>(std::move(noParams), std::move(block));
                            args.push_back(std::move(lambda));
                        }
                        return std::make_unique<havel::ast::CallExpression>(std::move(expr), std::move(args));
                    } else if (at().type == havel::TokenType::String || 
                               at().type == havel::TokenType::Number ||
                               at().type == havel::TokenType::Identifier ||
                               at().type == havel::TokenType::InterpolatedString) {
                        // Implicit call: identifier followed by a literal (e.g., send "Hello")
                        auto arg = parsePrimaryExpression();
                        std::vector<std::unique_ptr<havel::ast::Expression>> args;
                        args.push_back(std::move(arg));
                        return std::make_unique<havel::ast::CallExpression>(std::move(expr), std::move(args));
                    } else {
                        break;
                    }
                }
    
                return expr;
            }
    
            case havel::TokenType::Hotkey: {
                advance();
                return std::make_unique<havel::ast::HotkeyLiteral>(tk.value);
            }
    
            case havel::TokenType::OpenParen: {
                advance(); // consume '('
                
                // Detect empty or parameter list for arrow lambda: (a, b) => ... or () => ...
                std::vector<std::unique_ptr<havel::ast::Identifier>> params;
                bool isParamList = false;
                if (at().type == havel::TokenType::CloseParen) {
                    // ()
                    isParamList = true;
                } else if (at().type == havel::TokenType::Identifier) {
                    isParamList = true;
                    while (notEOF() && at().type == havel::TokenType::Identifier) {
                        params.push_back(std::make_unique<havel::ast::Identifier>(advance().value));
                        if (at().type == havel::TokenType::Comma) {
                            advance();
                        } else {
                            break;
                        }
                    }
                }
                if (at().type != havel::TokenType::CloseParen) {
                    throw std::runtime_error("Expected ')' after parameter list");
                }
                advance(); // consume ')'
                if (isParamList && at().type == havel::TokenType::Arrow) {
                    advance(); // consume '=>'
                    return parseLambdaFromParams(std::move(params));
                }
                
                // Not a lambda, treat as grouped expression
                auto expr = parseExpression();
                if (at().type != havel::TokenType::CloseParen) {
                    throw std::runtime_error("Expected ')'");
                }
                advance(); // consume ')'
                return expr;
            }
            
            case havel::TokenType::OpenBracket: {
                auto array = parseArrayLiteral();
                // Check for chained operations on array literals
                while (at().type == havel::TokenType::OpenBracket || 
                       at().type == havel::TokenType::Dot ||
                       at().type == havel::TokenType::OpenParen) {
                    if (at().type == havel::TokenType::OpenBracket) {
                        array = parseIndexExpression(std::move(array));
                    } else if (at().type == havel::TokenType::Dot) {
                        array = parseMemberExpression(std::move(array));
                    } else if (at().type == havel::TokenType::OpenParen) {
                        array = parseCallExpression(std::move(array));
                    }
                }
                return array;
            }
            
            case havel::TokenType::OpenBrace: {
                return parseObjectLiteral();
            }
            
            default:
                throw std::runtime_error(
                    "Unexpected token in expression: " + tk.value);
        }
    }
// Add these method declarations to Parser.h first, then implement in Parser.cpp

std::unique_ptr<havel::ast::Expression> Parser::parseCallExpression(
    std::unique_ptr<havel::ast::Expression> callee) {
    
    auto call = std::make_unique<havel::ast::CallExpression>(std::move(callee));
    
    advance(); // consume '('
    
    // Parse arguments
    while (notEOF() && at().type != havel::TokenType::CloseParen) {
        auto arg = parseExpression();
        call->args.push_back(std::move(arg));
        
        if (at().type == havel::TokenType::Comma) {
            advance(); // consume ','
        } else if (at().type != havel::TokenType::CloseParen) {
            throw std::runtime_error("Expected ',' or ')' in function call");
        }
    }
    
    if (at().type != havel::TokenType::CloseParen) {
        throw std::runtime_error("Expected ')' after function arguments");
    }
    advance(); // consume ')'
    
    return std::move(call);
}
std::unique_ptr<havel::ast::Expression> Parser::parseMemberExpression(
    std::unique_ptr<havel::ast::Expression> object) {
    
    auto member = std::make_unique<havel::ast::MemberExpression>();
    member->object = std::move(object);
    
    advance(); // consume '.'
    
    if (at().type != havel::TokenType::Identifier) {
        throw std::runtime_error("Expected property name after '.'");
    }
    
    auto property = advance();
    member->property = std::make_unique<havel::ast::Identifier>(property.value);
    
    return std::move(member);
}

std::unique_ptr<havel::ast::Expression> Parser::parseIndexExpression(
    std::unique_ptr<havel::ast::Expression> object) {
    
    advance(); // consume '['
    
    auto index = parseExpression();
    
    if (at().type != havel::TokenType::CloseBracket) {
        throw std::runtime_error("Expected ']' after array index");
    }
    advance(); // consume ']'
    
    return std::make_unique<havel::ast::IndexExpression>(std::move(object), std::move(index));
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
            throw std::runtime_error("Expected ',' or ']' in array literal");
        }
    }
    
    if (at().type != havel::TokenType::CloseBracket) {
        throw std::runtime_error("Expected ']' to close array literal");
    }
    advance(); // consume ']'
    
    return std::make_unique<havel::ast::ArrayLiteral>(std::move(elements));
}

std::unique_ptr<havel::ast::Expression> Parser::parseObjectLiteral() {
    std::vector<std::pair<std::string, std::unique_ptr<havel::ast::Expression>>> pairs;
    
    advance(); // consume '{'
    
    // Parse object key-value pairs
    while (notEOF() && at().type != havel::TokenType::CloseBrace) {
        // Skip newlines/semicolons between pairs
        while (at().type == havel::TokenType::NewLine || at().type == havel::TokenType::Semicolon) {
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
            throw std::runtime_error("Expected identifier or string as object key");
        }
        
        // Expect colon
        if (at().type != havel::TokenType::Colon) {
            throw std::runtime_error("Expected ':' after object key");
        }
        advance(); // consume ':'
        
        // Parse value
        auto value = parseExpression();
        pairs.push_back({key, std::move(value)});
        
        // Allow comma, newline, or semicolon as separators
        if (at().type == havel::TokenType::Comma || at().type == havel::TokenType::NewLine || at().type == havel::TokenType::Semicolon) {
            advance();
        } else if (at().type != havel::TokenType::CloseBrace) {
            throw std::runtime_error("Expected ',', newline, or '}' in object literal");
        }
    }
    
    if (at().type != havel::TokenType::CloseBrace) {
        throw std::runtime_error("Expected '}' to close object literal");
    }
    advance(); // consume '}'
    
    return std::make_unique<havel::ast::ObjectLiteral>(std::move(pairs));
}

std::unique_ptr<havel::ast::Expression> Parser::parseLambdaFromParams(std::vector<std::unique_ptr<havel::ast::Identifier>> params) {
    // Body can be block or expression
    if (at().type == havel::TokenType::OpenBrace) {
        auto block = parseBlockStatement();
        return std::make_unique<havel::ast::LambdaExpression>(std::move(params), std::move(block));
    }
    // Expression body: wrap in Return inside a block
    auto expr = parseExpression();
    auto exprStmt = std::make_unique<havel::ast::ExpressionStatement>(std::move(expr));
    auto block = std::make_unique<havel::ast::BlockStatement>();
    block->body.push_back(std::move(exprStmt));
    return std::make_unique<havel::ast::LambdaExpression>(std::move(params), std::move(block));
}

std::unique_ptr<havel::ast::Statement> Parser::parseConfigBlock() {
    advance(); // consume 'config'
    
    // Skip optional newlines
    while (at().type == havel::TokenType::NewLine) {
        advance();
    }
    
    if (at().type != havel::TokenType::OpenBrace) {
        throw std::runtime_error("Expected '{' after 'config'");
    }
    
    std::vector<std::pair<std::string, std::unique_ptr<havel::ast::Expression>>> pairs;
    
    advance(); // consume '{'
    
    // Parse config key-value pairs
    while (notEOF() && at().type != havel::TokenType::CloseBrace) {
        // Skip newlines
        if (at().type == havel::TokenType::NewLine || at().type == havel::TokenType::Semicolon) {
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
            throw std::runtime_error("Expected identifier or string as config key");
        }
        
        // Expect colon
        if (at().type != havel::TokenType::Colon) {
            throw std::runtime_error("Expected ':' after config key");
        }
        advance(); // consume ':'
        
        // Parse value
        auto value = parseExpression();
        pairs.push_back({key, std::move(value)});
        
        // Handle comma or newline as separator
        if (at().type == havel::TokenType::Comma || at().type == havel::TokenType::NewLine || at().type == havel::TokenType::Semicolon) {
            advance();
        } else if (at().type != havel::TokenType::CloseBrace) {
            throw std::runtime_error("Expected ',', newline, or '}' in config block");
        }
    }
    
    if (at().type != havel::TokenType::CloseBrace) {
        throw std::runtime_error("Expected '}' to close config block");
    }
    advance(); // consume '}'
    
    return std::make_unique<havel::ast::ConfigBlock>(std::move(pairs));
}

std::unique_ptr<havel::ast::Statement> Parser::parseDevicesBlock() {
    advance(); // consume 'devices'
    
    // Skip optional newlines
    while (at().type == havel::TokenType::NewLine) {
        advance();
    }
    
    if (at().type != havel::TokenType::OpenBrace) {
        throw std::runtime_error("Expected '{' after 'devices'");
    }
    
    std::vector<std::pair<std::string, std::unique_ptr<havel::ast::Expression>>> pairs;
    
    advance(); // consume '{'
    
    // Parse devices key-value pairs
    while (notEOF() && at().type != havel::TokenType::CloseBrace) {
        // Skip newlines
        if (at().type == havel::TokenType::NewLine || at().type == havel::TokenType::Semicolon) {
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
            throw std::runtime_error("Expected identifier or string as devices key");
        }
        
        // Expect colon
        if (at().type != havel::TokenType::Colon) {
            throw std::runtime_error("Expected ':' after devices key");
        }
        advance(); // consume ':'
        
        // Parse value
        auto value = parseExpression();
        pairs.push_back({key, std::move(value)});
        
        // Handle comma or newline as separator
        if (at().type == havel::TokenType::Comma || at().type == havel::TokenType::NewLine || at().type == havel::TokenType::Semicolon) {
            advance();
        } else if (at().type != havel::TokenType::CloseBrace) {
            throw std::runtime_error("Expected ',', newline, or '}' in devices block");
        }
    }
    
    if (at().type != havel::TokenType::CloseBrace) {
        throw std::runtime_error("Expected '}' to close devices block");
    }
    advance(); // consume '}'
    
    return std::make_unique<havel::ast::DevicesBlock>(std::move(pairs));
}

std::unique_ptr<havel::ast::Statement> Parser::parseModesBlock() {
    advance(); // consume 'modes'
    
    // Skip optional newlines
    while (at().type == havel::TokenType::NewLine) {
        advance();
    }
    
    if (at().type != havel::TokenType::OpenBrace) {
        throw std::runtime_error("Expected '{' after 'modes'");
    }
    
    std::vector<std::pair<std::string, std::unique_ptr<havel::ast::Expression>>> pairs;
    
    advance(); // consume '{'
    
    // Parse modes key-value pairs
    while (notEOF() && at().type != havel::TokenType::CloseBrace) {
        // Skip newlines
        if (at().type == havel::TokenType::NewLine || at().type == havel::TokenType::Semicolon) {
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
            throw std::runtime_error("Expected identifier or string as modes key");
        }
        
        // Expect colon
        if (at().type != havel::TokenType::Colon) {
            throw std::runtime_error("Expected ':' after modes key");
        }
        advance(); // consume ':'
        
        // Parse value
        auto value = parseExpression();
        pairs.push_back({key, std::move(value)});
        
        // Handle comma or newline as separator
        if (at().type == havel::TokenType::Comma || at().type == havel::TokenType::NewLine || at().type == havel::TokenType::Semicolon) {
            advance();
        } else if (at().type != havel::TokenType::CloseBrace) {
            throw std::runtime_error("Expected ',', newline, or '}' in modes block");
        }
    }
    
    if (at().type != havel::TokenType::CloseBrace) {
        throw std::runtime_error("Expected '}' to close modes block");
    }
    advance(); // consume '}'
    
    return std::make_unique<havel::ast::ModesBlock>(std::move(pairs));
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