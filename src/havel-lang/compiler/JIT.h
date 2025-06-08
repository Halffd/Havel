// src/havel-lang/compiler/JIT.hpp
#pragma once

#include "../llvm/LLVMWrapper.h"

// Import commonly used LLVM ORC types
using llvm::orc::JITTargetMachineBuilder;
using llvm::orc::ThreadSafeModule;
using llvm::orc::ExecutionSession;
using llvm::orc::RTDyldObjectLinkingLayer;
using llvm::orc::IRCompileLayer;
using llvm::JITEvaluatedSymbol;  // Moved from llvm::orc to llvm namespace
using llvm::orc::JITDylib;
using llvm::orc::ResourceTrackerSP;
using llvm::orc::SelfExecutorProcessControl;
using llvm::orc::SymbolMap;
using llvm::orc::ThreadSafeContext;
using llvm::Error;
using llvm::Expected;

#include "Compiler.h"
#include "../ast/AST.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>

namespace havel::compiler {

    class JIT {
    private:
        Compiler compiler;
        std::unordered_map<std::string, Compiler::HotkeyActionFunc> compiledHotkeys;

    public:
        JIT();

        // Compile and register hotkey with NATIVE SPEED! ⚡
        void CompileHotkey(const std::string& combination, const ast::Expression& action);

        // Execute compiled hotkey (pure machine code!)
        void ExecuteHotkey(const std::string& combination);

        // Compile entire Havel script
        void CompileScript(const ast::Program& program);
    };

} // namespace havel::compiler