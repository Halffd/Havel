#pragma once

#include "../ast/AST.h"

// Guard against macro conflicts
#ifdef emit
#undef emit
#endif

// Include LLVM headers in correct order
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/GenericValue.h>
#include <llvm/ExecutionEngine/MCJIT.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/Verifier.h>

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
    std::unique_ptr<llvm::Module> module;
    std::unique_ptr<llvm::ExecutionEngine> executionEngine;

    // Symbol tables
    std::unordered_map<std::string, llvm::Value*> namedValues;
    std::unordered_map<std::string, llvm::Function*> functions;

public:
    Compiler();
    ~Compiler() = default;

    // Initialize LLVM
    void Initialize();

    // Main compilation entry points
    llvm::Function* CompileProgram(const ast::Program& program);
    llvm::Function* CompileHotkeyAction(const ast::Expression& action);

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

    // Standard library functions
    void CreateStandardLibrary();

    // Variable management
    void SetVariable(const std::string& name, llvm::Value* value);
    llvm::Value* GetVariable(const std::string& name);

    // Utility
    void DumpModule() const;
    bool VerifyModule() const;
};

} // namespace havel::compiler