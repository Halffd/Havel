// src/havel-lang/ast/AST.h
#pragma once
#include "../lexer/Lexer.hpp"
#include <vector>
#include <memory>
#include <string>
#include <sstream>

namespace havel::ast {
    // Forward declaration for ASTVisitor
    class ASTVisitor;
    enum class NodeType {
        // Program structure
        Program,
        Module,
        ImportStatement,     // import List from "std/collections"

        // Core functional expressions
        HotkeyBinding,       // F1 => { ... }
        PipelineExpression,  // data | map(f) | filter(g) | reduce(h)
        BinaryExpression,    // 10 + 5, a && b
        UnaryExpression,     // not flag, -number
        CallExpression,      // map(f, list)
        MemberExpression,    // record.field
        LambdaExpression,    // fn(x) -> x * 2, |x| x + 1
        ApplicationExpression, // Curried function application

        // Pattern matching (essential for FP)
        MatchExpression,     // match value with | Some(x) -> x | None -> 0
        PatternLiteral,      // Some(x), [head|tail], {x, y}
        GuardExpression,     // | x when x > 0 -> "positive"
        BlockStatement,      // { ... }
        IfStatement,         // if condition { ... } else { ... }
        ReturnStatement,     // return value;
        WhileStatement,      // while condition { ... }
        // Immutable data structures
        ListExpression,      // [1, 2, 3]
        TupleExpression,     // (1, "hello", true)
        RecordExpression,    // {name: "John", age: 30}
        MapExpression,       // #{key1: val1, key2: val2}
        SetExpression,       // #{1, 2, 3}

        // Destructuring
        ListPattern,         // [head|tail], [a, b, c]
        TuplePattern,        // (x, y)
        RecordPattern,       // {name, age}

        // Literals
        StringLiteral,       // "Hello"
        NumberLiteral,       // 42, 3.14
        BooleanLiteral,      // true, false
        AtomLiteral,         // :ok, :error (like Elixir atoms)
        Identifier,          // variable names
        HotkeyLiteral,       // F1, Ctrl+V

        // Functional statements (minimize these)
        ExpressionStatement, // Most things are expressions
        LetDeclaration,          // let x = 5 (immutable)
        FunctionDeclaration, // let add = fn(a, b) -> a + b

        // Type system (if you want static typing)
        TypeDeclaration,     // type Point = {x: Float, y: Float}
        UnionType,          // type Result = Ok(a) | Error(String)
        TypeAnnotation,     // : List(Int)

        // Higher-order constructs
        PartialApplication, // add(5, _) creates fn(b) -> 5 + b
        Composition,        // f >> g, f << g

        // Monadic operations (advanced)
        DoExpression,       // do { x <- getX(); y <- getY(); return x + y }
        BindExpression,     // >>= operator

        // Error handling (functional style)
        TryExpression,      // try expr catch pattern -> handler

        // Lazy evaluation
        LazyExpression,     // lazy { expensive_computation() }
        ForceExpression,    // force lazy_value

        // Comprehensions
        ListComprehension,  // [x * 2 | x <- [1..10], x > 5]

        // Special
        ErrorNode,
        UnknownNode
    };

    // Base AST Node
    struct ASTNode {
        NodeType kind;

        virtual ~ASTNode() = default;

        virtual std::string toString() const = 0;

        virtual void accept(ASTVisitor &visitor) const = 0;
    };

    // Expression base (inherits from ASTNode)
    struct Expression : public ASTNode {
        // All expressions can be evaluated to values
    };

    // Statement base (inherits from ASTNode)
    struct Statement : public ASTNode {
        // Statements don't return values
    };

    // Program Node
    struct Program : public Statement {
        std::vector<std::unique_ptr<Statement> > body;

        Program() { kind = NodeType::Program; }

        std::string toString() const override {
            return "Program{body: [" + std::to_string(body.size()) +
                   " statements]}";
        }

        void accept(ASTVisitor &visitor) const override;
    };

    // Identifier
    struct Identifier : public Expression {
        std::string symbol;

        Identifier(const std::string &sym) : symbol(sym) {
            kind = NodeType::Identifier;
        }

        std::string toString() const override {
            return "Identifier{" + symbol + "}";
        }

        void accept(ASTVisitor &visitor) const override;
    };

    // Block Statement ({ ... })
    struct BlockStatement : public Statement {
        std::vector<std::unique_ptr<Statement> > body;

        BlockStatement() { kind = NodeType::BlockStatement; }

        std::string toString() const override {
            return "Block{" + std::to_string(body.size()) + " statements}";
        }

