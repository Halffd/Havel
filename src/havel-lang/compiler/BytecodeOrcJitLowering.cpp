// ===== LLVM IR lowering (TODO.md #21 decomposition slice) =====
//
// BytecodeOrcJIT::translate - the bytecode -> LLVM IR lowering concern,
// extracted from the ORC session/orchestration TU. The lowering maps
// validated linear bytecode to LLVM IR using the Runtime ABI bridges
// (JitRuntimeBridges.cpp) for everything outside the specialized fast
// paths, with speculative int48/double arithmetic driven by type feedback
// and AOT hints. The class stays in BytecodeOrcJIT.h; member function
// definitions live wherever the class is visible.

#include "BytecodeOrcJIT.h"
#include "JitBridgesCommon.hpp"
#include "compiler/runtime/RuntimeABI.hpp"
#include "runtime/HavelEngine.hpp"

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IR/Module.h>

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace llvm::orc;

namespace havel::compiler {

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
    llvm::Type *i8p = llvm::PointerType::get(ctx, 0);
    llvm::Type *i64p = llvm::PointerType::get(ctx, 0);

    std::vector<llvm::Type*> paramTypes = {i8p, i64p, i32};
    llvm::FunctionType *funcType = llvm::FunctionType::get(i64, paramTypes, false);
    llvm::Function *f = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, func.name, &module);

    llvm::BasicBlock *entryBB = llvm::BasicBlock::Create(ctx, "entry", f);
    B.SetInsertPoint(entryBB);

    llvm::Value *vmArg = f->getArg(0);
    llvm::Value *argsArg = f->getArg(1);

    llvm::Type *frameType = llvm::StructType::create(
        ctx,
        {i8p,
         llvm::ArrayType::get(i64, 32),
         llvm::ArrayType::get(i64, 32),
         i32,
         llvm::ArrayType::get(i32, 32),
         llvm::ArrayType::get(i32, 32),
         llvm::ArrayType::get(i32, 32),
         i32},
        "JITStackFrame");
    llvm::Value *frame = B.CreateAlloca(frameType, nullptr, "gc_frame");

    llvm::Function *fn_reg = module.getFunction("havel_gc_register_roots");
    if (!fn_reg) fn_reg = llvm::Function::Create(llvm::FunctionType::get(voidT, {i8p, llvm::PointerType::get(ctx, 0), i64p, i32}, false), llvm::Function::ExternalLinkage, "havel_gc_register_roots", &module);
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
    std::vector<size_t> jit_try_stack_depths;

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

    auto isStringLoc = [&](llvm::Value* v) -> llvm::Value* {
        llvm::Value* primaryTag = B.CreateAnd(v, llvm::ConstantInt::get(i64, TAG_MASK));
        llvm::Value* isStringId = B.CreateICmpEQ(primaryTag, llvm::ConstantInt::get(i64, 0x0005000000000000ULL));
        llvm::Value* extTag = B.CreateAnd(v, llvm::ConstantInt::get(i64, EXTENDED_TAG_MASK));
        llvm::Value* isStringVal = B.CreateICmpEQ(extTag, llvm::ConstantInt::get(i64, 0x0000600000000000ULL));
        return B.CreateOr(isStringId, isStringVal);
    };

