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
      host_builtin_names_.insert(std::move(name));
    }
    void addHostGlobal(std::string name) { host_global_names_.insert(std::move(name)); }
    void setHostBuiltins(std::unordered_set<std::string> names) {
      host_builtin_names_ = std::move(names);
    }
    const LexicalResolutionResult& lexicalResolution() const {
      return lexical_resolution_;
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
    void compileHotkeyBinding(const ast::HotkeyBinding& binding);
    void compileInputStatement(const ast::InputStatement& statement);
    void compileCallExpression(const ast::CallExpression& expression);
    void compileIfStatement(const ast::IfStatement& statement);
    void compileWhileStatement(const ast::WhileStatement& statement);
    void compileForStatement(const ast::ForStatement& statement);
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
        // Core functions
        "print", "sleep_ms", "clock_ms",
        "system.gc", "system.gcStats", "system_gc", "system_gcStats",
        // HTTP module
        "http.get", "http.post", "http.download",
        "network.get", "network.post", "network.download",
        "network.isOnline", "network.getExternalIp",
        // String module
        "string.len", "string.lower", "string.upper", "string.trim",
        "string.sub", "string.find", "string.replace", "string.split",
        "string.join", "string.startswith", "string.endswith", "string.includes",
        // Array module
        "array.len", "array.get", "array.set", "array.push", "array.pop",
        "array.insert", "array.remove", "array.slice", "array.concat",
        "array.find", "array.filter", "array.map", "array.reduce",
        "array.sort", "array.reverse", "array.includes", "array.join",
        // Math module
        "math.abs", "math.ceil", "math.floor", "math.round",
        "math.sin", "math.cos", "math.tan", "math.asin", "math.acos", "math.atan",
        "math.exp", "math.log", "math.log10", "math.log2",
        "math.sqrt", "math.cbrt", "math.pow",
        "math.min", "math.max", "math.clamp", "math.lerp",
        "math.random", "math.randint",
        "math.deg2rad", "math.rad2deg",
        "math.sign", "math.fract", "math.mod",
        "math.distance", "math.hypot",
        // Type module
        "type.isNumber", "type.isString", "type.isArray", "type.isObject",
        "type.isNull", "type.isBoolean",
        "type.toString", "type.toNumber",
        // Utility module
        "utility.keys", "utility.items", "utility.list",
        // Regex module
        "regex.match", "regex.search", "regex.replace", "regex.extract", "regex.split",
        // Physics module
        "physics.force", "physics.kinetic_energy", "physics.potential_energy",
        "physics.momentum", "physics.wavelength",
        // Time module
        "time.now", "time.format", "time.hour", "time.minute", "time.second",
        // Object module
        "object.keys", "object.values", "object.entries",
        "object.has", "object.delete", "object.clone",
        // Runtime type dispatch (any.* methods)
        "any.len", "any.trim", "any.upper", "any.lower", "any.includes",
        "any.startswith", "any.endswith", "any.find", "any.replace",
        "any.split", "any.join", "any.sub",
        "any.push", "any.pop", "any.get", "any.set", "any.sort",
        "any.filter", "any.map", "any.reduce"
    };
    std::unordered_set<std::string> host_global_names_{
        "window", "io", "system", "hotkey", "mode", "process",
        "string", "array", "object", "type", "utility", "regex",
        "physics", "time", "math"
    };
};

} // namespace havel::compiler
