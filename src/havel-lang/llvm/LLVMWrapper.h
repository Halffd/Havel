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

#include "llvm.h"
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
