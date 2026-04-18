#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IR/Module.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/TargetParser.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Support/CodeGen.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/MC/TargetRegistry.h>

#include <fstream>
#include <sstream>
#include <unordered_set>
#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/Passes/PassBuilder.h>

#include "BytecodeOrcJIT.h"
#include <cstring>
#include <iostream>

using namespace llvm::orc;

namespace havel::compiler {

// ============================================================================
// NaN-boxing constants (Phase 4 refined)
// ============================================================================
static constexpr uint64_t QNAN             = 0x7FF8000000000000ULL;
static constexpr uint64_t TAG_MASK         = 0x0007000000000000ULL;
static constexpr uint64_t PAYLOAD_MASK     = 0x0000FFFFFFFFFFFFULL;
static constexpr uint64_t EXT_PAYLOAD_MASK = 0x00000FFFFFFFFFFFULL;

static constexpr uint64_t INT_TAG          = 0x1;
static constexpr uint64_t EXT_TAG          = 0x7;

static constexpr uint64_t INT_TAG_BITS     = QNAN | (INT_TAG << 48); // 0x7FF9...
static constexpr uint64_t ARRAY_TAG_BITS   = QNAN | (EXT_TAG << 48) | (0x1ULL << 44);

// ============================================================================
// Native Bridge Helpers
// ============================================================================
extern "C" {

void havel_vm_throw_error(void* vm_ptr, const char* msg) {
    if (!vm_ptr) return;
    auto* vm = static_cast<VM*>(vm_ptr);
    vm->throwError(msg);
}

void havel_gc_write_barrier(void* vm_ptr, uint64_t new_value_bits) {
    if (!vm_ptr) return;
    auto* vm = static_cast<VM*>(vm_ptr);
    ::havel::core::Value val;
    std::memcpy(&val, &new_value_bits, sizeof(uint64_t));
    vm->pinExternalRoot(val);
}

void havel_gc_register_roots(void* vm_ptr, JITStackFrame* frame,
                              uint64_t* slot_bits, uint32_t count) {
    if (!vm_ptr || !frame) return;
    auto* vm = static_cast<VM*>(vm_ptr);
    frame->vm = vm_ptr;
    frame->root_count = 0;
    for (uint32_t i = 0; i < count && i < JITStackFrame::MAX_GC_ROOTS; ++i) {
        ::havel::core::Value val;
        std::memcpy(&val, &slot_bits[i], sizeof(uint64_t));
        frame->root_ids[frame->root_count]     = vm->pinExternalRoot(val);
        frame->slot_values[frame->root_count]  = slot_bits[i];
        ++frame->root_count;
    }
}

void havel_gc_unregister_roots(JITStackFrame* frame) {
    if (!frame || !frame->vm) return;
    auto* vm = static_cast<VM*>(frame->vm);
    for (uint32_t i = 0; i < frame->root_count; ++i) {
        vm->unpinExternalRoot(frame->root_ids[i]);
    }
    frame->root_count = 0;
}

void havel_deoptimize(void* vm_ptr, uint64_t l, uint64_t r, const char* func) {
    std::cerr << "[JIT] Deoptimizing in " << func << " for types: 0x"
              << std::hex << l << " op 0x" << r << std::dec << std::endl;
}

// JIT helper for function calls - delegates to VM
uint64_t havel_vm_call(void* vm_ptr, uint64_t* args, uint32_t count) {
    if (!vm_ptr) return 0x7FF8000000000003ULL; // null
    auto* vm = static_cast<VM*>(vm_ptr);
    
    // Convert args array to vector
    std::vector<Value> valArgs;
    for (uint32_t i = 0; i < count; ++i) {
        Value v;
        std::memcpy(&v, &args[i], sizeof(uint64_t));
        valArgs.push_back(v);
    }
    
    // The callee is the first argument (args[0])
    if (valArgs.empty()) {
        return 0x7FF8000000000003ULL; // null
    }
    
    Value callee = valArgs[0];
    valArgs.erase(valArgs.begin()); // Remove callee from args
    
    // Call the function through VM
    Value result = vm->callFunction(callee, valArgs);
    
    uint64_t bits;
    std::memcpy(&bits, &result, sizeof(uint64_t));
    return bits;
}

// JIT helper for tail calls - reuses current frame
uint64_t havel_vm_tail_call(void* vm_ptr, uint64_t* args, uint32_t count) {
    // Tail call: same as regular call but caller handles frame reuse
    return havel_vm_call(vm_ptr, args, count);
}

// Global variable access
uint64_t havel_vm_global_get(void* vm_ptr, uint32_t name_id) {
    if (!vm_ptr) return 0x7FF8000000000003ULL;
    auto* vm = static_cast<VM*>(vm_ptr);
    return vm->getGlobalByIndex(name_id).bits;
}

void havel_vm_global_set(void* vm_ptr, uint32_t name_id, uint64_t value) {
    if (!vm_ptr) return;
    auto* vm = static_cast<VM*>(vm_ptr);
    Value v;
    std::memcpy(&v, &value, sizeof(uint64_t));
    vm->setGlobalByIndex(name_id, v);
}

// Upvalue access for closures
uint64_t havel_vm_upvalue_get(void* vm_ptr, uint32_t slot) {
    if (!vm_ptr) return 0x7FF8000000000003ULL;
    auto* vm = static_cast<VM*>(vm_ptr);
    return vm->getUpvalue(slot).bits;
}

void havel_vm_upvalue_set(void* vm_ptr, uint32_t slot, uint64_t value) {
    if (!vm_ptr) return;
    auto* vm = static_cast<VM*>(vm_ptr);
    Value v;
    std::memcpy(&v, &value, sizeof(uint64_t));
    vm->setUpvalue(slot, v);
}

// Power function
uint64_t havel_vm_pow(uint64_t base_bits, uint64_t exp_bits) {
    Value base, exp;
    std::memcpy(&base, &base_bits, sizeof(uint64_t));
    std::memcpy(&exp, &exp_bits, sizeof(uint64_t));
    
    // Handle integer power
    if (base.isInt() && exp.isInt()) {
        int64_t b = base.asInt();
        int64_t e = exp.asInt();
        if (e < 0) return Value(0.0).bits; // Negative exponent -> 0 for integers
        int64_t result = 1;
        while (e > 0) {
            if (e & 1) result *= b;
            b *= b;
            e >>= 1;
        }
        return Value(result).bits;
    }
    // Float power
    double b = base.asDouble();
    double e = exp.asDouble();
    return Value(std::pow(b, e)).bits;
}

// Array operations
uint64_t havel_vm_array_new(void* vm_ptr) {
    if (!vm_ptr) return 0x7FF8000000000003ULL;
    auto* vm = static_cast<VM*>(vm_ptr);
    return vm->arrayNew().bits;
}

uint64_t havel_vm_array_get(void* vm_ptr, uint64_t arr_bits, uint64_t idx_bits) {
    if (!vm_ptr) return 0x7FF8000000000003ULL;
    auto* vm = static_cast<VM*>(vm_ptr);
    Value arr, idx;
    std::memcpy(&arr, &arr_bits, sizeof(uint64_t));
    std::memcpy(&idx, &idx_bits, sizeof(uint64_t));
    return vm->arrayGet(arr, idx).bits;
}

uint64_t havel_vm_array_set(void* vm_ptr, uint64_t arr_bits, uint64_t idx_bits, uint64_t val_bits) {
    if (!vm_ptr) return 0x7FF8000000000003ULL;
    auto* vm = static_cast<VM*>(vm_ptr);
    Value arr, idx, val;
    std::memcpy(&arr, &arr_bits, sizeof(uint64_t));
    std::memcpy(&idx, &idx_bits, sizeof(uint64_t));
    std::memcpy(&val, &val_bits, sizeof(uint64_t));
    vm->arraySet(arr, idx, val);
    return val_bits;
}

uint64_t havel_vm_array_len(void* vm_ptr, uint64_t arr_bits) {
    if (!vm_ptr) return 0x7FF8000000000003ULL;
    auto* vm = static_cast<VM*>(vm_ptr);
    Value arr;
    std::memcpy(&arr, &arr_bits, sizeof(uint64_t));
    return Value(static_cast<int64_t>(vm->arrayLen(arr))).bits;
}

void havel_vm_array_push(void* vm_ptr, uint64_t arr_bits, uint64_t val_bits) {
    if (!vm_ptr) return;
    auto* vm = static_cast<VM*>(vm_ptr);
    Value arr, val;
    std::memcpy(&arr, &arr_bits, sizeof(uint64_t));
    std::memcpy(&val, &val_bits, sizeof(uint64_t));
    vm->arrayPush(arr, val);
}

// Object operations
uint64_t havel_vm_object_new(void* vm_ptr) {
    if (!vm_ptr) return 0x7FF8000000000003ULL;
    auto* vm = static_cast<VM*>(vm_ptr);
    return vm->objectNew().bits;
}

uint64_t havel_vm_object_get(void* vm_ptr, uint64_t obj_bits, uint32_t key_id) {
    if (!vm_ptr) return 0x7FF8000000000003ULL;
    auto* vm = static_cast<VM*>(vm_ptr);
    Value obj;
    std::memcpy(&obj, &obj_bits, sizeof(uint64_t));
    return vm->objectGet(obj, key_id).bits;
}

uint64_t havel_vm_object_set(void* vm_ptr, uint64_t obj_bits, uint32_t key_id, uint64_t val_bits) {
    if (!vm_ptr) return 0x7FF8000000000003ULL;
    auto* vm = static_cast<VM*>(vm_ptr);
    Value obj, val;
    std::memcpy(&obj, &obj_bits, sizeof(uint64_t));
    std::memcpy(&val, &val_bits, sizeof(uint64_t));
    vm->objectSet(obj, key_id, val);
    return val_bits;
}

// Range and iterator operations
uint64_t havel_vm_range_new(void* vm_ptr, uint64_t start_bits, uint64_t end_bits) {
    if (!vm_ptr) return 0x7FF8000000000003ULL;
    auto* vm = static_cast<VM*>(vm_ptr);
    Value start, end;
    std::memcpy(&start, &start_bits, sizeof(uint64_t));
    std::memcpy(&end, &end_bits, sizeof(uint64_t));
    return vm->rangeNew(start, end).bits;
}

uint64_t havel_vm_iter_new(void* vm_ptr, uint64_t coll_bits) {
    if (!vm_ptr) return 0x7FF8000000000003ULL;
    auto* vm = static_cast<VM*>(vm_ptr);
    Value coll;
    std::memcpy(&coll, &coll_bits, sizeof(uint64_t));
    return vm->iterNew(coll).bits;
}

uint64_t havel_vm_iter_next(void* vm_ptr, uint64_t iter_bits) {
    if (!vm_ptr) return 0x7FF8000000000003ULL;
    auto* vm = static_cast<VM*>(vm_ptr);
    Value iter;
    std::memcpy(&iter, &iter_bits, sizeof(uint64_t));
    return vm->iterNext(iter).bits;
}

// Concurrency primitives
uint64_t havel_vm_thread_new(void* vm_ptr, uint32_t func_id) {
    if (!vm_ptr) return 0x7FF8000000000003ULL;
    auto* vm = static_cast<VM*>(vm_ptr);
    return vm->threadNew(func_id).bits;
}

uint64_t havel_vm_channel_new(void* vm_ptr, uint64_t cap_bits) {
    if (!vm_ptr) return 0x7FF8000000000003ULL;
    auto* vm = static_cast<VM*>(vm_ptr);
    int64_t cap = 0;
    Value capVal;
    std::memcpy(&capVal, &cap_bits, sizeof(uint64_t));
    if (capVal.isInt()) cap = capVal.asInt();
    return vm->channelNew(cap).bits;
}

void havel_vm_channel_send(void* vm_ptr, uint64_t chan_bits, uint64_t val_bits) {
    if (!vm_ptr) return;
    auto* vm = static_cast<VM*>(vm_ptr);
    Value chan, val;
    std::memcpy(&chan, &chan_bits, sizeof(uint64_t));
    std::memcpy(&val, &val_bits, sizeof(uint64_t));
    vm->channelSend(chan, val);
}

uint64_t havel_vm_channel_recv(void* vm_ptr, uint64_t chan_bits) {
    if (!vm_ptr) return 0x7FF8000000000003ULL;
    auto* vm = static_cast<VM*>(vm_ptr);
    Value chan;
    std::memcpy(&chan, &chan_bits, sizeof(uint64_t));
    return vm->channelRecv(chan).bits;
}

uint64_t havel_vm_yield(void* vm_ptr, uint64_t val_bits) {
    if (!vm_ptr) return 0x7FF8000000000003ULL;
    auto* vm = static_cast<VM*>(vm_ptr);
    Value val;
    std::memcpy(&val, &val_bits, sizeof(uint64_t));
    return vm->yieldValue(val).bits;
}

uint64_t havel_vm_await(void* vm_ptr, uint64_t val_bits) {
    if (!vm_ptr) return 0x7FF8000000000003ULL;
    auto* vm = static_cast<VM*>(vm_ptr);
    Value val;
    std::memcpy(&val, &val_bits, sizeof(uint64_t));
    return vm->awaitValue(val).bits;
}

// String operations
uint64_t havel_vm_string_len(void* vm_ptr, uint64_t str_bits) {
    if (!vm_ptr) return 0x7FF8000000000003ULL;
    auto* vm = static_cast<VM*>(vm_ptr);
    Value str;
    std::memcpy(&str, &str_bits, sizeof(uint64_t));
    return Value(static_cast<int64_t>(vm->stringLen(str))).bits;
}

uint64_t havel_vm_string_concat(void* vm_ptr, uint64_t l_bits, uint64_t r_bits) {
    if (!vm_ptr) return 0x7FF8000000000003ULL;
    auto* vm = static_cast<VM*>(vm_ptr);
    Value l, r;
    std::memcpy(&l, &l_bits, sizeof(uint64_t));
    std::memcpy(&r, &r_bits, sizeof(uint64_t));
    return vm->stringConcat(l, r).bits;
}

// Host function call
uint64_t havel_vm_call_host(void* vm_ptr, uint32_t host_idx, uint64_t* args, uint32_t count) {
    if (!vm_ptr) return 0x7FF8000000000003ULL;
    auto* vm = static_cast<VM*>(vm_ptr);
    
    std::vector<Value> valArgs;
    for (uint32_t i = 0; i < count; ++i) {
        Value v;
        std::memcpy(&v, &args[i], sizeof(uint64_t));
        valArgs.push_back(v);
    }
    
    return vm->callHostFunction(host_idx, valArgs).bits;
}

} // extern "C"

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
    initTargetMachine();

