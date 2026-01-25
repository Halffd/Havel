#pragma once
#include "../lexer/Lexer.hpp"
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace havel::ast {
// Forward declaration for ASTVisitor
class ASTVisitor;
enum class NodeType {
  // Program structure
  Program,
  Module,
  ImportStatement, // import List from "std/collections"

  // Core functional expressions
  HotkeyBinding,         // F1 => { ... }
  PipelineExpression,    // data | map(f) | filter(g) | reduce(h)
  BinaryExpression,      // 10 + 5, a && b
  UnaryExpression,       // not flag, -number
  UpdateExpression,      // i++, --i
  CallExpression,        // map(f, list)
  MemberExpression,      // record.field
  LambdaExpression,      // fn(x) -> x * 2, |x| x + 1
  ApplicationExpression, // Curried function application

  // Pattern matching (essential for FP)
  MatchExpression, // match value with | Some(x) -> x | None -> 0
  PatternLiteral,  // Some(x), [head|tail], {x, y}
  GuardExpression, // | x when x > 0 -> "positive"
  BlockStatement,  // { ... }
  IfStatement,     // if condition { ... } else { ... }
  TernaryExpression, // condition ? trueValue : falseValue
  RangeExpression,   // 0..10
  AssignmentExpression, // identifier = value
  ReturnStatement, // return value;
  WhileStatement,  // while condition { ... }
  ForStatement,    // for i in range { ... }
  LoopStatement,   // loop { ... }
  BreakStatement,  // break
  ContinueStatement, // continue
  OnModeStatement,  // on mode gaming { ... }
  OffModeStatement, // off mode gaming { ... }
  WhenModeExpression, // when mode gaming
  // Conditional hotkeys
  ConditionalHotkey, // hotkey "Ctrl+A" if mode == foo then ...
  WhenBlockStatement, // when condition { ... }
  // Immutable data structures
  ListExpression,   // [1, 2, 3]
  ArrayLiteral,     // [1, 2, 3] - actual implementation
  ObjectLiteral,    // {name: "John", age: 30} - actual implementation
  ConfigBlock,      // config { ... }
  DevicesBlock,     // devices { ... }
  ModesBlock,       // modes { ... }
  IndexExpression,  // arr[0] or obj["key"]
  TupleExpression,  // (1, "hello", true)
  RecordExpression, // {name: "John", age: 30}
  MapExpression,    // #{key1: val1, key2: val2}
  SetExpression,    // #{1, 2, 3}

  // Destructuring
  ListPattern,   // [head|tail], [a, b, c]
  TuplePattern,  // (x, y)
  RecordPattern, // {name, age}
  // Literals
  StringLiteral,  // "Hello"
  InterpolatedStringExpression, // "Hello ${name}"
  NumberLiteral,  // 42, 3.14
  BooleanLiteral, // true, false
  AtomLiteral,    // :ok, :error (like Elixir atoms)
  Identifier,     // variable names
  HotkeyLiteral,  // F1, Ctrl+V

  // Functional statements (minimize these)
  ExpressionStatement, // Most things are expressions
  LetDeclaration,      // let x = 5 (immutable)
  FunctionDeclaration, // let add = fn(a, b) -> a + b

  // Type system (if you want static typing)
  TypeDeclaration, // type Point = {x: Float, y: Float}
  UnionType,       // type Result = Ok(a) | Error(String)
  TypeAnnotation,  // : List(Int)

  // Higher-order constructs
  PartialApplication, // add(5, _) creates fn(b) -> 5 + b
  Composition,        // f >> g, f << g

  // Monadic operations (advanced)
  DoExpression,   // do { x <- getX(); y <- getY(); return x + y }
  BindExpression, // >>= operator

  // Error handling (functional style)
  TryExpression, // try expr catch pattern -> handler

  // Lazy evaluation
  LazyExpression,  // lazy { expensive_computation() }
  ForceExpression, // force lazy_value

  // Comprehensions
  ListComprehension, // [x * 2 | x <- [1..10], x > 5]

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
enum class BinaryOperator {
  Add,
  Sub,
  Mul,
  Div,
  Mod,
  Pow,
  AddAssign,
  SubAssign,
  MulAssign,
  DivAssign,
  ModAssign,
  PowAssign,
  LessEqual,
  GreaterEqual,
  Equal,
  NotEqual,
  Less,
  Greater,
  And,
  Or
};

// Overload operator<< for BinaryOperator
inline std::ostream &operator<<(std::ostream &os, BinaryOperator op) {
  switch (op) {
  case BinaryOperator::Add:
    return os << "+";
  case BinaryOperator::Sub:
    return os << "-";
  case BinaryOperator::Mul:
    return os << "*";
  case BinaryOperator::Div:
    return os << "/";
  case BinaryOperator::Mod:
    return os << "%";
  case BinaryOperator::Pow:
    return os << "**";
  case BinaryOperator::AddAssign:
    return os << "+=";
  case BinaryOperator::SubAssign:
    return os << "-=";
  case BinaryOperator::MulAssign:
    return os << "*=";
  case BinaryOperator::DivAssign:
    return os << "/=";
  case BinaryOperator::ModAssign:
    return os << "%=";
  case BinaryOperator::PowAssign:
    return os << "**=";
  case BinaryOperator::Equal:
    return os << "==";
  case BinaryOperator::NotEqual:
    return os << "!=";
  case BinaryOperator::Less:
    return os << "<";
  case BinaryOperator::Greater:
    return os << ">";
  case BinaryOperator::LessEqual:
    return os << "<=";
  case BinaryOperator::GreaterEqual:
    return os << ">=";
  case BinaryOperator::And:
    return os << "&&";
  case BinaryOperator::Or:
    return os << "||";
  default:
    return os << "UNKNOWN_OPERATOR";
  }
}

struct BinaryExpression : public Expression {
  std::unique_ptr<Expression> left;
  BinaryOperator operator_; // Changed from string
  std::unique_ptr<Expression> right;

  BinaryExpression(std::unique_ptr<Expression> l, BinaryOperator op,
                   std::unique_ptr<Expression> r)
      : left(std::move(l)), operator_(op), right(std::move(r)) {
    kind = NodeType::BinaryExpression;
  }

  std::string toString() const override {
    return "BinaryExpr{" + (left ? left->toString() : "nullptr") + " " +
           toString(operator_) + " " + (right ? right->toString() : "nullptr") +
           "}";
  }

  std::string toString(BinaryOperator op) const {
    switch (op) {
    case BinaryOperator::Add:
      return "+";
    case BinaryOperator::Sub:
      return "-";
    case BinaryOperator::Mul:
      return "*";
    case BinaryOperator::Div:
      return "/";
    case BinaryOperator::Mod:
      return "%";
    case BinaryOperator::Pow:
      return "**";
    case BinaryOperator::AddAssign:
      return "+=";
    case BinaryOperator::SubAssign:
      return "-=";
    case BinaryOperator::MulAssign:
      return "*=";
    case BinaryOperator::DivAssign:
      return "/=";
    case BinaryOperator::ModAssign:
      return "%=";
    case BinaryOperator::PowAssign:
      return "**=";
    case BinaryOperator::Equal:
      return "==";
    case BinaryOperator::NotEqual:
      return "!=";
    case BinaryOperator::Less:
      return "<";
    case BinaryOperator::Greater:
      return ">";
    case BinaryOperator::LessEqual:
      return "<=";
    case BinaryOperator::GreaterEqual:
      return ">=";
    case BinaryOperator::And:
      return "&&";
    case BinaryOperator::Or:
      return "||";
    }
    return "UNKNOWN_OPERATOR";
  }
  void accept(ASTVisitor &visitor) const override;
};
// Program Node
struct Program : public Statement {
  std::vector<std::unique_ptr<Statement>> body;

  Program() { kind = NodeType::Program; }

  std::string toString() const override {
    return "Program{body: [" + std::to_string(body.size()) + " statements]}";
  }

  void accept(ASTVisitor &visitor) const override;
};

// Identifier
struct Identifier : public Expression {
  std::string symbol;

  Identifier(const std::string &sym) : symbol(sym) {
    kind = NodeType::Identifier;
  }

  std::string toString() const override { return "Identifier{" + symbol + "}"; }

  void accept(ASTVisitor &visitor) const override;
};

// Block Statement ({ ... })
struct BlockStatement : public Statement {
  std::vector<std::unique_ptr<Statement>> body;

  BlockStatement() { kind = NodeType::BlockStatement; }

  std::string toString() const override {
    return "Block{" + std::to_string(body.size()) + " statements}";
  }

  void accept(ASTVisitor &visitor) const override;
};

// Hotkey Binding (Havel-specific)
struct HotkeyBinding : public Statement {
  std::vector<std::unique_ptr<Expression>> hotkeys;
  std::unique_ptr<Statement> action;
  // Changed from Expression to Statement

  // Conditional support
  std::vector<std::string> conditions;  // e.g., ["mode gaming", "title genshin"]

  // Direct key mapping support (e.g., Left => A)
  bool isKeyMapping = false;
  std::string mappedKey;  // Target key for mapping

  HotkeyBinding() { kind = NodeType::HotkeyBinding; }
  HotkeyBinding(std::vector<std::unique_ptr<Expression>> hks,
                std::unique_ptr<Statement> act)
      : hotkeys(std::move(hks)), action(std::move(act)) {
    kind = NodeType::HotkeyBinding;
  }

  std::string toString() const override {
    std::string result = "HotkeyBinding{hotkeys: [";
    for (size_t i = 0; i < hotkeys.size(); ++i) {
      if (i > 0) result += ", ";
      result += hotkeys[i] ? hotkeys[i]->toString() : "nullptr";
    }
    result += "], action: " + (action ? action->toString() : "nullptr");
    if (!conditions.empty()) {
      result += ", conditions: [";
      for (size_t i = 0; i < conditions.size(); ++i) {
        if (i > 0) result += ", ";
        result += conditions[i];
      }
      result += "]";
    }
    if (isKeyMapping) {
      result += ", mapping to: " + mappedKey;
    }
    result += "}";
    return result;
  }

  void accept(ASTVisitor &visitor) const override;
};

// Conditional Hotkey - Hotkey binding with an expression-based condition
struct ConditionalHotkey : public Statement {
  std::unique_ptr<Expression> condition;
  std::unique_ptr<HotkeyBinding> binding;

  ConditionalHotkey(std::unique_ptr<Expression> cond, std::unique_ptr<HotkeyBinding> bind)
      : condition(std::move(cond)), binding(std::move(bind)) {
    kind = NodeType::ConditionalHotkey;
  }

  std::string toString() const override {
    return "ConditionalHotkey{condition: " +
           (condition ? condition->toString() : "nullptr") +
           ", binding: " + (binding ? binding->toString() : "nullptr") + "}";
  }

  void accept(ASTVisitor &visitor) const override;
};

// When Block - Group of hotkeys under a common condition
struct WhenBlock : public Statement {
  std::unique_ptr<Expression> condition;
  std::vector<std::unique_ptr<Statement>> statements;

  WhenBlock(std::unique_ptr<Expression> cond, std::vector<std::unique_ptr<Statement>> stmts)
      : condition(std::move(cond)), statements(std::move(stmts)) {
    kind = NodeType::WhenBlockStatement;
  }

  std::string toString() const override {
    return "WhenBlock{condition: " +
           (condition ? condition->toString() : "nullptr") +
           ", statements: [" + std::to_string(statements.size()) + "]}";
  }

  void accept(ASTVisitor &visitor) const override;
};

// Pipeline Expression (clipbooard.get | text.upper | send)
struct PipelineExpression : public Expression {
  std::vector<std::unique_ptr<Expression>> stages;

  PipelineExpression(std::vector<std::unique_ptr<Expression>> stgs = {})
      : stages(std::move(stgs)) {
    kind = NodeType::PipelineExpression;
  }

  std::string toString() const override {
    return "Pipeline{stages: " + std::to_string(stages.size()) + "}";
  }

  void accept(ASTVisitor &visitor) const override;
};

// Call Expression (send("Hello"))
struct CallExpression : public Expression {
  std::unique_ptr<Expression> callee;
  std::vector<std::unique_ptr<Expression>> args;

  CallExpression(std::unique_ptr<Expression> cal,
                 std::vector<std::unique_ptr<Expression>> ags = {})
      : callee(std::move(cal)), args(std::move(ags)) {
    kind = NodeType::CallExpression;
  }

  std::string toString() const override {
    return "CallExpr{" + (callee ? callee->toString() : "nullptr") + "(" +
           std::to_string(args.size()) + " args)}";
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
    return "MemberExpr{" + (object ? object->toString() : "nullptr") + "." +
           (property ? property->toString() : "nullptr") + "}";
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

// Interpolated String Expression ("Hello ${name}")
struct InterpolatedStringExpression : public Expression {
  // Alternating string segments and expressions
  // e.g., "Hello ${name}!" -> ["Hello ", name_expr, "!"]
  struct Segment {
    bool isString;  // true for string literal, false for expression
    std::string stringValue;  // if isString
    std::unique_ptr<Expression> expression;  // if !isString
    
    Segment(const std::string& str) : isString(true), stringValue(str) {}
    Segment(std::unique_ptr<Expression> expr) : isString(false), expression(std::move(expr)) {}
  };
  
  std::vector<Segment> segments;
  
  InterpolatedStringExpression(std::vector<Segment> segs = {})
      : segments(std::move(segs)) {
    kind = NodeType::InterpolatedStringExpression;
  }
  
  std::string toString() const override {
    return "InterpolatedString{" + std::to_string(segments.size()) + " segments}";
  }
  
  void accept(ASTVisitor &visitor) const override;
};

// Number Literal
struct NumberLiteral : public Expression {
  double value;

  NumberLiteral(double val) : value(val) { kind = NodeType::NumberLiteral; }

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
  ExpressionStatement(std::unique_ptr<Expression> expr)
      : expression(std::move(expr)) {
    kind = NodeType::ExpressionStatement;
  }

  std::string toString() const override {
    return "ExpressionStatement{" +
           (expression ? expression->toString() : "nullptr") + "}";
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
    return "LetDeclaration{name: " + (name ? name->toString() : "nullptr") +
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

  IfStatement(std::unique_ptr<Expression> cond, std::unique_ptr<Statement> cons,
              std::unique_ptr<Statement> alt = nullptr)
      : condition(std::move(cond)), consequence(std::move(cons)),
        alternative(std::move(alt)) {
    kind = NodeType::IfStatement;
  }

  std::string toString() const override {
    std::string str =
        "IfStatement{condition: " +
        (condition ? condition->toString() : "nullptr") +
        ", consequence: " + (consequence ? consequence->toString() : "nullptr");
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

  ReturnStatement(std::unique_ptr<Expression> arg = nullptr)
      : argument(std::move(arg)) {
    kind = NodeType::ReturnStatement;
  }

  std::string toString() const override {
    return "ReturnStatement{" + (argument ? argument->toString() : "void") +
           "}";
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
    return "WhileStatement{condition: " +
           (condition ? condition->toString() : "nullptr") +
           ", body: " + (body ? body->toString() : "nullptr") + "}";
  }

  void accept(ASTVisitor &visitor) const override;
};

// For Statement (for i in range { ... }) or (for (key, value) in dict { ... })
struct ForStatement : public Statement {
  std::vector<std::unique_ptr<Identifier>> iterators;
  std::unique_ptr<Expression> iterable;
  std::unique_ptr<Statement> body;

  ForStatement(std::vector<std::unique_ptr<Identifier>> iters,
               std::unique_ptr<Expression> itbl,
               std::unique_ptr<Statement> bd)
      : iterators(std::move(iters)), iterable(std::move(itbl)), body(std::move(bd)) {
    kind = NodeType::ForStatement;
  }

  // Legacy constructor for single iterator
  ForStatement(std::unique_ptr<Identifier> iter,
               std::unique_ptr<Expression> itbl,
               std::unique_ptr<Statement> bd)
      : iterable(std::move(itbl)), body(std::move(bd)) {
    kind = NodeType::ForStatement;
    if (iter) {
      iterators.push_back(std::move(iter));
    }
  }

  std::string toString() const override {
    std::string iterStr;
    if (iterators.size() == 1) {
      iterStr = iterators[0] ? iterators[0]->toString() : "nullptr";
    } else {
      iterStr += "(";
      for (size_t i = 0; i < iterators.size(); ++i) {
        if (i > 0) iterStr += ", ";
        iterStr += iterators[i] ? iterators[i]->toString() : "nullptr";
      }
      iterStr += ")";
    }
    return "ForStatement{iterators: " + iterStr +
           ", iterable: " + (iterable ? iterable->toString() : "nullptr") +
           ", body: " + (body ? body->toString() : "nullptr") + "}";
  }

  void accept(ASTVisitor &visitor) const override;
};

// Loop Statement (infinite loop)
struct LoopStatement : public Statement {
  std::unique_ptr<Statement> body;

  LoopStatement(std::unique_ptr<Statement> bd)
      : body(std::move(bd)) {
    kind = NodeType::LoopStatement;
  }

  std::string toString() const override {
    return "LoopStatement{body: " + (body ? body->toString() : "nullptr") + "}";
  }

  void accept(ASTVisitor &visitor) const override;
};

// Break Statement
struct BreakStatement : public Statement {
  BreakStatement() { kind = NodeType::BreakStatement; }

  std::string toString() const override {
    return "BreakStatement{}";
  }

  void accept(ASTVisitor &visitor) const override;
};

// Continue Statement
struct ContinueStatement : public Statement {
  ContinueStatement() { kind = NodeType::ContinueStatement; }

  std::string toString() const override {
    return "ContinueStatement{}";
  }

  void accept(ASTVisitor &visitor) const override;
};

// On Mode Statement (on mode gaming { ... } else { ... })
struct OnModeStatement : public Statement {
  std::string modeName;
  std::unique_ptr<Statement> body;
  std::unique_ptr<Statement> alternative; // Optional else block
  
  OnModeStatement(const std::string& mode, std::unique_ptr<Statement> bd,
                  std::unique_ptr<Statement> alt = nullptr)
      : modeName(mode), body(std::move(bd)), alternative(std::move(alt)) {
    kind = NodeType::OnModeStatement;
  }
  
  std::string toString() const override {
    return "OnModeStatement{mode: " + modeName + ", body: " +
           (body ? body->toString() : "nullptr") +
           (alternative ? ", else: " + alternative->toString() : "") + "}";
  }
  
  void accept(ASTVisitor &visitor) const override;
};

// Off Mode Statement (off mode gaming { ... })
struct OffModeStatement : public Statement {
  std::string modeName;
  std::unique_ptr<Statement> body;
  
  OffModeStatement(const std::string& mode, std::unique_ptr<Statement> bd)
      : modeName(mode), body(std::move(bd)) {
    kind = NodeType::OffModeStatement;
  }
  
  std::string toString() const override {
    return "OffModeStatement{mode: " + modeName + ", body: " +
           (body ? body->toString() : "nullptr") + "}";
  }
  
  void accept(ASTVisitor &visitor) const override;
};

// Function Declaration
struct FunctionDeclaration : public Statement {
  std::unique_ptr<Identifier> name;
  std::vector<std::unique_ptr<Identifier>> parameters;
  std::unique_ptr<BlockStatement> body;

  FunctionDeclaration(std::unique_ptr<Identifier> n,
                      std::vector<std::unique_ptr<Identifier>> params,
                      std::unique_ptr<BlockStatement> bd)
      : name(std::move(n)), parameters(std::move(params)), body(std::move(bd)) {
    kind = NodeType::FunctionDeclaration;
  }

  std::string toString() const override {
    std::string paramsStr;
    for (size_t i = 0; i < parameters.size(); ++i) {
      paramsStr += parameters[i]->toString();
      if (i < parameters.size() - 1)
        paramsStr += ", ";
    }
    return "FunctionDeclaration{name: " +
           (name ? name->toString() : "nullptr") + ", params: [" + paramsStr +
           "]" + ", body: " + (body ? body->toString() : "nullptr") + "}";
  }

  void accept(ASTVisitor &visitor) const override;
};
struct TryExpression : public Statement {
  std::unique_ptr<Expression> tryBody;
  std::unique_ptr<Expression> catchBody;

  TryExpression() { kind = NodeType::TryExpression; }

  std::string toString() const override { return "TryExpression{}"; }

  void accept(ASTVisitor &visitor) const override;
};
// Type Definition base class
struct TypeDefinition : public ASTNode {
  std::string toString() const override { return "TypeDefinition{}"; }
  void accept([[maybe_unused]] ASTVisitor &visitor) const override {}
};

// Simple type reference (e.g., Int, String, MyCustomType)
struct TypeReference : public TypeDefinition {
  std::string name;

  TypeReference(const std::string &typeName) : name(typeName) {
    kind = NodeType::TypeAnnotation; // or create a new NodeType::TypeReference
  }

  std::string toString() const override {
    return "TypeReference{" + name + "}";
  }

  void accept(ASTVisitor &visitor) const override;
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

  void accept(ASTVisitor &visitor) const override;
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

  void accept(ASTVisitor &visitor) const override;
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

  void accept(ASTVisitor &visitor) const override;
};

// Type Declaration statement (e.g., type Point = {x: Float, y: Float})
struct TypeDeclaration : public Statement {
  std::string name;
  std::unique_ptr<TypeDefinition> definition;

  TypeDeclaration(const std::string &typeName,
                  std::unique_ptr<TypeDefinition> def)
      : name(typeName), definition(std::move(def)) {
    kind = NodeType::TypeDeclaration;
  }

  std::string toString() const override {
    return "TypeDeclaration{name: " + name + ", definition: " +
           (definition ? definition->toString() : "nullptr") + "}";
  }

  void accept(ASTVisitor &visitor) const override;
};

// Type Annotation (e.g., : List(Int))
struct TypeAnnotation : public ASTNode {
  std::unique_ptr<TypeDefinition> type;

  TypeAnnotation(std::unique_ptr<TypeDefinition> t) : type(std::move(t)) {
    kind = NodeType::TypeAnnotation;
  }

  std::string toString() const override {
    return "TypeAnnotation{" + (type ? type->toString() : "nullptr") + "}";
  }

  void accept(ASTVisitor &visitor) const override;
};
struct UnaryExpression : public Expression {
    enum class UnaryOperator {
        Not,      // !expr, not expr
        Minus,    // -expr
        Plus      // +expr (unary plus)
    };
    
    UnaryOperator operator_;
    std::unique_ptr<Expression> operand;
    
    UnaryExpression(UnaryOperator op, std::unique_ptr<Expression> operand)
        : operator_(op), operand(std::move(operand)) {
        kind = NodeType::UnaryExpression;
    }
    
    std::string toString() const override {
        std::string opStr = (operator_ == UnaryOperator::Not) ? "!" :
                           (operator_ == UnaryOperator::Minus) ? "-" : "+";
        return "UnaryExpr{" + opStr + (operand ? operand->toString() : "nullptr") + "}";
    }
    
    void accept(ASTVisitor &visitor) const override;
};
// Array Literal ([1, 2, 3])
struct ArrayLiteral : public Expression {
  std::vector<std::unique_ptr<Expression>> elements;

  ArrayLiteral(std::vector<std::unique_ptr<Expression>> elems = {})
      : elements(std::move(elems)) {
    kind = NodeType::ArrayLiteral;
  }

  std::string toString() const override {
    return "ArrayLiteral{" + std::to_string(elements.size()) + " elements}";
  }

  void accept(ASTVisitor &visitor) const override;
};

// Object Literal ({key: value, ...})
struct ObjectLiteral : public Expression {
  std::vector<std::pair<std::string, std::unique_ptr<Expression>>> pairs;

  ObjectLiteral(std::vector<std::pair<std::string, std::unique_ptr<Expression>>> p = {})
      : pairs(std::move(p)) {
    kind = NodeType::ObjectLiteral;
  }

  std::string toString() const override {
    return "ObjectLiteral{" + std::to_string(pairs.size()) + " pairs}";
  }

  void accept(ASTVisitor &visitor) const override;
};

// Config Block (config { ... })
struct ConfigBlock : public Statement {
  std::vector<std::pair<std::string, std::unique_ptr<Expression>>> pairs;

  ConfigBlock(std::vector<std::pair<std::string, std::unique_ptr<Expression>>> p = {})
      : pairs(std::move(p)) {
    kind = NodeType::ConfigBlock;
  }

  std::string toString() const override {
    return "ConfigBlock{" + std::to_string(pairs.size()) + " pairs}";
  }

  void accept(ASTVisitor &visitor) const override;
};

// Devices Block (devices { ... })
struct DevicesBlock : public Statement {
  std::vector<std::pair<std::string, std::unique_ptr<Expression>>> pairs;

  DevicesBlock(std::vector<std::pair<std::string, std::unique_ptr<Expression>>> p = {})
      : pairs(std::move(p)) {
    kind = NodeType::DevicesBlock;
  }

  std::string toString() const override {
    return "DevicesBlock{" + std::to_string(pairs.size()) + " pairs}";
  }

  void accept(ASTVisitor &visitor) const override;
};

// Modes Block (modes { ... })
struct ModesBlock : public Statement {
  std::vector<std::pair<std::string, std::unique_ptr<Expression>>> pairs;

  ModesBlock(std::vector<std::pair<std::string, std::unique_ptr<Expression>>> p = {})
      : pairs(std::move(p)) {
    kind = NodeType::ModesBlock;
  }

  std::string toString() const override {
    return "ModesBlock{" + std::to_string(pairs.size()) + " pairs}";
  }

  void accept(ASTVisitor &visitor) const override;
};

// Update Expression (i++, --i)
struct UpdateExpression : public Expression {
    std::unique_ptr<Expression> argument;
    bool isPrefix;
    enum class Operator { Increment, Decrement } operator_;

    UpdateExpression(std::unique_ptr<Expression> arg, Operator op, bool prefix)
        : argument(std::move(arg)), operator_(op), isPrefix(prefix) {
        kind = NodeType::UpdateExpression;
    }

    std::string toString() const override {
        std::string opStr = (operator_ == Operator::Increment) ? "++" : "--";
        if (isPrefix) return "UpdateExpr{" + opStr + (argument ? argument->toString() : "nullptr") + "}";
        else return "UpdateExpr{" + (argument ? argument->toString() : "nullptr") + opStr + "}";
    }

    void accept(ASTVisitor &visitor) const override;
};

// Lambda (arrow) Function Expression (() => { ... } or x => expr)
struct LambdaExpression : public Expression {
  std::vector<std::unique_ptr<Identifier>> parameters;
  std::unique_ptr<Statement> body; // BlockStatement or ExpressionStatement

  LambdaExpression(std::vector<std::unique_ptr<Identifier>> params,
                   std::unique_ptr<Statement> bd)
      : parameters(std::move(params)), body(std::move(bd)) {
    kind = NodeType::LambdaExpression;
  }

  std::string toString() const override {
    return "Lambda{" + std::to_string(parameters.size()) + " params}";
  }

  void accept(ASTVisitor &visitor) const override;
};

// Index Expression (arr[0] or obj["key"])
struct IndexExpression : public Expression {
  std::unique_ptr<Expression> object;
  std::unique_ptr<Expression> index;

  IndexExpression(std::unique_ptr<Expression> obj, std::unique_ptr<Expression> idx)
      : object(std::move(obj)), index(std::move(idx)) {
    kind = NodeType::IndexExpression;
  }

  std::string toString() const override {
    return "IndexExpression{" + 
           (object ? object->toString() : "nullptr") + "[" +
           (index ? index->toString() : "nullptr") + "]}";
  }

  void accept(ASTVisitor &visitor) const override;
};

// Ternary Expression (condition ? trueValue : falseValue)
struct TernaryExpression : public Expression {
  std::unique_ptr<Expression> condition;
  std::unique_ptr<Expression> trueValue;
  std::unique_ptr<Expression> falseValue;

  TernaryExpression(std::unique_ptr<Expression> cond, 
                    std::unique_ptr<Expression> tVal,
                    std::unique_ptr<Expression> fVal)
      : condition(std::move(cond)), trueValue(std::move(tVal)), falseValue(std::move(fVal)) {
    kind = NodeType::TernaryExpression;
  }

  std::string toString() const override {
    return "TernaryExpression{" + 
           (condition ? condition->toString() : "nullptr") + " ? " +
           (trueValue ? trueValue->toString() : "nullptr") + " : " +
           (falseValue ? falseValue->toString() : "nullptr") + "}";
  }

  void accept(ASTVisitor &visitor) const override;
};

// Range Expression (0..10)
struct RangeExpression : public Expression {
  std::unique_ptr<Expression> start;
  std::unique_ptr<Expression> end;

  RangeExpression(std::unique_ptr<Expression> s, std::unique_ptr<Expression> e)
      : start(std::move(s)), end(std::move(e)) {
    kind = NodeType::RangeExpression;
  }

  std::string toString() const override {
    return "RangeExpression{" + 
           (start ? start->toString() : "nullptr") + ".." +
           (end ? end->toString() : "nullptr") + "}";
  }

  void accept(ASTVisitor &visitor) const override;
};

// Assignment Expression (identifier = value)
struct AssignmentExpression : public Expression {
  std::unique_ptr<Expression> target;  // What we're assigning to
  std::unique_ptr<Expression> value;   // The new value
  std::string operator_;               // "=" for now

  AssignmentExpression(std::unique_ptr<Expression> t, 
                       std::unique_ptr<Expression> v,
                       std::string op = "=")
      : target(std::move(t)), value(std::move(v)), operator_(std::move(op)) {
    kind = NodeType::AssignmentExpression;
  }

  std::string toString() const override {
    return "AssignmentExpression{" + 
           (target ? target->toString() : "nullptr") + " " + operator_ + " " +
           (value ? value->toString() : "nullptr") + "}";
  }

  void accept(ASTVisitor &visitor) const override;
};

// Import Statement (import List from "std/collections")
struct ImportStatement : public Statement {
  std::string modulePath;
  // pair of <OriginalName, Alias>
  std::vector<std::pair<std::string, std::string>> importedItems;

  ImportStatement(const std::string &path,
                  std::vector<std::pair<std::string, std::string>> items = {})
      : modulePath(path), importedItems(std::move(items)) {
    kind = NodeType::ImportStatement;
  }

  std::string toString() const override {
    std::string result = "ImportStatement{module: " + modulePath;
    if (!importedItems.empty()) {
      result += ", items: [";
      for (size_t i = 0; i < importedItems.size(); ++i) {
        result += importedItems[i].first;
        if (importedItems[i].first != importedItems[i].second) {
            result += " as " + importedItems[i].second;
        }
        if (i < importedItems.size() - 1)
          result += ", ";
      }
      result += "]";
    }
    result += "}";
    return result;
  }

  void accept(ASTVisitor &visitor) const override;
};
// Visitor pattern interface for AST traversal
class ASTVisitor {
public:
  virtual ~ASTVisitor() = default;

  virtual void visitProgram(const Program &node) = 0;

  virtual void visitHotkeyBinding(const HotkeyBinding &node) = 0;

  virtual void visitPipelineExpression(const PipelineExpression &node) = 0;

  virtual void visitBinaryExpression(const BinaryExpression &node) = 0;

  virtual void visitCallExpression(const CallExpression &node) = 0;
  
  virtual void visitMemberExpression(const MemberExpression &node) = 0;
  
  virtual void visitLambdaExpression(const LambdaExpression &node) = 0;

  virtual void visitStringLiteral(const StringLiteral &node) = 0;

  virtual void visitInterpolatedStringExpression(const InterpolatedStringExpression &node) = 0;

  virtual void visitNumberLiteral(const NumberLiteral &node) = 0;

  virtual void visitIdentifier(const Identifier &node) = 0;

  virtual void visitHotkeyLiteral(const HotkeyLiteral &node) = 0;

  virtual void visitBlockStatement(const BlockStatement &node) = 0;

  virtual void visitExpressionStatement(const ExpressionStatement &node) = 0;

  virtual void visitIfStatement(const IfStatement &node) = 0;

  virtual void visitLetDeclaration(const LetDeclaration &node) = 0;

  virtual void visitReturnStatement(const ReturnStatement &node) = 0;

  virtual void visitWhileStatement(const WhileStatement &node) = 0;

  virtual void visitFunctionDeclaration(const FunctionDeclaration &node) = 0;

  virtual void visitTypeDeclaration(const TypeDeclaration &node) = 0;
  virtual void visitTypeAnnotation(const TypeAnnotation &node) = 0;
  virtual void visitUnionType(const UnionType &node) = 0;
  virtual void visitRecordType(const RecordType &node) = 0;
  virtual void visitFunctionType(const FunctionType &node) = 0;
  virtual void visitTypeReference(const TypeReference &node) = 0;
  virtual void visitTryExpression(const TryExpression &node) = 0;
  virtual void visitUnaryExpression(const UnaryExpression &node) = 0;
  virtual void visitUpdateExpression(const UpdateExpression &node) = 0;
  virtual void visitImportStatement(const ImportStatement& node) = 0;
  virtual void visitArrayLiteral(const ArrayLiteral& node) = 0;
  virtual void visitObjectLiteral(const ObjectLiteral& node) = 0;
  virtual void visitConfigBlock(const ConfigBlock& node) = 0;
  virtual void visitDevicesBlock(const DevicesBlock& node) = 0;
  virtual void visitModesBlock(const ModesBlock& node) = 0;
  virtual void visitIndexExpression(const IndexExpression& node) = 0;
  virtual void visitTernaryExpression(const TernaryExpression& node) = 0;
  virtual void visitRangeExpression(const RangeExpression& node) = 0;
  virtual void visitAssignmentExpression(const AssignmentExpression& node) = 0;
  virtual void visitForStatement(const ForStatement& node) = 0;
  virtual void visitLoopStatement(const LoopStatement& node) = 0;
  virtual void visitBreakStatement(const BreakStatement& node) = 0;
  virtual void visitContinueStatement(const ContinueStatement& node) = 0;
  virtual void visitOnModeStatement(const OnModeStatement& node) = 0;
  virtual void visitOffModeStatement(const OffModeStatement& node) = 0;
  virtual void visitConditionalHotkey(const ConditionalHotkey& node) = 0;
  virtual void visitWhenBlock(const WhenBlock& node) = 0;
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

inline void LambdaExpression::accept(ASTVisitor &visitor) const {
  visitor.visitLambdaExpression(*this);
}

inline void StringLiteral::accept(ASTVisitor &visitor) const {
  visitor.visitStringLiteral(*this);
}

inline void InterpolatedStringExpression::accept(ASTVisitor &visitor) const {
  visitor.visitInterpolatedStringExpression(*this);
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

inline void ArrayLiteral::accept(ASTVisitor &visitor) const {
  visitor.visitArrayLiteral(*this);
}

inline void ObjectLiteral::accept(ASTVisitor &visitor) const {
  visitor.visitObjectLiteral(*this);
}

inline void ConfigBlock::accept(ASTVisitor &visitor) const {
  visitor.visitConfigBlock(*this);
}

inline void DevicesBlock::accept(ASTVisitor &visitor) const {
  visitor.visitDevicesBlock(*this);
}

inline void ModesBlock::accept(ASTVisitor &visitor) const {
  visitor.visitModesBlock(*this);
}

inline void IndexExpression::accept(ASTVisitor &visitor) const {
  visitor.visitIndexExpression(*this);
}

inline void TernaryExpression::accept(ASTVisitor &visitor) const {
  visitor.visitTernaryExpression(*this);
}

inline void RangeExpression::accept(ASTVisitor &visitor) const {
  visitor.visitRangeExpression(*this);
}

inline void AssignmentExpression::accept(ASTVisitor &visitor) const {
  visitor.visitAssignmentExpression(*this);
}

inline void ForStatement::accept(ASTVisitor &visitor) const {
  visitor.visitForStatement(*this);
}

inline void LoopStatement::accept(ASTVisitor &visitor) const {
  visitor.visitLoopStatement(*this);
}

inline void BreakStatement::accept(ASTVisitor &visitor) const {
  visitor.visitBreakStatement(*this);
}

inline void ContinueStatement::accept(ASTVisitor &visitor) const {
  visitor.visitContinueStatement(*this);
}

inline void OnModeStatement::accept(ASTVisitor &visitor) const {
  visitor.visitOnModeStatement(*this);
}

inline void OffModeStatement::accept(ASTVisitor &visitor) const {
  visitor.visitOffModeStatement(*this);
}

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

inline void UpdateExpression::accept(ASTVisitor &visitor) const {
  visitor.visitUpdateExpression(*this);
}

inline void ImportStatement::accept(ASTVisitor& visitor) const {
  visitor.visitImportStatement(*this);
}

inline void ConditionalHotkey::accept(ASTVisitor& visitor) const {
  visitor.visitConditionalHotkey(*this);
}

inline void WhenBlock::accept(ASTVisitor& visitor) const {
  visitor.visitWhenBlock(*this);
}
} // namespace havel::ast
