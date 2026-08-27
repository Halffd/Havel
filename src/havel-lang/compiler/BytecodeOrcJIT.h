#pragma once

// BytecodeOrcJIT.h — public interface only.
// LLVM headers are intentionally NOT included here to prevent namespace
// pollution of std::__detail under libstdc++ 15 / LLVM 23+.
// All LLVM types are forward-declared or kept in the .cpp.

#include "compiler/core/BytecodeIR.hpp"
#include "compiler/vm/VM.hpp"
#include "core/Value.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// Forward-declare the LLVM types we store so the header stays LLVM-free
namespace llvm::orc { class LLJIT; }
namespace llvm       { class Module; class TargetMachine; class Function; }

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
    static constexpr uint32_t MAX_EXCEPTION_HANDLERS = 32;
    void*    vm;                           ///< opaque VM*
    uint64_t root_ids[MAX_GC_ROOTS];       ///< GC external-root handle array
    uint64_t slot_values[MAX_GC_ROOTS];    ///< value bits at pin time
    uint32_t root_count;                   ///< number of live roots
    uint32_t handler_catch_ip[MAX_EXCEPTION_HANDLERS];
    uint32_t handler_finally_ip[MAX_EXCEPTION_HANDLERS];
    uint32_t handler_stack_depth[MAX_EXCEPTION_HANDLERS];
    uint32_t handler_count;
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
    enum class TargetOS {
        Native,
        Linux,
        Windows,
        MacOS,
        Wasm
    };

    BytecodeOrcJIT();
    ~BytecodeOrcJIT() override;

    // JITCompiler interface
    void compileFunction(const BytecodeFunction &func) override;
    Value executeCompiled(VM* vm, const std::string &func_name,
                          const std::vector<Value> &args) override;
    bool isCompiled(const std::string &func_name) const override;

    void setDebugMode(bool enabled) override { debug_jit_ = enabled; }
    void setDumpIR(bool enabled) override { dump_ir_ = enabled; }
    void setDumpAsmToFile(bool enabled) override { dump_asm_to_file_ = enabled; }
    void setOptimizationLevel(uint8_t level) override { optimization_level_ = level > 3 ? 3 : level; }
    void setTargetOS(TargetOS os);
    void setShowWarnings(bool enabled) override { show_warnings_ = enabled; }
    void setLinkedLibraries(const std::vector<std::string>& libs) { linked_libraries_ = libs; }
    void addLinkedLibrary(const std::string& lib) { linked_libraries_.push_back(lib); }
    TargetOS targetOS() const { return target_os_; }
    bool showWarnings() const { return show_warnings_; }
    const std::vector<std::string>& linkedLibraries() const { return linked_libraries_; }
    // Global JIT diagnostics state for script/runtime introspection.
    static void setLastError(std::string err);
    static std::string lastError();
    static void clearLastError();
    void compileFunctionAtOptLevel(const BytecodeFunction &func, uint8_t level);
    void compileFunctionTier(const BytecodeFunction &func, uint8_t tier) override;
    void compileTrace(const BytecodeFunction &func, uint32_t start_ip, uint64_t hot_count) override;
    void setCompilationVM(const VM* vm) { compilation_vm_ = vm; }

    void dumpAssembly(const std::string &filename);

    // AOT: Translate function to LLVM IR (public for AOT compilation)
    void translate(const BytecodeFunction &func, llvm::Module &module);

    // Check if a function contains opcodes the JIT/AOT path cannot handle
    // (coroutine ops, fiber ops, etc.). These must stay in the interpreter.
    static bool hasUnsupportedOpcodes(const BytecodeFunction &func);