auto emitSpecializedBinop = [&](OpCode op, const TypeFeedback* fb, size_t ip, llvm::Value* left, llvm::Value* right) -> llvm::Value* {
    // Check for AOT type hint first, then fall back to runtime type feedback
    uint64_t type_hint = 0;
    if (fb && fb->has_aot_hint) {
        type_hint = fb->aot_type_hint;
    } else if (fb && fb->execution_count >= 100) {
        // Runtime feedback: if interpreter has seen only one type, specialize
        // (still uses guarded paths below for the speculative case)
        uint64_t combined = fb->left_type_mask | fb->right_type_mask;
        if (combined == TYPE_HINT_INT) {
            type_hint = TYPE_HINT_INT;
        } else if (combined == TYPE_HINT_NUMBER) {
            type_hint = TYPE_HINT_NUMBER;
        } else if (combined == (TYPE_HINT_INT | TYPE_HINT_NUMBER)) {
            type_hint = TYPE_HINT_NUMBER; // mixed int/double → use double path
        }
        // If mixed or polymorphic, type_hint stays 0 → fall through to guarded path
    }
    
    // If type hint says both operands are int, use direct integer path
    if ((type_hint & TYPE_HINT_INT) && !(type_hint & TYPE_HINT_NUMBER)) {
        // Pure integer operation - no runtime check needed
        llvm::Value *lIv = unboxInt(left);
        llvm::Value *rIv = unboxInt(right);
        llvm::Value *iRes = nullptr;
        if (op == OpCode::ADD) iRes = B.CreateAdd(lIv, rIv);
        else if (op == OpCode::SUB) iRes = B.CreateSub(lIv, rIv);
        else if (op == OpCode::MUL) iRes = B.CreateMul(lIv, rIv);
else if (op == OpCode::INT_DIV) iRes = B.CreateSDiv(lIv, rIv);
  else if (op == OpCode::REMAINDER) iRes = B.CreateSRem(lIv, rIv);
  else if (op == OpCode::DIV) {
    llvm::Value* lD = B.CreateSIToFP(lIv, f64);
    llvm::Value* rD = B.CreateSIToFP(rIv, f64);
    llvm::Value* dRes = B.CreateFDiv(lD, rD);
    return B.CreateBitCast(dRes, i64);
  }
  else if (op == OpCode::MOD) {
    // Python-style: sign follows divisor
    // result = ((l % r) + r) % r when signs differ
    llvm::Value* cRem = B.CreateSRem(lIv, rIv);
    llvm::Value* signsDiffer = B.CreateICmpSLT(B.CreateXor(cRem, rIv), llvm::ConstantInt::get(i64, 0));
    llvm::Value* adjusted = B.CreateAdd(cRem, rIv);
    iRes = B.CreateSelect(signsDiffer, adjusted, cRem);
  }
        else iRes = B.CreateAdd(lIv, rIv);
        return boxInt(iRes);
    }
    
    // If AOT hint says number (float), use direct float path
    if (type_hint & TYPE_HINT_NUMBER) {
        llvm::Value *lDv = B.CreateBitCast(left, f64);
        llvm::Value *rDv = B.CreateBitCast(right, f64);
        llvm::Value *dRes = nullptr;
        if (op == OpCode::ADD) dRes = B.CreateFAdd(lDv, rDv);
        else if (op == OpCode::SUB) dRes = B.CreateFSub(lDv, rDv);
        else if (op == OpCode::MUL) dRes = B.CreateFMul(lDv, rDv);
        else if (op == OpCode::DIV) dRes = B.CreateFDiv(lDv, rDv);
else if (op == OpCode::INT_DIV) {
    llvm::Value* lI = B.CreateFPToSI(lDv, i64);
    llvm::Value* rI = B.CreateFPToSI(rDv, i64);
    llvm::Value* iRes = B.CreateSDiv(lI, rI);
    return boxInt(iRes);
  }
  else if (op == OpCode::REMAINDER) {
    llvm::Value* lI = B.CreateFPToSI(lDv, i64);
    llvm::Value* rI = B.CreateFPToSI(rDv, i64);
    llvm::Value* iRes = B.CreateSRem(lI, rI);
    return boxInt(iRes);
  }
  else if (op == OpCode::MOD) dRes = B.CreateFRem(lDv, rDv);
        else dRes = B.CreateFAdd(lDv, rDv);
        return B.CreateBitCast(dRes, i64);
    }

    // No AOT hint - use speculative optimization with strict single-merge CFG.
    std::string pfx = "op" + std::to_string(ip) + "_";
    llvm::BasicBlock *intBB = llvm::BasicBlock::Create(ctx, pfx + "int", f);
    llvm::BasicBlock *chkDblBB = llvm::BasicBlock::Create(ctx, pfx + "chk_dbl", f);
    llvm::BasicBlock *dblBB = llvm::BasicBlock::Create(ctx, pfx + "dbl", f);
    llvm::BasicBlock *chkStrBB = llvm::BasicBlock::Create(ctx, pfx + "chk_str", f);
    llvm::BasicBlock *strBB = llvm::BasicBlock::Create(ctx, pfx + "str", f);
    llvm::BasicBlock *deoptBB = llvm::BasicBlock::Create(ctx, pfx + "deopt", f);
    llvm::BasicBlock *mergeBB = llvm::BasicBlock::Create(ctx, pfx + "merge", f);

    llvm::Value *bothInt = B.CreateAnd(isInt48Loc(left), isInt48Loc(right));
    B.CreateCondBr(bothInt, intBB, chkDblBB);

    B.SetInsertPoint(chkDblBB);
    llvm::Value *bothDbl = B.CreateAnd(isDblLoc(left), isDblLoc(right));
    B.CreateCondBr(bothDbl, dblBB, chkStrBB);

    B.SetInsertPoint(chkStrBB);
    llvm::Value *bothStr = B.CreateAnd(isStringLoc(left), isStringLoc(right));
    B.CreateCondBr(bothStr, strBB, deoptBB);

    B.SetInsertPoint(strBB);
    llvm::Function *fnStrCat = module.getFunction("havel_vm_string_concat");
    if (!fnStrCat) {
        fnStrCat = llvm::Function::Create(
            llvm::FunctionType::get(i64, {i8p, i64, i64}, false),
            llvm::Function::ExternalLinkage, "havel_vm_string_concat", &module);
    }
    llvm::Value *strBoxed = B.CreateCall(fnStrCat, {vmArg, left, right});
    llvm::BasicBlock *strExitBB = B.GetInsertBlock();
    B.CreateBr(mergeBB);

    B.SetInsertPoint(intBB);
    llvm::Value *lIv = unboxInt(left);
    llvm::Value *rIv = unboxInt(right);
    llvm::Value *iRes = nullptr;
    llvm::Value *intBoxed = nullptr;
    if (op == OpCode::ADD) iRes = B.CreateAdd(lIv, rIv);
    else if (op == OpCode::SUB) iRes = B.CreateSub(lIv, rIv);
    else if (op == OpCode::MUL) iRes = B.CreateMul(lIv, rIv);
    else if (op == OpCode::INT_DIV) iRes = B.CreateSDiv(lIv, rIv);
    else if (op == OpCode::REMAINDER) iRes = B.CreateSRem(lIv, rIv);
    else if (op == OpCode::DIV) {
      llvm::Value *lD = B.CreateSIToFP(lIv, f64);
      llvm::Value *rD = B.CreateSIToFP(rIv, f64);
      llvm::Value *dRes = B.CreateFDiv(lD, rD);
      intBoxed = B.CreateBitCast(dRes, i64);
    } else if (op == OpCode::MOD) {
      llvm::Value *cRem = B.CreateSRem(lIv, rIv);
      llvm::Value *signsDiffer = B.CreateICmpSLT(B.CreateXor(cRem, rIv), llvm::ConstantInt::get(i64, 0));
      llvm::Value *adjusted = B.CreateAdd(cRem, rIv);
      iRes = B.CreateSelect(signsDiffer, adjusted, cRem);
    } else iRes = B.CreateAdd(lIv, rIv);
    if (!intBoxed) intBoxed = boxInt(iRes);
    llvm::BasicBlock *intExitBB = B.GetInsertBlock();
    B.CreateBr(mergeBB);

    B.SetInsertPoint(dblBB);
    llvm::Value *lDv = B.CreateBitCast(left, f64);
    llvm::Value *rDv = B.CreateBitCast(right, f64);
    llvm::Value *dblBoxed = nullptr;
    if (op == OpCode::INT_DIV) {
      llvm::Value *lI = B.CreateFPToSI(lDv, i64);
      llvm::Value *rI = B.CreateFPToSI(rDv, i64);
      llvm::Value *iRes2 = B.CreateSDiv(lI, rI);
      dblBoxed = boxInt(iRes2);
    } else if (op == OpCode::REMAINDER) {
      llvm::Value *lI = B.CreateFPToSI(lDv, i64);
      llvm::Value *rI = B.CreateFPToSI(rDv, i64);
      llvm::Value *iRes2 = B.CreateSRem(lI, rI);
      dblBoxed = boxInt(iRes2);
    } else {
      llvm::Value *dRes = nullptr;
      if (op == OpCode::ADD) dRes = B.CreateFAdd(lDv, rDv);
      else if (op == OpCode::SUB) dRes = B.CreateFSub(lDv, rDv);
      else if (op == OpCode::MUL) dRes = B.CreateFMul(lDv, rDv);
      else if (op == OpCode::MOD) dRes = B.CreateFRem(lDv, rDv);
      else dRes = B.CreateFDiv(lDv, rDv);
      dblBoxed = B.CreateBitCast(dRes, i64);
    }
    llvm::BasicBlock *dblExitBB = B.GetInsertBlock();
    B.CreateBr(mergeBB);

    B.SetInsertPoint(deoptBB);
    llvm::Function *fn_deopt = module.getFunction("havel_deoptimize");
    if (!fn_deopt) fn_deopt = llvm::Function::Create(
        llvm::FunctionType::get(voidT, {i8p, i64, i64, i8p}, false),
        llvm::Function::ExternalLinkage, "havel_deoptimize", &module);
    llvm::Constant *funcNameStr =
        llvm::ConstantDataArray::getString(module.getContext(), func.name);
    llvm::GlobalVariable *gv = new llvm::GlobalVariable(
        module, funcNameStr->getType(), true,
        llvm::GlobalValue::PrivateLinkage, funcNameStr);
    llvm::Value *funcNameConst = B.CreatePointerCast(gv, i8p);
    B.CreateCall(fn_deopt, {vmArg, left, right, funcNameConst});
    llvm::Value *slowBoxed = makeNull();
    llvm::BasicBlock *slowExitBB = B.GetInsertBlock();
    B.CreateBr(mergeBB);

    B.SetInsertPoint(mergeBB);
    llvm::PHINode *phi = B.CreatePHI(i64, 4);
    phi->addIncoming(intBoxed, intExitBB);
    phi->addIncoming(dblBoxed, dblExitBB);
    phi->addIncoming(strBoxed, strExitBB);
    phi->addIncoming(slowBoxed, slowExitBB);
    return phi;
};

    // Build one block per bytecode instruction plus one exit/fallthrough block.
    // This gives correct branch fallthrough targets for conditional jumps.
    std::vector<llvm::BasicBlock*> basicBlocks(func.instructions.size() + 1, nullptr);
    for (size_t ip = 0; ip <= func.instructions.size(); ++ip) {
        basicBlocks[ip] = llvm::BasicBlock::Create(ctx, "ip" + std::to_string(ip), f);
    }

    std::unordered_map<llvm::BasicBlock*, size_t> blockToIp;
    blockToIp.reserve(basicBlocks.size());
    for (size_t ip = 0; ip < basicBlocks.size(); ++ip) {
        blockToIp[basicBlocks[ip]] = ip;
    }

    // Static predecessor map from bytecode CFG.
    std::vector<std::vector<size_t>> predecessors(basicBlocks.size());
    auto addStaticEdge = [&](size_t from, size_t to) {
        if (to >= basicBlocks.size()) return;
        predecessors[to].push_back(from);
    };
    for (size_t ip = 0; ip < func.instructions.size(); ++ip) {
        const auto &instr = func.instructions[ip];
        bool addsFallthrough = true;
        if (instr.opcode == OpCode::JUMP) {
            size_t target = instr.operands[0].asInt();
            addStaticEdge(ip, (target < basicBlocks.size()) ? target : (ip + 1));
            addsFallthrough = false;
        } else if (instr.opcode == OpCode::JUMP_IF_FALSE ||
                   instr.opcode == OpCode::JUMP_IF_TRUE ||
                   instr.opcode == OpCode::JUMP_IF_NULL) {
            size_t target = instr.operands[0].asInt();
            addStaticEdge(ip, (target < basicBlocks.size()) ? target : (ip + 1));
            addStaticEdge(ip, ip + 1);
            addsFallthrough = false;
        } else if (instr.opcode == OpCode::RETURN ||
                   instr.opcode == OpCode::TAIL_CALL ||
                   instr.opcode == OpCode::THROW) {
            addsFallthrough = false;
        }

        if (addsFallthrough) {
            addStaticEdge(ip, ip + 1);
        }
    }

    struct IncomingStackState {
        llvm::BasicBlock* pred = nullptr;
        std::vector<llvm::Value*> stack;
    };

    std::vector<std::vector<IncomingStackState>> pendingIncoming(basicBlocks.size());
    std::vector<std::vector<llvm::PHINode*>> entryStackPhis(basicBlocks.size());

    auto addIncomingState = [&](size_t targetIp, llvm::BasicBlock* predBB,
                                const std::vector<llvm::Value*>& stack) {
        if (targetIp >= basicBlocks.size() || !predBB) return;

        auto &phis = entryStackPhis[targetIp];
        if (!phis.empty()) {
            if (phis.size() != stack.size()) {
                return;
            }
            for (size_t i = 0; i < phis.size(); ++i) {
                phis[i]->addIncoming(stack[i], predBB);
            }
            return;
        }

        pendingIncoming[targetIp].push_back({predBB, stack});
    };

    auto resolveEntryStack = [&](size_t blockIp, llvm::BasicBlock* block) -> std::vector<llvm::Value*> {
        auto &incoming = pendingIncoming[blockIp];
        if (incoming.empty()) {
            return {};
        }

        size_t depth = incoming.front().stack.size();
        for (const auto &inc : incoming) {
            if (inc.stack.size() != depth) {
                depth = std::min(depth, inc.stack.size());
            }
        }

        if (predecessors[blockIp].size() <= 1) {
            std::vector<llvm::Value*> single = incoming.front().stack;
            if (single.size() > depth) single.resize(depth);
            return single;
        }

        auto &phis = entryStackPhis[blockIp];
        if (phis.empty()) {
            auto insertPt = block->begin();
            B.SetInsertPoint(block, insertPt);
            phis.reserve(depth);
            for (size_t slot = 0; slot < depth; ++slot) {
                auto *phi = B.CreatePHI(i64, static_cast<unsigned>(predecessors[blockIp].size()),
                                        "stack_phi_" + std::to_string(blockIp) + "_" + std::to_string(slot));
                phis.push_back(phi);
            }
            for (const auto &inc : incoming) {
                if (inc.stack.size() < depth) continue;
                for (size_t slot = 0; slot < depth; ++slot) {
                    phis[slot]->addIncoming(inc.stack[slot], inc.pred);
                }
            }
        }

        std::vector<llvm::Value*> merged;
        merged.reserve(phis.size());
        for (auto *phi : phis) {
            merged.push_back(phi);
        }
        return merged;
    };

    B.CreateBr(basicBlocks[0]);
    pendingIncoming[0].push_back({entryBB, {}});

    // Emit instructions with control flow.
    for (size_t ip = 0; ip < func.instructions.size(); ++ip) {
        B.SetInsertPoint(basicBlocks[ip]);
        llvm::BasicBlock* instrBlock = basicBlocks[ip]; // Save the instruction block

        // Skip blocks that are already fully emitted.
        if (instrBlock->getTerminator() != nullptr) {
            continue;
        }

        vstack = resolveEntryStack(ip, instrBlock);
        B.SetInsertPoint(instrBlock);

        const auto &instr = func.instructions[ip];
        const TypeFeedback* fb = (ip < func.type_feedback.size()) ? &func.type_feedback[ip] : nullptr;

        switch (instr.opcode) {
        case OpCode::LOAD_CONST: {
            uint64_t bits; std::memcpy(&bits, &func.constants[instr.operands[0].asInt()], 8);
            vstack.push_back(llvm::ConstantInt::get(i64, bits));
            break;
        }
        case OpCode::LOAD_VAR: vstack.push_back(B.CreateLoad(i64, vlocals[instr.operands[0].asInt()])); break;
case OpCode::STORE_VAR:
    case OpCode::STORE_IMMUT_VAR: {
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
    case OpCode::DIV:
case OpCode::INT_DIV:
    case OpCode::DIVMOD:
    case OpCode::REMAINDER: {
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
case OpCode::INCLOCAL:
        case OpCode::DECLOCAL:
        case OpCode::INCLOCAL_POST:
        case OpCode::DECLOCAL_POST: {
            uint32_t slot = instr.operands[0].asInt();
            llvm::Value* oldBoxed = B.CreateLoad(i64, vlocals[slot]);
            llvm::Value* oldRaw = unboxInt(oldBoxed);
            llvm::Value* delta = llvm::ConstantInt::get(i64,
                (instr.opcode == OpCode::INCLOCAL || instr.opcode == OpCode::INCLOCAL_POST) ? 1 : -1);
            llvm::Value* newRaw = B.CreateAdd(oldRaw, delta);
            llvm::Value* newBoxed = boxInt(newRaw);
            B.CreateStore(newBoxed, vlocals[slot]);

            // Postfix returns old value; prefix returns new value.
            if (instr.opcode == OpCode::INCLOCAL_POST || instr.opcode == OpCode::DECLOCAL_POST) {
                vstack.push_back(oldBoxed);
            } else {
                vstack.push_back(newBoxed);
            }
            break;
        }

      // Comparisons — use type feedback to specialize or fall back to bridge
      case OpCode::EQ:
      case OpCode::NEQ:
      case OpCode::LT:
      case OpCode::LTE:
      case OpCode::GT:
      case OpCode::GTE: {
        llvm::Value* r = vstack.back(); vstack.pop_back();
        llvm::Value* l = vstack.back(); vstack.pop_back();
        
        // Check if feedback says both sides are always int
        bool bothAlwaysInt = false;
        if (fb && fb->execution_count >= 100) {
            uint64_t combined = fb->left_type_mask | fb->right_type_mask;
            bothAlwaysInt = (combined == TYPE_HINT_INT);
        }
        if (fb && fb->has_aot_hint && (fb->aot_type_hint & TYPE_HINT_INT) && !(fb->aot_type_hint & TYPE_HINT_NUMBER)) {
            bothAlwaysInt = true;
        }
        
        // NaN-boxed bool constants
        uint64_t boolTrueBits = QNAN | (2ULL << 48) | 1ULL;
        uint64_t boolFalseBits = QNAN | (2ULL << 48);
        
        if (bothAlwaysInt) {
            // Fast path: inline integer comparison with guard
            std::string pfx = "cmp" + std::to_string(ip) + "_";
            llvm::BasicBlock *intCmpBB = llvm::BasicBlock::Create(ctx, pfx+"int", f);
            llvm::BasicBlock *slowBB = llvm::BasicBlock::Create(ctx, pfx+"slow", f);
            llvm::BasicBlock *mergeBB = llvm::BasicBlock::Create(ctx, pfx+"merge", f);
            
            llvm::Value* bothInt = B.CreateAnd(isInt48Loc(l), isInt48Loc(r));
            B.CreateCondBr(bothInt, intCmpBB, slowBB);
            
            // Integer fast path
            B.SetInsertPoint(intCmpBB);
            llvm::Value* lI = unboxInt(l);
            llvm::Value* rI = unboxInt(r);
            llvm::Value* cmpResult = nullptr;
            switch (instr.opcode) {
                case OpCode::EQ:  cmpResult = B.CreateICmpEQ(lI, rI);  break;
                case OpCode::NEQ: cmpResult = B.CreateICmpNE(lI, rI);  break;
                case OpCode::LT:  cmpResult = B.CreateICmpSLT(lI, rI); break;
                case OpCode::LTE: cmpResult = B.CreateICmpSLE(lI, rI); break;
                case OpCode::GT:  cmpResult = B.CreateICmpSGT(lI, rI); break;
                case OpCode::GTE: cmpResult = B.CreateICmpSGE(lI, rI); break;
                default: cmpResult = B.CreateICmpEQ(lI, rI); break;
            }
            llvm::Value* intResult = B.CreateSelect(cmpResult,
                llvm::ConstantInt::get(i64, boolTrueBits),
                llvm::ConstantInt::get(i64, boolFalseBits));
            auto* intExitBB = B.GetInsertBlock();
            B.CreateBr(mergeBB);
            
            // Slow path: bridge function
            B.SetInsertPoint(slowBB);
            const char* fname = nullptr;
            switch (instr.opcode) {
                case OpCode::EQ:  fname = "havel_vm_eq";  break;
                case OpCode::NEQ: fname = "havel_vm_neq"; break;
                case OpCode::LT:  fname = "havel_vm_lt";  break;
                case OpCode::LTE: fname = "havel_vm_lte"; break;
                case OpCode::GT:  fname = "havel_vm_gt";  break;
                case OpCode::GTE: fname = "havel_vm_gte"; break;
                default: fname = "havel_vm_eq"; break;
            }
            llvm::Function* fnComp = module.getFunction(fname);
            if (!fnComp) {
                fnComp = llvm::Function::Create(
                    llvm::FunctionType::get(i64, {i64, i64}, false),
                    llvm::Function::ExternalLinkage, fname, &module);
            }
            llvm::Value* slowResult = B.CreateCall(fnComp, {l, r});
            auto* slowExitBB = B.GetInsertBlock();
            B.CreateBr(mergeBB);
            
            // Merge
            B.SetInsertPoint(mergeBB);
            llvm::PHINode* phi = B.CreatePHI(i64, 2);
            phi->addIncoming(intResult, intExitBB);
            phi->addIncoming(slowResult, slowExitBB);
            vstack.push_back(phi);
        } else {
            // No feedback or polymorphic: use bridge
            const char* fname = nullptr;
            switch (instr.opcode) {
              case OpCode::EQ:  fname = "havel_vm_eq";  break;
              case OpCode::NEQ: fname = "havel_vm_neq"; break;
              case OpCode::LT:  fname = "havel_vm_lt";  break;
              case OpCode::LTE: fname = "havel_vm_lte"; break;
              case OpCode::GT:  fname = "havel_vm_gt";  break;
              case OpCode::GTE: fname = "havel_vm_gte"; break;
              default: fname = "havel_vm_eq"; break;
            }
            llvm::Function* fnComp = module.getFunction(fname);
            if (!fnComp) {
                fnComp = llvm::Function::Create(
                    llvm::FunctionType::get(i64, {i64, i64}, false),
                    llvm::Function::ExternalLinkage, fname, &module);
            }
            vstack.push_back(B.CreateCall(fnComp, {l, r}));
        }
        break;
      }
    // CLOSURE - operand: func_index
    case OpCode::CLOSURE: {
        uint32_t funcIndex = instr.operands[0].asInt();
        llvm::Function* fnClosure = module.getFunction("havel_vm_closure_new");
        if (!fnClosure) {
            fnClosure = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i32}, false),
                llvm::Function::ExternalLinkage, "havel_vm_closure_new", &module);
        }
        vstack.push_back(B.CreateCall(fnClosure, {vmArg, llvm::ConstantInt::get(i32, funcIndex)}));
        break;
    }
    case OpCode::DEFINE_FUNC:
        break; // No-op in JIT, function already compiled

    // Array additional operations
    case OpCode::ARRAY_DEL: {
        llvm::Value* idx = vstack.back(); vstack.pop_back();
        llvm::Value* arr = vstack.back(); vstack.pop_back();
        llvm::Function* fnDel = module.getFunction("havel_vm_array_del");
        if (!fnDel) {
            fnDel = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_array_del", &module);
        }
        vstack.push_back(B.CreateCall(fnDel, {vmArg, arr, idx}));
        break;
    }
    case OpCode::ARRAY_FREEZE: {
        llvm::Value* arr = vstack.back(); vstack.pop_back();
        llvm::Function* fnFreeze = module.getFunction("havel_vm_array_freeze");
        if (!fnFreeze) {
            fnFreeze = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_array_freeze", &module);
        }
        vstack.push_back(B.CreateCall(fnFreeze, {vmArg, arr}));
        break;
    }
    case OpCode::ARRAY_POP: {
        llvm::Value* arr = vstack.back(); vstack.pop_back();
        llvm::Function* fnPop = module.getFunction("havel_vm_array_pop");
        if (!fnPop) {
            fnPop = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_array_pop", &module);
        }
        vstack.push_back(B.CreateCall(fnPop, {vmArg, arr}));
        break;
    }
    case OpCode::ARRAY_HAS: {
        llvm::Value* val = vstack.back(); vstack.pop_back();
        llvm::Value* arr = vstack.back(); vstack.pop_back();
        llvm::Function* fnHas = module.getFunction("havel_vm_array_has");
        if (!fnHas) {
            fnHas = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_array_has", &module);
        }
        vstack.push_back(B.CreateCall(fnHas, {vmArg, arr, val}));
        break;
    }
    case OpCode::ARRAY_FIND: {
        llvm::Value* val = vstack.back(); vstack.pop_back();
        llvm::Value* arr = vstack.back(); vstack.pop_back();
        llvm::Function* fnFind = module.getFunction("havel_vm_array_find");
        if (!fnFind) {
            fnFind = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_array_find", &module);
        }
        vstack.push_back(B.CreateCall(fnFind, {vmArg, arr, val}));
        break;
    }
    case OpCode::ARRAY_MAP: {
        llvm::Value* fn = vstack.back(); vstack.pop_back();
        llvm::Value* arr = vstack.back(); vstack.pop_back();
        llvm::Function* fnMap = module.getFunction("havel_vm_array_map");
        if (!fnMap) {
            fnMap = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_array_map", &module);
        }
        vstack.push_back(B.CreateCall(fnMap, {vmArg, arr, fn}));
        break;
    }
    case OpCode::ARRAY_FILTER: {
        llvm::Value* fn = vstack.back(); vstack.pop_back();
        llvm::Value* arr = vstack.back(); vstack.pop_back();
        llvm::Function* fnFilter = module.getFunction("havel_vm_array_filter");
        if (!fnFilter) {
            fnFilter = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_array_filter", &module);
        }
        vstack.push_back(B.CreateCall(fnFilter, {vmArg, arr, fn}));
        break;
    }
    case OpCode::ARRAY_REDUCE: {
        llvm::Value* init = vstack.back(); vstack.pop_back();
        llvm::Value* fn = vstack.back(); vstack.pop_back();
        llvm::Value* arr = vstack.back(); vstack.pop_back();
        llvm::Function* fnReduce = module.getFunction("havel_vm_array_reduce");
        if (!fnReduce) {
            fnReduce = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i64, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_array_reduce", &module);
        }
        vstack.push_back(B.CreateCall(fnReduce, {vmArg, arr, fn, init}));
        break;
    }
    case OpCode::ARRAY_FOREACH: {
        llvm::Value* fn = vstack.back(); vstack.pop_back();
        llvm::Value* arr = vstack.back(); vstack.pop_back();
        llvm::Function* fnForeach = module.getFunction("havel_vm_array_foreach");
        if (!fnForeach) {
            fnForeach = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_array_foreach", &module);
        }
        vstack.push_back(B.CreateCall(fnForeach, {vmArg, arr, fn}));
        break;
    }

    // Set operations
    case OpCode::SET_NEW: {
        llvm::Function* fnNew = module.getFunction("havel_vm_set_new");
        if (!fnNew) {
            fnNew = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p}, false),
                llvm::Function::ExternalLinkage, "havel_vm_set_new", &module);
        }
        vstack.push_back(B.CreateCall(fnNew, {vmArg}));
        break;
    }
    case OpCode::SET_SET: {
        // Stack: [..., set, value, key] - pops key, value, set
        llvm::Value* key = vstack.back(); vstack.pop_back();
        llvm::Value* val = vstack.back(); vstack.pop_back();
        llvm::Value* set = vstack.back(); vstack.pop_back();
        llvm::Function* fnSet = module.getFunction("havel_vm_set_set");
        if (!fnSet) {
            fnSet = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i64, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_set_set", &module);
        }
        B.CreateCall(fnSet, {vmArg, set, val, key});
        // SET_SET does not push set back - caller manages it
        break;
    }
    case OpCode::SET_DEL: {
        // Stack: [..., set, key]
        llvm::Value* key = vstack.back(); vstack.pop_back();
        llvm::Value* set = vstack.back(); vstack.pop_back();
        llvm::Function* fnDel = module.getFunction("havel_vm_set_del");
        if (!fnDel) {
            fnDel = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_set_del", &module);
        }
        vstack.push_back(B.CreateCall(fnDel, {vmArg, set, key}));
        break;
    }

    // Range with step
    case OpCode::RANGE_STEP_NEW: {
        // Stack: [..., start, end, step]
        llvm::Value* step = vstack.back(); vstack.pop_back();
        llvm::Value* end = vstack.back(); vstack.pop_back();
        llvm::Value* start = vstack.back(); vstack.pop_back();
        llvm::Function* fnRange = module.getFunction("havel_vm_range_step_new");
        if (!fnRange) {
            fnRange = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i64, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_range_step_new", &module);
        }
        vstack.push_back(B.CreateCall(fnRange, {vmArg, start, end, step}));
        break;
    }

    // Enum operations
    case OpCode::ENUM_NEW: {
        // Operands: typeId, tag, payloadCount
    uint32_t typeId = instr.operands[0].asInt();
    uint32_t tag = instr.operands[1].asInt();
    uint32_t payloadCount = instr.operands[2].asInt();
    llvm::Function* fnEnum = module.getFunction("havel_vm_enum_new");
    if (!fnEnum) {
        fnEnum = llvm::Function::Create(
            llvm::FunctionType::get(i64, {i8p, i32, i32, i32}, false),
            llvm::Function::ExternalLinkage, "havel_vm_enum_new", &module);
    }
    vstack.push_back(B.CreateCall(fnEnum, {vmArg,
        llvm::ConstantInt::get(i32, typeId),
        llvm::ConstantInt::get(i32, tag),
        llvm::ConstantInt::get(i32, payloadCount)}));
        break;
    }
    case OpCode::ENUM_TAG: {
        llvm::Value* enumVal = vstack.back(); vstack.pop_back();
        llvm::Function* fnTag = module.getFunction("havel_vm_enum_tag");
        if (!fnTag) {
            fnTag = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_enum_tag", &module);
        }
        vstack.push_back(B.CreateCall(fnTag, {vmArg, enumVal}));
        break;
    }
    case OpCode::ENUM_PAYLOAD: {
        // Stack: [..., enum, index]
        llvm::Value* idx = vstack.back(); vstack.pop_back();
        llvm::Value* enumVal = vstack.back(); vstack.pop_back();
        llvm::Function* fnPayload = module.getFunction("havel_vm_enum_payload");
        if (!fnPayload) {
            fnPayload = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i32}, false),
                llvm::Function::ExternalLinkage, "havel_vm_enum_payload", &module);
        }
        // Bridge takes (vm, enum_bits, uint32_t idx) but idx is on stack as i64
        // Need to truncate i64 to i32
        vstack.push_back(B.CreateCall(fnPayload, {vmArg, enumVal, B.CreateTrunc(idx, i32, "enum_idx")}));
        break;
    }
    case OpCode::ENUM_MATCH: {
        // Stack: [..., enum, expectedTag] - compare enum tag with expected
        // ENUM_MATCH pops both and pushes bool
        llvm::Value* tag = vstack.back(); vstack.pop_back();
        llvm::Value* enumVal = vstack.back(); vstack.pop_back();
        // Use enum_tag bridge then compare
        llvm::Function* fnTag = module.getFunction("havel_vm_enum_tag");
        if (!fnTag) {
            fnTag = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_enum_tag", &module);
        }
        llvm::Value* actualTag = B.CreateCall(fnTag, {vmArg, enumVal});
        // Both are i64, compare with EQ semantics (both are NaN-boxed ints)
        // For simplicity, use the eq bridge
        llvm::Function* fnEq = module.getFunction("havel_vm_eq");
        if (!fnEq) {
            fnEq = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i64, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_eq", &module);
        }
        vstack.push_back(B.CreateCall(fnEq, {actualTag, tag}));
        break;
    }

    // Object additional operations
    case OpCode::OBJECT_NEW_UNSORTED: {
        llvm::Function* fnNew = module.getFunction("havel_vm_object_new_unsorted");
        if (!fnNew) {
            fnNew = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p}, false),
                llvm::Function::ExternalLinkage, "havel_vm_object_new_unsorted", &module);
        }
        vstack.push_back(B.CreateCall(fnNew, {vmArg}));
        break;
    }
    case OpCode::OBJECT_KEYS: {
        llvm::Value* obj = vstack.back(); vstack.pop_back();
        llvm::Function* fnKeys = module.getFunction("havel_vm_object_keys");
        if (!fnKeys) {
            fnKeys = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_object_keys", &module);
        }
        vstack.push_back(B.CreateCall(fnKeys, {vmArg, obj}));
        break;
    }
    case OpCode::OBJECT_VALUES: {
        llvm::Value* obj = vstack.back(); vstack.pop_back();
        llvm::Function* fnVals = module.getFunction("havel_vm_object_values");
        if (!fnVals) {
            fnVals = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_object_values", &module);
        }
        vstack.push_back(B.CreateCall(fnVals, {vmArg, obj}));
        break;
    }
    case OpCode::OBJECT_ENTRIES: {
        llvm::Value* obj = vstack.back(); vstack.pop_back();
        llvm::Function* fnEntries = module.getFunction("havel_vm_object_entries");
        if (!fnEntries) {
            fnEntries = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_object_entries", &module);
        }
        vstack.push_back(B.CreateCall(fnEntries, {vmArg, obj}));
        break;
    }
    case OpCode::OBJECT_HAS: {
        llvm::Value* key = vstack.back(); vstack.pop_back();
        llvm::Value* obj = vstack.back(); vstack.pop_back();
        llvm::Function* fnHas = module.getFunction("havel_vm_object_has_raw");
        if (!fnHas) {
            fnHas = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_object_has_raw", &module);
        }
        vstack.push_back(B.CreateCall(fnHas, {vmArg, obj, key}));
        break;
    }
    case OpCode::OBJECT_DELETE: {
        llvm::Value* key = vstack.back(); vstack.pop_back();
        llvm::Value* obj = vstack.back(); vstack.pop_back();
        llvm::Function* fnDel = module.getFunction("havel_vm_object_delete_raw");
        if (!fnDel) {
            fnDel = llvm::Function::Create(
                llvm::FunctionType::get(voidT, {i8p, i64, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_object_delete_raw", &module);
        }
        B.CreateCall(fnDel, {vmArg, obj, key});
        vstack.push_back(makeNull());
        break;
    }
        case OpCode::OBJECT_GET_RAW: {
            llvm::Value* key = vstack.back(); vstack.pop_back();
            llvm::Value* obj = vstack.back(); vstack.pop_back();
            llvm::Function* fnGetRaw = module.getFunction("havel_vm_object_get_raw_ic");
            if (!fnGetRaw) {
                fnGetRaw = llvm::Function::Create(
                    llvm::FunctionType::get(i64, {i8p, i64, i64}, false),
                    llvm::Function::ExternalLinkage, "havel_vm_object_get_raw_ic", &module);
            }
            vstack.push_back(B.CreateCall(fnGetRaw, {vmArg, obj, key}));
            break;
        }

    // String additional operations
    case OpCode::STRING_UPPER: {
        llvm::Value* str = vstack.back(); vstack.pop_back();
        llvm::Function* fnUpper = module.getFunction("havel_vm_string_upper");
        if (!fnUpper) {
            fnUpper = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_string_upper", &module);
        }
        vstack.push_back(B.CreateCall(fnUpper, {vmArg, str}));
        break;
    }
    case OpCode::STRING_LOWER: {
        llvm::Value* str = vstack.back(); vstack.pop_back();
        llvm::Function* fnLower = module.getFunction("havel_vm_string_lower");
        if (!fnLower) {
            fnLower = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_string_lower", &module);
        }
        vstack.push_back(B.CreateCall(fnLower, {vmArg, str}));
        break;
    }
    case OpCode::STRING_TRIM: {
        llvm::Value* str = vstack.back(); vstack.pop_back();
        llvm::Function* fnTrim = module.getFunction("havel_vm_string_trim");
        if (!fnTrim) {
            fnTrim = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_string_trim", &module);
        }
        vstack.push_back(B.CreateCall(fnTrim, {vmArg, str}));
        break;
    }
    case OpCode::STRING_SUB: {
        // Stack: [..., str, start, len]
        llvm::Value* len = vstack.back(); vstack.pop_back();
        llvm::Value* start = vstack.back(); vstack.pop_back();
        llvm::Value* str = vstack.back(); vstack.pop_back();
        llvm::Function* fnSub = module.getFunction("havel_vm_string_sub");
        if (!fnSub) {
            fnSub = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i64, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_string_sub", &module);
        }
        vstack.push_back(B.CreateCall(fnSub, {vmArg, str, start, len}));
        break;
    }
    case OpCode::STRING_FIND: {
        // Stack: [..., str, substr]
        llvm::Value* sub = vstack.back(); vstack.pop_back();
        llvm::Value* str = vstack.back(); vstack.pop_back();
        llvm::Function* fnFind = module.getFunction("havel_vm_string_find");
        if (!fnFind) {
            fnFind = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_string_find", &module);
        }
        vstack.push_back(B.CreateCall(fnFind, {vmArg, str, sub}));
        break;
    }
    case OpCode::STRING_HAS: {
        llvm::Value* sub = vstack.back(); vstack.pop_back();
        llvm::Value* str = vstack.back(); vstack.pop_back();
        llvm::Function* fnHas = module.getFunction("havel_vm_string_has");
        if (!fnHas) {
            fnHas = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_string_has", &module);
        }
        vstack.push_back(B.CreateCall(fnHas, {vmArg, str, sub}));
        break;
    }
    case OpCode::STRING_STARTS: {
        llvm::Value* prefix = vstack.back(); vstack.pop_back();
        llvm::Value* str = vstack.back(); vstack.pop_back();
        llvm::Function* fnStarts = module.getFunction("havel_vm_string_starts");
        if (!fnStarts) {
            fnStarts = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_string_starts", &module);
        }
        vstack.push_back(B.CreateCall(fnStarts, {vmArg, str, prefix}));
        break;
    }
    case OpCode::STRING_ENDS: {
        llvm::Value* suffix = vstack.back(); vstack.pop_back();
        llvm::Value* str = vstack.back(); vstack.pop_back();
        llvm::Function* fnEnds = module.getFunction("havel_vm_string_ends");
        if (!fnEnds) {
            fnEnds = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_string_ends", &module);
        }
        vstack.push_back(B.CreateCall(fnEnds, {vmArg, str, suffix}));
        break;
    }
    case OpCode::STRING_SPLIT: {
        // Stack: [..., str, delimiter]
        llvm::Value* delim = vstack.back(); vstack.pop_back();
        llvm::Value* str = vstack.back(); vstack.pop_back();
        llvm::Function* fnSplit = module.getFunction("havel_vm_string_split");
        if (!fnSplit) {
            fnSplit = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_string_split", &module);
        }
        vstack.push_back(B.CreateCall(fnSplit, {vmArg, str, delim}));
        break;
    }
    case OpCode::STRING_REPLACE: {
        // Stack: [..., str, old, new]
        llvm::Value* newVal = vstack.back(); vstack.pop_back();
        llvm::Value* oldVal = vstack.back(); vstack.pop_back();
        llvm::Value* str = vstack.back(); vstack.pop_back();
        llvm::Function* fnReplace = module.getFunction("havel_vm_string_replace");
        if (!fnReplace) {
            fnReplace = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i64, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_string_replace", &module);
        }
        vstack.push_back(B.CreateCall(fnReplace, {vmArg, str, oldVal, newVal}));
        break;
    }
    case OpCode::STRING_PROMOTE: {
        llvm::Value* str = vstack.back(); vstack.pop_back();
        llvm::Function* fnPromote = module.getFunction("havel_vm_string_promote");
        if (!fnPromote) {
            fnPromote = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_string_promote", &module);
        }
        vstack.push_back(B.CreateCall(fnPromote, {vmArg, str}));
        break;
    }

    // Spread - pushes multiple values onto VM stack, returns count
    case OpCode::SPREAD: {
        llvm::Value* val = vstack.back(); vstack.pop_back();
        llvm::Function* fnSpread = module.getFunction("havel_vm_spread");
        if (!fnSpread) {
            fnSpread = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_spread", &module);
        }
        // spread pushes elements onto VM stack directly, doesn't return a value to vstack
        B.CreateCall(fnSpread, {vmArg, val});
        break;
    }
    case OpCode::SPREAD_CALL: {
        // Same as SPREAD
        llvm::Value* val = vstack.back(); vstack.pop_back();
        llvm::Function* fnSpread = module.getFunction("havel_vm_spread");
        if (!fnSpread) {
            fnSpread = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_spread", &module);
        }
        B.CreateCall(fnSpread, {vmArg, val});
        break;
    }

    // Type conversions
    case OpCode::TO_INT: {
        llvm::Value* val = vstack.back(); vstack.pop_back();
        llvm::Function* fnToInt = module.getFunction("havel_vm_to_int");
        if (!fnToInt) {
            fnToInt = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_to_int", &module);
        }
        vstack.push_back(B.CreateCall(fnToInt, {vmArg, val}));
        break;
    }
    case OpCode::TO_FLOAT: {
        llvm::Value* val = vstack.back(); vstack.pop_back();
        llvm::Function* fnToFloat = module.getFunction("havel_vm_to_float");
        if (!fnToFloat) {
            fnToFloat = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_to_float", &module);
        }
        vstack.push_back(B.CreateCall(fnToFloat, {vmArg, val}));
        break;
    }
    case OpCode::TO_STRING: {
        llvm::Value* val = vstack.back(); vstack.pop_back();
        llvm::Function* fnToStr = module.getFunction("havel_vm_to_string");
        if (!fnToStr) {
            fnToStr = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_to_string", &module);
        }
        vstack.push_back(B.CreateCall(fnToStr, {vmArg, val}));
        break;
    }
    case OpCode::TO_BOOL: {
        llvm::Value* val = vstack.back(); vstack.pop_back();
        llvm::Function* fnToBool = module.getFunction("havel_vm_to_bool");
        if (!fnToBool) {
            fnToBool = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_to_bool", &module);
        }
        vstack.push_back(B.CreateCall(fnToBool, {vmArg, val}));
        break;
    }
    case OpCode::TYPE_OF: {
        llvm::Value* val = vstack.back(); vstack.pop_back();
        llvm::Function* fnTypeOf = module.getFunction("havel_vm_type_of");
        if (!fnTypeOf) {
            fnTypeOf = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_type_of", &module);
        }
        vstack.push_back(B.CreateCall(fnTypeOf, {vmArg, val}));
        break;
    }
    case OpCode::AS_TYPE: {
        // Operand: type_name_id (string val id)
        uint32_t typeNameId = instr.operands[0].asStringValId();
        llvm::Value* val = vstack.back(); vstack.pop_back();
        llvm::Function* fnAsType = module.getFunction("havel_vm_as_type");
        if (!fnAsType) {
            fnAsType = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i32}, false),
                llvm::Function::ExternalLinkage, "havel_vm_as_type", &module);
        }
        vstack.push_back(B.CreateCall(fnAsType, {vmArg, val, llvm::ConstantInt::get(i32, typeNameId)}));
        break;
    }

    // Special operations
    case OpCode::PRINT: {
        llvm::Value* val = vstack.back(); vstack.pop_back();
        llvm::Function* fnPrint = module.getFunction("havel_vm_print");
        if (!fnPrint) {
            fnPrint = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_print", &module);
        }
        B.CreateCall(fnPrint, {vmArg, val});
        break;
    }
    case OpCode::DEBUG: {
        llvm::Function* fnDebug = module.getFunction("havel_vm_debug");
        if (!fnDebug) {
            fnDebug = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p}, false),
                llvm::Function::ExternalLinkage, "havel_vm_debug", &module);
        }
        B.CreateCall(fnDebug, {vmArg});
        break;
    }
    case OpCode::IMPORT: {
        llvm::Value* path = vstack.back(); vstack.pop_back();
        llvm::Function* fnImport = module.getFunction("havel_vm_import");
        if (!fnImport) {
            fnImport = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_import", &module);
        }
        vstack.push_back(B.CreateCall(fnImport, {vmArg, path}));
        break;
    }

    case OpCode::IMPORT_WILDCARD: {
        llvm::Value* exports = vstack.back(); vstack.pop_back();
        llvm::Function* fnImportWildcard = module.getFunction("havel_vm_import_wildcard");
        if (!fnImportWildcard) {
            fnImportWildcard = llvm::Function::Create(
                llvm::FunctionType::get(llvm::Type::getVoidTy(ctx), {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_import_wildcard", &module);
        }
        B.CreateCall(fnImportWildcard, {vmArg, exports});
        break;
    }

    // Class operations
    case OpCode::STRUCT_NEW: {
        // Operands: type_name_id, arg_count
        uint32_t typeId = instr.operands[0].asStringValId();
        uint32_t argCount = instr.operands[1].asInt();
        llvm::Value* argsArray = B.CreateAlloca(llvm::ArrayType::get(i64, argCount), nullptr, "struct_args");
        for (uint32_t i = 0; i < argCount; ++i) {
            llvm::Value* arg = vstack.back(); vstack.pop_back();
            B.CreateStore(arg, B.CreateInBoundsGEP(llvm::ArrayType::get(i64, argCount), argsArray,
                {llvm::ConstantInt::get(i32, 0), llvm::ConstantInt::get(i32, argCount - 1 - i)}));
        }
        llvm::Function* fnStructNew = module.getFunction("havel_vm_struct_new");
        if (!fnStructNew) {
            fnStructNew = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i32, i64p, i32}, false),
                llvm::Function::ExternalLinkage, "havel_vm_struct_new", &module);
        }
        vstack.push_back(B.CreateCall(fnStructNew, {vmArg,
            llvm::ConstantInt::get(i32, typeId),
            B.CreateInBoundsGEP(llvm::ArrayType::get(i64, argCount), argsArray,
                {llvm::ConstantInt::get(i32, 0), llvm::ConstantInt::get(i32, 0)}),
            llvm::ConstantInt::get(i32, argCount)}));
        break;
    }
    case OpCode::STRUCT_GET: {
        uint32_t fieldId = instr.operands[0].asStringValId();
        llvm::Value* obj = vstack.back(); vstack.pop_back();
        llvm::Function* fnStructGet = module.getFunction("havel_vm_struct_get");
        if (!fnStructGet) {
            fnStructGet = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i32}, false),
                llvm::Function::ExternalLinkage, "havel_vm_struct_get", &module);
        }
        vstack.push_back(B.CreateCall(fnStructGet, {vmArg, obj, llvm::ConstantInt::get(i32, fieldId)}));
        break;
    }
    case OpCode::STRUCT_SET: {
        uint32_t fieldId = instr.operands[0].asStringValId();
        llvm::Value* val = vstack.back(); vstack.pop_back();
        llvm::Value* obj = vstack.back(); vstack.pop_back();
        llvm::Function* fnStructSet = module.getFunction("havel_vm_struct_set");
        if (!fnStructSet) {
            fnStructSet = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i32, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_struct_set", &module);
        }
        vstack.push_back(B.CreateCall(fnStructSet, {vmArg, obj, llvm::ConstantInt::get(i32, fieldId), val}));
        break;
    }
    case OpCode::PROT_CHECK: {
        uint32_t protoId = instr.operands[0].asStringValId();
        llvm::Value* val = vstack.back(); vstack.pop_back();
        llvm::Function* fnProtCheck = module.getFunction("havel_vm_prot_check");
        if (!fnProtCheck) {
            fnProtCheck = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i32}, false),
                llvm::Function::ExternalLinkage, "havel_vm_prot_check", &module);
        }
        vstack.push_back(B.CreateCall(fnProtCheck, {vmArg, val, llvm::ConstantInt::get(i32, protoId)}));
        break;
    }
    case OpCode::PROT_CAST: {
        uint32_t protoId = instr.operands[0].asStringValId();
        llvm::Value* val = vstack.back(); vstack.pop_back();
        llvm::Function* fnProtCast = module.getFunction("havel_vm_prot_cast");
        if (!fnProtCast) {
            fnProtCast = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i32}, false),
                llvm::Function::ExternalLinkage, "havel_vm_prot_cast", &module);
        }
        vstack.push_back(B.CreateCall(fnProtCast, {vmArg, val, llvm::ConstantInt::get(i32, protoId)}));
        break;
    }
    case OpCode::CLASS_NEW: {
        // Operands: typeId, parentTypeId, fieldCount
        uint32_t typeId = instr.operands[0].asInt();
        uint32_t parentTypeId = instr.operands[1].asInt();
        uint32_t fieldCount = instr.operands[2].asInt();
        llvm::Function* fnClassNew = module.getFunction("havel_vm_class_new");
        if (!fnClassNew) {
            fnClassNew = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i32, i32, i32}, false),
                llvm::Function::ExternalLinkage, "havel_vm_class_new", &module);
        }
        vstack.push_back(B.CreateCall(fnClassNew, {vmArg,
            llvm::ConstantInt::get(i32, typeId),
            llvm::ConstantInt::get(i32, parentTypeId),
            llvm::ConstantInt::get(i32, fieldCount)}));
        break;
    }
    case OpCode::CLASS_GET_FIELD: {
        // Operand: field_id; Stack: [object]
        uint32_t fieldId = instr.operands[0].asInt();
        llvm::Value* obj = vstack.back(); vstack.pop_back();
        llvm::Function* fnGetField = module.getFunction("havel_vm_class_get_field");
        if (!fnGetField) {
            fnGetField = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i32}, false),
                llvm::Function::ExternalLinkage, "havel_vm_class_get_field", &module);
        }
        vstack.push_back(B.CreateCall(fnGetField, {vmArg, obj, llvm::ConstantInt::get(i32, fieldId)}));
        break;
    }
    case OpCode::CLASS_SET_FIELD: {
        // Operand: field_id; Stack: [object, value]
        uint32_t fieldId = instr.operands[0].asInt();
        llvm::Value* val = vstack.back(); vstack.pop_back();
        llvm::Value* obj = vstack.back(); vstack.pop_back();
        llvm::Function* fnSetField = module.getFunction("havel_vm_class_set_field");
        if (!fnSetField) {
            fnSetField = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i32, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_class_set_field", &module);
        }
        vstack.push_back(B.CreateCall(fnSetField, {vmArg, obj, llvm::ConstantInt::get(i32, fieldId), val}));
        break;
    }
    case OpCode::LOAD_CLASS_PROTO: {
        // Operand: type_id
        uint32_t typeId = instr.operands[0].asInt();
        llvm::Function* fnLoadProto = module.getFunction("havel_vm_load_class_proto");
        if (!fnLoadProto) {
            fnLoadProto = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i32}, false),
                llvm::Function::ExternalLinkage, "havel_vm_load_class_proto", &module);
        }
        vstack.push_back(B.CreateCall(fnLoadProto, {vmArg, llvm::ConstantInt::get(i32, typeId)}));
        break;
    }
    case OpCode::CALL_SUPER: {
        // Operands: method_name_id, arg_count
        uint32_t methodNameId = instr.operands[0].asStringValId();
        uint32_t argCount = instr.operands[1].asInt();
        llvm::Value* argsArray = B.CreateAlloca(llvm::ArrayType::get(i64, argCount), nullptr, "super_args");
        for (uint32_t i = 0; i < argCount; ++i) {
            llvm::Value* arg = vstack.back(); vstack.pop_back();
            B.CreateStore(arg, B.CreateInBoundsGEP(llvm::ArrayType::get(i64, argCount), argsArray,
                {llvm::ConstantInt::get(i32, 0), llvm::ConstantInt::get(i32, argCount - 1 - i)}));
        }
        llvm::Value* obj = vstack.back(); vstack.pop_back();
        llvm::Function* fnSuper = module.getFunction("havel_vm_call_super");
        if (!fnSuper) {
            fnSuper = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i32, i64p, i32}, false),
                llvm::Function::ExternalLinkage, "havel_vm_call_super", &module);
        }
        vstack.push_back(B.CreateCall(fnSuper, {vmArg, obj,
            llvm::ConstantInt::get(i32, methodNameId),
            B.CreateInBoundsGEP(llvm::ArrayType::get(i64, argCount), argsArray,
                {llvm::ConstantInt::get(i32, 0), llvm::ConstantInt::get(i32, 0)}),
            llvm::ConstantInt::get(i32, argCount)}));
        break;
    }

    // Coroutine operations
    case OpCode::YIELD_RESUME: {
        llvm::Value* co = vstack.back(); vstack.pop_back();
        llvm::Function* fnResume = module.getFunction("havel_vm_yield_resume");
        if (!fnResume) {
            fnResume = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_yield_resume", &module);
        }
        vstack.push_back(B.CreateCall(fnResume, {vmArg, co}));
        break;
    }
    case OpCode::GO_ASYNC: {
        llvm::Value* fn = vstack.back(); vstack.pop_back();
        llvm::Function* fnGoAsync = module.getFunction("havel_vm_go_async");
        if (!fnGoAsync) {
            fnGoAsync = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_go_async", &module);
        }
        vstack.push_back(B.CreateCall(fnGoAsync, {vmArg, fn}));
        break;
    }

  case OpCode::BEGIN_MODULE: {
        llvm::Function* fnBegin = module.getFunction("havel_vm_begin_module");
        if (!fnBegin) {
            fnBegin = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p}, false),
                llvm::Function::ExternalLinkage, "havel_vm_begin_module", &module);
        }
        B.CreateCall(fnBegin, {vmArg});
        break;
    }
    case OpCode::END_MODULE: {
        llvm::Function* fnEnd = module.getFunction("havel_vm_end_module");
        if (!fnEnd) {
            fnEnd = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p}, false),
                llvm::Function::ExternalLinkage, "havel_vm_end_module", &module);
        }
        vstack.push_back(B.CreateCall(fnEnd, {vmArg}));
        break;
    }

    case OpCode::IS: {
        llvm::Value* r = vstack.back(); vstack.pop_back();
        llvm::Value* l = vstack.back(); vstack.pop_back();
        llvm::Function* fnIs = module.getFunction("havel_vm_is");
        if (!fnIs) {
          fnIs = llvm::Function::Create(
            llvm::FunctionType::get(i64, {i64, i64}, false),
            llvm::Function::ExternalLinkage, "havel_vm_is", &module);
        }
        vstack.push_back(B.CreateCall(fnIs, {l, r}));
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
    llvm::Function* fnNot = module.getFunction("havel_vm_not");
    if (!fnNot) {
        fnNot = llvm::Function::Create(
            llvm::FunctionType::get(i64, {i64}, false),
            llvm::Function::ExternalLinkage, "havel_vm_not", &module);
    }
    vstack.push_back(B.CreateCall(fnNot, {v}));
    break;
}
case OpCode::LENGTH: {
    llvm::Value* v = vstack.back(); vstack.pop_back();
    llvm::Function* fnLen = module.getFunction("havel_vm_length");
    if (!fnLen) {
        fnLen = llvm::Function::Create(
            llvm::FunctionType::get(i64, {i8p, i64}, false),
            llvm::Function::ExternalLinkage, "havel_vm_length", &module);
    }
    vstack.push_back(B.CreateCall(fnLen, {vmArg, v}));
    break;
}

    // Control flow
    case OpCode::JUMP: {
        size_t target = instr.operands[0].asInt();
        if (target < ip) {
            llvm::Function* fnBe = module.getFunction("havel_vm_backedge");
            if (!fnBe) {
                fnBe = llvm::Function::Create(
                    llvm::FunctionType::get(llvm::Type::getVoidTy(ctx), {i8p, i32}, false),
                    llvm::Function::ExternalLinkage, "havel_vm_backedge", &module);
            }
            B.CreateCall(fnBe, {vmArg, llvm::ConstantInt::get(i32, static_cast<uint32_t>(ip))});
        }
        if (target < basicBlocks.size()) {
            B.CreateBr(basicBlocks[target]);
        } else {
            B.CreateBr(basicBlocks[ip + 1]);
        }
        break;
    }
    case OpCode::JUMP_IF_FALSE: {
        size_t target = instr.operands[0].asInt();
        llvm::Value* cond = vstack.back(); vstack.pop_back();
        llvm::Function* fnTruthy = module.getFunction("havel_vm_is_truthy");
        if (!fnTruthy) {
            fnTruthy = llvm::Function::Create(
                llvm::FunctionType::get(i32, {i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_is_truthy", &module);
        }
        llvm::Value* truthyResult = B.CreateCall(fnTruthy, {cond});
        llvm::Value* isFalsy = B.CreateICmpEQ(truthyResult, llvm::ConstantInt::get(i32, 0));
        if (target < ip) {
            llvm::Function* fnBe = module.getFunction("havel_vm_backedge");
            if (!fnBe) {
                fnBe = llvm::Function::Create(
                    llvm::FunctionType::get(llvm::Type::getVoidTy(ctx), {i8p, i32}, false),
                    llvm::Function::ExternalLinkage, "havel_vm_backedge", &module);
            }
            B.CreateCall(fnBe, {vmArg, llvm::ConstantInt::get(i32, static_cast<uint32_t>(ip))});
        }
        if (target < basicBlocks.size()) {
            B.CreateCondBr(isFalsy, basicBlocks[target], basicBlocks[ip + 1]);
        } else {
            B.CreateBr(basicBlocks[ip + 1]);
        }
        break;
    }
    case OpCode::JUMP_IF_TRUE: {
        size_t target = instr.operands[0].asInt();
        llvm::Value* cond = vstack.back(); vstack.pop_back();
        llvm::Function* fnTruthy = module.getFunction("havel_vm_is_truthy");
        if (!fnTruthy) {
            fnTruthy = llvm::Function::Create(
                llvm::FunctionType::get(i32, {i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_is_truthy", &module);
        }
        llvm::Value* truthyResult = B.CreateCall(fnTruthy, {cond});
        llvm::Value* isTruthy = B.CreateICmpNE(truthyResult, llvm::ConstantInt::get(i32, 0));
        if (target < ip) {
            llvm::Function* fnBe = module.getFunction("havel_vm_backedge");
            if (!fnBe) {
                fnBe = llvm::Function::Create(
                    llvm::FunctionType::get(llvm::Type::getVoidTy(ctx), {i8p, i32}, false),
                    llvm::Function::ExternalLinkage, "havel_vm_backedge", &module);
            }
            B.CreateCall(fnBe, {vmArg, llvm::ConstantInt::get(i32, static_cast<uint32_t>(ip))});
        }
        if (target < basicBlocks.size()) {
            B.CreateCondBr(isTruthy, basicBlocks[target], basicBlocks[ip + 1]);
        } else {
            B.CreateBr(basicBlocks[ip + 1]);
        }
        break;
    }
    case OpCode::JUMP_IF_NULL: {
        size_t target = instr.operands[0].asInt();
        llvm::Value* v = vstack.back();
        llvm::Value* isNull = B.CreateICmpEQ(v, makeNull());
        vstack.pop_back(); // Coalesce consumes the value
        if (target < basicBlocks.size()) {
            B.CreateCondBr(isNull, basicBlocks[target], basicBlocks[ip + 1]);
        } else {
            B.CreateBr(basicBlocks[ip + 1]);
        }
        break;
    }

    // Function calls
    case OpCode::CALL: {
        uint32_t argCount = instr.operands[0].asInt();
        // Collect args from stack (in reverse order for calling convention)
        // Stack layout: [callee, arg0, arg1, ..., argN]
        // havel_vm_call expects: args[0] = callee, args[1..N] = actual args
        std::vector<llvm::Value*> args;
        args.push_back(vmArg);
        // Create args array on stack (argCount + 1 for callee)
        llvm::Value* argsArray = B.CreateAlloca(llvm::ArrayType::get(i64, argCount + 1), nullptr, "call_args");
        // Pop args in reverse order (argN, argN-1, ..., arg0)
        for (uint32_t i = 0; i < argCount; ++i) {
            llvm::Value* arg = vstack.back(); vstack.pop_back();
            B.CreateStore(arg, B.CreateInBoundsGEP(llvm::ArrayType::get(i64, argCount + 1), argsArray,
                {llvm::ConstantInt::get(i32, 0), llvm::ConstantInt::get(i32, argCount - i)}));
        }
        // Pop callee and store at index 0
        llvm::Value* callee = vstack.back(); vstack.pop_back();
        B.CreateStore(callee, B.CreateInBoundsGEP(llvm::ArrayType::get(i64, argCount + 1), argsArray,
            {llvm::ConstantInt::get(i32, 0), llvm::ConstantInt::get(i32, 0)}));
        args.push_back(B.CreateInBoundsGEP(llvm::ArrayType::get(i64, argCount + 1), argsArray,
            {llvm::ConstantInt::get(i32, 0), llvm::ConstantInt::get(i32, 0)}));
        args.push_back(llvm::ConstantInt::get(i32, argCount + 1)); // +1 for callee

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
    case OpCode::FFI_CALL: {
        // FFI_CALL operands: [ret_type_raw, param_types_raw, arg_count]
        // Stack: [..., fn_ptr, arg0, arg1, ..., argN, arg_count, param_types_raw, ret_type_raw]
        // We need to pop: ret_type_raw, param_types_raw, arg_count, then args...
        if (instr.operands.size() != 3 || !instr.operands[0].isInt() || 
            !instr.operands[1].isInt() || !instr.operands[2].isInt()) {
            vstack.push_back(makeNull());
            break;
        }
        
        uint64_t ret_type_raw = instr.operands[0].asInt();
        uint64_t param_types_raw = instr.operands[1].asInt();
        uint32_t arg_count = instr.operands[2].asInt();
        
        // Pop arg_count from stack (number of arguments to the FFI function)
        llvm::Value* argCountVal = vstack.back(); vstack.pop_back();
        llvm::Value* argCount = B.CreateZExtOrTrunc(argCountVal, i32);
        
        // Pop args array (the actual arguments to the FFI function)
        std::vector<llvm::Value*> args(arg_count);
        for (uint32_t i = 0; i < arg_count; ++i) {
            args[i] = vstack.back(); vstack.pop_back();
        }
        
        // Pop fn_ptr
        llvm::Value* fn_ptr = vstack.back(); vstack.pop_back();
        
        // Call VM helper for direct libffi call
        llvm::Function* fnFFICall = module.getFunction("havel_vm_ffi_call");
        if (!fnFFICall) {
            fnFFICall = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i64, i64, i64, i32}, false),
                llvm::Function::ExternalLinkage, "havel_vm_ffi_call", &module);
        }
        
        // args_array: we need to pass the arguments array
        // For simplicity, we'll allocate an array and store args in it
        llvm::Value* argsArray = B.CreateAlloca(llvm::ArrayType::get(i64, arg_count), nullptr, "ffi_args");
        for (uint32_t i = 0; i < arg_count; ++i) {
            B.CreateStore(args[i], B.CreateInBoundsGEP(llvm::ArrayType::get(i64, arg_count), argsArray,
                {llvm::ConstantInt::get(i32, 0), llvm::ConstantInt::get(i32, i)}));
        }
        
        vstack.push_back(B.CreateCall(fnFFICall, {
            vmArg,
            fn_ptr,                    // fn_ptr_raw
            llvm::ConstantInt::get(i64, ret_type_raw),   // ret_type_raw
            llvm::ConstantInt::get(i64, param_types_raw), // param_types_raw
            B.CreateInBoundsGEP(llvm::ArrayType::get(i64, arg_count), argsArray,
                {llvm::ConstantInt::get(i32, 0), llvm::ConstantInt::get(i32, 0)}), // args_array_raw
            llvm::ConstantInt::get(i32, arg_count)        // arg_count
        }));
        break;
    }
    case OpCode::CALL_DYN: {
        llvm::Value* argCountVal = vstack.back(); vstack.pop_back();
        llvm::Value* argCount = B.CreateZExtOrTrunc(argCountVal, i32);
        
        llvm::Function* fnCallDyn = module.getFunction("havel_vm_call_dyn");
        if (!fnCallDyn) {
            fnCallDyn = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i32}, false),
                llvm::Function::ExternalLinkage, "havel_vm_call_dyn", &module);
        }
        vstack.push_back(B.CreateCall(fnCallDyn, {vmArg, argCount}));
        break;
    }
    case OpCode::CALL_SPREAD: {
        uint32_t lit_before = instr.operands[0].asInt();
        uint32_t lit_after = instr.operands[1].asInt();
        
        std::vector<llvm::Value*> after_args(lit_after);
        for (uint32_t i = 0; i < lit_after; ++i) {
            after_args[i] = vstack.back(); vstack.pop_back();
        }
        
        llvm::Value* array_val = vstack.back(); vstack.pop_back();
        
        std::vector<llvm::Value*> before_args(lit_before);
        for (uint32_t i = 0; i < lit_before; ++i) {
            before_args[i] = vstack.back(); vstack.pop_back();
        }
        
        llvm::Value* callee = vstack.back(); vstack.pop_back();
        
        llvm::Function* fnSpread = module.getFunction("havel_vm_call_spread");
        if (!fnSpread) {
            fnSpread = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i32, i32, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_call_spread", &module);
        }
        vstack.push_back(B.CreateCall(fnSpread, {
            vmArg,
            callee,
            llvm::ConstantInt::get(i32, lit_before),
            llvm::ConstantInt::get(i32, lit_after),
            array_val
        }));
        break;
    }
    case OpCode::CALL_METHOD_SPREAD: {
        if (instr.operands.size() != 3 || 
            !instr.operands[0].isStringValId() || 
            !instr.operands[1].isInt() || 
            !instr.operands[2].isInt()) {
            vstack.push_back(makeNull());
            break;
        }
        
        uint32_t methodNameId = instr.operands[0].asStringValId();
        uint32_t lit_before = instr.operands[1].asInt();
        uint32_t lit_after = instr.operands[2].asInt();
        
        std::vector<llvm::Value*> after_args(lit_after);
        for (uint32_t i = 0; i < lit_after; ++i) {
            after_args[i] = vstack.back(); vstack.pop_back();
        }
        
        llvm::Value* array_val = vstack.back(); vstack.pop_back();
        
        std::vector<llvm::Value*> before_args(lit_before);
        for (uint32_t i = 0; i < lit_before; ++i) {
            before_args[i] = vstack.back(); vstack.pop_back();
        }
        
        llvm::Value* receiver = vstack.back(); vstack.pop_back();
        
        llvm::Function* fnMethodSpread = module.getFunction("havel_vm_call_method_spread");
        if (!fnMethodSpread) {
            fnMethodSpread = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i32, i32, i32, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_call_method_spread", &module);
        }
        
        vstack.push_back(B.CreateCall(fnMethodSpread, {
            vmArg,
            receiver,
            llvm::ConstantInt::get(i32, methodNameId),
            llvm::ConstantInt::get(i32, lit_before),
            llvm::ConstantInt::get(i32, lit_after),
            array_val
        }));
        break;
    }
    case OpCode::CALL_IF_FUNCTION: {
        llvm::Value* val = vstack.back();
        
        llvm::Function* fnCallIf = module.getFunction("havel_vm_call_if_function");
        if (!fnCallIf) {
            fnCallIf = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_call_if_function", &module);
        }
        vstack.pop_back();
        vstack.push_back(B.CreateCall(fnCallIf, {vmArg, val}));
        break;
    }
    case OpCode::TAIL_CALL: {
        uint32_t argCount = instr.operands[0].asInt();
        // The bytecode-level arg count is the number of actual args (NOT
        // including the callee). Interpreter (VMControlFlow.cpp TAIL_CALL)
        // pops arg_count+1 items: arg_count args + the callee underneath.
        // havel_vm_tail_call / havel_vm_call both expect args[0] = callee,
        // args[1..N] = actual args, count = arg_count + 1.
        //
        // Previously this handler popped only arg_count items, leaving the
        // callee on the VM stack and passing args[0] = first actual arg to
        // havel_vm_call — which then read args[0] as the callee. That made
        // every TAIL_CALL treat its first argument as the callee ("TAIL_CALL
        // expects function ... (got int)" when calling print(int)).
        const uint32_t callCount = argCount + 1;
        std::vector<llvm::Value*> args;
        args.push_back(vmArg);
        llvm::Function* fnMalloc = module.getFunction("malloc");
        if (!fnMalloc) {
            fnMalloc = llvm::Function::Create(
                llvm::FunctionType::get(i8p, {i64}, false),
                llvm::Function::ExternalLinkage, "malloc", &module);
        }
        llvm::Value* mallocSize = llvm::ConstantInt::get(i64, callCount * sizeof(uint64_t));
        llvm::Value* argsArrayI8 = B.CreateCall(fnMalloc, {mallocSize});
        llvm::Value* argsArray = B.CreatePointerCast(argsArrayI8, llvm::PointerType::get(ctx, 0), "tail_args");
        // Pop arg_count actual args (in reverse order: argN, ..., arg1, arg0)
        for (uint32_t i = 0; i < argCount; ++i) {
            llvm::Value* arg = vstack.back(); vstack.pop_back();
            // args[callCount - 1 - i] = argN, args[callCount - 2] = arg(N-1)...
            // The callee slot (index 0) is written last after the pop below.
            B.CreateStore(arg, B.CreateInBoundsGEP(llvm::ArrayType::get(i64, callCount), argsArray,
                {llvm::ConstantInt::get(i32, 0), llvm::ConstantInt::get(i32, callCount - 1 - i)}));
        }
        // Pop the callee and store it at args[0].
        llvm::Value* callee = vstack.back(); vstack.pop_back();
        B.CreateStore(callee, B.CreateInBoundsGEP(llvm::ArrayType::get(i64, callCount), argsArray,
            {llvm::ConstantInt::get(i32, 0), llvm::ConstantInt::get(i32, 0)}));
        args.push_back(B.CreateInBoundsGEP(llvm::ArrayType::get(i64, callCount), argsArray,
            {llvm::ConstantInt::get(i32, 0), llvm::ConstantInt::get(i32, 0)}));
        args.push_back(llvm::ConstantInt::get(i32, callCount));

        // Unregister GC roots before tail call
        llvm::Function *fn_unreg = module.getFunction("havel_gc_unregister_roots");
        if (!fn_unreg) fn_unreg = llvm::Function::Create(llvm::FunctionType::get(voidT, {llvm::PointerType::get(ctx, 0)}, false), llvm::Function::ExternalLinkage, "havel_gc_unregister_roots", &module);
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
        // Close open upvalues for this frame before returning
        llvm::Function* fnClose = module.getFunction("havel_vm_close_upvalues");
        if (!fnClose) {
            fnClose = llvm::Function::Create(
                llvm::FunctionType::get(voidT, {i8p, i32}, false),
                llvm::Function::ExternalLinkage, "havel_vm_close_upvalues", &module);
        }
        llvm::Function* fnLocalsBase = module.getFunction("havel_vm_locals_base");
        if (!fnLocalsBase) {
            fnLocalsBase = llvm::Function::Create(
                llvm::FunctionType::get(i32, {i8p}, false),
                llvm::Function::ExternalLinkage, "havel_vm_locals_base", &module);
        }
        llvm::Value* lb = B.CreateCall(fnLocalsBase, {vmArg});
        B.CreateCall(fnClose, {vmArg, lb});
        llvm::Function *fn_unreg = module.getFunction("havel_gc_unregister_roots");
        if (!fn_unreg) fn_unreg = llvm::Function::Create(llvm::FunctionType::get(voidT, {llvm::PointerType::get(ctx, 0)}, false), llvm::Function::ExternalLinkage, "havel_gc_unregister_roots", &module);
        B.CreateCall(fn_unreg, {frame});
        B.CreateRet(vstack.empty() ? makeNull() : vstack.back());
        break;
    }
    case OpCode::TRY_ENTER: {
        uint32_t catchIp = instr.operands[0].asInt();
        uint32_t finallyIp = (instr.operands.size() >= 2 && instr.operands[1].isInt()) ? instr.operands[1].asInt() : 0;
        llvm::Function* fnTryEnter = module.getFunction("havel_vm_try_enter");
        if (!fnTryEnter) {
            fnTryEnter = llvm::Function::Create(
                llvm::FunctionType::get(voidT, {llvm::PointerType::get(ctx, 0), i32, i32, i32}, false),
                llvm::Function::ExternalLinkage, "havel_vm_try_enter", &module);
        }
        B.CreateCall(fnTryEnter, {frame,
                                  llvm::ConstantInt::get(i32, catchIp),
                                  llvm::ConstantInt::get(i32, finallyIp),
                                  llvm::ConstantInt::get(i32, static_cast<uint32_t>(vstack.size()))});
        jit_try_stack_depths.push_back(vstack.size());
        break;
    }
    case OpCode::TRY_EXIT: {
        llvm::Function* fnTryExit = module.getFunction("havel_vm_try_exit");
        if (!fnTryExit) {
            fnTryExit = llvm::Function::Create(
                llvm::FunctionType::get(voidT, {llvm::PointerType::get(ctx, 0)}, false),
                llvm::Function::ExternalLinkage, "havel_vm_try_exit", &module);
        }
        B.CreateCall(fnTryExit, {frame});
        if (!jit_try_stack_depths.empty()) {
            jit_try_stack_depths.pop_back();
        }
        break;
    }
    case OpCode::LOAD_EXCEPTION: {
        llvm::Function* fnLoadExc = module.getFunction("havel_vm_load_exception");
        if (!fnLoadExc) {
            fnLoadExc = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p}, false),
                llvm::Function::ExternalLinkage, "havel_vm_load_exception", &module);
        }
        vstack.push_back(B.CreateCall(fnLoadExc, {vmArg}));
        break;
    }
    case OpCode::THROW: {
        llvm::Value* thrown = vstack.empty() ? makeNull() : vstack.back();
        if (!vstack.empty()) {
            vstack.pop_back();
        }
        llvm::Function* fnSetExc = module.getFunction("havel_vm_set_exception");
        if (!fnSetExc) {
            fnSetExc = llvm::Function::Create(
                llvm::FunctionType::get(voidT, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_set_exception", &module);
        }
        B.CreateCall(fnSetExc, {vmArg, thrown});

        llvm::Value* catchDepthAlloca = B.CreateAlloca(i32, nullptr, "catch_depth");
        llvm::Value* poppedCountAlloca = B.CreateAlloca(i32, nullptr, "popped_count");
        B.CreateStore(llvm::ConstantInt::get(i32, 0), catchDepthAlloca);
        B.CreateStore(llvm::ConstantInt::get(i32, 0), poppedCountAlloca);

        llvm::Function* fnFindHandler = module.getFunction("havel_vm_try_find_throw_target");
        if (!fnFindHandler) {
            fnFindHandler = llvm::Function::Create(
                llvm::FunctionType::get(
                    i32,
                    {llvm::PointerType::get(ctx, 0),
                     llvm::PointerType::get(ctx, 0),
                     llvm::PointerType::get(ctx, 0)},
                    false),
                llvm::Function::ExternalLinkage, "havel_vm_try_find_throw_target", &module);
        }
        llvm::Value* catchIp =
            B.CreateCall(fnFindHandler, {frame, catchDepthAlloca, poppedCountAlloca});
        llvm::Value* hasHandler =
            B.CreateICmpNE(catchIp, llvm::ConstantInt::get(i32, UINT32_MAX));

        llvm::BasicBlock* throwDispatchBB = llvm::BasicBlock::Create(ctx, "throw_dispatch", f);
        llvm::BasicBlock* throwUnwindBB = llvm::BasicBlock::Create(ctx, "throw_unwind", f);
        B.CreateCondBr(hasHandler, throwDispatchBB, throwUnwindBB);

        B.SetInsertPoint(throwDispatchBB);
        // Conservative stack-state reset for catch entry. This avoids
        // reusing stale SSA values after handler-walk pops.
        vstack.clear();
        jit_try_stack_depths.clear();
        llvm::SwitchInst* sw = B.CreateSwitch(catchIp, throwUnwindBB, basicBlocks.size());
        for (size_t target = 0; target < basicBlocks.size(); ++target) {
            auto* caseVal = llvm::cast<llvm::ConstantInt>(
                llvm::ConstantInt::get(i32, static_cast<uint32_t>(target)));
            sw->addCase(caseVal, basicBlocks[target]);
        }

        B.SetInsertPoint(throwUnwindBB);
        llvm::Function* fnThrow = module.getFunction("havel_vm_throw_from_jit");
        if (!fnThrow) {
            fnThrow = llvm::Function::Create(
                llvm::FunctionType::get(voidT, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_throw_from_jit", &module);
        }
        B.CreateCall(fnThrow, {vmArg, thrown});
        B.CreateUnreachable();
        break;
    }

    // Global and upvalue access - critical for closures
    case OpCode::LOAD_GLOBAL: {
        // The operand is a chunk-local StringValId packing
        // (chunkId << 31 | stringIndex); the interpreter resolves the low
        // 31 bits (asStringValId() masks chunkId out) and the Runtime ABI
        // bridge indexes the owning chunk's string table with it. Reading
        // the raw asInt() here passed the full 33-bit value for module
        // chunks, so havel_vm_global_get's bounds check rejected EVERY
        // global load in module code - compiled module functions saw all
        // their globals as null (JIT'd getBPTABLE rebuilt the binding
        // table with null keys every call; getBindingPower then read
        // BP_NONE for everything and the self-hosted parser broke).
        const uint32_t nameId =
            static_cast<uint32_t>(instr.operands[0].asStringValId());
        llvm::Function* fnGet = module.getFunction("havel_vm_global_get");
        if (!fnGet) {
            fnGet = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i32}, false),
                llvm::Function::ExternalLinkage, "havel_vm_global_get", &module);
        }
        vstack.push_back(B.CreateCall(fnGet, {vmArg, llvm::ConstantInt::get(i32, nameId)}));
        break;
    }
    case OpCode::STORE_GLOBAL:
    case OpCode::STORE_IMMUT_GLOBAL: {
        // Same StringValId masking as LOAD_GLOBAL above.
        const uint32_t nameId =
            static_cast<uint32_t>(instr.operands[0].asStringValId());
        llvm::Value* v = vstack.back(); vstack.pop_back();
        llvm::Function* fnSet = module.getFunction("havel_vm_global_set");
        if (!fnSet) {
            fnSet = llvm::Function::Create(
                llvm::FunctionType::get(voidT, {i8p, i32, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_global_set", &module);
        }
        B.CreateCall(fnSet, {vmArg, llvm::ConstantInt::get(i32, nameId), v});
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
        llvm::Value* r = vstack.back(); vstack.pop_back();
        llvm::Value* l = vstack.back(); vstack.pop_back();
        llvm::Function* fnTruthy = module.getFunction("havel_vm_is_truthy");
        if (!fnTruthy) {
            fnTruthy = llvm::Function::Create(
                llvm::FunctionType::get(i32, {i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_is_truthy", &module);
        }
        llvm::Value* lTruthy = B.CreateCall(fnTruthy, {l});
        llvm::Value* lIsFalsy = B.CreateICmpEQ(lTruthy, llvm::ConstantInt::get(i32, 0));
        vstack.push_back(B.CreateSelect(lIsFalsy, l, r));
        break;
    }
    case OpCode::OR: {
        llvm::Value* r = vstack.back(); vstack.pop_back();
        llvm::Value* l = vstack.back(); vstack.pop_back();
        llvm::Function* fnTruthy = module.getFunction("havel_vm_is_truthy");
        if (!fnTruthy) {
            fnTruthy = llvm::Function::Create(
                llvm::FunctionType::get(i32, {i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_is_truthy", &module);
        }
        llvm::Value* lTruthy = B.CreateCall(fnTruthy, {l});
        llvm::Value* lIsTruthy = B.CreateICmpNE(lTruthy, llvm::ConstantInt::get(i32, 0));
        vstack.push_back(B.CreateSelect(lIsTruthy, l, r));
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
        llvm::Function* fnGet = module.getFunction("havel_vm_collection_get_raw_ic");
        if (!fnGet) {
            fnGet = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_collection_get_raw_ic", &module);
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
        llvm::Value* key = vstack.back(); vstack.pop_back();
        llvm::Value* obj = vstack.back(); vstack.pop_back();
        llvm::Function* fnGet = module.getFunction("havel_vm_object_get_raw_ic");
        if (!fnGet) {
            fnGet = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_object_get_raw_ic", &module);
        }
        vstack.push_back(B.CreateCall(fnGet, {vmArg, obj, key}));
        break;
    }
    case OpCode::OBJECT_SET: {
        // Matches interpreter stack protocol (VMCollections.cpp OBJECT_SET):
        //   Stack: [..., obj, value, key] → pops key, then value, then obj
        // The interpreter pops key FIRST (top of stack), value SECOND, obj
        // LAST. havel_vm_object_set_raw(vm, obj, key, val) takes key/val
        // in that order. Previously this handler popped in val/key/obj
        // order, silently swapping key and value, which made every
        // JIT/AOT OBJECT_SET write the value under the wrong key — and
        // the matching get returned null.
        llvm::Value* key = vstack.back(); vstack.pop_back();
        llvm::Value* val = vstack.back(); vstack.pop_back();
        llvm::Value* obj = vstack.back(); vstack.pop_back();
        llvm::Function* fnSet = module.getFunction("havel_vm_object_set_raw");
        if (!fnSet) {
            fnSet = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i64, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_object_set_raw", &module);
        }
        vstack.push_back(B.CreateCall(fnSet, {vmArg, obj, key, val}));
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
        llvm::Value* iter = vstack.back(); vstack.pop_back();
        llvm::Function* fnNext = module.getFunction("havel_vm_iter_next");
        if (!fnNext) {
            fnNext = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_iter_next", &module);
        }
        // Interpreter semantics: consume iterator operand and push next result.
        llvm::Value* result = B.CreateCall(fnNext, {vmArg, iter});
        vstack.push_back(result);
        break;
    }
    case OpCode::TIME_NOW: {
        llvm::Function* fnTimeNow = module.getFunction("havel_vm_time_now");
        if (!fnTimeNow) {
            fnTimeNow = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p}, false),
                llvm::Function::ExternalLinkage, "havel_vm_time_now", &module);
        }
        vstack.push_back(B.CreateCall(fnTimeNow, {vmArg}));
        break;
    }

    // Math intrinsics
    case OpCode::MATH_SIN: {
        llvm::Value* arg = vstack.back(); vstack.pop_back();
        vstack.push_back(B.CreateUnaryIntrinsic(llvm::Intrinsic::sin, arg));
        break;
    }
    case OpCode::MATH_COS: {
        llvm::Value* arg = vstack.back(); vstack.pop_back();
        vstack.push_back(B.CreateUnaryIntrinsic(llvm::Intrinsic::cos, arg));
        break;
    }
    case OpCode::MATH_TAN: {
        llvm::Value* arg = vstack.back(); vstack.pop_back();
        vstack.push_back(B.CreateUnaryIntrinsic(llvm::Intrinsic::tan, arg));
        break;
    }
    case OpCode::MATH_ASIN: {
        llvm::Value* arg = vstack.back(); vstack.pop_back();
        vstack.push_back(B.CreateUnaryIntrinsic(llvm::Intrinsic::asin, arg));
        break;
    }
    case OpCode::MATH_ACOS: {
        llvm::Value* arg = vstack.back(); vstack.pop_back();
        vstack.push_back(B.CreateUnaryIntrinsic(llvm::Intrinsic::acos, arg));
        break;
    }
    case OpCode::MATH_ATAN: {
        llvm::Value* arg = vstack.back(); vstack.pop_back();
        vstack.push_back(B.CreateUnaryIntrinsic(llvm::Intrinsic::atan, arg));
        break;
    }
    case OpCode::MATH_ATAN2: {
        llvm::Value* y = vstack.back(); vstack.pop_back();
        llvm::Value* x = vstack.back(); vstack.pop_back();
        vstack.push_back(B.CreateBinaryIntrinsic(llvm::Intrinsic::atan2, x, y));
        break;
    }
    case OpCode::MATH_SINH: {
        llvm::Value* arg = vstack.back(); vstack.pop_back();
        vstack.push_back(B.CreateUnaryIntrinsic(llvm::Intrinsic::sinh, arg));
        break;
    }
    case OpCode::MATH_COSH: {
        llvm::Value* arg = vstack.back(); vstack.pop_back();
        vstack.push_back(B.CreateUnaryIntrinsic(llvm::Intrinsic::cosh, arg));
        break;
    }
    case OpCode::MATH_TANH: {
        llvm::Value* arg = vstack.back(); vstack.pop_back();
        vstack.push_back(B.CreateUnaryIntrinsic(llvm::Intrinsic::tanh, arg));
        break;
    }
    case OpCode::MATH_SQRT: {
        llvm::Value* arg = vstack.back(); vstack.pop_back();
        vstack.push_back(B.CreateIntrinsic(llvm::Intrinsic::sqrt, {f64}, {arg}));
        break;
    }
    case OpCode::MATH_LOG: {
        llvm::Value* arg = vstack.back(); vstack.pop_back();
        vstack.push_back(B.CreateIntrinsic(llvm::Intrinsic::log, {f64}, {arg}));
        break;
    }
    case OpCode::MATH_LOG2: {
        llvm::Value* arg = vstack.back(); vstack.pop_back();
        vstack.push_back(B.CreateIntrinsic(llvm::Intrinsic::log2, {f64}, {arg}));
        break;
    }
    case OpCode::MATH_LOG10: {
        llvm::Value* arg = vstack.back(); vstack.pop_back();
        vstack.push_back(B.CreateIntrinsic(llvm::Intrinsic::log10, {f64}, {arg}));
        break;
    }
    case OpCode::MATH_EXP: {
        llvm::Value* arg = vstack.back(); vstack.pop_back();
        vstack.push_back(B.CreateIntrinsic(llvm::Intrinsic::exp, {f64}, {arg}));
        break;
    }
    case OpCode::MATH_CEIL: {
        llvm::Value* arg = vstack.back(); vstack.pop_back();
        llvm::Function* fnCeil = module.getFunction("ceil");
        if (!fnCeil) {
            fnCeil = llvm::Function::Create(
                llvm::FunctionType::get(f64, {f64}, false),
                llvm::Function::ExternalLinkage, "ceil", &module);
        }
        vstack.push_back(B.CreateCall(fnCeil, {arg}));
        break;
    }
    case OpCode::MATH_FLOOR: {
        llvm::Value* arg = vstack.back(); vstack.pop_back();
        llvm::Function* fnFloor = module.getFunction("floor");
        if (!fnFloor) {
            fnFloor = llvm::Function::Create(
                llvm::FunctionType::get(f64, {f64}, false),
                llvm::Function::ExternalLinkage, "floor", &module);
        }
        vstack.push_back(B.CreateCall(fnFloor, {arg}));
        break;
    }
    case OpCode::MATH_ROUND: {
        llvm::Value* arg = vstack.back(); vstack.pop_back();
        llvm::Function* fnRound = module.getFunction("llvm.round.f64");
        if (!fnRound) {
            fnRound = llvm::Function::Create(
                llvm::FunctionType::get(f64, {f64}, false),
                llvm::Function::ExternalLinkage, "llvm.round.f64", &module);
        }
        vstack.push_back(B.CreateCall(fnRound, {arg}));
        break;
    }
    case OpCode::MATH_ABS: {
        llvm::Value* arg = vstack.back(); vstack.pop_back();
        vstack.push_back(B.CreateUnaryIntrinsic(llvm::Intrinsic::fabs, arg));
        break;
    }

    // Concurrency primitives - threads, coroutines, channels
    case OpCode::THREAD_SPAWN: {
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
    case OpCode::THREAD_JOIN: {
        llvm::Value* thread = vstack.back(); vstack.pop_back();
        llvm::Function* fnJoin = module.getFunction("havel_vm_thread_join");
        if (!fnJoin) {
            fnJoin = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_thread_join", &module);
        }
        vstack.push_back(B.CreateCall(fnJoin, {vmArg, thread}));
        break;
    }
    case OpCode::THREAD_SEND: {
        llvm::Value* msg = vstack.back(); vstack.pop_back();
        llvm::Value* thread = vstack.back(); vstack.pop_back();
        llvm::Function* fnSend = module.getFunction("havel_vm_thread_send");
        if (!fnSend) {
            fnSend = llvm::Function::Create(
                llvm::FunctionType::get(voidT, {i8p, i64, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_thread_send", &module);
        }
        B.CreateCall(fnSend, {vmArg, thread, msg});
        vstack.push_back(makeNull());
        break;
    }
    case OpCode::THREAD_RECEIVE: {
        llvm::Value* thread = vstack.back(); vstack.pop_back();
        llvm::Function* fnRecv = module.getFunction("havel_vm_thread_recv");
        if (!fnRecv) {
            fnRecv = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_thread_recv", &module);
        }
        vstack.push_back(B.CreateCall(fnRecv, {vmArg, thread}));
        break;
    }
    case OpCode::INTERVAL_START: {
        llvm::Value* callback = vstack.back(); vstack.pop_back();
        llvm::Value* duration = vstack.back(); vstack.pop_back();
        llvm::Function* fnStart = module.getFunction("havel_vm_interval_start");
        if (!fnStart) {
            fnStart = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_interval_start", &module);
        }
        vstack.push_back(B.CreateCall(fnStart, {vmArg, duration, callback}));
        break;
    }
    case OpCode::INTERVAL_STOP: {
        llvm::Value* interval = vstack.back(); vstack.pop_back();
        llvm::Function* fnStop = module.getFunction("havel_vm_interval_stop");
        if (!fnStop) {
            fnStop = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_interval_stop", &module);
        }
        vstack.push_back(B.CreateCall(fnStop, {vmArg, interval}));
        break;
    }
    case OpCode::TIMEOUT_START: {
        llvm::Value* callback = vstack.back(); vstack.pop_back();
        llvm::Value* delay = vstack.back(); vstack.pop_back();
        llvm::Function* fnStart = module.getFunction("havel_vm_timeout_start");
        if (!fnStart) {
            fnStart = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_timeout_start", &module);
        }
        vstack.push_back(B.CreateCall(fnStart, {vmArg, delay, callback}));
        break;
    }
    case OpCode::TIMEOUT_CANCEL: {
        llvm::Value* timeout = vstack.back(); vstack.pop_back();
        llvm::Function* fnCancel = module.getFunction("havel_vm_timeout_cancel");
        if (!fnCancel) {
            fnCancel = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_timeout_cancel", &module);
        }
        vstack.push_back(B.CreateCall(fnCancel, {vmArg, timeout}));
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
    case OpCode::CHANNEL_RECEIVE: {
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
    case OpCode::CHANNEL_CLOSE: {
        llvm::Value* chan = vstack.back(); vstack.pop_back();
        llvm::Function* fnClose = module.getFunction("havel_vm_channel_close");
        if (!fnClose) {
            fnClose = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64}, false),
                llvm::Function::ExternalLinkage, "havel_vm_channel_close", &module);
        }
        vstack.push_back(B.CreateCall(fnClose, {vmArg, chan}));
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
    // AWAIT opcode doesn't exist - awaiting is handled by interpreter

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

    // Bitwise operations
    case OpCode::BIT_AND: {
        llvm::Function* fn = module.getFunction("havel_vm_bit_and");
        if (!fn) fn = llvm::Function::Create(
            llvm::FunctionType::get(i64, {i64, i64}, false),
            llvm::Function::ExternalLinkage, "havel_vm_bit_and", &module);
        llvm::Value* r = vstack.back(); vstack.pop_back();
        llvm::Value* l = vstack.back(); vstack.pop_back();
        vstack.push_back(B.CreateCall(fn, {l, r}));
        break;
    }
    case OpCode::BIT_OR: {
        llvm::Function* fn = module.getFunction("havel_vm_bit_or");
        if (!fn) fn = llvm::Function::Create(
            llvm::FunctionType::get(i64, {i64, i64}, false),
            llvm::Function::ExternalLinkage, "havel_vm_bit_or", &module);
        llvm::Value* r = vstack.back(); vstack.pop_back();
        llvm::Value* l = vstack.back(); vstack.pop_back();
        vstack.push_back(B.CreateCall(fn, {l, r}));
        break;
    }
    case OpCode::BIT_XOR: {
        llvm::Function* fn = module.getFunction("havel_vm_bit_xor");
        if (!fn) fn = llvm::Function::Create(
            llvm::FunctionType::get(i64, {i64, i64}, false),
            llvm::Function::ExternalLinkage, "havel_vm_bit_xor", &module);
        llvm::Value* r = vstack.back(); vstack.pop_back();
        llvm::Value* l = vstack.back(); vstack.pop_back();
        vstack.push_back(B.CreateCall(fn, {l, r}));
        break;
    }
    case OpCode::BIT_NOT: {
        llvm::Function* fn = module.getFunction("havel_vm_bit_not");
        if (!fn) fn = llvm::Function::Create(
            llvm::FunctionType::get(i64, {i64}, false),
            llvm::Function::ExternalLinkage, "havel_vm_bit_not", &module);
        llvm::Value* v = vstack.back(); vstack.pop_back();
        vstack.push_back(B.CreateCall(fn, {v}));
        break;
    }
    case OpCode::BIT_LSH: {
        llvm::Function* fn = module.getFunction("havel_vm_bit_lsh");
        if (!fn) fn = llvm::Function::Create(
            llvm::FunctionType::get(i64, {i64, i64}, false),
            llvm::Function::ExternalLinkage, "havel_vm_bit_lsh", &module);
        llvm::Value* r = vstack.back(); vstack.pop_back();
        llvm::Value* l = vstack.back(); vstack.pop_back();
        vstack.push_back(B.CreateCall(fn, {l, r}));
        break;
    }
    case OpCode::BIT_RSH: {
        llvm::Function* fn = module.getFunction("havel_vm_bit_rsh");
        if (!fn) fn = llvm::Function::Create(
            llvm::FunctionType::get(i64, {i64, i64}, false),
            llvm::Function::ExternalLinkage, "havel_vm_bit_rsh", &module);
        llvm::Value* r = vstack.back(); vstack.pop_back();
        llvm::Value* l = vstack.back(); vstack.pop_back();
        vstack.push_back(B.CreateCall(fn, {l, r}));
        break;
    }

    // Fiber operations in JIT/AOT context
    case OpCode::FIBER_SLEEP: {
        llvm::Function* fn = module.getFunction("havel_vm_fiber_sleep");
        if (!fn) fn = llvm::Function::Create(
            llvm::FunctionType::get(i64, {i8p, i64}, false),
            llvm::Function::ExternalLinkage, "havel_vm_fiber_sleep", &module);
        llvm::Value* ms = vstack.back(); vstack.pop_back();
        vstack.push_back(B.CreateCall(fn, {vmArg, ms}));
        break;
    }
    case OpCode::FIBER_AWAIT: {
        llvm::Function* fn = module.getFunction("havel_vm_await");
        if (!fn) fn = llvm::Function::Create(
            llvm::FunctionType::get(i64, {i8p, i64}, false),
            llvm::Function::ExternalLinkage, "havel_vm_await", &module);
        llvm::Value* val = vstack.back(); vstack.pop_back();
        vstack.push_back(B.CreateCall(fn, {vmArg, val}));
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
    case OpCode::NOP:
        break;

    case OpCode::CALL_METHOD: {
        if (instr.operands.size() != 2 || !instr.operands[0].isStringValId() || !instr.operands[1].isInt()) {
            vstack.push_back(makeNull());
            break;
        }

        uint32_t methodNameId = instr.operands[0].asStringValId();
        uint32_t argCount = static_cast<uint32_t>(instr.operands[1].asInt());

        llvm::Value* argsArray = B.CreateAlloca(llvm::ArrayType::get(i64, argCount), nullptr, "method_args");
        for (uint32_t i = 0; i < argCount; ++i) {
            llvm::Value* arg = vstack.back(); vstack.pop_back();
            B.CreateStore(arg, B.CreateInBoundsGEP(llvm::ArrayType::get(i64, argCount), argsArray,
                {llvm::ConstantInt::get(i32, 0), llvm::ConstantInt::get(i32, argCount - 1 - i)}));
        }
        llvm::Value* receiver = vstack.back(); vstack.pop_back();

        llvm::Function* fnMethod = module.getFunction("havel_vm_call_method");
        if (!fnMethod) {
            fnMethod = llvm::Function::Create(
                llvm::FunctionType::get(i64, {i8p, i64, i32, i64p, i32}, false),
                llvm::Function::ExternalLinkage, "havel_vm_call_method", &module);
        }

        vstack.push_back(B.CreateCall(fnMethod, {
            vmArg,
            receiver,
            llvm::ConstantInt::get(i32, methodNameId),
            B.CreateInBoundsGEP(llvm::ArrayType::get(i64, argCount), argsArray,
                {llvm::ConstantInt::get(i32, 0), llvm::ConstantInt::get(i32, 0)}),
            llvm::ConstantInt::get(i32, argCount)
        }));
        break;
    }

    default: break;
    }

    // Default fallthrough to next instruction block if the opcode didn't terminate.
    // We terminate the current insert block (which might be a merge block from a specialized op).
    if (B.GetInsertBlock()->getTerminator() == nullptr) {
        B.CreateBr(basicBlocks[ip + 1]);
    }

    // Capture outgoing stack state for each successor. This feeds entry PHIs.
    if (auto *term = B.GetInsertBlock()->getTerminator()) {
        if (auto *br = llvm::dyn_cast<llvm::BranchInst>(term)) {
            std::unordered_set<size_t> seenSuccs;
            for (unsigned s = 0; s < br->getNumSuccessors(); ++s) {
                llvm::BasicBlock* succ = br->getSuccessor(s);
                auto it = blockToIp.find(succ);
                if (it == blockToIp.end()) continue;
                if (!seenSuccs.insert(it->second).second) continue;
                addIncomingState(it->second, br->getParent(), vstack);
            }
        }
    }
}

// Function epilogue for paths that reach the synthetic exit block.
B.SetInsertPoint(basicBlocks[func.instructions.size()]);
if (B.GetInsertBlock()->getTerminator() == nullptr) {
    llvm::Function *fn_unreg = module.getFunction("havel_gc_unregister_roots");
    if (!fn_unreg) {
        fn_unreg = llvm::Function::Create(
            llvm::FunctionType::get(voidT, {llvm::PointerType::get(ctx, 0)}, false),
            llvm::Function::ExternalLinkage, "havel_gc_unregister_roots", &module);
    }
    B.CreateCall(fn_unreg, {frame});
    B.CreateRet(makeNull());
}
}


}  // namespace havel::compiler