    auto jit_or_err = LLJITBuilder().create();
    if (!jit_or_err) {
        llvm::consumeError(jit_or_err.takeError());
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

addSym("havel_vm_throw_error", reinterpret_cast<void*>(&havel_vm_throw_error));
addSym("havel_gc_write_barrier", reinterpret_cast<void*>(&havel_gc_write_barrier));
addSym("havel_gc_register_roots", reinterpret_cast<void*>(&havel_gc_register_roots));
addSym("havel_gc_unregister_roots",reinterpret_cast<void*>(&havel_gc_unregister_roots));
addSym("havel_deoptimize", reinterpret_cast<void*>(&havel_deoptimize));
addSym("havel_vm_call", reinterpret_cast<void*>(&havel_vm_call));
addSym("havel_vm_tail_call", reinterpret_cast<void*>(&havel_vm_tail_call));
addSym("havel_vm_global_get", reinterpret_cast<void*>(&havel_vm_global_get));
addSym("havel_vm_global_set", reinterpret_cast<void*>(&havel_vm_global_set));
addSym("havel_vm_upvalue_get", reinterpret_cast<void*>(&havel_vm_upvalue_get));
addSym("havel_vm_upvalue_set", reinterpret_cast<void*>(&havel_vm_upvalue_set));
addSym("havel_vm_pow", reinterpret_cast<void*>(&havel_vm_pow));
addSym("havel_vm_array_new", reinterpret_cast<void*>(&havel_vm_array_new));
addSym("havel_vm_array_get", reinterpret_cast<void*>(&havel_vm_array_get));
addSym("havel_vm_array_set", reinterpret_cast<void*>(&havel_vm_array_set));
addSym("havel_vm_array_len", reinterpret_cast<void*>(&havel_vm_array_len));
addSym("havel_vm_array_push", reinterpret_cast<void*>(&havel_vm_array_push));
addSym("havel_vm_object_new", reinterpret_cast<void*>(&havel_vm_object_new));
addSym("havel_vm_object_get", reinterpret_cast<void*>(&havel_vm_object_get));
addSym("havel_vm_object_set", reinterpret_cast<void*>(&havel_vm_object_set));
addSym("havel_vm_range_new", reinterpret_cast<void*>(&havel_vm_range_new));
addSym("havel_vm_iter_new", reinterpret_cast<void*>(&havel_vm_iter_new));
addSym("havel_vm_iter_next", reinterpret_cast<void*>(&havel_vm_iter_next));
addSym("havel_vm_thread_new", reinterpret_cast<void*>(&havel_vm_thread_new));
addSym("havel_vm_channel_new", reinterpret_cast<void*>(&havel_vm_channel_new));
addSym("havel_vm_channel_send", reinterpret_cast<void*>(&havel_vm_channel_send));
addSym("havel_vm_channel_recv", reinterpret_cast<void*>(&havel_vm_channel_recv));
addSym("havel_vm_yield", reinterpret_cast<void*>(&havel_vm_yield));
addSym("havel_vm_await", reinterpret_cast<void*>(&havel_vm_await));
addSym("havel_vm_string_len", reinterpret_cast<void*>(&havel_vm_string_len));
addSym("havel_vm_string_concat", reinterpret_cast<void*>(&havel_vm_string_concat));
addSym("havel_vm_call_host", reinterpret_cast<void*>(&havel_vm_call_host));

    if (auto err = jd.define(absoluteSymbols(std::move(syms)))) {
        llvm::consumeError(std::move(err));
    }
}

BytecodeOrcJIT::~BytecodeOrcJIT() = default;

void BytecodeOrcJIT::initTargetMachine() {
    auto target_triple_str = llvm::sys::getDefaultTargetTriple();
    llvm::Triple target_triple(target_triple_str);
    std::string error;
    auto target = llvm::TargetRegistry::lookupTarget(target_triple, error);
    if (target) {
        llvm::TargetOptions opt;
        target_machine_.reset(target->createTargetMachine(
            target_triple, "generic", "", opt, llvm::Reloc::PIC_));
    }
}

void BytecodeOrcJIT::compileFunction(const BytecodeFunction &func) {
    auto context = std::make_unique<llvm::LLVMContext>();
    auto module  = std::make_unique<llvm::Module>(func.name, *context);

    if (target_machine_) {
        module->setDataLayout(target_machine_->createDataLayout());
        module->setTargetTriple(target_machine_->getTargetTriple());
    }

    translate(func, *module);
    runOptimizations(*module);

    if (dump_ir_) {
        std::cerr << "--- LLVM IR for " << func.name << " ---" << std::endl;
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
                        std::cerr << "--- Assembly for " << func.name << " ---" << std::endl;
                        std::cerr << last_asm_ << std::endl;
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
        llvm::consumeError(std::move(err));
        return;
    }

    auto sym = lljit_->lookup(func.name);
    if (!sym) {
        llvm::consumeError(sym.takeError());
        return;
    }

    fptrs_[func.name] = reinterpret_cast<void*>((*sym).getValue());
    func.jit_compiled = true;
}

Value BytecodeOrcJIT::executeCompiled(VM* vm, const std::string &func_name,
                                      const std::vector<Value> &args) {
    auto it = fptrs_.find(func_name);
    if (it == fptrs_.end()) return Value::makeNull();

    typedef uint64_t (*NativeFunc)(void*, const Value*, uint32_t);
    auto func = reinterpret_cast<NativeFunc>(it->second);

    uint64_t res_bits = func(static_cast<void*>(vm), args.data(), static_cast<uint32_t>(args.size()));
    Value res;
    std::memcpy(&res, &res_bits, sizeof(uint64_t));
    return res;
}

bool BytecodeOrcJIT::isCompiled(const std::string &func_name) const {
    return fptrs_.count(func_name) > 0;
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
    
    llvm::ModulePassManager mpm = pb.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O3);
    mpm.run(module, mam);
}

