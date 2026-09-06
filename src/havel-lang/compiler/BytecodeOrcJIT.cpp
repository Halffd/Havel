#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IR/Module.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/TargetParser.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Support/CodeGen.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/MC/TargetRegistry.h>

#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <fstream>
#include <functional>
#include <filesystem>
#include <sstream>
#include <unordered_set>
#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/Passes/PassBuilder.h>
#include "BytecodeOrcJIT.h"
#include "JitBridgesCommon.hpp"
#include "compiler/runtime/RuntimeABI.hpp"
#include "core/util/Env.hpp"
#include "../../utils/Logger.hpp"
#include <cstring>
#include <iostream>
#include <array>
#include "runtime/HavelEngine.hpp"

using namespace llvm::orc;

namespace havel::compiler {

std::mutex BytecodeOrcJIT::last_error_mutex_;
std::string BytecodeOrcJIT::last_error_;

void BytecodeOrcJIT::setLastError(std::string err) {
    std::lock_guard<std::mutex> lock(last_error_mutex_);
    last_error_ = std::move(err);
}

std::string BytecodeOrcJIT::lastError() {
    std::lock_guard<std::mutex> lock(last_error_mutex_);
    return last_error_;
}

void BytecodeOrcJIT::clearLastError() {
    std::lock_guard<std::mutex> lock(last_error_mutex_);
    last_error_.clear();
}

namespace {

void reportLLVMError(const std::string& stage, llvm::Error err, bool showWarnings) {
    if (!err) return;
    std::string details = llvm::toString(std::move(err));
    BytecodeOrcJIT::setLastError(stage + ": " + details);
    if (showWarnings) {
        ::havel::warning("BytecodeOrcJIT [{}]: {}", stage, details);
    }
}

} // namespace

// ============================================================================

// BytecodeOrcJIT – implementation
// ============================================================================

void BytecodeOrcJIT::InitializeLLVM() {
    static bool initialized = false;
    if (initialized) return;
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();
    initialized = true;
}

BytecodeOrcJIT::BytecodeOrcJIT() {
    InitializeLLVM();
    if (const char* osEnv = std::getenv("HAVEL_JIT_TARGET_OS")) {
        std::string os = osEnv;
        std::transform(os.begin(), os.end(), os.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (os == "linux") target_os_ = TargetOS::Linux;
        else if (os == "windows" || os == "win") target_os_ = TargetOS::Windows;
        else if (os == "macos" || os == "darwin" || os == "mac") target_os_ = TargetOS::MacOS;
        else if (os == "wasm" || os == "webassembly") target_os_ = TargetOS::Wasm;
    }
    if (const char* warnEnv = std::getenv("HAVEL_JIT_WARNINGS")) {
        std::string v = warnEnv;
        std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (v == "0" || v == "false" || v == "off") {
            show_warnings_ = false;
        }
    }
    initTargetMachine();
    if (const char* optEnv = std::getenv("HAVEL_JIT_OPT_LEVEL")) {
        int parsed = std::atoi(optEnv);
        if (parsed < 0) parsed = 0;
        if (parsed > 3) parsed = 3;
        optimization_level_ = static_cast<uint8_t>(parsed);
    }

    auto jit_or_err = LLJITBuilder().create();
    if (!jit_or_err) {
        reportLLVMError("create", jit_or_err.takeError(), show_warnings_);
        return;
    }
    lljit_ = std::move(*jit_or_err);

    auto &jd = lljit_->getMainJITDylib();
    auto &es = lljit_->getExecutionSession();

    SymbolMap syms;
    auto addSym = [&](const char* name, void* ptr) {
        syms[es.intern(name)] = {
            ExecutorAddr::fromPtr(ptr),
            llvm::JITSymbolFlags::Exported
        };
    };

    // Register the full Runtime ABI (RuntimeABI.hpp X-macro) with the JIT
    // dylib: single source of truth for names, so a new ABI entry is
    // automatically resolvable from generated code without touching this
    // file (the runtime-abi-drift-guard test keeps definitions in sync).
#define HAVEL_RUNTIME_ABI_REG(name, ret, args, contract) \
    addSym(#name, reinterpret_cast<void*>(&name));
    HAVEL_RUNTIME_ABI(HAVEL_RUNTIME_ABI_REG)
#undef HAVEL_RUNTIME_ABI_REG

    if (auto err = jd.define(absoluteSymbols(std::move(syms)))) {
        reportLLVMError("define-symbols", std::move(err), show_warnings_);
    }

    // Phase 3 cache metadata: load persisted hash->symbol aliases.
    const char* customCache = std::getenv("HAVEL_JIT_CACHE_INDEX");
    if (customCache && *customCache) {
        cache_index_path_ = customCache;
    } else if (const char* home = std::getenv("HOME")) {
        cache_index_path_ = std::string(home) + "/.cache/havel/jit_function_cache.idx";
    } else {
        cache_index_path_ = "/tmp/havel_jit_function_cache.idx";
    }
    loadCompileCacheIndex();
}

BytecodeOrcJIT::~BytecodeOrcJIT() = default;

void BytecodeOrcJIT::setTargetOS(TargetOS os) {
    target_os_ = os;
    initTargetMachine();
}

std::string BytecodeOrcJIT::resolveTargetTriple() const {
    const std::string hostTriple = llvm::sys::getDefaultTargetTriple();
    if (target_os_ == TargetOS::Native) {
        return hostTriple;
    }

    llvm::Triple host(hostTriple);
    std::string arch = host.getArchName().str();
    if (arch.empty()) {
        arch = "x86_64";
    }

    switch (target_os_) {
        case TargetOS::Linux:
            return arch + "-pc-linux-gnu";
        case TargetOS::Windows:
            return arch + "-pc-windows-msvc";
        case TargetOS::MacOS:
            return arch + "-apple-darwin";
        case TargetOS::Wasm:
            return "wasm32-unknown-unknown";
        case TargetOS::Native:
        default:
            return hostTriple;
    }
}

void BytecodeOrcJIT::initTargetMachine() {
    auto target_triple_str = resolveTargetTriple();
    llvm::Triple target_triple(target_triple_str);
    std::string error;
    auto target = llvm::TargetRegistry::lookupTarget(target_triple, error);
    if (!target) {
        if (show_warnings_) {
            ::havel::warning("BytecodeOrcJIT: cannot resolve target '{}': {}",
                             target_triple_str, error);
        }
        target_machine_.reset();
        return;
    }
    llvm::TargetOptions opt;
    opt.GuaranteedTailCallOpt = true; // Enable aggressive tail call optimization
    target_machine_.reset(target->createTargetMachine(
        target_triple, llvm::sys::getHostCPUName(), "", opt, llvm::Reloc::PIC_, std::nullopt, llvm::CodeGenOptLevel::Default));
}

bool BytecodeOrcJIT::hasUnsupportedOpcodes(const BytecodeFunction &func) {
    for (const auto& instr : func.instructions) {
        switch (instr.opcode) {
            case OpCode::YIELD:
            case OpCode::YIELD_RESUME:
            case OpCode::GO_ASYNC:
            case OpCode::FIBER_SLEEP:
            case OpCode::FIBER_AWAIT:
                return true;
            default:
                break;
        }
    }
    return false;
}

void BytecodeOrcJIT::compileFunction(const BytecodeFunction &func) {
    if (!lljit_) {
        setLastError("compile:" + func.name + ": JIT is not initialized");
        if (show_warnings_) {
            ::havel::warning("BytecodeOrcJIT: compile requested for '{}' but JIT is not initialized",
                             func.name);
        }
        return;
    }

  // Functions containing coroutine/scheduler opcodes are not JIT-compiled
  // because JIT frames cannot be suspended mid-execution. These opcodes
  // are handled by the interpreter which runs one instruction per step.
  // Safety net: if a JIT function somehow reaches these opcodes (e.g.
  // via dynamic dispatch), JitCoroutineSignal is thrown and callFunction()
  // falls back to the interpreter path.
  if (hasUnsupportedOpcodes(func)) {
    return;
  }


    const uint64_t func_hash = computeFunctionHash(func);
    auto cached = compile_cache_.find(func_hash);
    if (cached != compile_cache_.end()) {
        auto existing = fptrs_.find(cached->second.canonical_name);
        if (existing != fptrs_.end()) {
            fptrs_[func.name] = existing->second;
            return;
        }
    }

    auto context = std::make_unique<llvm::LLVMContext>();
    auto module  = std::make_unique<llvm::Module>(func.name, *context);

    if (target_machine_) {
        module->setDataLayout(target_machine_->createDataLayout());
        module->setTargetTriple(target_machine_->getTargetTriple());
    }

    translate(func, *module);
    if (optimization_level_ > 0) {
        runOptimizations(*module);
    }

    if (dump_ir_) {
        ::havel::debug("--- LLVM IR for {} ---", func.name);
        module->print(llvm::errs(), nullptr);
    }

    if ((debug_jit_ || dump_asm_to_file_) && target_machine_) {
        // Use raw_fd_ostream which is compatible with addPassesToEmitFile
        std::error_code ec;
        std::string asm_file = "/tmp/havel_asm_" + func.name + ".s";
        llvm::raw_fd_ostream ros(asm_file, ec, llvm::sys::fs::OF_None);
        
        if (!ec) {
            // Use a local pass manager for file emission (legacy but necessary for this)
            llvm::legacy::PassManager pm;
            if (!target_machine_->addPassesToEmitFile(pm, ros, nullptr, llvm::CodeGenFileType::AssemblyFile)) {
                pm.run(*module);
                ros.flush();
                ros.close();
                
                // Read the assembly into memory for debug output
                std::ifstream asm_input(asm_file);
                if (asm_input.is_open()) {
                    std::stringstream buffer;
                    buffer << asm_input.rdbuf();
                    last_asm_ = buffer.str();
                    asm_input.close();
                    
                    if (debug_jit_) {
        ::havel::debug("--- Assembly for {} ---", func.name);
        ::havel::debug("{}", last_asm_);
                    }
                }
            }
            
            if (dump_asm_to_file_) {
                dumpAssembly(func.name + ".s");
                std::error_code ec2;
                llvm::raw_fd_ostream ir_os(func.name + ".ll", ec2, llvm::sys::fs::OF_None);
                if (!ec2) module->print(ir_os, nullptr);
            }
        }
    }

    if (auto err = lljit_->addIRModule(ThreadSafeModule(std::move(module), std::move(context)))) {
        reportLLVMError("add-module:" + func.name, std::move(err), show_warnings_);
        return;
    }

    auto sym = lljit_->lookup(func.name);
    if (!sym) {
        reportLLVMError("lookup:" + func.name, sym.takeError(), show_warnings_);
        return;
    }

    void* func_ptr = reinterpret_cast<void*>((*sym).getValue());
    if (debug_jit_) {
      // fprintf(stderr, "[COMPILE-DEBUG] func=%s ptr=%p\n", func.name.c_str(), func_ptr);
      // fflush(stderr);
    }
    fptrs_[func.name] = func_ptr;
    compile_cache_[func_hash] = CachedFunction{func.name};
    saveCompileCacheIndex();
    func.jit_compiled = true;
}

void BytecodeOrcJIT::compileFunctionAtOptLevel(const BytecodeFunction &func, uint8_t level) {
    const uint8_t saved = optimization_level_;
    optimization_level_ = level > 3 ? 3 : level;
    compileFunction(func);
    optimization_level_ = saved;
}

void BytecodeOrcJIT::compileFunctionTier(const BytecodeFunction &func, uint8_t tier) {
    // Tier mapping:
    // tier 1 -> O0 (fast startup / baseline JIT)
    // tier 2 -> O2 (optimizing background recompile)
    if (tier <= 1) {
        compileFunctionAtOptLevel(func, 0);
        return;
    }
    compileFunctionAtOptLevel(func, 2);
}

void BytecodeOrcJIT::compileTrace(const BytecodeFunction &func, uint32_t start_ip, uint64_t hot_count) {
    if (!lljit_) {
        setLastError("trace:" + func.name + ": JIT is not initialized");
        return;
    }
    if (start_ip >= func.instructions.size()) {
        return;
    }

    uint64_t trace_hash = computeFunctionHash(func);
    trace_hash ^= static_cast<uint64_t>(start_ip) * 0x9e3779b97f4a7c15ULL;
    trace_hash ^= hot_count + 0x27d4eb2f165667c5ULL + (trace_hash << 7) + (trace_hash >> 3);

    auto cached = trace_cache_.find(trace_hash);
    if (cached != trace_cache_.end()) {
        auto existing = fptrs_.find(cached->second.function_name);
        if (existing != fptrs_.end()) {
            fptrs_[func.name] = existing->second;
            return;
        }
    }

    size_t trace_len = 0;
    std::vector<uint32_t> trace_ips;
    for (uint32_t ip = start_ip; ip < func.instructions.size() && trace_len < 64; ++ip, ++trace_len) {
        trace_ips.push_back(ip);
        const auto &instr = func.instructions[ip];
        if (instr.opcode == OpCode::RETURN ||
            instr.opcode == OpCode::THROW ||
            instr.opcode == OpCode::TAIL_CALL ||
            instr.opcode == OpCode::JUMP ||
            instr.opcode == OpCode::JUMP_IF_FALSE ||
            instr.opcode == OpCode::JUMP_IF_TRUE ||
            instr.opcode == OpCode::JUMP_IF_NULL) {
            break;
        }
    }

    if (show_warnings_) {
        ::havel::debug("[trace] compiling '{}' trace@{} len={} hot_count={}",
                       func.name, start_ip, trace_ips.size(), hot_count);
    }

    compileFunctionAtOptLevel(func, 0);
    trace_cache_[trace_hash] = CachedTrace{func.name, start_ip, hot_count, trace_hash};
}

Value BytecodeOrcJIT::executeCompiled(VM* vm, const std::string &func_name,
                                      const std::vector<Value> &args) {
  auto it = fptrs_.find(func_name);
  if (it == fptrs_.end()) return Value::makeNull();

  typedef uint64_t (*NativeFunc)(void*, const Value*, uint32_t);
  auto func = reinterpret_cast<NativeFunc>(it->second);

  try {

    // This avoids C stack growth during deep JIT recursion by returning
    // to this loop when a tail call is requested.
    void* current_vm_ptr = static_cast<void*>(vm);
    const Value* current_args_ptr = args.data();
    uint32_t current_args_count = static_cast<uint32_t>(args.size());

    while (true) {
      if (debug_jit_) {
        // fprintf(stderr, "[EXECJIT-DEBUG] calling func=%p with %u args\n", (void*)func, current_args_count);
        // fflush(stderr);
      }
      vm->setJitTailCall(false); // Reset flag before calling

      uint64_t res_bits =
        func(static_cast<void*>(vm), current_args_ptr, current_args_count);
      if (debug_jit_) {
        // fprintf(stderr, "[EXECJIT-DEBUG] func returned 0x%llx\n", (unsigned long long)res_bits);
        // fflush(stderr);
      }

      // Check if a tail call occurred that we can handle in JIT
      if (vm->hasJitTailCall()) {
        const BytecodeFunction* next_func = vm->currentFunction();
        if (next_func && isCompiled(next_func->name)) {
          // Stay in JIT: update function pointer and continue loop
          func = reinterpret_cast<NativeFunc>(fptrs_[next_func->name]);

          // Args for the tail call are already set up in the VM's locals array
          // by doTailCall. We need to pass them to the next JIT function.
          size_t lb = vm->currentLocalsBasePublic();
          current_args_ptr = vm->getLocalsPointerPublic(lb);
          current_args_count = next_func->param_count;

          continue; // Loop again with new function
        }

        // If the next function is NOT JIT-compiled, return back to VM loop
        // which will handle the interpreter execution.
        return Value::makeNull();
      }

      Value res;
      std::memcpy(&res, &res_bits, sizeof(uint64_t));
      return res;
    }
  } catch (const JitCoroutineSignal&) {
    // JIT hit a coroutine/scheduler opcode (YIELD, AWAIT, GO_ASYNC,
    // FIBER_SLEEP, YIELD_RESUME) that requires interpreter-level frame
    // management. Re-throw so callFunction() can fall back to the
    // interpreter path for this function call.
    throw;
  } catch (const ScriptThrow&) {
    // Preserve script exception semantics so VM dispatch can route to
    // TRY_ENTER handlers in active VM frames.
    throw;
  } catch (const std::exception& e) {
    setLastError("runtime:" + func_name + ": " + std::string(e.what()));
    if (show_warnings_) {
      ::havel::warning("BytecodeOrcJIT runtime exception in '{}': {}", func_name, e.what());
    }
    vm->throwError(std::string("JIT exception in ") + func_name + ": " + e.what());
    return Value::makeNull();
  } catch (...) {
    setLastError("runtime:" + func_name + ": unknown exception");
    if (show_warnings_) {
      ::havel::warning("BytecodeOrcJIT runtime exception in '{}': unknown exception", func_name);
    }
    vm->throwError(std::string("Unknown JIT exception in ") + func_name);
    return Value::makeNull();
  }
}

bool BytecodeOrcJIT::isCompiled(const std::string &func_name) const {
    return fptrs_.count(func_name) > 0;
}

uint64_t BytecodeOrcJIT::computeFunctionHash(const BytecodeFunction &func) const {
    uint64_t seed = 1469598103934665603ULL;
    auto mix = [&](uint64_t v) {
        seed ^= v;
        seed *= 1099511628211ULL;
    };

    mix(static_cast<uint64_t>(func.param_count));
    mix(static_cast<uint64_t>(func.local_count));
    mix(static_cast<uint64_t>(func.instructions.size()));
    mix(static_cast<uint64_t>(func.constants.size()));

    for (const auto &ins : func.instructions) {
        mix(static_cast<uint64_t>(ins.opcode));
        mix(static_cast<uint64_t>(ins.operands.size()));
        for (const auto &op : ins.operands) {
            mix(op.rawBits());
            if (op.isStringValId()) {
                for (char c : op.toString()) {
                    mix(static_cast<uint64_t>(static_cast<unsigned char>(c)));
                }
            }
        }
    }

    for (const auto &c : func.constants) {
        mix(c.rawBits());
    }
    return seed;
}

// Get receiver type hash from value - extracts class/prototype type for inline caching
uint64_t BytecodeOrcJIT::getReceiverTypeHash(const VM* /*vm*/, const Value& /*receiver*/) const {
    // Stub: PGO/type-hash path requires a per-type-id API on core::Value.
    // Will be implemented once Value exposes tag/payload accessors.
    return 0;
}

void BytecodeOrcJIT::loadCompileCacheIndex() {
    std::ifstream in(cache_index_path_);
    if (!in.is_open()) {
        return;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        auto pos = line.find(' ');
        if (pos == std::string::npos || pos == 0 || pos + 1 >= line.size()) {
            continue;
        }
        try {
            uint64_t hash = static_cast<uint64_t>(std::stoull(line.substr(0, pos), nullptr, 16));
            std::string name = line.substr(pos + 1);
            compile_cache_[hash] = CachedFunction{name};
        } catch (...) {
            // Ignore malformed lines.
        }
    }
}

void BytecodeOrcJIT::saveCompileCacheIndex() const {
    if (cache_index_path_.empty()) return;
    try {
        std::filesystem::path p(cache_index_path_);
        if (p.has_parent_path()) {
            std::filesystem::create_directories(p.parent_path());
        }
    } catch (...) {
        return;
    }

    std::ofstream out(cache_index_path_, std::ios::trunc);
    if (!out.is_open()) {
        return;
    }
    out << std::hex;
    for (const auto& [hash, entry] : compile_cache_) {
        out << hash << " " << entry.canonical_name << "\n";
    }
}

void BytecodeOrcJIT::dumpAssembly(const std::string &filename) {
    std::error_code ec;
    llvm::raw_fd_ostream os(filename, ec, llvm::sys::fs::OF_None);
    if (!ec) os << last_asm_;
}

void BytecodeOrcJIT::runOptimizations(llvm::Module &module) {
    llvm::LoopAnalysisManager lam;
    llvm::FunctionAnalysisManager fam;
    llvm::CGSCCAnalysisManager cgam;
    llvm::ModuleAnalysisManager mam;
    
    llvm::PassBuilder pb;
    pb.registerModuleAnalyses(mam);
    pb.registerCGSCCAnalyses(cgam);
    pb.registerFunctionAnalyses(fam);
    pb.registerLoopAnalyses(lam);
    pb.crossRegisterProxies(lam, fam, cgam, mam);
    
    llvm::OptimizationLevel level = llvm::OptimizationLevel::O1;
    switch (optimization_level_) {
      case 0: level = llvm::OptimizationLevel::O0; break;
      case 1: level = llvm::OptimizationLevel::O1; break;
      case 2: level = llvm::OptimizationLevel::O2; break;
      case 3: level = llvm::OptimizationLevel::O3; break;
      default: level = llvm::OptimizationLevel::O1; break;
    }
    llvm::ModulePassManager mpm = pb.buildPerModuleDefaultPipeline(level);
    mpm.run(module, mam);
}

void BytecodeOrcJIT::applyProfileGuidedOptimizations(llvm::Module& module) {
    // Apply profile-guided optimizations using collected profile data
    llvm::FunctionAnalysisManager fam;
    llvm::LoopAnalysisManager lam;
    llvm::CGSCCAnalysisManager cgam;
    llvm::ModuleAnalysisManager mam;
    
    llvm::PassBuilder pb;
    pb.registerModuleAnalyses(mam);
    pb.registerCGSCCAnalyses(cgam);
    pb.registerFunctionAnalyses(fam);
    pb.registerLoopAnalyses(lam);
    pb.crossRegisterProxies(lam, fam, cgam, mam);
    
    llvm::OptimizationLevel level = llvm::OptimizationLevel::O2;
    llvm::ModulePassManager mpm = pb.buildPerModuleDefaultPipeline(level);
    mpm.run(module, mam);
}

void BytecodeOrcJIT::saveProfileData(const std::string& path) const {
    (void)path;
}

void BytecodeOrcJIT::loadProfileData(const std::string& path) {
    (void)path;
}

// ============================================================================
// Code Layout Optimization Implementation
// ============================================================================

void BytecodeOrcJIT::optimizeCodeLayout(llvm::Module& /*module*/) {
    // Stub: code-layout optimization requires profile data; no-op without active PGO.
}

void BytecodeOrcJIT::optimizeBlockOrder(llvm::Function* /*function*/) {
    // Stub: block reordering requires profile data; no-op without active PGO.
}

void BytecodeOrcJIT::separateColdBlocks(llvm::Function* /*function*/) {
    // Stub: cold-block separation requires profile data; no-op without active PGO.
}

double BytecodeOrcJIT::getBlockHotness(uint64_t /*func_hash*/, uint32_t /*block_id*/) const {
    return 0.0;
}

// ============================================================================
// Compilation Queue System Implementation
// ============================================================================

void BytecodeOrcJIT::enqueueCompileTask(CompileTask task) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    compile_queue_.push(std::move(task));
    queue_cv_.notify_one();
}

void BytecodeOrcJIT::processCompileQueue() {
    while (compile_thread_running_) {
        CompileTask task;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] { return !compile_queue_.empty() || !compile_thread_running_; });
            if (!compile_thread_running_) break;
            task = std::move(compile_queue_.front());
            compile_queue_.pop();
        }
        
        if (compile_thread_running_ && task.callback) {
        }
    }
}

void BytecodeOrcJIT::shutdownCompileQueue() {
    compile_thread_running_ = false;
    queue_cv_.notify_all();
    if (compile_thread_.joinable()) {
        compile_thread_.join();
    }
}

} // namespace havel::compiler