        void accept(ASTVisitor &visitor) const override;
    };

    // Hotkey Binding (Havel-specific)
    struct HotkeyBinding : public Statement {
        std::unique_ptr<Expression> hotkey;
        std::unique_ptr<Statement> action;
        // Changed from Expression to Statement

       HotkeyBinding() { kind = NodeType::HotkeyBinding; }
        HotkeyBinding(std::unique_ptr<Expression> hk,
                      std::unique_ptr<Statement> act)
            : hotkey(std::move(hk)), action(std::move(act)) {
            kind = NodeType::HotkeyBinding;
        }

        std::string toString() const override {
            return "HotkeyBinding{hotkey: " + (hotkey
                                                   ? hotkey->toString()
                                                   : "nullptr") +
                   ", action: " + (action ? action->toString() : "nullptr") +
                   "}";
        }

        void accept(ASTVisitor &visitor) const override;
    };

    // Pipeline Expression (clipbooard.get | text.upper | send)
    struct PipelineExpression : public Expression {
        std::vector<std::unique_ptr<Expression> > stages;

        PipelineExpression(
            std::vector<std::unique_ptr<Expression> > stgs = {}) : stages(
            std::move(stgs)) {
            kind = NodeType::PipelineExpression;
        }

        std::string toString() const override {
            return "Pipeline{stages: " + std::to_string(stages.size()) + "}";
        }

        void accept(ASTVisitor &visitor) const override;
    };

    // Binary Expression
    struct BinaryExpression : public Expression {
        std::unique_ptr<Expression> left;
        std::string operator_;
        std::unique_ptr<Expression> right;

        BinaryExpression(std::unique_ptr<Expression> l, std::string op,
                         std::unique_ptr<Expression> r)
            : left(std::move(l)), operator_(std::move(op)),
              right(std::move(r)) {
            kind = NodeType::BinaryExpression;
        }

        std::string toString() const override {
            return "BinaryExpr{" + (left ? left->toString() : "nullptr") + " " +
                   operator_ + " " + (right ? right->toString() : "nullptr") +
                   "}";
        }

        void accept(ASTVisitor &visitor) const override;
    };

    // Call Expression (send("Hello"))
    struct CallExpression : public Expression {
        std::unique_ptr<Expression> callee;
        std::vector<std::unique_ptr<Expression> > args;

        CallExpression(std::unique_ptr<Expression> cal,
                       std::vector<std::unique_ptr<Expression> > ags = {})
            : callee(std::move(cal)), args(std::move(ags)) {
            kind = NodeType::CallExpression;
        }

        std::string toString() const override {
            return "CallExpr{" + (callee ? callee->toString() : "nullptr") + "("
                   + std::to_string(args.size()) + " args)}";
        }

        void accept(ASTVisitor &visitor) const override;
    };

    // Member Expression (clipboard.get)
    struct MemberExpression : public Expression {
        std::unique_ptr<Expression> object;
        std::unique_ptr<Expression> property; // e.g., an Identifier

        MemberExpression() { kind = NodeType::MemberExpression; }
      MemberExpression(std::unique_ptr<Expression> obj,
                         std::unique_ptr<Expression> prop)
            : object(std::move(obj)), property(std::move(prop)) {
            kind = NodeType::MemberExpression;
        }

        std::string toString() const override {
            return "MemberExpr{" + (object ? object->toString() : "nullptr") +
                   "." + (property ? property->toString() : "nullptr") + "}";
        }

        void accept(ASTVisitor &visitor) const override;
    };

    // String Literal
    struct StringLiteral : public Expression {
        std::string value;

        StringLiteral(const std::string &val) : value(val) {
            kind = NodeType::StringLiteral;
        }

        std::string toString() const override {
            // Basic escaping for quotes in the string for display
            std::string display_val = value;
            size_t pos = 0;
            while ((pos = display_val.find("\"", pos)) != std::string::npos) {
                display_val.replace(pos, 1, "\\\"");
                pos += 2;
            }
            return "StringLiteral{\"" + display_val + "\"}";
        }

        void accept(ASTVisitor &visitor) const override;
    };

    // Number Literal
    struct NumberLiteral : public Expression {
        double value;

        NumberLiteral(double val) : value(val) {
            kind = NodeType::NumberLiteral;
        }

        std::string toString() const override {
            // Use stringstream to avoid trailing zeros for whole numbers
            std::ostringstream oss;
            oss << value;
            return "NumberLiteral{" + oss.str() + "}";
        }

        void accept(ASTVisitor &visitor) const override;
    };