static bool opcodeProducesHeapRef(OpCode op) {
    switch (op) {
        case OpCode::ARRAY_NEW:
        case OpCode::OBJECT_NEW:
        case OpCode::LOAD_CONST:
        case OpCode::LOAD_VAR:
        case OpCode::LOAD_GLOBAL:
        case OpCode::CLOSURE:
            return true;
        default:
            return false;
    }
}

void BytecodeOrcJIT::translate(const BytecodeFunction &func, llvm::Module &module) {
    llvm::LLVMContext &ctx = module.getContext();
    llvm::IRBuilder<> B(ctx);

    llvm::Type *i1  = llvm::Type::getInt1Ty(ctx);
    llvm::Type *i32 = llvm::Type::getInt32Ty(ctx);
    llvm::Type *i64 = llvm::Type::getInt64Ty(ctx);
    llvm::Type *f64 = llvm::Type::getDoubleTy(ctx);
    llvm::Type *voidT = llvm::Type::getVoidTy(ctx);
    llvm::Type *i8p = llvm::PointerType::getUnqual(llvm::Type::getInt8Ty(ctx));
    llvm::Type *i64p = llvm::PointerType::getUnqual(i64);

    std::vector<llvm::Type*> paramTypes = {i8p, i64p, i32};
    llvm::FunctionType *funcType = llvm::FunctionType::get(i64, paramTypes, false);
    llvm::Function *f = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, func.name, &module);

    llvm::BasicBlock *entryBB = llvm::BasicBlock::Create(ctx, "entry", f);
    B.SetInsertPoint(entryBB);

    llvm::Value *vmArg = f->getArg(0);
    llvm::Value *argsArg = f->getArg(1);

    llvm::Type *frameType = llvm::StructType::create(ctx, {i8p, llvm::ArrayType::get(i64, 32), llvm::ArrayType::get(i64, 32), i32}, "JITStackFrame");
    llvm::Value *frame = B.CreateAlloca(frameType, nullptr, "gc_frame");

    llvm::Function *fn_reg = module.getFunction("havel_gc_register_roots");
    if (!fn_reg) fn_reg = llvm::Function::Create(llvm::FunctionType::get(voidT, {i8p, llvm::PointerType::getUnqual(frameType), i64p, i32}, false), llvm::Function::ExternalLinkage, "havel_gc_register_roots", &module);
    B.CreateCall(fn_reg, {vmArg, frame, B.CreateInBoundsGEP(i64, argsArg, llvm::ConstantInt::get(i32, 0)), llvm::ConstantInt::get(i32, func.local_count)});

    std::vector<llvm::Value*> vlocals;
    for (uint32_t i = 0; i < func.local_count; ++i) {
        vlocals.push_back(B.CreateAlloca(i64, nullptr, "l" + std::to_string(i)));
        if (i < func.param_count) {
             B.CreateStore(B.CreateLoad(i64, B.CreateInBoundsGEP(i64, argsArg, llvm::ConstantInt::get(i32, i))), vlocals[i]);
        } else {
             B.CreateStore(llvm::ConstantInt::get(i64, QNAN | (3ULL << 48)), vlocals[i]);
        }
    }

    std::vector<llvm::Value*> vstack;

    auto makeNull = [&]() { return llvm::ConstantInt::get(i64, QNAN | (3ULL << 48)); };

    auto emitWriteBarrier = [&](llvm::Value* val) {
        llvm::Function *fn_wb = module.getFunction("havel_gc_write_barrier");
        if (!fn_wb) fn_wb = llvm::Function::Create(llvm::FunctionType::get(voidT, {i8p, i64}, false), llvm::Function::ExternalLinkage, "havel_gc_write_barrier", &module);
        B.CreateCall(fn_wb, {vmArg, val});
    };

    auto unboxInt = [&](llvm::Value* boxed) {
        llvm::Value* payload = B.CreateAnd(boxed, llvm::ConstantInt::get(i64, PAYLOAD_MASK));
        llvm::Value* shl = B.CreateShl(payload, llvm::ConstantInt::get(i64, 16));
        return B.CreateAShr(shl, llvm::ConstantInt::get(i64, 16));
    };
    
    auto boxInt = [&](llvm::Value* raw) {
        llvm::Value* masked = B.CreateAnd(raw, llvm::ConstantInt::get(i64, PAYLOAD_MASK));
        return B.CreateOr(masked, llvm::ConstantInt::get(i64, INT_TAG_BITS));
    };

    auto isInt48Loc = [&](llvm::Value* v) -> llvm::Value* {
        llvm::Value* tag = B.CreateAnd(v, llvm::ConstantInt::get(i64, TAG_MASK));
        return B.CreateICmpEQ(tag, llvm::ConstantInt::get(i64, 0x0001000000000000ULL));
    };

    auto isDblLoc = [&](llvm::Value* v) -> llvm::Value* {
        return B.CreateICmpNE(B.CreateAnd(v, llvm::ConstantInt::get(i64, QNAN)), llvm::ConstantInt::get(i64, QNAN));
    };

    auto emitSpecializedBinop = [&](OpCode op, const TypeFeedback* fb, size_t ip, llvm::Value* left, llvm::Value* right) -> llvm::Value* {
        std::string pfx = "op" + std::to_string(ip) + "_";
        llvm::BasicBlock *intBB = llvm::BasicBlock::Create(ctx, pfx+"int", f);
        llvm::BasicBlock *chkDblBB = llvm::BasicBlock::Create(ctx, pfx+"chk_dbl", f);
        llvm::BasicBlock *dblBB = llvm::BasicBlock::Create(ctx, pfx+"dbl", f);
        llvm::BasicBlock *deoptBB = llvm::BasicBlock::Create(ctx, pfx+"deopt", f);
        llvm::BasicBlock *mergeBB = llvm::BasicBlock::Create(ctx, pfx+"merge", f);

        llvm::Value* bothInt = B.CreateAnd(isInt48Loc(left), isInt48Loc(right));
B.CreateCondBr(bothInt, intBB, chkDblBB);

        B.SetInsertPoint(intBB);
        llvm::Value *lIv = unboxInt(left);
        llvm::Value *rIv = unboxInt(right);
        llvm::Value *iRes = nullptr;
        if (op == OpCode::ADD) iRes = B.CreateAdd(lIv, rIv);
        else if (op == OpCode::SUB) iRes = B.CreateSub(lIv, rIv);
        else if (op == OpCode::MUL) iRes = B.CreateMul(lIv, rIv);
        else if (op == OpCode::DIV) iRes = B.CreateSDiv(lIv, rIv);
        else if (op == OpCode::MOD) iRes = B.CreateSRem(lIv, rIv);
        else iRes = B.CreateAdd(lIv, rIv); // Fallback
        llvm::Value *iBoxed = boxInt(iRes);
        auto* iExitBB = B.GetInsertBlock();
        B.CreateBr(mergeBB);

        B.SetInsertPoint(chkDblBB);
        llvm::Value* bothDbl = B.CreateAnd(isDblLoc(left), isDblLoc(right));
        B.CreateCondBr(bothDbl, dblBB, deoptBB);

        B.SetInsertPoint(dblBB);
        llvm::Value *lDv = B.CreateBitCast(left, f64);
        llvm::Value *rDv = B.CreateBitCast(right, f64);
        llvm::Value *dRes = nullptr;
        if (op == OpCode::ADD) dRes = B.CreateFAdd(lDv, rDv);
        else if (op == OpCode::SUB) dRes = B.CreateFSub(lDv, rDv);
        else if (op == OpCode::MUL) dRes = B.CreateFMul(lDv, rDv);
        else if (op == OpCode::MOD) dRes = B.CreateFRem(lDv, rDv);
        else dRes = B.CreateFDiv(lDv, rDv);
        llvm::Value *dBoxed = B.CreateBitCast(dRes, i64);
        auto* dExitBB = B.GetInsertBlock();
        B.CreateBr(mergeBB);

        B.SetInsertPoint(chkDblBB);
        llvm::Value* bothDbl = B.CreateAnd(isDblLoc(left), isDblLoc(right));
        B.CreateCondBr(bothDbl, dblBB, deoptBB);

        B.SetInsertPoint(dblBB);
        llvm::Value *lDv = B.CreateBitCast(left, f64);
        llvm::Value *rDv = B.CreateBitCast(right, f64);
        llvm::Value *dRes = nullptr;
        if      (op == OpCode::ADD) dRes = B.CreateFAdd(lDv, rDv);
        else if (op == OpCode::SUB) dRes = B.CreateFSub(lDv, rDv);
        else if (op == OpCode::MUL) dRes = B.CreateFMul(lDv, rDv);
        else                        dRes = B.CreateFDiv(lDv, rDv);
        llvm::Value *dBoxed = B.CreateBitCast(dRes, i64);
        auto* dExitBB = B.GetInsertBlock();
        B.CreateBr(mergeBB);

        B.SetInsertPoint(deoptBB); 
        llvm::Function *fn_deopt = module.getFunction("havel_deoptimize");
        if (!fn_deopt) fn_deopt = llvm::Function::Create(llvm::FunctionType::get(voidT, {i8p, i64, i64, i8p}, false), llvm::Function::ExternalLinkage, "havel_deoptimize", &module);
        
        // Create global string constant for function name
        llvm::Constant *funcNameStr = llvm::ConstantDataArray::getString(module.getContext(), func.name);
        llvm::GlobalVariable *gv = new llvm::GlobalVariable(
            module, funcNameStr->getType(), true,
            llvm::GlobalValue::PrivateLinkage, funcNameStr);
        llvm::Value* funcNameConst = B.CreatePointerCast(gv, i8p);
        
        B.CreateCall(fn_deopt, {vmArg, left, right, funcNameConst});
        B.CreateBr(mergeBB);

        B.SetInsertPoint(mergeBB);
        llvm::PHINode *phi = B.CreatePHI(i64, 3);
        phi->addIncoming(iBoxed, iExitBB);
        phi->addIncoming(dBoxed, dExitBB);
phi->addIncoming(makeNull(), deoptBB);
        return phi;
    };

    // First pass: identify jump targets and create basic blocks
    std::vector<llvm::BasicBlock*> basicBlocks(func.instructions.size(), nullptr);
    std::unordered_set<size_t> jumpTargets;

    // Find all jump targets
    for (size_t ip = 0; ip < func.instructions.size(); ++ip) {
        const auto &instr = func.instructions[ip];
        if (instr.opcode == OpCode::JUMP) {
            jumpTargets.insert(instr.operands[0].asInt());
        } else if (instr.opcode == OpCode::JUMP_IF_FALSE ||
                   instr.opcode == OpCode::JUMP_IF_TRUE ||
                   instr.opcode == OpCode::JUMP_IF_NULL) {
            jumpTargets.insert(instr.operands[1].asInt());
        }
    }
    // Entry point is always a jump target
    jumpTargets.insert(0);

    // Create basic blocks for jump targets
    for (size_t ip : jumpTargets) {
        if (ip < func.instructions.size()) {
            basicBlocks[ip] = llvm::BasicBlock::Create(ctx, "ip" + std::to_string(ip), f);
        }
    }

    // Second pass: emit instructions with control flow
    for (size_t ip = 0; ip < func.instructions.size(); ++ip) {
        // If this is a jump target, switch to its basic block
        if (basicBlocks[ip] != nullptr) {
            // Terminate previous block if needed
            if (B.GetInsertBlock()->getTerminator() == nullptr) {
                B.CreateBr(basicBlocks[ip]);
            }
            B.SetInsertPoint(basicBlocks[ip]);
        }

        // Skip if current block is already terminated
        if (B.GetInsertBlock()->getTerminator() != nullptr) {
            continue;
        }

        const auto &instr = func.instructions[ip];
        const TypeFeedback* fb = (ip < func.type_feedback.size()) ? &func.type_feedback[ip] : nullptr;

        switch (instr.opcode) {
        case OpCode::LOAD_CONST: {
            uint64_t bits; std::memcpy(&bits, &func.constants[instr.operands[0].asInt()], 8);
            vstack.push_back(llvm::ConstantInt::get(i64, bits));
            break;
        }
        case OpCode::LOAD_VAR: vstack.push_back(B.CreateLoad(i64, vlocals[instr.operands[0].asInt()])); break;
        case OpCode::STORE_VAR: {
            llvm::Value* v = vstack.back(); vstack.pop_back();
            B.CreateStore(v, vlocals[instr.operands[0].asInt()]);
            if (opcodeProducesHeapRef(instr.opcode)) emitWriteBarrier(v);
            break;
        }
        case OpCode::POP: vstack.pop_back(); break;
        case OpCode::DUP: vstack.push_back(vstack.back()); break;

        case OpCode::ADD:
        case OpCode::SUB:
        case OpCode::MUL:
        case OpCode::DIV: {
            llvm::Value* r = vstack.back(); vstack.pop_back();
            llvm::Value* l = vstack.back(); vstack.pop_back();
            vstack.push_back(emitSpecializedBinop(instr.opcode, fb, ip, l, r));
            break;
        }
        case OpCode::NEGATE: {
            llvm::Value* v = vstack.back(); vstack.pop_back();
            // Negate: for int48, unbox, negate, rebox; for double, just fneg
            llvm::Value* isInt = isInt48Loc(v);
            llvm::BasicBlock *intBB = llvm::BasicBlock::Create(ctx, "neg_int", f);
            llvm::BasicBlock *dblBB = llvm::BasicBlock::Create(ctx, "neg_dbl", f);
            llvm::BasicBlock *mergeBB = llvm::BasicBlock::Create(ctx, "neg_merge", f);
            B.CreateCondBr(isInt, intBB, dblBB);
            B.SetInsertPoint(intBB);
            llvm::Value* intNeg = boxInt(B.CreateNeg(unboxInt(v)));
            B.CreateBr(mergeBB);
            B.SetInsertPoint(dblBB);
            llvm::Value* dblNeg = B.CreateBitCast(B.CreateFNeg(B.CreateBitCast(v, f64)), i64);
            B.CreateBr(mergeBB);
            B.SetInsertPoint(mergeBB);
            llvm::PHINode* phi = B.CreatePHI(i64, 2);
            phi->addIncoming(intNeg, intBB);
            phi->addIncoming(dblNeg, dblBB);
            vstack.push_back(phi);
            break;
        }

        // Comparisons
        case OpCode::EQ:
        case OpCode::NEQ:
        case OpCode::LT:
        case OpCode::LTE:
        case OpCode::GT:
        case OpCode::GTE: {
            llvm::Value* r = vstack.back(); vstack.pop_back();
            llvm::Value* l = vstack.back(); vstack.pop_back();
            // Compare as integers (boxed values)
            llvm::Value* cmp = nullptr;
            switch (instr.opcode) {
                case OpCode::EQ: cmp = B.CreateICmpEQ(l, r); break;
                case OpCode::NEQ: cmp = B.CreateICmpNE(l, r); break;
                case OpCode::LT: cmp = B.CreateICmpSLT(l, r); break;
                case OpCode::LTE: cmp = B.CreateICmpSLE(l, r); break;
                case OpCode::GT: cmp = B.CreateICmpSGT(l, r); break;
                case OpCode::GTE: cmp = B.CreateICmpSGE(l, r); break;
                default: break;
            }
            // Box boolean result as int48: true = 1, false = 0
            vstack.push_back(boxInt(B.CreateZExt(cmp, i64)));
            break;
        }
        case OpCode::IS_NULL: {
            llvm::Value* v = vstack.back(); vstack.pop_back();
            llvm::Value* isNull = B.CreateICmpEQ(v, makeNull());
            vstack.push_back(boxInt(B.CreateZExt(isNull, i64)));
            break;
        }
        case OpCode::NOT: {
            llvm::Value* v = vstack.back(); vstack.pop_back();
            // Truthy check: null, 0, false are falsy
            llvm::Value* isFalsy = B.CreateOr(
                B.CreateICmpEQ(v, makeNull()),
                B.CreateICmpEQ(v, boxInt(llvm::ConstantInt::get(i64, 0)))
            );
            vstack.push_back(boxInt(B.CreateZExt(isFalsy, i64)));
            break;
        }

        // Control flow
        case OpCode::JUMP: {
            size_t target = instr.operands[0].asInt();
            if (basicBlocks[target]) {
                B.CreateBr(basicBlocks[target]);
            }
            break;
        }
        case OpCode::JUMP_IF_FALSE: {
            size_t target = instr.operands[1].asInt();
            llvm::Value* cond = vstack.back(); vstack.pop_back();
            // Falsy: null, 0, false
            llvm::Value* isFalsy = B.CreateOr(
                B.CreateICmpEQ(cond, makeNull()),
                B.CreateICmpEQ(cond, boxInt(llvm::ConstantInt::get(i64, 0)))
            );
            if (basicBlocks[target]) {
                B.CreateCondBr(isFalsy, basicBlocks[target], B.GetInsertBlock());
            }
            break;
        }
        case OpCode::JUMP_IF_TRUE: {
            size_t target = instr.operands[1].asInt();
            llvm::Value* cond = vstack.back(); vstack.pop_back();
            llvm::Value* isTruthy = B.CreateAnd(
                B.CreateICmpNE(cond, makeNull()),
                B.CreateICmpNE(cond, boxInt(llvm::ConstantInt::get(i64, 0)))
            );
            if (basicBlocks[target]) {
                B.CreateCondBr(isTruthy, basicBlocks[target], B.GetInsertBlock());
            }
            break;
        }
        case OpCode::JUMP_IF_NULL: {
            size_t target = instr.operands[1].asInt();
            llvm::Value* v = vstack.back();
            llvm::Value* isNull = B.CreateICmpEQ(v, makeNull());
            vstack.pop_back(); // Coalesce consumes the value
            if (basicBlocks[target]) {
                B.CreateCondBr(isNull, basicBlocks[target], B.GetInsertBlock());
            }
            break;
        }

        // Function calls
        case OpCode::CALL: {
            uint32_t argCount = instr.operands[0].asInt();
            // Collect args from stack (in reverse order for calling convention)
            std::vector<llvm::Value*> args;
            args.push_back(vmArg);
            // Create args array on stack
            llvm::Value* argsArray = B.CreateAlloca(llvm::ArrayType::get(i64, argCount), nullptr, "call_args");
            for (uint32_t i = 0; i < argCount; ++i) {
                llvm::Value* arg = vstack.back(); vstack.pop_back();
                B.CreateStore(arg, B.CreateInBoundsGEP(llvm::ArrayType::get(i64, argCount), argsArray,
                    {llvm::ConstantInt::get(i32, 0), llvm::ConstantInt::get(i32, argCount - 1 - i)}));
            }
            args.push_back(B.CreateInBoundsGEP(llvm::ArrayType::get(i64, argCount), argsArray,
                {llvm::ConstantInt::get(i32, 0), llvm::ConstantInt::get(i32, 0)}));
            args.push_back(llvm::ConstantInt::get(i32, argCount));

            // Call havel_vm_call(vm, args, count)
            llvm::Function* fnCall = module.getFunction("havel_vm_call");
            if (!fnCall) {
                fnCall = llvm::Function::Create(
                    llvm::FunctionType::get(i64, {i8p, i64p, i32}, false),
                    llvm::Function::ExternalLinkage, "havel_vm_call", &module);
            }
            vstack.push_back(B.CreateCall(fnCall, args));
            break;
        }
        case OpCode::TAIL_CALL: {
            uint32_t argCount = instr.operands[0].asInt();
            // Collect args from stack
            std::vector<llvm::Value*> args;
            args.push_back(vmArg);
            llvm::Value* argsArray = B.CreateAlloca(llvm::ArrayType::get(i64, argCount), nullptr, "tail_args");
            for (uint32_t i = 0; i < argCount; ++i) {
                llvm::Value* arg = vstack.back(); vstack.pop_back();
                B.CreateStore(arg, B.CreateInBoundsGEP(llvm::ArrayType::get(i64, argCount), argsArray,
                    {llvm::ConstantInt::get(i32, 0), llvm::ConstantInt::get(i32, argCount - 1 - i)}));
            }
            args.push_back(B.CreateInBoundsGEP(llvm::ArrayType::get(i64, argCount), argsArray,
                {llvm::ConstantInt::get(i32, 0), llvm::ConstantInt::get(i32, 0)}));
            args.push_back(llvm::ConstantInt::get(i32, argCount));

            // Unregister GC roots before tail call
            llvm::Function *fn_unreg = module.getFunction("havel_gc_unregister_roots");
            if (!fn_unreg) fn_unreg = llvm::Function::Create(llvm::FunctionType::get(voidT, {llvm::PointerType::getUnqual(frameType)}, false), llvm::Function::ExternalLinkage, "havel_gc_unregister_roots", &module);
            B.CreateCall(fn_unreg, {frame});

            // Call havel_vm_tail_call which handles frame reuse
            llvm::Function* fnTailCall = module.getFunction("havel_vm_tail_call");
            if (!fnTailCall) {
                fnTailCall = llvm::Function::Create(
                    llvm::FunctionType::get(i64, {i8p, i64p, i32}, false),
                    llvm::Function::ExternalLinkage, "havel_vm_tail_call", &module);
            }
            // Musttail call for proper tail call optimization
            llvm::CallInst* call = B.CreateCall(fnTailCall, args);
            call->setTailCallKind(llvm::CallInst::TCK_MustTail);
            B.CreateRet(call);
            break;
        }

        case OpCode::RETURN: {
            llvm::Function *fn_unreg = module.getFunction("havel_gc_unregister_roots");
            if (!fn_unreg) fn_unreg = llvm::Function::Create(llvm::FunctionType::get(voidT, {llvm::PointerType::getUnqual(frameType)}, false), llvm::Function::ExternalLinkage, "havel_gc_unregister_roots", &module);
            B.CreateCall(fn_unreg, {frame});
            B.CreateRet(vstack.empty() ? makeNull() : vstack.back());
            break;
        }

        // Global and upvalue access - critical for closures
        case OpCode::LOAD_GLOBAL: {
            uint32_t nameId = instr.operands[0].asInt();
            llvm::Function* fnGet = module.getFunction("havel_vm_global_get");
            if (!fnGet) {
                fnGet = llvm::Function::Create(
                    llvm::FunctionType::get(i64, {i8p, i32}, false),
                    llvm::Function::ExternalLinkage, "havel_vm_global_get", &module);
            }
            vstack.push_back(B.CreateCall(fnGet, {vmArg, llvm::ConstantInt::get(i32, nameId)}));
            break;
        }
        case OpCode::STORE_GLOBAL: {
            uint32_t nameId = instr.operands[0].asInt();
            llvm::Value* v = vstack.back(); vstack.pop_back();
            llvm::Function* fnSet = module.getFunction("havel_vm_global_set");
            if (!fnSet) {
                fnSet = llvm::Function::Create(
                    llvm::FunctionType::get(voidT, {i8p, i32, i64}, false),
                    llvm::Function::ExternalLinkage, "havel_vm_global_set", &module);
            }
            B.CreateCall(fnSet, {vmArg, llvm::ConstantInt::get(i32, nameId), v});
            vstack.push_back(v); // Store returns the value
            break;
        }
        case OpCode::LOAD_UPVALUE: {
            uint32_t slot = instr.operands[0].asInt();
            llvm::Function* fnUp = module.getFunction("havel_vm_upvalue_get");
            if (!fnUp) {
                fnUp = llvm::Function::Create(
                    llvm::FunctionType::get(i64, {i8p, i32}, false),
                    llvm::Function::ExternalLinkage, "havel_vm_upvalue_get", &module);
            }
            vstack.push_back(B.CreateCall(fnUp, {vmArg, llvm::ConstantInt::get(i32, slot)}));
            break;
        }
        case OpCode::STORE_UPVALUE: {
            uint32_t slot = instr.operands[0].asInt();
            llvm::Value* v = vstack.back(); vstack.pop_back();
            llvm::Function* fnUp = module.getFunction("havel_vm_upvalue_set");
            if (!fnUp) {
                fnUp = llvm::Function::Create(
                    llvm::FunctionType::get(voidT, {i8p, i32, i64}, false),
                    llvm::Function::ExternalLinkage, "havel_vm_upvalue_set", &module);
            }
            B.CreateCall(fnUp, {vmArg, llvm::ConstantInt::get(i32, slot), v});
            vstack.push_back(v);
            break;
        }

        // Arithmetic - MOD and POW for loops
        case OpCode::MOD: {
            llvm::Value* r = vstack.back(); vstack.pop_back();
            llvm::Value* l = vstack.back(); vstack.pop_back();
            // Use specialized path for MOD
            vstack.push_back(emitSpecializedBinop(OpCode::MOD, fb, ip, l, r));
            break;
        }
        case OpCode::POW: {
            // Power requires runtime call (no LLVM pow intrinsic for integers)
            llvm::Value* exp = vstack.back(); vstack.pop_back();
            llvm::Value* base = vstack.back(); vstack.pop_back();
            llvm::Function* fnPow = module.getFunction("havel_vm_pow");
            if (!fnPow) {
                fnPow = llvm::Function::Create(
                    llvm::FunctionType::get(i64, {i64, i64}, false),
                    llvm::Function::ExternalLinkage, "havel_vm_pow", &module);
            }
            vstack.push_back(B.CreateCall(fnPow, {base, exp}));
            break;
        }

        // Logical operations
        case OpCode::AND: {
            // Short-circuit AND - handled by JUMP_IF_FALSE, but also as binary op
            llvm::Value* r = vstack.back(); vstack.pop_back();
            llvm::Value* l = vstack.back(); vstack.pop_back();
            // AND: result is l if l is falsy, else r
            llvm::Value* lFalsy = B.CreateOr(
                B.CreateICmpEQ(l, makeNull()),
                B.CreateICmpEQ(l, boxInt(llvm::ConstantInt::get(i64, 0)))
            );
            vstack.push_back(B.CreateSelect(lFalsy, l, r));
            break;
        }
        case OpCode::OR: {
            // Short-circuit OR
            llvm::Value* r = vstack.back(); vstack.pop_back();
            llvm::Value* l = vstack.back(); vstack.pop_back();
            // OR: result is l if l is truthy, else r
            llvm::Value* lTruthy = B.CreateAnd(
                B.CreateICmpNE(l, makeNull()),
                B.CreateICmpNE(l, boxInt(llvm::ConstantInt::get(i64, 0)))
            );
            vstack.push_back(B.CreateSelect(lTruthy, l, r));
            break;
        }

        // Array operations - critical for loops
        case OpCode::ARRAY_NEW: {
            llvm::Function* fnNew = module.getFunction("havel_vm_array_new");
            if (!fnNew) {
                fnNew = llvm::Function::Create(
                    llvm::FunctionType::get(i64, {i8p}, false),
                    llvm::Function::ExternalLinkage, "havel_vm_array_new", &module);
            }
            vstack.push_back(B.CreateCall(fnNew, {vmArg}));
            break;
        }
        case OpCode::ARRAY_GET: {
            llvm::Value* idx = vstack.back(); vstack.pop_back();
            llvm::Value* arr = vstack.back(); vstack.pop_back();
            llvm::Function* fnGet = module.getFunction("havel_vm_array_get");
            if (!fnGet) {
                fnGet = llvm::Function::Create(
                    llvm::FunctionType::get(i64, {i8p, i64, i64}, false),
                    llvm::Function::ExternalLinkage, "havel_vm_array_get", &module);
            }
            vstack.push_back(B.CreateCall(fnGet, {vmArg, arr, idx}));
            break;
        }
        case OpCode::ARRAY_SET: {
            llvm::Value* val = vstack.back(); vstack.pop_back();
            llvm::Value* idx = vstack.back(); vstack.pop_back();
            llvm::Value* arr = vstack.back(); vstack.pop_back();
            llvm::Function* fnSet = module.getFunction("havel_vm_array_set");
            if (!fnSet) {
                fnSet = llvm::Function::Create(
                    llvm::FunctionType::get(i64, {i8p, i64, i64, i64}, false),
                    llvm::Function::ExternalLinkage, "havel_vm_array_set", &module);
            }
            vstack.push_back(B.CreateCall(fnSet, {vmArg, arr, idx, val}));
            break;
        }
        case OpCode::ARRAY_LEN: {
            llvm::Value* arr = vstack.back(); vstack.pop_back();
            llvm::Function* fnLen = module.getFunction("havel_vm_array_len");
            if (!fnLen) {
                fnLen = llvm::Function::Create(
                    llvm::FunctionType::get(i64, {i8p, i64}, false),
                    llvm::Function::ExternalLinkage, "havel_vm_array_len", &module);
            }
            vstack.push_back(B.CreateCall(fnLen, {vmArg, arr}));
            break;
        }
        case OpCode::ARRAY_PUSH: {
            llvm::Value* val = vstack.back(); vstack.pop_back();
            llvm::Value* arr = vstack.back(); vstack.pop_back();
            llvm::Function* fnPush = module.getFunction("havel_vm_array_push");
            if (!fnPush) {
                fnPush = llvm::Function::Create(
                    llvm::FunctionType::get(voidT, {i8p, i64, i64}, false),
                    llvm::Function::ExternalLinkage, "havel_vm_array_push", &module);
            }
            B.CreateCall(fnPush, {vmArg, arr, val});
            vstack.push_back(arr); // Push array back
            break;
        }

        // Object operations
        case OpCode::OBJECT_NEW: {
            llvm::Function* fnNew = module.getFunction("havel_vm_object_new");
            if (!fnNew) {
                fnNew = llvm::Function::Create(
                    llvm::FunctionType::get(i64, {i8p}, false),
                    llvm::Function::ExternalLinkage, "havel_vm_object_new", &module);
            }
            vstack.push_back(B.CreateCall(fnNew, {vmArg}));
            break;
        }
        case OpCode::OBJECT_GET: {
            uint32_t keyId = instr.operands[0].asInt();
            llvm::Value* obj = vstack.back(); vstack.pop_back();
            llvm::Function* fnGet = module.getFunction("havel_vm_object_get");
            if (!fnGet) {
                fnGet = llvm::Function::Create(
                    llvm::FunctionType::get(i64, {i8p, i64, i32}, false),
                    llvm::Function::ExternalLinkage, "havel_vm_object_get", &module);
            }
            vstack.push_back(B.CreateCall(fnGet, {vmArg, obj, llvm::ConstantInt::get(i32, keyId)}));
            break;
        }
        case OpCode::OBJECT_SET: {
            uint32_t keyId = instr.operands[0].asInt();
            llvm::Value* val = vstack.back(); vstack.pop_back();
            llvm::Value* obj = vstack.back(); vstack.pop_back();
            llvm::Function* fnSet = module.getFunction("havel_vm_object_set");
            if (!fnSet) {
                fnSet = llvm::Function::Create(
                    llvm::FunctionType::get(i64, {i8p, i64, i32, i64}, false),
                    llvm::Function::ExternalLinkage, "havel_vm_object_set", &module);
            }
            vstack.push_back(B.CreateCall(fnSet, {vmArg, obj, llvm::ConstantInt::get(i32, keyId), val}));
            break;
        }

        // Range and iterators - critical for for loops
        case OpCode::RANGE_NEW: {
            llvm::Value* end = vstack.back(); vstack.pop_back();
            llvm::Value* start = vstack.back(); vstack.pop_back();
            llvm::Function* fnRange = module.getFunction("havel_vm_range_new");
            if (!fnRange) {
                fnRange = llvm::Function::Create(
                    llvm::FunctionType::get(i64, {i8p, i64, i64}, false),
                    llvm::Function::ExternalLinkage, "havel_vm_range_new", &module);
            }
            vstack.push_back(B.CreateCall(fnRange, {vmArg, start, end}));
            break;
        }
        case OpCode::ITER_NEW: {
            llvm::Value* coll = vstack.back(); vstack.pop_back();
            llvm::Function* fnIter = module.getFunction("havel_vm_iter_new");
            if (!fnIter) {
                fnIter = llvm::Function::Create(
                    llvm::FunctionType::get(i64, {i8p, i64}, false),
                    llvm::Function::ExternalLinkage, "havel_vm_iter_new", &module);
            }
            vstack.push_back(B.CreateCall(fnIter, {vmArg, coll}));
            break;
        }
        case OpCode::ITER_NEXT: {
            llvm::Value* iter = vstack.back();
            llvm::Function* fnNext = module.getFunction("havel_vm_iter_next");
            if (!fnNext) {
                fnNext = llvm::Function::Create(
                    llvm::FunctionType::get(i64, {i8p, i64}, false),
                    llvm::Function::ExternalLinkage, "havel_vm_iter_next", &module);
            }
            // Returns value or null if exhausted; stack has [iter], result pushed
            llvm::Value* result = B.CreateCall(fnNext, {vmArg, iter});
            // Don't pop iter - it's kept for next iteration
            vstack.push_back(result);
            break;
        }

        // Concurrency primitives - threads, coroutines, channels
        case OpCode::THREAD_NEW: {
            uint32_t funcId = instr.operands[0].asInt();
            llvm::Function* fnThread = module.getFunction("havel_vm_thread_new");
            if (!fnThread) {
                fnThread = llvm::Function::Create(
                    llvm::FunctionType::get(i64, {i8p, i32}, false),
                    llvm::Function::ExternalLinkage, "havel_vm_thread_new", &module);
            }
            vstack.push_back(B.CreateCall(fnThread, {vmArg, llvm::ConstantInt::get(i32, funcId)}));
            break;
        }
        case OpCode::CHANNEL_NEW: {
            llvm::Value* cap = vstack.empty() ? llvm::ConstantInt::get(i64, 0) : vstack.back();
            if (!vstack.empty()) vstack.pop_back();
            llvm::Function* fnChan = module.getFunction("havel_vm_channel_new");
            if (!fnChan) {
                fnChan = llvm::Function::Create(
                    llvm::FunctionType::get(i64, {i8p, i64}, false),
                    llvm::Function::ExternalLinkage, "havel_vm_channel_new", &module);
            }
            vstack.push_back(B.CreateCall(fnChan, {vmArg, cap}));
            break;
        }
        case OpCode::CHANNEL_SEND: {
            llvm::Value* val = vstack.back(); vstack.pop_back();
            llvm::Value* chan = vstack.back(); vstack.pop_back();
            llvm::Function* fnSend = module.getFunction("havel_vm_channel_send");
            if (!fnSend) {
                fnSend = llvm::Function::Create(
                    llvm::FunctionType::get(voidT, {i8p, i64, i64}, false),
                    llvm::Function::ExternalLinkage, "havel_vm_channel_send", &module);
            }
            B.CreateCall(fnSend, {vmArg, chan, val});
            vstack.push_back(makeNull());
            break;
        }
        case OpCode::CHANNEL_RECV: {
            llvm::Value* chan = vstack.back(); vstack.pop_back();
            llvm::Function* fnRecv = module.getFunction("havel_vm_channel_recv");
            if (!fnRecv) {
                fnRecv = llvm::Function::Create(
                    llvm::FunctionType::get(i64, {i8p, i64}, false),
                    llvm::Function::ExternalLinkage, "havel_vm_channel_recv", &module);
            }
            vstack.push_back(B.CreateCall(fnRecv, {vmArg, chan}));
            break;
        }
        case OpCode::YIELD: {
            llvm::Value* val = vstack.empty() ? makeNull() : vstack.back();
            if (!vstack.empty()) vstack.pop_back();
            llvm::Function* fnYield = module.getFunction("havel_vm_yield");
            if (!fnYield) {
                fnYield = llvm::Function::Create(
                    llvm::FunctionType::get(i64, {i8p, i64}, false),
                    llvm::Function::ExternalLinkage, "havel_vm_yield", &module);
            }
            vstack.push_back(B.CreateCall(fnYield, {vmArg, val}));
            break;
        }
        case OpCode::AWAIT: {
            llvm::Value* val = vstack.back(); vstack.pop_back();
            llvm::Function* fnAwait = module.getFunction("havel_vm_await");
            if (!fnAwait) {
                fnAwait = llvm::Function::Create(
                    llvm::FunctionType::get(i64, {i8p, i64}, false),
                    llvm::Function::ExternalLinkage, "havel_vm_await", &module);
            }
            vstack.push_back(B.CreateCall(fnAwait, {vmArg, val}));
            break;
        }

        // String operations
        case OpCode::STRING_LEN: {
            llvm::Value* str = vstack.back(); vstack.pop_back();
            llvm::Function* fnLen = module.getFunction("havel_vm_string_len");
            if (!fnLen) {
                fnLen = llvm::Function::Create(
                    llvm::FunctionType::get(i64, {i8p, i64}, false),
                    llvm::Function::ExternalLinkage, "havel_vm_string_len", &module);
            }
            vstack.push_back(B.CreateCall(fnLen, {vmArg, str}));
            break;
        }
        case OpCode::STRING_CONCAT: {
            llvm::Value* r = vstack.back(); vstack.pop_back();
            llvm::Value* l = vstack.back(); vstack.pop_back();
            llvm::Function* fnCat = module.getFunction("havel_vm_string_concat");
            if (!fnCat) {
                fnCat = llvm::Function::Create(
                    llvm::FunctionType::get(i64, {i8p, i64, i64}, false),
                    llvm::Function::ExternalLinkage, "havel_vm_string_concat", &module);
            }
            vstack.push_back(B.CreateCall(fnCat, {vmArg, l, r}));
            break;
        }

        // Stack manipulation
        case OpCode::PUSH_NULL:
            vstack.push_back(makeNull());
            break;
        case OpCode::SWAP: {
            llvm::Value* b = vstack.back(); vstack.pop_back();
            llvm::Value* a = vstack.back(); vstack.pop_back();
            vstack.push_back(b);
            vstack.push_back(a);
            break;
        }

        // Host function calls
        case OpCode::CALL_HOST: {
            uint32_t hostIdx = instr.operands[0].asInt();
            uint32_t argCount = instr.operands[1].asInt();
            // Collect args
            llvm::Value* argsArray = B.CreateAlloca(llvm::ArrayType::get(i64, argCount), nullptr, "host_args");
            for (uint32_t i = 0; i < argCount; ++i) {
                llvm::Value* arg = vstack.back(); vstack.pop_back();
                B.CreateStore(arg, B.CreateInBoundsGEP(llvm::ArrayType::get(i64, argCount), argsArray,
                    {llvm::ConstantInt::get(i32, 0), llvm::ConstantInt::get(i32, argCount - 1 - i)}));
            }
            llvm::Function* fnHost = module.getFunction("havel_vm_call_host");
            if (!fnHost) {
                fnHost = llvm::Function::Create(
                    llvm::FunctionType::get(i64, {i8p, i32, i64p, i32}, false),
                    llvm::Function::ExternalLinkage, "havel_vm_call_host", &module);
            }
            vstack.push_back(B.CreateCall(fnHost, {
                vmArg,
                llvm::ConstantInt::get(i32, hostIdx),
                B.CreateInBoundsGEP(llvm::ArrayType::get(i64, argCount), argsArray,
                    {llvm::ConstantInt::get(i32, 0), llvm::ConstantInt::get(i32, 0)}),
                llvm::ConstantInt::get(i32, argCount)
            }));
            break;
        }

        default: break;
        }
    }
}

} // namespace havel::compiler
