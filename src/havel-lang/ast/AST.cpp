// src/havel-lang/ast/AST.cpp
#include "AST.h"
#include <iostream>

namespace havel::ast {

// Virtual destructors are already defaulted in the header

// Visitor pattern implementation for AST traversal
class ASTVisitor {
public:
    virtual void visitProgram(const Program& node) = 0;
    virtual void visitHotkeyBinding(const HotkeyBinding& node) = 0;
    virtual void visitBlockStatement(const BlockStatement& node) = 0;
    virtual void visitExpressionStatement(const ExpressionStatement& node) = 0;
    virtual void visitPipelineExpression(const PipelineExpression& node) = 0;
    virtual void visitBinaryExpression(const BinaryExpression& node) = 0;
    virtual void visitCallExpression(const CallExpression& node) = 0;
    virtual void visitMemberExpression(const MemberExpression& node) = 0;
    virtual void visitStringLiteral(const StringLiteral& node) = 0;
    virtual void visitNumberLiteral(const NumberLiteral& node) = 0;
    virtual void visitIdentifier(const Identifier& node) = 0;
    virtual void visitHotkeyLiteral(const HotkeyLiteral& node) = 0;
};

// AST Printer implementation
class ASTPrinter : public ASTVisitor {
private:
    int indentLevel = 0;
    std::string getIndent() const {
        return std::string(indentLevel * 2, ' ');
    }

public:
    void visitProgram(const Program& node) override {
        std::cout << getIndent() << "Program {" << std::endl;
        indentLevel++;
        for (const auto& stmt : node.body) {
            // Dispatch based on statement type
            // This would use double dispatch in a full implementation
            std::cout << getIndent() << stmt->toString() << std::endl;
        }
        indentLevel--;
        std::cout << getIndent() << "}" << std::endl;
    }

    void visitHotkeyBinding(const HotkeyBinding& node) override {
        std::cout << getIndent() << "HotkeyBinding {" << std::endl;
        indentLevel++;
        std::cout << getIndent() << "hotkey: " << node.hotkey->toString() << std::endl;
        std::cout << getIndent() << "action: " << node.action->toString() << std::endl;
        indentLevel--;
        std::cout << getIndent() << "}" << std::endl;
    }

    void visitBlockStatement(const BlockStatement& node) override {
        std::cout << getIndent() << "Block {" << std::endl;
        indentLevel++;
        for (const auto& stmt : node.body) {
            std::cout << getIndent() << stmt->toString() << std::endl;
        }
        indentLevel--;
        std::cout << getIndent() << "}" << std::endl;
    }

    void visitExpressionStatement(const ExpressionStatement& node) override {
        std::cout << getIndent() << "ExpressionStatement {" << std::endl;
        indentLevel++;
        std::cout << getIndent() << node.expression->toString() << std::endl;
        indentLevel--;
        std::cout << getIndent() << "}" << std::endl;
    }

    void visitPipelineExpression(const PipelineExpression& node) override {
        std::cout << getIndent() << "Pipeline {" << std::endl;
        indentLevel++;
        for (const auto& stage : node.stages) {
            std::cout << getIndent() << stage->toString() << std::endl;
        }
        indentLevel--;
        std::cout << getIndent() << "}" << std::endl;
    }

    void visitBinaryExpression(const BinaryExpression& node) override {
        std::cout << getIndent() << "BinaryExpr {" << std::endl;
        indentLevel++;
        std::cout << getIndent() << "left: " << node.left->toString() << std::endl;
        std::cout << getIndent() << "operator: " << node.operator_ << std::endl;
        std::cout << getIndent() << "right: " << node.right->toString() << std::endl;
        indentLevel--;
        std::cout << getIndent() << "}" << std::endl;
    }

    void visitCallExpression(const CallExpression& node) override {
        std::cout << getIndent() << "CallExpr {" << std::endl;
        indentLevel++;
        std::cout << getIndent() << "callee: " << node.callee->toString() << std::endl;
        std::cout << getIndent() << "args: [" << std::endl;
        indentLevel++;
        for (const auto& arg : node.args) {
            std::cout << getIndent() << arg->toString() << std::endl;
        }
        indentLevel--;
        std::cout << getIndent() << "]" << std::endl;
        indentLevel--;
        std::cout << getIndent() << "}" << std::endl;
    }

    void visitMemberExpression(const MemberExpression& node) override {
        std::cout << getIndent() << "MemberExpr {" << std::endl;
        indentLevel++;
        std::cout << getIndent() << "object: " << node.object->toString() << std::endl;
        std::cout << getIndent() << "property: " << node.property->toString() << std::endl;
        indentLevel--;
        std::cout << getIndent() << "}" << std::endl;
    }

    void visitStringLiteral(const StringLiteral& node) override {
        std::cout << getIndent() << "StringLiteral {\"" << node.value << "\"}" << std::endl;
    }

    void visitNumberLiteral(const NumberLiteral& node) override {
        std::cout << getIndent() << "NumberLiteral {" << node.value << "}" << std::endl;
    }

    void visitIdentifier(const Identifier& node) override {
        std::cout << getIndent() << "Identifier {" << node.symbol << "}" << std::endl;
    }

    void visitHotkeyLiteral(const HotkeyLiteral& node) override {
        std::cout << getIndent() << "HotkeyLiteral {" << node.combination << "}" << std::endl;
    }
};

} // namespace havel::ast