    // Hotkey Literal (F1, Ctrl+V, etc.)
    struct HotkeyLiteral : public Expression {
        std::string combination;

        HotkeyLiteral(const std::string &combo) : combination(combo) {
            kind = NodeType::HotkeyLiteral;
        }

        std::string toString() const override {
            return "HotkeyLiteral{" + combination + "}";
        }

        void accept(ASTVisitor &visitor) const override;
    };

    // Expression Statement (wraps an expression as a statement)
    struct ExpressionStatement : public Statement {
        std::unique_ptr<Expression> expression;
        ExpressionStatement() { kind = NodeType::ExpressionStatement; }
        ExpressionStatement(std::unique_ptr<Expression> expr) : expression(
            std::move(expr)) {
            kind = NodeType::ExpressionStatement;
        }

        std::string toString() const override {
            return "ExpressionStatement{" + (expression
                                                 ? expression->toString()
                                                 : "nullptr") + "}";
        }

        void accept(ASTVisitor &visitor) const override;
    };

    // Let Declaration
    struct LetDeclaration : public Statement {
        std::unique_ptr<Identifier> name;
        std::unique_ptr<Expression> value;
        // Value can be optional if language supports `let x;`

        LetDeclaration(std::unique_ptr<Identifier> id,
                       std::unique_ptr<Expression> val = nullptr)
            : name(std::move(id)), value(std::move(val)) {
            kind = NodeType::LetDeclaration;
        }

        std::string toString() const override {
            return "LetDeclaration{name: " + (name
                                                  ? name->toString()
                                                  : "nullptr") +
                   (value ? ", value: " + value->toString() : "") + "}";
        }

        void accept(ASTVisitor &visitor) const override;
    };

    // If Statement
    struct IfStatement : public Statement {
        std::unique_ptr<Expression> condition;
        std::unique_ptr<Statement> consequence; // Typically a BlockStatement
        std::unique_ptr<Statement> alternative;
        // Optional, typically BlockStatement or another IfStatement

        IfStatement(std::unique_ptr<Expression> cond,
                    std::unique_ptr<Statement> cons,
                    std::unique_ptr<Statement> alt = nullptr)
            : condition(std::move(cond)), consequence(std::move(cons)),
              alternative(std::move(alt)) {
            kind = NodeType::IfStatement;
        }

        std::string toString() const override {
            std::string str = "IfStatement{condition: " + (condition
                                  ? condition->toString()
                                  : "nullptr") +
                              ", consequence: " + (consequence
                                                       ? consequence->toString()
                                                       : "nullptr");
            if (alternative) {
                str += ", alternative: " + alternative->toString();
            }
            str += "}";
            return str;
        }

        void accept(ASTVisitor &visitor) const override;
    };

    // Return Statement
    struct ReturnStatement : public Statement {
        std::unique_ptr<Expression> argument; // Can be nullptr for `return;`

        ReturnStatement(std::unique_ptr<Expression> arg = nullptr) : argument(
            std::move(arg)) {
            kind = NodeType::ReturnStatement;
        }

        std::string toString() const override {
            return "ReturnStatement{" + (argument
                                             ? argument->toString()
                                             : "void") + "}";
        }

        void accept(ASTVisitor &visitor) const override;
    };

    // While Statement
    struct WhileStatement : public Statement {
        std::unique_ptr<Expression> condition;
        std::unique_ptr<Statement> body; // Typically a BlockStatement

        WhileStatement(std::unique_ptr<Expression> cond,
                       std::unique_ptr<Statement> bd)
            : condition(std::move(cond)), body(std::move(bd)) {
            kind = NodeType::WhileStatement;
        }

        std::string toString() const override {
            return "WhileStatement{condition: " + (condition
                                                       ? condition->toString()
                                                       : "nullptr") +
                   ", body: " + (body ? body->toString() : "nullptr") + "}";
        }

        void accept(ASTVisitor &visitor) const override;
    };

    // Function Declaration
    struct FunctionDeclaration : public Statement {
        std::unique_ptr<Identifier> name;
        std::vector<std::unique_ptr<Identifier> > parameters;
        std::unique_ptr<BlockStatement> body;

        FunctionDeclaration(std::unique_ptr<Identifier> n,
                            std::vector<std::unique_ptr<Identifier> > params,
                            std::unique_ptr<BlockStatement> bd)
            : name(std::move(n)), parameters(std::move(params)),
              body(std::move(bd)) {
            kind = NodeType::FunctionDeclaration;
        }

