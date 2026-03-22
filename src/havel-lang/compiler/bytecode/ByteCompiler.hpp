#pragma once

#include "LexicalResolver.hpp"
#include "BytecodeIR.hpp"
#include "../../ast/AST.h"
#include <optional>
#include <memory>
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
    std::unique_ptr<BytecodeChunk> compile(const ast::Program& program) override;
    void addHostBuiltin(std::string name) {
      auto dot = name.find('.');
      if (dot != std::string::npos && dot > 0) {
        host_global_names_.insert(name.substr(0, dot));
      }
      // Store lowercase version for case-insensitive lookup
      std::string lowerName = name;
      std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
      host_builtin_names_.insert(std::move(lowerName));
      // Also store original for host function lookup
      host_builtin_names_original_.push_back(name);
    }
    void addHostGlobal(std::string name) { host_global_names_.insert(std::move(name)); }
    void setHostBuiltins(std::unordered_set<std::string> names) {
      host_builtin_names_ = std::move(names);
    }
    const LexicalResolutionResult& lexicalResolution() const {
      return lexical_resolution_;
    }
    
    // Case-insensitive lookup for host builtin
    std::optional<std::string> findHostBuiltin(const std::string& name) const {
      std::string lowerName = name;
      std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
      if (host_builtin_names_.find(lowerName) != host_builtin_names_.end()) {
        // Find original case version
        for (const auto& original : host_builtin_names_original_) {
          std::string lower = original;
          std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
          if (lower == lowerName) {
            return original;
          }
        }
      }
      return std::nullopt;
    }

private:
    struct SourceLocationScope {
      ByteCompiler *compiler = nullptr;
      std::optional<SourceLocation> previous;
      SourceLocationScope(ByteCompiler *owner, const ast::ASTNode &node)
          : compiler(owner), previous(owner->current_source_location_) {
        owner->current_source_location_ =
            SourceLocation{static_cast<uint32_t>(node.line),
                           static_cast<uint32_t>(node.column)};
      }
      ~SourceLocationScope() {
        compiler->current_source_location_ = previous;
      }
    };

    SourceLocationScope atNode(const ast::ASTNode &node) {
      return SourceLocationScope(this, node);
    }

    void emit(OpCode op);
    void emit(OpCode op, BytecodeValue operand);
    void emit(OpCode op, std::vector<BytecodeValue> operands);
    uint32_t addConstant(const BytecodeValue& value);
    uint32_t emitJump(OpCode op);
    void patchJump(uint32_t jump_instruction_index, uint32_t target);

    void compileFunction(const ast::FunctionDeclaration& function);
    void collectFunctionDeclarations(const ast::Statement& statement,
                                     std::vector<const ast::FunctionDeclaration*>& out) const;
    void compileStatement(const ast::Statement& statement);
    void compileExpression(const ast::Expression& expression);
    void compileCallExpression(const ast::CallExpression& expression);
    void compileIfStatement(const ast::IfStatement& statement);
    void compileWhileStatement(const ast::WhileStatement& statement);
    void compileBlockStatement(const ast::BlockStatement& block);
    std::optional<std::string> getCalleeName(const ast::Expression& callee) const;
    const ResolvedBinding* bindingFor(const ast::Identifier& id) const;
    uint32_t declarationSlot(const ast::Identifier& id) const;
    void reserveLocalSlot(uint32_t slot);

    void enterFunction(BytecodeFunction&& function);
    void leaveFunction();
    void resetLocals();

    std::unique_ptr<BytecodeChunk> chunk;
    std::unique_ptr<BytecodeFunction> current_function;
    std::vector<std::unique_ptr<BytecodeFunction>> compiled_functions;
    std::unordered_map<const ast::FunctionDeclaration *, uint32_t>
        function_indices_by_node_;
    std::unordered_map<std::string, uint32_t> top_level_function_indices_by_name_;
    uint32_t next_local_index = 0;
    std::optional<SourceLocation> current_source_location_;
    LexicalResolutionResult lexical_resolution_;
    std::unordered_set<std::string> host_builtin_names_{
        "print",      "sleep_ms",        "clock_ms",
        "system.gc",  "system.gcStats",  "system_gc",
        "system_gcStats"};
    std::vector<std::string> host_builtin_names_original_;  // For case-insensitive lookup
    std::unordered_set<std::string> host_global_names_{
        "window", "io", "system", "hotkey", "mode", "process"};
};

} // namespace havel::compiler
