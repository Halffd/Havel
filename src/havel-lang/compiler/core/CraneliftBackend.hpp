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
//   JUMP / JUMP_IF_FALSE / JUMP_IF_TRUE / CALL / RETURN
//   POP / DUP / SWAP / PUSH_NULL / IS_NULL / NOT / LENGTH
//   STRING_LEN / STRING_UPPER / STRING_LOWER / STRING_TRIM /
//   STRING_PROMOTE / STRING_CONCAT
//   BIT_AND / BIT_OR / BIT_XOR / BIT_NOT / BIT_LSH / BIT_RSH
//   LOAD_GLOBAL / STORE_GLOBAL
//   LOAD_UPVALUE / STORE_UPVALUE (closure captures via the runtime
//   bridges; the JIT execute path runs closures with a frame context)
//   OBJECT_GET / OBJECT_SET (member access via the raw bridges the ORC
//   lowering uses; GET goes through the inline-cache variant)
//   ITER_NEW / ITER_NEXT
//   ARRAY_GET / ARRAY_SET / ARRAY_LEN / ARRAY_PUSH (GET via the
//   collection inline-cache bridge)
//   OBJECT_NEW / OBJECT_NEW_UNSORTED / ARRAY_NEW / SET_NEW / RANGE_NEW
//   (constructors)
//   JUMP_IF_NULL (inline null-word compare)
// Everything else is refused by can_lower() so a function is never
// partially compiled: a backend that declines leaves the function to the
// interpreter (the VM's tiering only marks jit_compiled on success).
//
// Value words cross the boundary as raw NaN-boxed bits, identical on both
// sides (asserted by the cranelift_proto_driver test).

#ifdef HAVEL_ENABLE_CRANELIFT

#include "Backend.hpp"
#include "BytecodeIR.hpp"
#include "../runtime/RuntimeABI.hpp"