        std::string toString() const override {
            std::string paramsStr;
            for (size_t i = 0; i < parameters.size(); ++i) {
                paramsStr += parameters[i]->toString();
                if (i < parameters.size() - 1) paramsStr += ", ";
            }
            return "FunctionDeclaration{name: " + (name
                                                       ? name->toString()
                                                       : "nullptr") +
                   ", params: [" + paramsStr + "]" +
                   ", body: " + (body ? body->toString() : "nullptr") + "}";
        }

        void accept(ASTVisitor &visitor) const override;
    };
    struct TryExpression : public Statement {
        std::unique_ptr<Expression> tryBody;
        std::unique_ptr<Expression> catchBody;

        TryExpression() { kind = NodeType::TryExpression; }

        std::string toString() const override {
            return "TryExpression{}";
        }

        void accept(ASTVisitor& visitor) const override;
    };
// Type Definition base class
    struct TypeDefinition : public ASTNode {
        std::string toString() const override { return "TypeDefinition{}"; }
        void accept(ASTVisitor& visitor) const override { /* default implementation */ }
    };

// Simple type reference (e.g., Int, String, MyCustomType)
struct TypeReference : public TypeDefinition {
    std::string name;

    TypeReference(const std::string& typeName) : name(typeName) {
        kind = NodeType::TypeAnnotation; // or create a new NodeType::TypeReference
    }

    std::string toString() const override {
        return "TypeReference{" + name + "}";
    }

    void accept(ASTVisitor& visitor) const override;
};

// Union type (e.g., Result = Ok(a) | Error(String))
struct UnionType : public TypeDefinition {
    std::vector<std::unique_ptr<TypeDefinition>> variants;

    UnionType(std::vector<std::unique_ptr<TypeDefinition>> vars)
        : variants(std::move(vars)) {
        kind = NodeType::UnionType;
    }

    std::string toString() const override {
        return "UnionType{" + std::to_string(variants.size()) + " variants}";
    }

    void accept(ASTVisitor& visitor) const override;
};

// Record type (e.g., {name: String, age: Int})
struct RecordType : public TypeDefinition {
    std::vector<std::pair<std::string, std::unique_ptr<TypeDefinition>>> fields;

    RecordType() {
        kind = NodeType::RecordExpression; // Reuse or create RecordType
    }

    std::string toString() const override {
        return "RecordType{" + std::to_string(fields.size()) + " fields}";
    }

    void accept(ASTVisitor& visitor) const override;
};

// Function type (e.g., (Int, String) -> Bool)
struct FunctionType : public TypeDefinition {
    std::vector<std::unique_ptr<TypeDefinition>> paramTypes;
    std::unique_ptr<TypeDefinition> returnType;

    FunctionType(std::vector<std::unique_ptr<TypeDefinition>> params,
                 std::unique_ptr<TypeDefinition> ret)
        : paramTypes(std::move(params)), returnType(std::move(ret)) {
        kind = NodeType::FunctionDeclaration; // Reuse or create FunctionType
    }

    std::string toString() const override {
        return "FunctionType{" + std::to_string(paramTypes.size()) + " -> 1}";
    }

    void accept(ASTVisitor& visitor) const override;
};

// Type Declaration statement (e.g., type Point = {x: Float, y: Float})
struct TypeDeclaration : public Statement {
    std::string name;
    std::unique_ptr<TypeDefinition> definition;

    TypeDeclaration(const std::string& typeName,
                    std::unique_ptr<TypeDefinition> def)
        : name(typeName), definition(std::move(def)) {
        kind = NodeType::TypeDeclaration;
    }

    std::string toString() const override {
        return "TypeDeclaration{name: " + name +
               ", definition: " + (definition ? definition->toString() : "nullptr") + "}";
    }

    void accept(ASTVisitor& visitor) const override;
};

// Type Annotation (e.g., : List(Int))
struct TypeAnnotation : public ASTNode {
    std::unique_ptr<TypeDefinition> type;

    TypeAnnotation(std::unique_ptr<TypeDefinition> t)
        : type(std::move(t)) {
        kind = NodeType::TypeAnnotation;
    }

    std::string toString() const override {
        return "TypeAnnotation{" + (type ? type->toString() : "nullptr") + "}";
    }

    void accept(ASTVisitor& visitor) const override;
};
    // Import Statement (import List from "std/collections")
    struct ImportStatement : public Statement {
        std::string moduleName;
        std::vector<std::string> importedItems; // Optional: specific items to import

        ImportStatement(const std::string& module,
                       std::vector<std::string> items = {})
            : moduleName(module), importedItems(std::move(items)) {
            kind = NodeType::ImportStatement;
        }

