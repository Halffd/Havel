#pragma once

#include "compiler/core/BytecodeIR.hpp"
#include "compiler/vm/VM.hpp"
#include "core/Value.hpp"

#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <memory>
#include <string>
#include <vector>

namespace havel::compiler {

/**
 * Modern LLVM OrcV2-based JIT compiler for Havel Bytecode.
 * Translates BytecodeFunction into native machine code.
 */
class BytecodeOrcJIT : public JITCompiler {
public:
    BytecodeOrcJIT();
    ~BytecodeOrcJIT() override;

    // JITCompiler interface
    void compileFunction(const BytecodeFunction &func) override;
    Value executeCompiled(VM* vm, const std::string &func_name,
                          const std::vector<Value> &args) override;
    bool isCompiled(const std::string &func_name) const override;

private:
    std::unique_ptr<llvm::orc::LLJIT> lljit;
    std::unordered_map<std::string, void*> function_pointers;
    
    // Internal translation methods
    void translate(const BytecodeFunction &func, llvm::Module &module);

    // Initialization
    static void InitializeLLVM();
};

} // namespace havel::compiler
