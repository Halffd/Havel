#pragma once

#include "../../ast/AST.h"
#include "BytecodeIR.hpp"
#include "../semantic/LexicalResolver.hpp"
#include "../module/ModuleLoader.hpp"
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Qt defines 'emit' as a macro - we need to undefine it for our method name
#ifdef emit
#undef emit
#endif

namespace havel::compiler {

// Exception that carries source location through stack unwind
struct CompilerError : std::runtime_error {
  uint32_t line = 0, column = 0;
  CompilerError(const std::string& msg, uint32_t l = 0, uint32_t c = 0)
      : std::runtime_error(msg), line(l), column(c) {}
};

class ByteCompiler;
class HostBridge;
class VM;

class ByteCompiler : public BytecodeCompiler {
public:
  std::unique_ptr<BytecodeChunk> compile(const ast::Program &program) override;
  std::unique_ptr<BytecodeChunk>
  compileWithModuleLoader(const ast::Program &program, ModuleLoader &loader,
                          const std::filesystem::path &basePath);
  void addHostGlobal(std::string name) {
    host_global_names_.insert(std::move(name));
  }
  // Pre-populate known global names (for REPL persistence across compiles)
  void setKnownGlobals(const std::unordered_set<std::string> &names) {
    known_globals_ = names;
  }
  const LexicalResolutionResult &lexicalResolution() const {
    return lexical_resolution_;
  }
  // Extract current chunk even if compilation failed (for debugging)
  std::unique_ptr<BytecodeChunk> takeCurrentChunk() {
    if (current_function) {
      leaveFunction();
    }
    // Add any compiled functions to the chunk before taking it
    for (auto &fn : compiled_functions) {
      if (fn) {
        chunk->addFunction(std::move(*fn));
      }
    }
    compiled_functions.clear();
    return std::move(chunk);
  }

  // Set HostBridge for lazy module loading
  void setHostBridge(HostBridge *bridge) { host_bridge_ = bridge; }

  // Error collection for linting
  bool hasErrors() const { return has_error_; }
  const std::vector<CompilerError> &errors() const { return errors_; }
  void setCollectErrors(bool collect) { collect_errors_ = collect; }

  // Shadow helpers so COMPILER_THROW macro picks up member location
  uint32_t _compiler_err_line() const {
    return current_source_location_ ? current_source_location_->line : 0;
  }
  uint32_t _compiler_err_col() const {
    return current_source_location_ ? current_source_location_->column : 0;
  }

private:
  std::unique_ptr<BytecodeChunk> compileImpl(const ast::Program &program);
  struct SourceLocationScope {
    ByteCompiler *compiler = nullptr;
    std::optional<SourceLocation> previous;
    SourceLocationScope(ByteCompiler *owner, const ast::ASTNode &node)
        : compiler(owner), previous(owner->current_source_location_) {
      owner->current_source_location_ = SourceLocation{
          "", static_cast<uint32_t>(node.line), static_cast<uint32_t>(node.column), static_cast<uint32_t>(node.length)};
    }
    ~SourceLocationScope() { compiler->current_source_location_ = previous; }
  };

  SourceLocationScope atNode(const ast::ASTNode &node) {
    return SourceLocationScope(this, node);
  }

void emit(OpCode op);
void emit(OpCode op, Value operand);
void emit(OpCode op, std::vector<Value> operands);
uint32_t addConstant(const Value &value);
uint32_t addStringConstant(const std::string &str);
uint32_t emitJump(OpCode op);
void patchJump(uint32_t jump_instruction_index, uint32_t target);
uint32_t effectiveSlot(uint32_t slot) const;
void optimizeJumps();  // Jump threading optimization