#include "../../../utils/Logger.hpp"

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace havel::compiler {

// hclb_* C ABI implemented by the Rust staticlib.
extern "C" {
void* hclb_create(void);
void* hclb_create_with_symbols(const char** names, const void** addrs,
                               uint32_t count);
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
  // The embedder passes the Runtime ABI addresses explicitly: release
  // builds use -fvisibility=hidden (no .dynsym), so dlsym can never resolve
  // the runtime from the main executable. Only the bridge surface the
  // lowering actually calls is registered, so consumers that link this
  // header (the proto driver) do not need the whole runtime on their link
  // line.
  CraneliftBackend() {
    std::vector<const char*> names;
    std::vector<const void*> addrs;
    auto add = [&](const char* n, const void* a) {
      names.push_back(n);
      addrs.push_back(a);
    };
    add("havel_vm_add", reinterpret_cast<const void*>(&havel_vm_add));
    add("havel_vm_sub", reinterpret_cast<const void*>(&havel_vm_sub));
    add("havel_vm_mul", reinterpret_cast<const void*>(&havel_vm_mul));
    add("havel_vm_eq", reinterpret_cast<const void*>(&havel_vm_eq));
    add("havel_vm_neq", reinterpret_cast<const void*>(&havel_vm_neq));
    add("havel_vm_lt", reinterpret_cast<const void*>(&havel_vm_lt));
    add("havel_vm_lte", reinterpret_cast<const void*>(&havel_vm_lte));
    add("havel_vm_gt", reinterpret_cast<const void*>(&havel_vm_gt));
    add("havel_vm_gte", reinterpret_cast<const void*>(&havel_vm_gte));
    add("havel_vm_is_truthy",
        reinterpret_cast<const void*>(&havel_vm_is_truthy));
    add("havel_vm_call", reinterpret_cast<const void*>(&havel_vm_call));
    add("havel_vm_global_get",
        reinterpret_cast<const void*>(&havel_vm_global_get));
    add("havel_vm_global_set",
        reinterpret_cast<const void*>(&havel_vm_global_set));
    // VM-aware equality: EQ/NEQ may compare string content via the heap.
    add("havel_vm_eq_vm",
        reinterpret_cast<const void*>(&havel_vm_eq_vm));
    add("havel_vm_neq_vm",
        reinterpret_cast<const void*>(&havel_vm_neq_vm));
    add("havel_vm_string_concat",
        reinterpret_cast<const void*>(&havel_vm_string_concat));
    add("havel_vm_length", reinterpret_cast<const void*>(&havel_vm_length));
    add("havel_vm_string_len",
        reinterpret_cast<const void*>(&havel_vm_string_len));
    add("havel_vm_string_upper",
        reinterpret_cast<const void*>(&havel_vm_string_upper));
    add("havel_vm_string_lower",
        reinterpret_cast<const void*>(&havel_vm_string_lower));
    add("havel_vm_string_trim",
        reinterpret_cast<const void*>(&havel_vm_string_trim));
    add("havel_vm_string_promote",
        reinterpret_cast<const void*>(&havel_vm_string_promote));
    add("havel_vm_object_new",
        reinterpret_cast<const void*>(&havel_vm_object_new));
    add("havel_vm_object_new_unsorted",
        reinterpret_cast<const void*>(&havel_vm_object_new_unsorted));
    add("havel_vm_array_new",
        reinterpret_cast<const void*>(&havel_vm_array_new));
    add("havel_vm_set_new",
        reinterpret_cast<const void*>(&havel_vm_set_new));
    add("havel_vm_range_new",
        reinterpret_cast<const void*>(&havel_vm_range_new));
    add("havel_vm_set_set",
        reinterpret_cast<const void*>(&havel_vm_set_set));
    add("havel_vm_not", reinterpret_cast<const void*>(&havel_vm_not));
    add("havel_vm_bit_and",
        reinterpret_cast<const void*>(&havel_vm_bit_and));
    add("havel_vm_bit_or", reinterpret_cast<const void*>(&havel_vm_bit_or));
    add("havel_vm_bit_xor",
        reinterpret_cast<const void*>(&havel_vm_bit_xor));
    add("havel_vm_bit_not",
        reinterpret_cast<const void*>(&havel_vm_bit_not));
    add("havel_vm_bit_lsh",
        reinterpret_cast<const void*>(&havel_vm_bit_lsh));
    add("havel_vm_bit_rsh",
        reinterpret_cast<const void*>(&havel_vm_bit_rsh));
    add("havel_vm_backedge",
        reinterpret_cast<const void*>(&havel_vm_backedge));
    add("havel_vm_upvalue_get",
        reinterpret_cast<const void*>(&havel_vm_upvalue_get));
    add("havel_vm_upvalue_set",
        reinterpret_cast<const void*>(&havel_vm_upvalue_set));
    add("havel_vm_object_get_raw_ic",
        reinterpret_cast<const void*>(&havel_vm_object_get_raw_ic));
    add("havel_vm_object_set_raw",
        reinterpret_cast<const void*>(&havel_vm_object_set_raw));
    add("havel_vm_iter_new",
        reinterpret_cast<const void*>(&havel_vm_iter_new));
    add("havel_vm_iter_next",
        reinterpret_cast<const void*>(&havel_vm_iter_next));
    add("havel_vm_collection_get_raw_ic",
        reinterpret_cast<const void*>(&havel_vm_collection_get_raw_ic));
    add("havel_vm_array_set",
        reinterpret_cast<const void*>(&havel_vm_array_set));
    add("havel_vm_array_len",
        reinterpret_cast<const void*>(&havel_vm_array_len));
    add("havel_vm_array_push",
        reinterpret_cast<const void*>(&havel_vm_array_push));
    add("havel_vm_call_method",
        reinterpret_cast<const void*>(&havel_vm_call_method));
    handle_ = hclb_create_with_symbols(
        names.data(), addrs.data(), static_cast<uint32_t>(names.size()));
  }
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
        case OpCode::POP:
        case OpCode::DUP:
        case OpCode::SWAP:
        case OpCode::PUSH_NULL:
        case OpCode::LOAD_GLOBAL:
        case OpCode::STORE_GLOBAL:
        case OpCode::LOAD_UPVALUE:
        case OpCode::STORE_UPVALUE:
        case OpCode::OBJECT_GET:
        case OpCode::OBJECT_SET:
        case OpCode::ITER_NEW:
        case OpCode::ITER_NEXT:
        case OpCode::ARRAY_GET:
        case OpCode::ARRAY_SET:
        case OpCode::ARRAY_LEN:
        case OpCode::ARRAY_PUSH:
        case OpCode::OBJECT_NEW:
        case OpCode::OBJECT_NEW_UNSORTED:
        case OpCode::ARRAY_NEW:
        case OpCode::SET_NEW:
        case OpCode::RANGE_NEW:
        case OpCode::SET_SET:
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
        case OpCode::NOT:
        case OpCode::IS_NULL:
        case OpCode::LENGTH:
        case OpCode::STRING_LEN:
        case OpCode::STRING_UPPER:
        case OpCode::STRING_LOWER:
        case OpCode::STRING_TRIM:
        case OpCode::STRING_PROMOTE:
        case OpCode::STRING_CONCAT:
        case OpCode::BIT_AND:
        case OpCode::BIT_OR:
        case OpCode::BIT_XOR:
        case OpCode::BIT_NOT:
        case OpCode::BIT_LSH:
        case OpCode::BIT_RSH:
          break;
        case OpCode::JUMP:
        case OpCode::JUMP_IF_FALSE:
        case OpCode::JUMP_IF_TRUE:
        case OpCode::JUMP_IF_NULL: {
          if (inst.operands.empty() || !inst.operands[0].isInt()) return false;
          const int64_t t = inst.operands[0].asInt();
          if (t < 0 || static_cast<size_t>(t) >= n) return false;
          break;
        }
        case OpCode::CALL:
          // CALL's operand is the argument count; the runtime bridge
          // (havel_vm_call) resolves the callee.
          break;
        case OpCode::CALL_METHOD: {
          // Two operands: chunk-local method-name StringValId + arg count.
          // The flat stream carries the second operand as an
          // OP_EXTENDED_ARG pseudo-pair after the instruction.
          if (inst.operands.size() != 2 || !inst.operands[0].isStringValId() ||
              !inst.operands[1].isInt()) {
            return false;
          }
          break;
        }
        default: {
          // Diagnostic: which opcode kept a function out of the fast
          // tier. Gated by HAVEL_CRANELIFT_TRACE; one line per refusal.
          static const bool trace_refusals =
              std::getenv("HAVEL_CRANELIFT_TRACE") != nullptr;
          if (trace_refusals) {
            ::havel::debug("[cranelift] can_lower refused {} (opcode {})",
                           func.name, opcodeName(inst.opcode));
          }
          return false;
        }
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
  // jump operands are instruction indices remapped to EMITTED pair
  // positions (CALL_METHOD consumes a following OP_EXTENDED_ARG data pair,
  // so pair indices diverge from source instruction indices).
  static std::vector<uint32_t> lower(const BytecodeFunction& func) {
    std::vector<uint32_t> out;
    out.reserve(func.instructions.size() * 2);
    // Emitted pair index per source instruction (jump-operand remap).
    std::vector<uint32_t> pair_of(func.instructions.size(), 0);
    {
      uint32_t pair_cursor = 0;
      for (size_t i = 0; i < func.instructions.size(); ++i) {
        pair_of[i] = pair_cursor;
        pair_cursor +=
            func.instructions[i].opcode == OpCode::CALL_METHOD ? 2 : 1;
      }
    }
    for (const auto& inst : func.instructions) {
      uint32_t op = 0;
      uint32_t operand = 0;
      switch (inst.opcode) {
        case OpCode::LOAD_CONST: op = 0; break;  // hclb OP_LOAD_CONST
        case OpCode::LOAD_VAR: op = 1; break;
        case OpCode::STORE_VAR: op = 2; break;
        case OpCode::POP: op = 16; break;
        case OpCode::DUP: op = 17; break;
        case OpCode::PUSH_NULL: op = 18; break;
        case OpCode::LOAD_GLOBAL: op = 19; break;
        case OpCode::STORE_GLOBAL: op = 20; break;
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
        case OpCode::NOT: op = 21; break;
        case OpCode::IS_NULL: op = 22; break;
        case OpCode::LENGTH: op = 23; break;
        case OpCode::STRING_LEN: op = 24; break;
        case OpCode::STRING_UPPER: op = 25; break;
        case OpCode::STRING_LOWER: op = 26; break;
        case OpCode::STRING_TRIM: op = 27; break;
        case OpCode::STRING_PROMOTE: op = 39; break;
        case OpCode::STRING_CONCAT: op = 28; break;
        case OpCode::BIT_AND: op = 29; break;
        case OpCode::BIT_OR: op = 30; break;
        case OpCode::BIT_XOR: op = 31; break;
        case OpCode::BIT_NOT: op = 32; break;
        case OpCode::BIT_LSH: op = 33; break;
        case OpCode::BIT_RSH: op = 34; break;
        case OpCode::SWAP: op = 35; break;
        case OpCode::JUMP_IF_TRUE: op = 36; break;
        case OpCode::LOAD_UPVALUE: op = 37; break;
        case OpCode::STORE_UPVALUE: op = 38; break;
        case OpCode::OBJECT_GET: op = 40; break;
        case OpCode::OBJECT_SET: op = 41; break;
        case OpCode::ITER_NEW: op = 42; break;
        case OpCode::ITER_NEXT: op = 43; break;
        case OpCode::ARRAY_GET: op = 44; break;
        case OpCode::ARRAY_SET: op = 45; break;
        case OpCode::ARRAY_LEN: op = 46; break;
        case OpCode::ARRAY_PUSH: op = 47; break;
        case OpCode::JUMP_IF_NULL: op = 48; break;
        case OpCode::CALL_METHOD: op = 49; break;
        case OpCode::OBJECT_NEW: op = 51; break;
        case OpCode::OBJECT_NEW_UNSORTED: op = 52; break;
        case OpCode::ARRAY_NEW: op = 53; break;
        case OpCode::SET_NEW: op = 54; break;
        case OpCode::RANGE_NEW: op = 55; break;
        case OpCode::SET_SET: op = 56; break;
        default: break;  // can_lower() already refused anything else
      }
      if (!inst.operands.empty()) {
        // Global names travel as chunk-local StringValIds; the StringValId
        // payload packs (chunkId << 31 | stringIndex), so the low 31 bits
        // are the index and the operand resolves exactly like the
        // interpreter's LOAD_GLOBAL path (asStringValId masks them out).
        // Everything else in the subset carries integer operands.
        if (inst.operands[0].isStringValId()) {
          operand = inst.operands[0].asStringValId() & 0x7FFFFFFFu;
        } else if (inst.operands[0].isInt()) {
          operand = static_cast<uint32_t>(inst.operands[0].asInt());
        }
        // Jump operands are SOURCE instruction indices; remap them to the
        // emitted pair indices so CALL_METHOD's extra data pair cannot
        // skew a jump target.
        switch (inst.opcode) {
          case OpCode::JUMP:
          case OpCode::JUMP_IF_FALSE:
          case OpCode::JUMP_IF_TRUE:
          case OpCode::JUMP_IF_NULL: {
            const size_t target = static_cast<size_t>(inst.operands[0].asInt());
            if (target < pair_of.size()) {
              operand = pair_of[target];
            }
            break;
          }
          default:
            break;
        }
      }
      out.push_back(op);
      out.push_back(operand);
      // CALL_METHOD's second operand (arg count) travels as an
      // OP_EXTENDED_ARG pseudo-pair; the Rust lowering consumes it with
      // the instruction and skips it (never a jump target).
      if (inst.opcode == OpCode::CALL_METHOD) {
        const uint32_t argc =
            static_cast<uint32_t>(inst.operands[1].asInt());
        out.push_back(50 /* OP_EXTENDED_ARG */);
        out.push_back(argc);
      }
    }
    return out;
  }

  void* handle_ = nullptr;
};

}  // namespace havel::compiler

#endif  // HAVEL_ENABLE_CRANELIFT
