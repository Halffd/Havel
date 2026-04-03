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

class ByteCompiler : public BytecodeCompiler {
public:
  std::unique_ptr<BytecodeChunk> compile(const ast::Program &program) override;
  std::unique_ptr<BytecodeChunk>
  compileWithModuleLoader(const ast::Program &program, ModuleLoader &loader,
                          const std::filesystem::path &basePath);
  void addHostGlobal(std::string name) {
    host_global_names_.insert(std::move(name));
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

private:
  struct SourceLocationScope {
    ByteCompiler *compiler = nullptr;
    std::optional<SourceLocation> previous;
    SourceLocationScope(ByteCompiler *owner, const ast::ASTNode &node)
        : compiler(owner), previous(owner->current_source_location_) {
      owner->current_source_location_ = SourceLocation{
          "", static_cast<uint32_t>(node.line), static_cast<uint32_t>(node.column)};
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

  void compileFunction(const ast::FunctionDeclaration &function);
  void compileLambda(const ast::LambdaExpression &lambda);
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
  void compileHotkeyBinding(const ast::HotkeyBinding &binding);
  void compileWhenBlock(const ast::WhenBlock &whenBlock);
  void compileInputStatement(const ast::InputStatement &statement);
  void compileTryStatement(const ast::TryExpression &statement);
  void compileCallExpression(const ast::CallExpression &expression);
  void compileCallExpressionTail(const ast::CallExpression &expression); // TCO
  void compileIfStatement(const ast::IfStatement &statement);
  void compileWhileStatement(const ast::WhileStatement &statement);
  void compileDoWhileStatement(const ast::DoWhileStatement &statement);
  void compileForStatement(const ast::ForStatement &statement);
  void compileLoopStatement(const ast::LoopStatement &statement);
  void compileBlockStatement(const ast::BlockStatement &block);
  std::optional<std::string> getCalleeName(const ast::Expression &callee) const;
  const ResolvedBinding *bindingFor(const ast::Identifier &id) const;
  uint32_t declarationSlot(const ast::Identifier &id) const;
  void reserveLocalSlot(uint32_t slot);

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
  std::unordered_map<const ast::LambdaExpression *, uint32_t>
      lambda_indices_by_node_;
  std::optional<uint32_t> current_function_slot_;
  std::unordered_map<std::string, uint32_t> top_level_function_indices_by_name_;
  std::unordered_set<std::string> top_level_struct_names_;
  uint32_t next_local_index = 0;
  std::optional<SourceLocation> current_source_location_;
  LexicalResolutionResult lexical_resolution_;
  std::unordered_set<std::string> host_global_names_{
      "print",   "sleep",    "sleep_ms", "clock_ms", "clock_ns", "clock_us",
      "assert",  "time.now", "window",   "io",       "system",   "hotkey",
      "mode",    "process",  "async",    "struct",   "thread",   "interval",
      "timeout", "string",   "array",    "object",   "type",     "utility",
      "regex",   "physics",  "time",     "math"};

  // Module loading
  ModuleLoader *module_loader_ = nullptr;
  std::filesystem::path base_path_;
  std::unordered_map<std::string, uint32_t> module_function_indices_by_name_;
  std::unordered_map<std::string, uint32_t> module_class_indices_by_name_;

  // Tail call optimization state
  bool in_tail_position_ = false;
  bool emitted_tail_call_ = false;
};

} // namespace havel::compiler