  void compileFunction(const ast::FunctionDeclaration &function);
  void compileLambda(const ast::LambdaExpression &lambda);
  void compileClassMethod(const std::string &class_name,
                          const ast::ClassMethodDef &method,
                          const std::vector<ast::ClassFieldDef> &fields,
                          const std::string &parent_class_name);
  void compileParameterPattern(const ast::Expression &pattern,
                               uint32_t paramIndex);
  void compileParameterPatternValue(const ast::Expression &pattern);
  void collectParameterPatternSlots(const ast::Expression &pattern);
  void collectFunctionDeclarations(
      const ast::Statement &statement,
      std::vector<const ast::FunctionDeclaration *> &out) const;
  void collectLambdaExpressions(
      const ast::Statement &statement,
      std::vector<const ast::LambdaExpression *> &out) const;
  void collectLambdaExpressions(
      const ast::Expression &expression,
      std::vector<const ast::LambdaExpression *> &out) const;
  void compileStatement(const ast::Statement &statement);
  void compileUseStatement(const ast::UseStatement &statement);
  void compileExportStatement(const ast::ExportStatement &statement);
  void compileExpression(const ast::Expression &expression);
  void compilePattern(const ast::Expression &pattern, uint32_t discSlot);
  void compileHotkeyBinding(const ast::HotkeyBinding &binding);
  void compileHotkeyBindingExpr(const ast::HotkeyBinding &binding);
  void compileWhenBlock(const ast::WhenBlock &whenBlock);
  void compileInputStatement(const ast::InputStatement &statement);
  void compileShellCommandStatement(const ast::ShellCommandStatement &statement);
  void compileWaitStatement(const ast::WaitStatement &statement);
  void compileGetInputExpression(const ast::GetInputExpression &expression);
  void compileTryStatement(const ast::TryExpression &statement);
  void compileCallExpression(const ast::CallExpression &expression);
  void compileCallExpressionTail(const ast::CallExpression &expression); // TCO
  void compileThreadExpression(const ast::ThreadExpression &expression);
  void compileIntervalExpression(const ast::IntervalExpression &expression);
  void compileTimeoutExpression(const ast::TimeoutExpression &expression);
  void compileYieldExpression(const ast::YieldExpression &expression);
  void compileGoStatement(const ast::GoStatement &statement);
  void compileGoExpression(const ast::GoExpression &expression);
  void compileDelTarget(const ast::Expression &target);
  void compileChannelExpression(const ast::ChannelExpression &expression);
  void compileIfStatement(const ast::IfStatement &statement);
  void compileWhileStatement(const ast::WhileStatement &statement);
  void compileDoWhileStatement(const ast::DoWhileStatement &statement);
  void compileForStatement(const ast::ForStatement &statement);
  void compileLoopStatement(const ast::LoopStatement &statement);
  void compileBlockStatement(const ast::BlockStatement &block);
  // Closure body compilation helpers
  void compileClosureBody(const ast::Statement &body, const std::string &name);
void collectUpvaluesFromBody(const ast::Statement &stmt, std::vector<UpvalueDescriptor> &upvalues);
void collectUpvaluesFromExpr(const ast::Expression &expr, std::vector<UpvalueDescriptor> &upvalues);
std::optional<std::string> getCalleeName(const ast::Expression &callee) const;
std::optional<std::string>
normalizeTypeAnnotation(const ast::TypeAnnotation *annotation) const;
uint64_t typeHintFromAnnotation(const ast::TypeAnnotation *annotation) const;
void setTypeFeedbackHint(uint32_t ip, uint64_t type_mask);
void emitTypeAssertionForLocal(const std::string &normalized_expected,
                                uint32_t slot,
                                const std::string &label);
const ResolvedBinding *bindingFor(const ast::Identifier &id) const;
  uint32_t declarationSlot(const ast::Identifier &id) const;
  void reserveLocalSlot(uint32_t slot);

  // Phase 3B-3: Generator detection
  bool functionContainsYield(const ast::BlockStatement &body) const;
  bool statementContainsYield(const ast::Statement &stmt) const;
  bool expressionContainsYield(const ast::Expression &expr) const;

  void enterFunction(BytecodeFunction &&function,
                     std::optional<uint32_t> slot = std::nullopt);
  void leaveFunction();
  void resetLocals();

  // Tail call optimization - track tail position context
  void enterTailPosition();
  void exitTailPosition();
  bool isInTailPosition() const;
  bool wasTailCall() const;
  void clearTailCallFlag();

  std::unique_ptr<BytecodeChunk> chunk;
  std::unique_ptr<BytecodeFunction> current_function;
  std::vector<std::unique_ptr<BytecodeFunction>> compiled_functions;
  // Stack for saving function contexts during nested function compilation
  std::vector<
      std::pair<std::unique_ptr<BytecodeFunction>, std::optional<uint32_t>>>
      saved_functions_;
  std::unordered_map<const ast::FunctionDeclaration *, uint32_t>
      function_indices_by_node_;
  std::unordered_map<const ast::ClassMethodDef *, uint32_t>
      class_method_indices_by_node_;
  std::unordered_map<const ast::LambdaExpression *, uint32_t>
      lambda_indices_by_node_;
  std::optional<uint32_t> current_function_slot_;
  std::unordered_map<std::string, uint32_t> top_level_function_indices_by_name_;
  std::unordered_set<std::string> top_level_struct_names_;
  std::unordered_set<std::string> top_level_class_names_;
  uint32_t next_local_index = 0;
  std::optional<SourceLocation> current_source_location_;
  LexicalResolutionResult lexical_resolution_;
  std::unordered_set<std::string> host_global_names_{
      "print",   "sleep",    "sleep_ms", "clock_ms", "clock_ns", "clock_us",
      "assert",  "time.now", "window",   "io",       "system",   "hotkey",
      "mode",    "process",  "display",  "async",    "struct",   "thread",   "interval",
      "timeout", "string",   "array",    "object",   "type",     "utility",  "inherits",
      "regex",   "physics",  "time",     "math",
      // Bare process globals
      "run", "runDetached",
      // Reflection and runtime evaluation
      "eval", "caller", "describe", "bytecode", "tokenize",
      "inspect", "prototypes", "defun",
      // Prototype OOP primitives
      "proto", "getproto", "setproto", "del",
      // Duck typing / protocol functions
      "iter",    "next",     "callable", "hasattr",  "isIterable", "isIndexable",
      "items",   "capital"};

  // Pre-known globals (from previous REPL sessions)
  std::unordered_set<std::string> known_globals_;

  // Module loading
  ModuleLoader *module_loader_ = nullptr;
  std::filesystem::path base_path_;
  std::unordered_map<std::string, uint32_t> module_function_indices_by_name_;
  std::unordered_map<std::string, uint32_t> module_class_indices_by_name_;
  std::string current_class_name_;
  std::string current_parent_class_name_;
  uint32_t local_slot_offset_ = 0; // Offset for local variable slots (used in class methods)

  // Tail call optimization state
  bool in_tail_position_ = false;
  bool emitted_tail_call_ = false;

  // HostBridge for lazy module loading and permission checks
  HostBridge *host_bridge_ = nullptr;

  // Error collection for linting
  bool collect_errors_ = false;
  bool has_error_ = false;
  std::vector<CompilerError> errors_;
};

} // namespace havel::compiler
