// src/havel-lang/ast/AST.cpp
#include "AST.h"
#include <iostream>
namespace havel::ast {
    // Virtual destructors are already defaulted in the header for ASTNode and ASTVisitor.

    // AST Printer implementation
    class ASTPrinter : public ASTVisitor {
    private:
        int indentLevel = 0;
        std::ostream &out;
        // Allow printing to different streams, e.g. std::cout or a file

        std::string getIndent() const {
            return std::string(indentLevel * 2, ' ');
        }

        // Helper to print a node if it's not null
        template<typename T>
        void printChildNode(const char *label, const std::unique_ptr<T> &node) {
            out << getIndent() << label;
            if (node) {
                out << std::endl;
                node->accept(*this);
            } else {
                out << "nullptr" << std::endl;
            }
        }

        // Overload for non-unique_ptr (e.g. direct objects or references if needed)
        // For now, unique_ptr is the main use case for children.

    public:
        ASTPrinter(std::ostream &output_stream = std::cout) : out(
            output_stream) {
        }

        void visitProgram(const Program &node) override {
            out << getIndent() << "Program {" << std::endl;
            indentLevel++;
            for (const auto &stmt: node.body) {
                if (stmt) {
                    stmt->accept(*this);
                } else {
                    out << getIndent() << "nullptr_statement" << std::endl;
                }
            }
            indentLevel--;
            out << getIndent() << "}" << std::endl;
        }

        void visitHotkeyBinding(const HotkeyBinding &node) override {
            out << getIndent() << "HotkeyBinding {" << std::endl;
            indentLevel++;
            printChildNode("hotkey: ", node.hotkey);
            printChildNode("action: ", node.action);
            indentLevel--;
            out << getIndent() << "}" << std::endl;
        }

        void visitBlockStatement(const BlockStatement &node) override {
            out << getIndent() << "BlockStatement {" << std::endl;
            indentLevel++;
            for (const auto &stmt: node.body) {
                if (stmt) {
                    stmt->accept(*this);
                } else {
                    out << getIndent() << "nullptr_statement_in_block" <<
                            std::endl;
                }
            }
            indentLevel--;
            out << getIndent() << "}" << std::endl;
        }

        void visitExpressionStatement(
            const ExpressionStatement &node) override {
            out << getIndent() << "ExpressionStatement {" << std::endl;
            indentLevel++;
            printChildNode("expression: ", node.expression);
            indentLevel--;
            out << getIndent() << "}" << std::endl;
        }

        void visitPipelineExpression(const PipelineExpression &node) override {
            out << getIndent() << "PipelineExpression {" << std::endl;
            indentLevel++;
            out << getIndent() << "stages: [" << std::endl;
            indentLevel++;
            for (const auto &stage: node.stages) {
                if (stage) {
                    stage->accept(*this);
                } else {
                    out << getIndent() << "nullptr_stage" << std::endl;
                }
            }
            indentLevel--;
            out << getIndent() << "]" << std::endl;
            indentLevel--;
            out << getIndent() << "}" << std::endl;
        }

        void visitBinaryExpression(const BinaryExpression &node) override {
            out << getIndent() << "BinaryExpression {" << std::endl;
            indentLevel++;
            printChildNode("left: ", node.left);
            out << getIndent() << "operator: " << node.operator_ << std::endl;
            printChildNode("right: ", node.right);
            indentLevel--;
            out << getIndent() << "}" << std::endl;
        }

        void visitCallExpression(const CallExpression &node) override {
            out << getIndent() << "CallExpression {" << std::endl;
            indentLevel++;
            printChildNode("callee: ", node.callee);
            out << getIndent() << "args: [" << std::endl;
            indentLevel++;
            for (const auto &arg: node.args) {
                if (arg) {
                    arg->accept(*this);
                } else {
                    out << getIndent() << "nullptr_arg" << std::endl;
                }
            }
            indentLevel--;
            out << getIndent() << "]" << std::endl;
            indentLevel--;
            out << getIndent() << "}" << std::endl;
        }

