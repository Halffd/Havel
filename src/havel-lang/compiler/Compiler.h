#pragma once

#include "../ast/AST.h"

// Guard against macro conflicts
#ifdef emit
#undef emit
#endif

#include "runtime/Interpreter.hpp"
// Include LLVM headers in correct order
#include "llvm.h"

// Standard library includes
#include <memory>
#include <unordered_map>
#include <string>
#include <vector>


namespace havel::compiler {

class Compiler {
private:
    llvm::LLVMContext context;
    llvm::IRBuilder<> builder;
    llvm::Module* module;  // Raw pointer (owned by ExecutionEngine)
    std::unique_ptr<llvm::ExecutionEngine> executionEngine;
    std::unordered_map<std::string, llvm::Function*> hotkeyHandlers;
    // Symbol tables
    std::unordered_map<std::string, llvm::Value*> namedValues;
    std::unordered_map<std::string, llvm::Function*> functions;
    std::unordered_map<std::string, llvm::Value*> symbolTable;
    std::unordered_map<std::string, ast::TypeDefinition> typeRegistry;
    std::unordered_map<std::string, bool> loadedModules;
public:
    Compiler();
    ~Compiler() = default;
    // Initialize LLVM
    void Initialize();

    // Main compilation entry points
    llvm::Function* CompileProgram(const ast::Program& program);
    llvm::Function* CompileHotkeyAction(const ast::Expression& action);
    void RegisterHotkey(const ast::HotkeyLiteral& hotkey, llvm::Function* handler);

    void RegisterSystemHotkey(const ast::HotkeyLiteral &hotkey);

    // JIT execution
    typedef void (*HotkeyActionFunc)();
    HotkeyActionFunc GetCompiledFunction(const std::string& name);

    // AST to LLVM IR generation
    llvm::Value* GenerateExpression(const ast::Expression& expr);
    llvm::Value* GenerateStatement(const ast::Statement& stmt);

    // Your specific node generators
    llvm::Value* GeneratePipeline(const ast::PipelineExpression& pipeline);
    llvm::Value* GenerateHotkeyBinding(const ast::HotkeyBinding& binding);
    llvm::Value* GenerateCall(const ast::CallExpression& call);
    llvm::Value* GenerateMember(const ast::MemberExpression& member);
    llvm::Value* GenerateBinary(const ast::BinaryExpression& binary);

    // Literals
    llvm::Value* GenerateStringLiteral(const ast::StringLiteral& str);
    llvm::Value* GenerateNumberLiteral(const ast::NumberLiteral& num);
    llvm::Value* GenerateIdentifier(const ast::Identifier& id);
    llvm::Value* GenerateHotkeyLiteral(const ast::HotkeyLiteral& hotkey);
    void RegisterType(const std::string& name, const ast::TypeDefinition& definition);
    void LoadModule(const std::string& moduleName);

    // Standard library functions
    void CreateStandardLibrary();

    // Variable management
    void SetVariable(const std::string& name, llvm::Value* value);
    llvm::Value* GetVariable(const std::string& name);

    // Utility
    void DumpModule() const;
    bool VerifyModule() const;
    // Method to get registered hotkey handlers
    const std::unordered_map<std::string, llvm::Function*>& GetHotkeyHandlers() const {
        return hotkeyHandlers;
    }
};

} // namespace havel::compiler