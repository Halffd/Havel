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
#include <llvm/Support/CodeGen.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/MC/TargetRegistry.h>
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

    addSym("havel_vm_throw_error",     reinterpret_cast<void*>(&havel_vm_throw_error));
    addSym("havel_gc_write_barrier",   reinterpret_cast<void*>(&havel_gc_write_barrier));
    addSym("havel_gc_register_roots",  reinterpret_cast<void*>(&havel_gc_register_roots));
    addSym("havel_gc_unregister_roots",reinterpret_cast<void*>(&havel_gc_unregister_roots));
    addSym("havel_deoptimize",         reinterpret_cast<void*>(&havel_deoptimize));

    if (auto err = jd.define(absoluteSymbols(std::move(syms)))) {
        llvm::consumeError(std::move(err));
    }
}

BytecodeOrcJIT::~BytecodeOrcJIT() = default;

void BytecodeOrcJIT::initTargetMachine() {
    auto target_triple = llvm::sys::getDefaultTargetTriple();
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
        module->setTargetTriple(target_machine_->getTargetTriple().getTriple());
    }

    translate(func, *module);
    runOptimizations(*module);

    if (dump_ir_) {
        std::cerr << "--- LLVM IR for " << func.name << " ---" << std::endl;
        module->print(llvm::errs(), nullptr);
    }

    if ((debug_jit_ || dump_asm_to_file_) && target_machine_) {
        std::string asm_str;
        llvm::raw_string_ostream ros(asm_str);
        
        // Use a local pass manager for file emission (legacy but necessary for this)
        #include <llvm/IR/LegacyPassManager.h>
        llvm::legacy::PassManager pm;
        if (!target_machine_->addPassesToEmitFile(pm, ros, nullptr, llvm::CodeGenFileType::AssemblyFile)) {
            pm.run(*module);
            ros.flush();
            last_asm_ = asm_str;
            
            if (debug_jit_) {
                 std::cerr << "--- Assembly for " << func.name << " ---" << std::endl;
                 std::cerr << last_asm_ << std::endl;
            }
            
            if (dump_asm_to_file_) {
                 dumpAssembly(func.name + ".s");
                 std::error_code ec;
                 llvm::raw_fd_ostream ir_os(func.name + ".ll", ec, llvm::sys::fs::OF_None);
                 if (!ec) module->print(ir_os, nullptr);
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
        llvm::Value* funcNameConst = B.CreateGlobalStringPtr(func.name);
        B.CreateCall(fn_deopt, {vmArg, left, right, funcNameConst});
        B.CreateBr(mergeBB);

        B.SetInsertPoint(mergeBB);
        llvm::PHINode *phi = B.CreatePHI(i64, 3);
        phi->addIncoming(iBoxed, iExitBB);
        phi->addIncoming(dBoxed, dExitBB);
        phi->addIncoming(makeNull(), deoptBB);
        return phi;
    };

    for (size_t ip = 0; ip < func.instructions.size(); ++ip) {
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
            case OpCode::ADD:
            case OpCode::SUB:
            case OpCode::MUL:
            case OpCode::DIV: {
                llvm::Value* r = vstack.back(); vstack.pop_back();
                llvm::Value* l = vstack.back(); vstack.pop_back();
                vstack.push_back(emitSpecializedBinop(instr.opcode, fb, ip, l, r));
                break;
            }
            case OpCode::RETURN: {
                llvm::Function *fn_unreg = module.getFunction("havel_gc_unregister_roots");
                if (!fn_unreg) fn_unreg = llvm::Function::Create(llvm::FunctionType::get(voidT, {llvm::PointerType::getUnqual(frameType)}, false), llvm::Function::ExternalLinkage, "havel_gc_unregister_roots", &module);
                B.CreateCall(fn_unreg, {frame});
                B.CreateRet(vstack.empty() ? makeNull() : vstack.back());
                break;
            }
            default: break;
        }
    }
}

} // namespace havel::compiler