        void visitMemberExpression(const MemberExpression &node) override {
            out << getIndent() << "MemberExpression {" << std::endl;
            indentLevel++;
            printChildNode("object: ", node.object);
            printChildNode("property: ", node.property);
            indentLevel--;
            out << getIndent() << "}" << std::endl;
        }

        void visitStringLiteral(const StringLiteral &node) override {
            // Use the toString method for literals as it's already well-formatted for them
            out << getIndent() << node.toString() << std::endl;
        }

        void visitNumberLiteral(const NumberLiteral &node) override {
            out << getIndent() << node.toString() << std::endl;
        }

        void visitIdentifier(const Identifier &node) override {
            out << getIndent() << node.toString() << std::endl;
        }

        void visitHotkeyLiteral(const HotkeyLiteral &node) override {
            out << getIndent() << node.toString() << std::endl;
        }

        void visitLetDeclaration(const LetDeclaration &node) override {
            out << getIndent() << "LetDeclaration {" << std::endl;
            indentLevel++;
            printChildNode("name: ", node.name);
            if (node.value) {
                // Value is optional
                printChildNode("value: ", node.value);
            } else {
                out << getIndent() << "value: (uninitialized)" << std::endl;
            }
            indentLevel--;
            out << getIndent() << "}" << std::endl;
        }

        void visitIfStatement(const IfStatement &node) override {
            out << getIndent() << "IfStatement {" << std::endl;
            indentLevel++;
            printChildNode("condition: ", node.condition);
            printChildNode("consequence: ", node.consequence);
            if (node.alternative) {
                printChildNode("alternative: ", node.alternative);
            }
            indentLevel--;
            out << getIndent() << "}" << std::endl;
        }

        void visitReturnStatement(const ReturnStatement &node) override {
            out << getIndent() << "ReturnStatement {" << std::endl;
            indentLevel++;
            if (node.argument) {
                printChildNode("argument: ", node.argument);
            } else {
                out << getIndent() << "argument: void" << std::endl;
            }
            indentLevel--;
            out << getIndent() << "}" << std::endl;
        }

        void visitWhileStatement(const WhileStatement &node) override {
            out << getIndent() << "WhileStatement {" << std::endl;
            indentLevel++;
            printChildNode("condition: ", node.condition);
            printChildNode("body: ", node.body);
            indentLevel--;
            out << getIndent() << "}" << std::endl;
        }