private:
    struct CachedFunction {
        std::string canonical_name;
    };

    struct CachedTrace {
        std::string function_name;
        uint32_t start_ip = 0;
        uint64_t hot_count = 0;
        uint64_t trace_hash = 0;
    };

    // Inline cache for method calls (CALL_METHOD, OBJECT_GET, OBJECT_SET)
    // Monomorphic -> Polymorphic (up to MAX_IC_ENTRIES) -> Megamorphic
    static constexpr size_t MAX_IC_ENTRIES = 4;
    
    struct ICEntry {
        uint64_t receiver_type_hash;  // Hash of receiver type (class/prototype)
        uint32_t method_name_id;      // Method name ID
        void* method_ptr;             // Compiled method function pointer
        uint64_t hit_count = 0;
    };
    
    struct InlineCache {
        std::array<ICEntry, MAX_IC_ENTRIES> entries{};
        uint32_t count = 0;
        bool is_megamorphic = false;
        uint64_t miss_count = 0;
        
        void* lookup(uint64_t type_hash, uint32_t method_name_id) const {
            for (size_t i = 0; i < count; ++i) {
                if (entries[i].receiver_type_hash == type_hash && 
                    entries[i].method_name_id == method_name_id) {
                    return entries[i].method_ptr;
                }
            }
            return nullptr;
        }
        
        void insert(uint64_t type_hash, uint32_t method_name_id, void* method_ptr) {
            if (count < MAX_IC_ENTRIES) {
                entries[count++] = {type_hash, method_name_id, method_ptr, 0};
            } else {
                is_megamorphic = true;
            }
        }
        
        void record_hit(uint64_t type_hash, uint32_t method_name_id) {
            for (size_t i = 0; i < count; ++i) {
                if (entries[i].receiver_type_hash == type_hash && 
                    entries[i].method_name_id == method_name_id) {
                    entries[i].hit_count++;
                    break;
                }
            }
        }
    };
    
    // Inline caches per call site (function IP -> InlineCache)
    std::unordered_map<uint64_t, InlineCache> inline_caches_;

    std::unique_ptr<llvm::orc::LLJIT> lljit_;
    std::unordered_map<std::string, void*> fptrs_;
    std::unordered_map<uint64_t, CachedFunction> compile_cache_;
    std::unordered_map<uint64_t, CachedTrace> trace_cache_;
    std::string cache_index_path_;
    std::unique_ptr<llvm::TargetMachine> target_machine_;
    bool debug_jit_ = false;
    bool dump_ir_ = false;
    bool dump_asm_to_file_ = false;
    uint8_t optimization_level_ = 1; // 0=O0 fast start, 1=O1 baseline, 2=O2, 3=O3
    TargetOS target_os_ = TargetOS::Native;
    bool show_warnings_ = true;
    std::vector<std::string> linked_libraries_;
    const VM* compilation_vm_ = nullptr;
    std::string last_asm_;
    static std::mutex last_error_mutex_;
    static std::string last_error_;

    std::string resolveTargetTriple() const;
    void initTargetMachine();
    void runOptimizations(llvm::Module &module);
    uint64_t computeFunctionHash(const BytecodeFunction &func) const;
    void loadCompileCacheIndex();
    void saveCompileCacheIndex() const;
    
    // Inline cache key: function_hash << 32 | ip
    static uint64_t makeICKey(uint64_t func_hash, uint32_t ip) {
        return (func_hash << 32) | ip;
    }
    
    // Get receiver type hash from value
    uint64_t getReceiverTypeHash(const VM* vm, const Value& receiver) const;
    
    // ============================================================================
    // PGO (Profile-Guided Optimization) Infrastructure
    // ============================================================================
    
    struct ProfileData {
        // Per-function execution counts
        std::unordered_map<uint64_t, uint64_t> function_call_counts;
        // Per-basic-block execution counts
        std::unordered_map<uint64_t, uint64_t> block_execution_counts;
        // Edge frequencies (branch taken/not taken)
        std::unordered_map<uint64_t, std::pair<uint64_t, uint64_t>> edge_counts;
        // Function hotness threshold for tier promotion
        static constexpr uint64_t HOT_THRESHOLD = 1000;
        static constexpr uint64_t VERY_HOT_THRESHOLD = 10000;
    };
    
    // Profile data collection
    ProfileData profile_data_;
    
    // Record function entry
    void recordFunctionEntry(uint64_t func_hash) {
        profile_data_.function_call_counts[func_hash]++;
    }
    
    // Record basic block execution
    void recordBlockExecution(uint64_t block_hash) {
        profile_data_.block_execution_counts[block_hash]++;
    }
    
    // Record branch taken/not taken
    void recordBranch(uint64_t branch_id, bool taken) {
        auto& pair = profile_data_.edge_counts[branch_id];
        if (taken) pair.first++; else pair.second++;
    }
    
    // Get function hotness level
    int getFunctionHotness(uint64_t func_hash) const {
        auto it = profile_data_.function_call_counts.find(func_hash);
        if (it == profile_data_.function_call_counts.end()) return 0;
        uint64_t count = it->second;
        if (count >= ProfileData::VERY_HOT_THRESHOLD) return 3;
        if (count >= ProfileData::HOT_THRESHOLD) return 2;
        if (count > 10) return 1;
        return 0;
    }
    
    // Apply profile data to optimize module
    void applyProfileGuidedOptimizations(llvm::Module& module);
    
    // Serialize profile data to file
    void saveProfileData(const std::string& path) const;
    // Load profile data from file
    void loadProfileData(const std::string& path);
    
    static void InitializeLLVM();
    
    // ============================================================================
    // Code Layout Optimization
    // ============================================================================
    
    // Hot/cold block splitting based on profile data
    void optimizeCodeLayout(llvm::Module& module);
    
    // Block ordering optimization using profile data
    void optimizeBlockOrder(llvm::Function* function);
    
    // Cold block separation
    void separateColdBlocks(llvm::Function* function);
    
    // Compute basic block hotness from profile data
    double getBlockHotness(uint64_t func_hash, uint32_t block_id) const;
    
    // ============================================================================
    // Compilation Queue System
    // ============================================================================
    
    enum class CompilePriority { Low, Normal, High, Critical };
    
    struct CompileTask {
        uint64_t func_hash;
        uint32_t start_ip;
        uint32_t end_ip;
        CompilePriority priority;
        std::function<void(std::unique_ptr<llvm::Module>)> callback;
    };
    
    // Async compilation queue
    void enqueueCompileTask(CompileTask task);
    void processCompileQueue();
    void shutdownCompileQueue();
    
    // Background compilation thread
    std::thread compile_thread_;
    std::atomic<bool> compile_thread_running_{false};
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::queue<CompileTask> compile_queue_;
};


} // namespace havel::compiler
