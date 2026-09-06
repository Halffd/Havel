#pragma once

// ===== Cranelift backend adapter (TODO.md #24 / #25) =====
//
// Presents the Rust Cranelift prototype (cranelift-backend/) as a C++
// CompilerBackend. Available only when built with ENABLE_CRANELIFT
// (HAVEL_ENABLE_CRANELIFT defined): the adapter links the Rust staticlib
// and drives it through the hclb_* C ABI.
//
// Supported lowering subset (see the Rust lib for the full contract):
//   LOAD_CONST / LOAD_VAR / STORE_VAR
//   ADD / SUB / MUL / EQ / NEQ / LT / LTE / GT / GTE
//   JUMP / JUMP_IF_FALSE / CALL / RETURN
// Everything else is refused by can_lower() so a function is never
// partially compiled: a backend that declines leaves the function to the
// interpreter (the VM's tiering only marks jit_compiled on success).
//
// Value words cross the boundary as raw NaN-boxed bits, identical on both
// sides (asserted by the cranelift_proto_driver test).

#ifdef HAVEL_ENABLE_CRANELIFT

#include "Backend.hpp"
#include "BytecodeIR.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace havel::compiler {

// hclb_* C ABI implemented by the Rust staticlib.
extern "C" {
void* hclb_create(void);
void hclb_destroy(void* handle);
bool hclb_compile(void* handle, const char* name, const uint32_t* code,
                  uint32_t code_len, const uint64_t* constants,
                  uint32_t constants_len, uint32_t arg_count);
bool hclb_is_compiled(void* handle, const char* name);
bool hclb_execute(void* handle, void* vm, const char* name,
                  const uint64_t* args, uint32_t arg_count, uint64_t* out);
}

class CraneliftBackend final : public CompilerBackend {
public:
  CraneliftBackend() : handle_(hclb_create()) {}
  ~CraneliftBackend() override {
    if (handle_) hclb_destroy(handle_);
  }

  CraneliftBackend(const CraneliftBackend&) = delete;
  CraneliftBackend& operator=(const CraneliftBackend&) = delete;

  bool available() const { return handle_ != nullptr; }

  // Whether every instruction of `func` is inside the lowering subset and
  // every jump stays inside the function. A function with any unsupported
  // opcode is left to the interpreter.
  static bool can_lower(const BytecodeFunction& func) {
    const size_t n = func.instructions.size();
    for (const auto& inst : func.instructions) {
      switch (inst.opcode) {
        case OpCode::LOAD_CONST:
        case OpCode::LOAD_VAR:
        case OpCode::STORE_VAR:
        case OpCode::ADD:
        case OpCode::SUB:
        case OpCode::MUL:
        case OpCode::EQ:
        case OpCode::NEQ:
        case OpCode::LT:
        case OpCode::LTE:
        case OpCode::GT:
        case OpCode::GTE:
        case OpCode::RETURN:
          break;
        case OpCode::JUMP:
        case OpCode::JUMP_IF_FALSE: {
          if (inst.operands.empty() || !inst.operands[0].isInt()) return false;
          const int64_t t = inst.operands[0].asInt();
          if (t < 0 || static_cast<size_t>(t) >= n) return false;
          break;
        }
        case OpCode::CALL:
          // CALL's operand is the argument count; the runtime bridge
          // (havel_vm_call) resolves the callee.
          break;
        default:
          return false;
      }
    }
    return n > 0;
  }

  bool compile(const BytecodeFunction& func) override {
    if (!handle_ || !can_lower(func)) return false;
    const auto code = lower(func);
    std::vector<uint64_t> constants;
    constants.reserve(func.constants.size());
    for (const auto& c : func.constants) constants.push_back(c.rawBits());
    return hclb_compile(handle_, func.name.c_str(), code.data(),
                        static_cast<uint32_t>(code.size()),
                        constants.empty() ? nullptr : constants.data(),
                        static_cast<uint32_t>(constants.size()),
                        func.param_count);
  }

  bool is_compiled(const std::string& func_name) const override {
    return handle_ && hclb_is_compiled(handle_, func_name.c_str());
  }

  bool execute(VM* vm, const std::string& func_name,
               const std::vector<Value>& args, Value* out) override {
    if (!handle_) return false;
    std::vector<uint64_t> raw;
    raw.reserve(args.size());
    for (const auto& a : args) raw.push_back(a.rawBits());
    uint64_t result_bits = 0;
    if (!hclb_execute(handle_, reinterpret_cast<void*>(vm),
                      func_name.c_str(),
                      raw.empty() ? nullptr : raw.data(),
                      static_cast<uint32_t>(raw.size()), &result_bits)) {
      return false;
    }
    if (out) *out = Value::fromRawBits(result_bits);
    return true;
  }

  const char* name() const override { return "cranelift"; }

private:
  // Translate the C++ instruction stream into the Rust lowering's flat
  // (opcode, operand) u32 pairs. Opcodes map 1:1 into the subset namespace;
  // jump operands (absolute instruction indices) pass through unchanged.
  static std::vector<uint32_t> lower(const BytecodeFunction& func) {
    std::vector<uint32_t> out;
    out.reserve(func.instructions.size() * 2);
    for (const auto& inst : func.instructions) {
      uint32_t op = 0;
      uint32_t operand = 0;
      switch (inst.opcode) {
        case OpCode::LOAD_CONST: op = 0; break;  // hclb OP_LOAD_CONST
        case OpCode::LOAD_VAR: op = 1; break;
        case OpCode::STORE_VAR: op = 2; break;
        case OpCode::ADD: op = 3; break;
        case OpCode::SUB: op = 4; break;
        case OpCode::MUL: op = 5; break;
        case OpCode::LT: op = 6; break;
        case OpCode::RETURN: op = 7; break;
        case OpCode::JUMP: op = 8; break;
        case OpCode::JUMP_IF_FALSE: op = 9; break;
        case OpCode::EQ: op = 10; break;
        case OpCode::NEQ: op = 11; break;
        case OpCode::LTE: op = 12; break;
        case OpCode::GT: op = 13; break;
        case OpCode::GTE: op = 14; break;
        case OpCode::CALL: op = 15; break;
        default: break;  // can_lower() already refused anything else
      }
      if (!inst.operands.empty() && inst.operands[0].isInt()) {
        operand = static_cast<uint32_t>(inst.operands[0].asInt());
      }
      out.push_back(op);
      out.push_back(operand);
    }
    return out;
  }

  void* handle_ = nullptr;
};

}  // namespace havel::compiler

#endif  // HAVEL_ENABLE_CRANELIFT