        void visitFunctionDeclaration(
            const FunctionDeclaration &node) override {
            out << getIndent() << "FunctionDeclaration {" << std::endl;
            indentLevel++;
            printChildNode("name: ", node.name);
            out << getIndent() << "parameters: [" << std::endl;
            indentLevel++;
            for (const auto &param: node.parameters) {
                if (param) {
                    param->accept(*this);
                } else {
                    out << getIndent() << "nullptr_param" << std::endl;
                }
            }
            indentLevel--;
            out << getIndent() << "]" << std::endl;
            printChildNode("body: ", node.body);
            indentLevel--;
            out << getIndent() << "}" << std::endl;
        }


void visitTypeDeclaration(const TypeDeclaration& node) override {
    out << getIndent() << "TypeDeclaration {" << std::endl;
    indentLevel++;
    out << getIndent() << "name: " << node.name << std::endl;
    if (node.definition) {
        out << getIndent() << "definition:" << std::endl;
        indentLevel++;
        node.definition->accept(*this);
        indentLevel--;
    } else {
        out << getIndent() << "definition: nullptr" << std::endl;
    }
    indentLevel--;
    out << getIndent() << "}" << std::endl;
}

void visitTypeAnnotation(const TypeAnnotation& node) override {
    out << getIndent() << "TypeAnnotation {" << std::endl;
    indentLevel++;
    printChildNode("type: ", node.type);
    indentLevel--;
    out << getIndent() << "}" << std::endl;
}

void visitUnionType(const UnionType& node) override {
    out << getIndent() << "UnionType {" << std::endl;
    indentLevel++;
    out << getIndent() << "variants: [" << std::endl;
    indentLevel++;
    for (const auto& variant : node.variants) {
        if (variant) {
            variant->accept(*this);
        } else {
            out << getIndent() << "nullptr_variant" << std::endl;
        }
    }
    indentLevel--;
    out << getIndent() << "]" << std::endl;
    indentLevel--;
    out << getIndent() << "}" << std::endl;
}

void visitRecordType(const RecordType& node) override {
    out << getIndent() << "RecordType {" << std::endl;
    indentLevel++;
    out << getIndent() << "fields: [" << std::endl;
    indentLevel++;
    for (const auto& field : node.fields) {
        out << getIndent() << "field: " << field.first << " -> ";
        if (field.second) {
            out << std::endl;
            indentLevel++;
            field.second->accept(*this);
            indentLevel--;
        } else {
            out << "nullptr" << std::endl;
        }
    }
    indentLevel--;
    out << getIndent() << "]" << std::endl;
    indentLevel--;
    out << getIndent() << "}" << std::endl;
}

void visitFunctionType(const FunctionType& node) override {
    out << getIndent() << "FunctionType {" << std::endl;
    indentLevel++;
    out << getIndent() << "parameters: [" << std::endl;
    indentLevel++;
    for (const auto& param : node.paramTypes) {
        if (param) {
            param->accept(*this);
        } else {
            out << getIndent() << "nullptr_param_type" << std::endl;
        }
    }
    indentLevel--;
    out << getIndent() << "]" << std::endl;
    printChildNode("returnType: ", node.returnType);
    indentLevel--;
    out << getIndent() << "}" << std::endl;
}

void visitTypeReference(const TypeReference& node) override {
    out << getIndent() << "TypeReference{" << node.name << "}" << std::endl;
}
        void visitTryExpression(const TryExpression& node) override {
            out << getIndent() << "TryExpression {" << std::endl;
            indentLevel++;
            printChildNode("tryBody: ", node.tryBody);
            printChildNode("catchBody: ", node.catchBody);
            indentLevel--;
            out << getIndent() << "}" << std::endl;
        }
        void visitUnaryExpression(const havel::ast::UnaryExpression &node) override {
            out << getIndent() << "UnaryExpression {" << std::endl;
            indentLevel++;
            
            // Print operator info
            std::string operatorName;
            std::string operatorSymbol;
            
            switch (node.operator_) {
                case havel::ast::UnaryExpression::UnaryOperator::Not:
                    operatorName = "Not";
                    operatorSymbol = "!";
                    break;
                case havel::ast::UnaryExpression::UnaryOperator::Minus:
                    operatorName = "Minus";
                    operatorSymbol = "-";
                    break;
                case havel::ast::UnaryExpression::UnaryOperator::Plus:
                    operatorName = "Plus";
                    operatorSymbol = "+";
                    break;
                default:
                    operatorName = "Unknown";
                    operatorSymbol = "?";
            }
            
            out << getIndent() << "operator: " << operatorSymbol << " (" << operatorName << ")" << std::endl;
            printChildNode("operand: ", node.operand);
            
            indentLevel--;
            out << getIndent() << "}" << std::endl;
        }
    };
    inline void TypeDeclaration::accept(ASTVisitor& visitor) const {
        visitor.visitTypeDeclaration(*this);
    }

    inline void TypeAnnotation::accept(ASTVisitor& visitor) const {
        visitor.visitTypeAnnotation(*this);
    }

    inline void UnionType::accept(ASTVisitor& visitor) const {
        visitor.visitUnionType(*this);
    }

    inline void RecordType::accept(ASTVisitor& visitor) const {
        visitor.visitRecordType(*this);
    }

    inline void FunctionType::accept(ASTVisitor& visitor) const {
        visitor.visitFunctionType(*this);
    }

    inline void TypeReference::accept(ASTVisitor& visitor) const {
        visitor.visitTypeReference(*this);
    }

    inline void TryExpression::accept(ASTVisitor& visitor) const {
        visitor.visitTryExpression(*this);
    }
    inline void UnaryExpression::accept(ASTVisitor &visitor) const {
        visitor.visitUnaryExpression(*this);
    }
} // namespace havel::ast
