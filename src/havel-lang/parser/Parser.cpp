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

        return program;
    }

    std::unique_ptr<havel::ast::Statement> Parser::parseStatement() {
        switch (at().type) {
            case havel::TokenType::Hotkey:
                return parseHotkeyBinding();
            case havel::TokenType::Let:
                return parseLetDeclaration();
            case havel::TokenType::If:
                return parseIfStatement();
            case havel::TokenType::While:
                return parseWhileStatement();
            case havel::TokenType::Fn:
                return parseFunctionDeclaration();
            case havel::TokenType::Return:
                return parseReturnStatement();
            case havel::TokenType::OpenBrace:
                return parseBlockStatement();
            case havel::TokenType::Import:
                return parseImportStatement();
            default: {
                auto expr = parseExpression();
                return std::make_unique<havel::ast::ExpressionStatement>(std::move(expr));
            }
        }
    }
    
    std::unique_ptr<havel::ast::Statement> Parser::parseFunctionDeclaration() {
        advance(); // consume "fn"

        if (at().type != havel::TokenType::Identifier) {
            throw std::runtime_error("Expected function name after 'fn'");
        }
        auto name = std::make_unique<havel::ast::Identifier>(advance().value);

        if (at().type != havel::TokenType::OpenParen) {
            throw std::runtime_error("Expected '(' after function name");
        }
        advance(); // consume '('

        std::vector<std::unique_ptr<havel::ast::Identifier>> params;
        while (notEOF() && at().type != havel::TokenType::CloseParen) {
            if (at().type != havel::TokenType::Identifier) {
                throw std::runtime_error("Expected identifier in parameter list");
            }
            params.push_back(std::make_unique<havel::ast::Identifier>(advance().value));

            if (at().type == havel::TokenType::Comma) {
                advance(); // consume ','
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

        if (at().type != havel::TokenType::From) {
            throw std::runtime_error("Expected 'from' keyword in import statement");
        }
        advance(); // consume 'from'

        if (at().type != havel::TokenType::String) {
            throw std::runtime_error("Expected module path string after 'from'");
        }
        std::string path = advance().value;

        return std::make_unique<havel::ast::ImportStatement>(path, items);
    }

    std::unique_ptr<havel::ast::Expression> Parser::parseExpression() {
        return parsePipelineExpression();
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
    
            case havel::TokenType::Identifier: {
                auto identTk = advance();
                std::unique_ptr<havel::ast::Expression> expr = std::make_unique<havel::ast::Identifier>(identTk.value);
    
                // Handle postfix operations in a loop to support chaining: arr[0].prop() etc.
                while (true) {
                    if (at().type == havel::TokenType::OpenParen) {
                        expr = parseCallExpression(std::move(expr));
                    } else if (at().type == havel::TokenType::Dot) {
                        expr = parseMemberExpression(std::move(expr));
                    } else if (at().type == havel::TokenType::OpenBracket) {
                        expr = parseIndexExpression(std::move(expr));
                    } else if (at().type == havel::TokenType::String || at().type == havel::TokenType::Number) {
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
        // Handle trailing comma
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
        
        if (at().type == havel::TokenType::Comma) {
            advance(); // consume ','
        } else if (at().type != havel::TokenType::CloseBrace) {
            throw std::runtime_error("Expected ',' or '}' in object literal");
        }
    }
    
    if (at().type != havel::TokenType::CloseBrace) {
        throw std::runtime_error("Expected '}' to close object literal");
    }
    advance(); // consume '}'
    
    return std::make_unique<havel::ast::ObjectLiteral>(std::move(pairs));
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