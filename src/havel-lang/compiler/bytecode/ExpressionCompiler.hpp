#pragma once

#include "CodeEmitter.hpp"
#include "BindingResolver.hpp"
#include "LexicalResolver.hpp"
#include <unordered_set>
#include <unordered_map>

namespace havel::compiler {

// Forward declarations
class ByteCompiler;

// ============================================================================
// ExpressionCompiler - Handles compilation of AST expressions to bytecode
// ============================================================================
class ExpressionCompiler {
public:
  ExpressionCompiler(CodeEmitter& emitter,
                     BindingResolver& bindingResolver,
                     const LexicalResolutionResult& lexicalResult);

  // Main compilation entry point
  void compile(const ast::Expression& expression);

  // Specific expression types
  void compileIdentifier(const ast::Identifier& id);
  void compileBinaryExpression(const ast::BinaryExpression& binary);
  void compileUnaryExpression(const ast::UnaryExpression& unary);
  void compileCallExpression(const ast::CallExpression& call);
  void compileAssignmentExpression(const ast::AssignmentExpression& assignment);
  void compileMemberExpression(const ast::MemberExpression& member);
  void compileIndexExpression(const ast::IndexExpression& index);
  void compileLambdaExpression(const ast::LambdaExpression& lambda);
  void compileArrayLiteral(const ast::ArrayLiteral& array);
  void compileObjectLiteral(const ast::ObjectLiteral& object);
  void compileTernaryExpression(const ast::TernaryExpression& ternary);
  void compileUpdateExpression(const ast::UpdateExpression& update);
  void compileAwaitExpression(const ast::AwaitExpression& await);
  void compileSpreadExpression(const ast::SpreadExpression& spread);
  void compileRangeExpression(const ast::RangeExpression& range);
  void compilePipelineExpression(const ast::PipelineExpression& pipeline);
  void compileInterpolatedString(const ast::InterpolatedStringExpression& interp);

  // Helper methods
  void emitLoadVar(uint32_t slot);
  void emitStoreVar(uint32_t slot);
  void emitLoadUpvalue(uint32_t slot);
  void emitStoreUpvalue(uint32_t slot);
  void emitLoadGlobal(const std::string& name);
  void emitStoreGlobal(const std::string& name);
  void emitLoadConst(const BytecodeValue& value);

private:
  CodeEmitter& emitter_;
  BindingResolver& bindingResolver_;
  const LexicalResolutionResult& lexicalResult_;
  std::unordered_map<std::string, uint32_t> topLevelFunctionIndices_;
  std::unordered_set<std::string> hostGlobalNames_;

  // Internal helpers
  OpCode binaryOpToBytecode(ast::BinaryOperator op) const;
  bool isAssignmentOp(ast::BinaryOperator op) const;
  void emitCompoundAssignment(const ast::Identifier& target,
                              const ast::Expression& value,
                              OpCode mathOp);
};

// ============================================================================
// StatementCompiler - Handles compilation of AST statements to bytecode
// ============================================================================
class StatementCompiler {
public:
  StatementCompiler(CodeEmitter& emitter,
                    ExpressionCompiler& exprCompiler,
                    BindingResolver& bindingResolver);

  // Main compilation entry point
  void compile(const ast::Statement& statement);

  // Specific statement types
  void compileExpressionStatement(const ast::ExpressionStatement& stmt);
  void compileLetDeclaration(const ast::LetDeclaration& let);
  void compileIfStatement(const ast::IfStatement& ifStmt);
  void compileWhileStatement(const ast::WhileStatement& whileStmt);
  void compileDoWhileStatement(const ast::DoWhileStatement& doWhile);
  void compileForStatement(const ast::ForStatement& forStmt);
  void compileLoopStatement(const ast::LoopStatement& loop);
  void compileReturnStatement(const ast::ReturnStatement& ret);
  void compileBlockStatement(const ast::BlockStatement& block);
  void compileTryStatement(const ast::TryExpression& tryExpr);
  void compileThrowStatement(const ast::ThrowStatement& throwStmt);
  void compileWhenBlock(const ast::WhenBlock& when);
  void compileModeBlock(const ast::ModeBlock& mode);
  void compileHotkeyBinding(const ast::HotkeyBinding& hotkey);
  void compileConditionalHotkey(const ast::ConditionalHotkey& condHotkey);
  void compileInputStatement(const ast::InputStatement& input);
  void compileUseStatement(const ast::UseStatement& use);
  void compileExportStatement(const ast::ExportStatement& exportStmt);

private:
  CodeEmitter& emitter_;
  ExpressionCompiler& exprCompiler_;
  BindingResolver& bindingResolver_;

  // Loop context for break/continue
  struct LoopContext {
    std::vector<uint32_t> breakJumps;
    std::vector<uint32_t> continueJumps;
    uint32_t loopStart = 0;
  };
  std::vector<LoopContext> loopStack_;

  void pushLoopContext(uint32_t start);
  void popLoopContext();
  void patchBreaks(uint32_t target);
  void patchContinues(uint32_t target);
};

// ============================================================================
// PatternCompiler - Handles compilation of destructuring patterns
// ============================================================================
class PatternCompiler {
public:
  PatternCompiler(CodeEmitter& emitter, BindingResolver& bindingResolver);

  // Compile pattern matching with value already on stack
  void compilePatternMatch(const ast::Expression& pattern, uint32_t valueSlot);

  // Collect all identifiers from a pattern
  std::vector<const ast::Identifier*> collectIdentifiers(const ast::Expression& pattern) const;

  // Declare pattern variables in scope
  void declarePatternVariables(const ast::Expression& pattern);

private:
  CodeEmitter& emitter_;
  BindingResolver& bindingResolver_;

  void compileArrayPattern(const ast::ArrayPattern& pattern, uint32_t valueSlot);
  void compileObjectPattern(const ast::ObjectPattern& pattern, uint32_t valueSlot);
  void compileIdentifierPattern(const ast::Identifier& ident);
};

// ============================================================================
// FunctionCompiler - Handles function and lambda compilation
// ============================================================================
class FunctionCompiler {
public:
  FunctionCompiler(CodeEmitter& emitter,
                   StatementCompiler& stmtCompiler,
                   ExpressionCompiler& exprCompiler,
                   PatternCompiler& patternCompiler,
                   BindingResolver& bindingResolver);

  // Compile function declaration
  uint32_t compileFunction(const ast::FunctionDeclaration& function);

  // Compile lambda expression
  uint32_t compileLambda(const ast::LambdaExpression& lambda);

  // Compile parameters with pattern support
  void compileParameters(const std::vector<std::unique_ptr<ast::FunctionParameter>>& params);

private:
  CodeEmitter& emitter_;
  StatementCompiler& stmtCompiler_;
  ExpressionCompiler& exprCompiler_;
  PatternCompiler& patternCompiler_;
  BindingResolver& bindingResolver_;

  void compileFunctionBody(const ast::BlockStatement& body);
  void emitPrologue();
  void emitEpilogue();
};

} // namespace havel::compiler
