#include "BytecodeOrcJIT.h"
#include <llvm/Support/TargetSelect.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IR/IRBuilder.h>
#include <iostream>

using namespace llvm;
using namespace llvm::orc;

namespace havel::compiler {

void BytecodeOrcJIT::InitializeLLVM() {
    static bool initialized = false;
    if (initialized) return;
    
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();
    InitializeNativeTargetAsmParser();
    initialized = true;
}

// Native bridge helpers
extern "C" {
    void havel_vm_throw_error(void* vm_ptr, const char* msg) {
        if (vm_ptr) {
            auto* vm = static_cast<VM*>(vm_ptr);
            vm->throwError(msg);
        }
    }
}

BytecodeOrcJIT::BytecodeOrcJIT() {
    InitializeLLVM();
    
    auto jit_or_err = LLJITBuilder().create();
    if (!jit_or_err) {
        consumeError(jit_or_err.takeError());
        return;
    }
    lljit = std::move(*jit_or_err);

    // Map host symbols
    auto &jd = lljit->getMainJITDylib();
    auto &es = lljit->getExecutionSession();
    
    SymbolMap hostSymbols;
    hostSymbols[es.intern("havel_vm_throw_error")] = {
        ExecutorAddr::fromPtr(&havel_vm_throw_error),
        JITSymbolFlags::Exported
    };
    
    if (auto err = jd.define(absoluteSymbols(std::move(hostSymbols)))) {
        consumeError(std::move(err));
    }
}

BytecodeOrcJIT::~BytecodeOrcJIT() = default;

void BytecodeOrcJIT::compileFunction(const BytecodeFunction &func) {
    auto context = std::make_unique<LLVMContext>();
    auto module = std::make_unique<Module>(func.name, *context);
    
    translate(func, *module);
    
    // Add module to JIT
    if (auto err = lljit->addIRModule(ThreadSafeModule(std::move(module), std::move(context)))) {
        consumeError(std::move(err));
        return;
    }
    
    // Lookup the function pointer
    auto sym_or_err = lljit->lookup(func.name);
    if (!sym_or_err) {
        consumeError(sym_or_err.takeError());
        return;
    }
    
    function_pointers[func.name] = reinterpret_cast<void*>(sym_or_err->getValue().getValue());
    const_cast<BytecodeFunction&>(func).jit_compiled = true;
}

Value BytecodeOrcJIT::executeCompiled(VM* vm, const std::string &func_name,
                                      const std::vector<Value> &args) {
    auto it = function_pointers.find(func_name);
    if (it == function_pointers.end()) {
        return Value::makeNull();
    }
    
    typedef uint64_t (*NativeFunc)(void*, const Value*, uint32_t);
    auto func = reinterpret_cast<NativeFunc>(it->second);
    
    uint64_t result_bits = func(static_cast<void*>(vm), args.data(), static_cast<uint32_t>(args.size()));
    
    // Treat result_bits as double/int/etc depending on the NaN bits
    // We can use a trick to construct Value from bits:
    Value result;
    std::memcpy(&result, &result_bits, sizeof(uint64_t));
    return result;
}

bool BytecodeOrcJIT::isCompiled(const std::string &func_name) const {
    return function_pointers.count(func_name) > 0;
}

void BytecodeOrcJIT::translate(const BytecodeFunction &func, Module &module) {
    LLVMContext &ctx = module.getContext();
    IRBuilder<> builder(ctx);
    
    Type *i64 = Type::getInt64Ty(ctx);
    Type *vmPtrType = Type::getInt8PtrTy(ctx);
    Type *valPtrType = PointerType::getUnqual(i64);
    
    // Function signature: i64 func(vm*, args*, count)
    std::vector<Type*> paramTypes = {vmPtrType, valPtrType, Type::getInt32Ty(ctx)};
    FunctionType *funcType = FunctionType::get(i64, paramTypes, false);
    Function *f = Function::Create(funcType, Function::ExternalLinkage, func.name, &module);
    
    BasicBlock *entry = BasicBlock::Create(ctx, "entry", f);
    builder.SetInsertPoint(entry);
    
    // Virtual Stack for SSA mapping
    std::vector<llvm::Value*> vstack;
    
    // Allocate space for locals on the native stack for simplicity (could use SSA for immutable locals)
    std::vector<llvm::Value*> vlocals;
    for (uint32_t i = 0; i < func.local_count; ++i) {
        vlocals.push_back(builder.CreateAlloca(i64, nullptr, "local_" + std::to_string(i)));
        // Initialize with null (0x7FFb000000000000 pattern)
        builder.CreateStore(ConstantInt::get(i64, 0x7FF8000000000000ULL | (3ULL << 48)), vlocals.back());
    }

    // Map parameters to initial locals
    llvm::Value* vmArg = f->getArg(0);
    llvm::Value* argsArg = f->getArg(1);
    llvm::Value* countArg = f->getArg(2);
    
    for (uint32_t i = 0; i < func.param_count; ++i) {
        llvm::Value* argPtr = builder.CreateInBoundsGEP(i64, argsArg, ConstantInt::get(Type::getInt32Ty(ctx), i));
        llvm::Value* argVal = builder.CreateLoad(i64, argPtr);
        builder.CreateStore(argVal, vlocals[i]);
    }

    // Helpers for unboxing (very simplified for now)
    auto unboxInt = [&](llvm::Value* boxed) {
        // Tag is 1, Payload is bits 0-47. 
        // Simply mask for now (ignoring sign extension for briefness)
        return builder.CreateAnd(boxed, ConstantInt::get(i64, 0x0000FFFFFFFFFFFFULL));
    };
    
    auto boxInt = [&](llvm::Value* raw) {
        uint64_t tagBits = (0x7FF8000000000000ULL | (1ULL << 48));
        return builder.CreateOr(raw, ConstantInt::get(i64, tagBits));
    };

    // Iterate through instructions
    for (size_t ip = 0; ip < func.instructions.size(); ++ip) {
        const auto &instr = func.instructions[ip];
        
        switch (instr.opcode) {
            case OpCode::LOAD_CONST: {
                uint32_t idx = instr.operands[0].asInt();
                uint64_t bits;
                std::memcpy(&bits, &func.constants[idx], sizeof(uint64_t));
                vstack.push_back(ConstantInt::get(i64, bits));
                break;
            }
            case OpCode::LOAD_VAR: {
                uint32_t idx = instr.operands[0].asInt();
                vstack.push_back(builder.CreateLoad(i64, vlocals[idx]));
                break;
            }
            case OpCode::STORE_VAR: {
                uint32_t idx = instr.operands[0].asInt();
                llvm::Value* val = vstack.back();
                vstack.pop_back();
                builder.CreateStore(val, vlocals[idx]);
                break;
            }
            case OpCode::ADD:
            case OpCode::SUB:
            case OpCode::MUL:
            case OpCode::DIV: {
                llvm::Value* right = vstack.back(); vstack.pop_back();
                llvm::Value* left = vstack.back(); vstack.pop_back();
                
                bool isMonomorphicInt = false;
                if (ip < func.type_feedback.size()) {
                    const auto &fb = func.type_feedback[ip];
                    if (fb.left_type == 1 && fb.right_type == 1) {
                        isMonomorphicInt = true;
                    }
                }
                
                if (isMonomorphicInt) {
                    llvm::Value* l_raw = unboxInt(left);
                    llvm::Value* r_raw = unboxInt(right);
                    llvm::Value* res_raw = nullptr;
                    
                    if (instr.opcode == OpCode::ADD) {
                        res_raw = builder.CreateAdd(l_raw, r_raw);
                    } else if (instr.opcode == OpCode::SUB) {
                        res_raw = builder.CreateSub(l_raw, r_raw);
                    } else if (instr.opcode == OpCode::MUL) {
                        res_raw = builder.CreateMul(l_raw, r_raw);
                    } else if (instr.opcode == OpCode::DIV) {
                        // Division by zero check
                        llvm::Value* isZero = builder.CreateICmpEQ(r_raw, ConstantInt::get(i64, 0));
                        BasicBlock* errorBB = BasicBlock::Create(ctx, "div_zero_err", f);
                        BasicBlock* contBB = BasicBlock::Create(ctx, "div_cont", f);
                        builder.CreateCondBr(isZero, errorBB, contBB);
                        
                        builder.SetInsertPoint(errorBB);
                        llvm::Function* throwFunc = module.getFunction("havel_vm_throw_error");
                        if (!throwFunc) {
                            // Define if missing
                            std::vector<Type*> throwParams = {vmPtrType, Type::getInt8PtrTy(ctx)};
                            FunctionType* throwType = FunctionType::get(Type::getVoidTy(ctx), throwParams, false);
                            throwFunc = Function::Create(throwType, Function::ExternalLinkage, "havel_vm_throw_error", &module);
                        }
                        builder.CreateCall(throwFunc, {vmArg, builder.CreateGlobalStringPtr("Division by zero")});
                        builder.CreateRet(ConstantInt::get(i64, 0x7FF8000000000000ULL | (3ULL << 48))); // Return Null
                        
                        builder.SetInsertPoint(contBB);
                        res_raw = builder.CreateSDiv(l_raw, r_raw);
                    }
                    vstack.push_back(boxInt(res_raw));
                } else {
                    vstack.push_back(ConstantInt::get(i64, 0x7FF8000000000000ULL | (3ULL << 48)));
                }
                break;
            }
            case OpCode::RETURN: {
                llvm::Value* res = vstack.empty() ? ConstantInt::get(i64, 0x7FF8000000000000ULL | (3ULL << 48)) : vstack.back();
                builder.CreateRet(res);
                break;
            }
            default:
                // Unimplemented: skip or fallback
                break;
        }
    }
    
    // Safety return
    if (!builder.GetInsertBlock()->getTerminator()) {
        builder.CreateRet(ConstantInt::get(i64, 0x7FF8000000000000ULL | (3ULL << 48)));
    }
}

} // namespace havel::compiler
