// src/havel-lang/ast/AST.h
#pragma once
#include "../lexer/Lexer.hpp"
#include <vector>
#include <memory>
#include <string>

namespace havel::ast {

// AST Node Types (like Tyler's NodeType enum)
enum class NodeType {
    // Program structure
    Program,

    // Expressions
    HotkeyBinding,      // F1 => { ... }
    PipelineExpression, // clipboard.out | text.upper | send
    BinaryExpression,   // 10 + 5
    CallExpression,     // send("Hello")
    MemberExpression,   // clipboard.out

    // Literals
    StringLiteral,      // "Hello World"
    NumberLiteral,      // 42
    Identifier,         // send, clipboard, etc.

    // Statements
    BlockStatement,     // { ... }
    ExpressionStatement, // expression;
    IfStatement,        // if condition { ... }
    LetDeclaration,     // let x = 5

    // Special
    HotkeyLiteral      // F1, Ctrl+V, etc.
};

// Base AST Node (like Tyler's Statement interface)
struct ASTNode {
    NodeType kind;
    virtual ~ASTNode() = default;
    virtual std::string toString() const = 0;
};

// Expression base (inherits from ASTNode)
struct Expression : public ASTNode {
    // All expressions can be evaluated to values
};

// Statement base (inherits from ASTNode)
struct Statement : public ASTNode {
    // Statements don't return values
};

// Program Node (like Tyler's Program interface)
struct Program : public Statement {
    std::vector<std::unique_ptr<Statement>> body;

    Program() { kind = NodeType::Program; }

    std::string toString() const override {
        return "Program{body: [" + std::to_string(body.size()) + " statements]}";
    }
};

// Hotkey Binding (Havel-specific)
struct HotkeyBinding : public Statement {
    std::unique_ptr<Expression> hotkey;    // F1, Ctrl+V
    std::unique_ptr<Expression> action;    // send "Hello" or { block }

    HotkeyBinding() { kind = NodeType::HotkeyBinding; }

    std::string toString() const override {
        return "HotkeyBinding{hotkey: " + hotkey->toString() +
               ", action: " + action->toString() + "}";
    }
};

// Pipeline Expression (clipboard.out | text.upper | send)
struct PipelineExpression : public Expression {
    std::vector<std::unique_ptr<Expression>> stages;

    PipelineExpression() { kind = NodeType::PipelineExpression; }

    std::string toString() const override {
        return "Pipeline{stages: " + std::to_string(stages.size()) + "}";
    }
};

// Binary Expression (like Tyler's BinaryExpression)
struct BinaryExpression : public Expression {
    std::unique_ptr<Expression> left;
    std::string operator_;
    std::unique_ptr<Expression> right;

    BinaryExpression() { kind = NodeType::BinaryExpression; }

    std::string toString() const override {
        return "BinaryExpr{" + left->toString() + " " + operator_ + " " + right->toString() + "}";
    }
};

// Call Expression (send("Hello"))
struct CallExpression : public Expression {
    std::unique_ptr<Expression> callee;    // function name
    std::vector<std::unique_ptr<Expression>> args;

    CallExpression() { kind = NodeType::CallExpression; }

    std::string toString() const override {
        return "CallExpr{" + callee->toString() + "(" + std::to_string(args.size()) + " args)}";
    }
};

// Member Expression (clipboard.out)
struct MemberExpression : public Expression {
    std::unique_ptr<Expression> object;    // clipboard
    std::unique_ptr<Expression> property;  // out

    MemberExpression() { kind = NodeType::MemberExpression; }

    std::string toString() const override {
        return "MemberExpr{" + object->toString() + "." + property->toString() + "}";
    }
};

// String Literal
struct StringLiteral : public Expression {
    std::string value;

    StringLiteral(const std::string& val) : value(val) {
        kind = NodeType::StringLiteral;
    }

    std::string toString() const override {
        return "StringLiteral{\"" + value + "\"}";
    }
};

// Number Literal (like Tyler's NumericLiteral)
struct NumberLiteral : public Expression {
    double value;

    NumberLiteral(double val) : value(val) {
        kind = NodeType::NumberLiteral;
    }

    std::string toString() const override {
        return "NumberLiteral{" + std::to_string(value) + "}";
    }
};

// Identifier (like Tyler's Identifier)
struct Identifier : public Expression {
    std::string symbol;

    Identifier(const std::string& sym) : symbol(sym) {
        kind = NodeType::Identifier;
    }

    std::string toString() const override {
        return "Identifier{" + symbol + "}";
    }
};

// Hotkey Literal (F1, Ctrl+V, etc.)
struct HotkeyLiteral : public Expression {
    std::string combination;

    HotkeyLiteral(const std::string& combo) : combination(combo) {
        kind = NodeType::HotkeyLiteral;
    }

    std::string toString() const override {
        return "HotkeyLiteral{" + combination + "}";
    }
};

// Block Statement ({ ... })
struct BlockStatement : public Statement {
    std::vector<std::unique_ptr<Statement>> body;

    BlockStatement() { kind = NodeType::BlockStatement; }

    std::string toString() const override {
        return "Block{" + std::to_string(body.size()) + " statements}";
    }
};

// Expression Statement (wraps an expression as a statement)
struct ExpressionStatement : public Statement {
    std::unique_ptr<Expression> expression;
    ExpressionStatement(std::unique_ptr<Expression> expr) : expression(std::move(expr)) {
        kind = NodeType::ExpressionStatement;
    }
    std::string toString() const override {
        return "ExpressionStatement{" + expression->toString() + "}";
    }
};

} // namespace havel::ast
