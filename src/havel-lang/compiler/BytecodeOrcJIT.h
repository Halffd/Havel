#pragma once

// BytecodeOrcJIT.h — public interface only.
// LLVM headers are intentionally NOT included here to prevent namespace
// pollution of std::__detail under libstdc++ 15 / LLVM 23+.
// All LLVM types are forward-declared or kept in the .cpp.

#include "compiler/core/BytecodeIR.hpp"
#include "compiler/vm/VM.hpp"
#include "core/Value.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// Forward-declare the LLVM types we store so the header stays LLVM-free
namespace llvm::orc { class LLJIT; }
namespace llvm       { class Module; class TargetMachine; }

namespace havel::compiler {

/**
 * Per-function GC stack-frame descriptor.
 *
 * Each JIT-compiled function allocates one of these on the C stack in its
 * prolog.  havel_gc_register_roots() pins every local slot that might hold a
 * heap-tagged Value; havel_gc_unregister_roots() releases them on return.
 * This gives the incremental GC a conservative snapshot of JIT frames without
 * requiring a shadow stack or LLVM's stackmap infrastructure.
 */
struct JITStackFrame {
    static constexpr uint32_t MAX_GC_ROOTS = 32;
    void*    vm;                           ///< opaque VM*
    uint64_t root_ids[MAX_GC_ROOTS];       ///< GC external-root handle array
    uint64_t slot_values[MAX_GC_ROOTS];    ///< value bits at pin time
    uint32_t root_count;                   ///< number of live roots
};

/**
 * Modern LLVM OrcV2-based JIT compiler for Havel Bytecode.
 *
 * Translation pipeline:
 *   BytecodeFunction (+ TypeFeedback) → LLVM IR → native code
 *
 * Features:
 *   Phase 3 – GC root registration + write barriers in generated IR.
 *   Phase 4 – int48 and f64 monomorphic specialization; llvm.expect hints.
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

    void setDebugMode(bool enabled) { debug_jit_ = enabled; }
    void setDumpIR(bool enabled) { dump_ir_ = enabled; }
    void setDumpAsmToFile(bool enabled) { dump_asm_to_file_ = enabled; }
    void setOptimizationLevel(uint8_t level) { optimization_level_ = level > 3 ? 3 : level; }

    void dumpAssembly(const std::string &filename);

    // AOT: Translate function to LLVM IR (public for AOT compilation)
    void translate(const BytecodeFunction &func, llvm::Module &module);

private:
    std::unique_ptr<llvm::orc::LLJIT> lljit_;
    std::unordered_map<std::string, void*> fptrs_;
    std::unique_ptr<llvm::TargetMachine> target_machine_;
    bool debug_jit_ = false;
    bool dump_ir_ = false;
    bool dump_asm_to_file_ = false;
    uint8_t optimization_level_ = 1; // 0=O0 fast start, 1=O1 baseline, 2=O2, 3=O3
    std::string last_asm_;

    void initTargetMachine();
    void runOptimizations(llvm::Module &module);

    static void InitializeLLVM();
};


} // namespace havel::compiler
