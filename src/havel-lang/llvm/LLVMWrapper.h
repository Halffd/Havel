#pragma once

// Disable specific warnings that might come from LLVM headers
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpragmas"
#pragma GCC diagnostic ignored "-Wunknown-warning-option"
#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#pragma GCC diagnostic ignored "-Wmacro-redefined"
#pragma GCC diagnostic ignored "-Wignored-pragmas"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#pragma GCC diagnostic ignored "-Wdeprecated"
#endif

// Save X11 macro definitions if they exist
#pragma push_macro("None")
#pragma push_macro("Bool")
#pragma push_macro("True")
#pragma push_macro("False")
#pragma push_macro("Status")
#pragma push_macro("Always")
#pragma push_macro("DestroyAll")

// Undefine X11 macros that conflict with LLVM headers
#undef None
#undef Bool
#undef True
#undef False
#undef Status
#undef Always
#undef DestroyAll

// Define LLVM_DISABLE_ABI_BREAKING_CHECKS_ENFORCING to avoid ABI breakage warnings
#ifndef LLVM_DISABLE_ABI_BREAKING_CHECKS_ENFORCING
#define LLVM_DISABLE_ABI_BREAKING_CHECKS_ENFORCING 1
#endif

// Include LLVM headers with minimal dependencies
#include <llvm/Support/Error.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Verifier.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/GenericValue.h>
#include <llvm/ExecutionEngine/MCJIT.h>
#include <llvm/ExecutionEngine/SectionMemoryManager.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/ExecutionEngine/Orc/ExecutorProcessControl.h>
#include <llvm/ExecutionEngine/Orc/CompileUtils.h>

// Restore X11 macros after LLVM includes
#pragma pop_macro("DestroyAll")
#pragma pop_macro("Always")
#pragma pop_macro("Status")
#pragma pop_macro("False")
#pragma pop_macro("True")
#pragma pop_macro("Bool")
#pragma pop_macro("None")

// Re-enable warnings
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

// Forward declare commonly used LLVM classes in the llvm namespace
namespace llvm {
class TargetMachine;
class DataLayout;
class raw_ostream;
class StringRef;
class EngineBuilder;

namespace orc {
class ThreadSafeModule;
class JITTargetMachineBuilder;
class ExecutionSession;
class RTDyldObjectLinkingLayer;
class IRCompileLayer;
} // namespace orc
} // namespace llvm

/**
 * LLVM initialization helper
 */
struct LLVMInitializer {
    LLVMInitializer() {
        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmPrinter();
        llvm::InitializeNativeTargetAsmParser();
        llvm::InitializeNativeTargetDisassembler();
    }
    
    ~LLVMInitializer() {
        // Modern LLVM versions handle cleanup automatically
    }
};

// Global instance for LLVM initialization
static LLVMInitializer gLLVMInitializer;