        std::string toString() const override {
            std::string result = "ImportStatement{module: " + moduleName;
            if (!importedItems.empty()) {
                result += ", items: [";
                for (size_t i = 0; i < importedItems.size(); ++i) {
                    result += importedItems[i];
                    if (i < importedItems.size() - 1) result += ", ";
                }
                result += "]";
            }
            result += "}";
            return result;
        }

        void accept(ASTVisitor& visitor) const override;
    };
    // Visitor pattern interface for AST traversal
    class ASTVisitor {
    public:
        virtual ~ASTVisitor() = default;

        virtual void visitProgram(const Program &node) = 0;

        virtual void visitHotkeyBinding(const HotkeyBinding &node) = 0;

        virtual void visitPipelineExpression(const PipelineExpression &node) =
        0;

        virtual void visitBinaryExpression(const BinaryExpression &node) = 0;

        virtual void visitCallExpression(const CallExpression &node) = 0;

        virtual void visitMemberExpression(const MemberExpression &node) = 0;

        virtual void visitStringLiteral(const StringLiteral &node) = 0;

        virtual void visitNumberLiteral(const NumberLiteral &node) = 0;

        virtual void visitIdentifier(const Identifier &node) = 0;

        virtual void visitHotkeyLiteral(const HotkeyLiteral &node) = 0;

        virtual void visitBlockStatement(const BlockStatement &node) = 0;

        virtual void visitExpressionStatement(const ExpressionStatement &node) =
        0;

        virtual void visitIfStatement(const IfStatement &node) = 0;

        virtual void visitLetDeclaration(const LetDeclaration &node) = 0;

        virtual void visitReturnStatement(const ReturnStatement &node) = 0;

        virtual void visitWhileStatement(const WhileStatement &node) = 0;

        virtual void visitFunctionDeclaration(const FunctionDeclaration &node) =
        0;

        virtual void visitTypeDeclaration(const TypeDeclaration& node) = 0;
        virtual void visitTypeAnnotation(const TypeAnnotation& node) = 0;
        virtual void visitUnionType(const UnionType& node) = 0;
        virtual void visitRecordType(const RecordType& node) = 0;
        virtual void visitFunctionType(const FunctionType& node) = 0;
        virtual void visitTypeReference(const TypeReference& node) = 0;
        virtual void visitTryExpression(const TryExpression& node) = 0;
    };
    // Definitions of accept methods (must be after ASTVisitor declaration)
    inline void Program::accept(ASTVisitor &visitor) const {
        visitor.visitProgram(*this);
    }

    inline void Identifier::accept(ASTVisitor &visitor) const {
        visitor.visitIdentifier(*this);
    }

    inline void BlockStatement::accept(ASTVisitor &visitor) const {
        visitor.visitBlockStatement(*this);
    }

    inline void HotkeyBinding::accept(ASTVisitor &visitor) const {
        visitor.visitHotkeyBinding(*this);
    }

    inline void PipelineExpression::accept(ASTVisitor &visitor) const {
        visitor.visitPipelineExpression(*this);
    }

    inline void BinaryExpression::accept(ASTVisitor &visitor) const {
        visitor.visitBinaryExpression(*this);
    }

    inline void CallExpression::accept(ASTVisitor &visitor) const {
        visitor.visitCallExpression(*this);
    }

    inline void MemberExpression::accept(ASTVisitor &visitor) const {
        visitor.visitMemberExpression(*this);
    }

    inline void StringLiteral::accept(ASTVisitor &visitor) const {
        visitor.visitStringLiteral(*this);
    }

    inline void NumberLiteral::accept(ASTVisitor &visitor) const {
        visitor.visitNumberLiteral(*this);
    }

    inline void HotkeyLiteral::accept(ASTVisitor &visitor) const {
        visitor.visitHotkeyLiteral(*this);
    }

    inline void ExpressionStatement::accept(ASTVisitor &visitor) const {
        visitor.visitExpressionStatement(*this);
    }

    inline void LetDeclaration::accept(ASTVisitor &visitor) const {
        visitor.visitLetDeclaration(*this);
    }

    inline void IfStatement::accept(ASTVisitor &visitor) const {
        visitor.visitIfStatement(*this);
    }

    inline void ReturnStatement::accept(ASTVisitor &visitor) const {
        visitor.visitReturnStatement(*this);
    }

    inline void WhileStatement::accept(ASTVisitor &visitor) const {
        visitor.visitWhileStatement(*this);
    }

    inline void FunctionDeclaration::accept(ASTVisitor &visitor) const {
        visitor.visitFunctionDeclaration(*this);
    }
} // namespace havel::ast
