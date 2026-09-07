#pragma once

// ===== JIT value-boxing constants (shared by lowering and bridges) =====
//
// The NaN-boxed Value word layout constants used by both the LLVM IR
// lowering (BytecodeOrcJIT.cpp) and the runtime bridges
// (JitRuntimeBridges.cpp). They must stay bit-identical to
// src/havel-lang/core/Value.hpp (QNAN | tag<<48 | payload).

#include <cstdint>

namespace havel::compiler {

static constexpr uint64_t QNAN = 0x7FF8000000000000ULL;
static constexpr uint64_t TAG_MASK = 0x0007000000000000ULL;
static constexpr uint64_t PAYLOAD_MASK = 0x0000FFFFFFFFFFFFULL;
static constexpr uint64_t EXT_PAYLOAD_MASK = 0x000007FFFFFFFFFFULL;  // 43 bits
static constexpr uint64_t EXTENDED_TAG_MASK = 0x0000F80000000000ULL;  // bits 43-47

static constexpr uint64_t INT_TAG = 0x1;
static constexpr uint64_t EXT_TAG = 0x7;

static constexpr uint64_t INT_TAG_BITS = QNAN | (INT_TAG << 48);  // 0x7FF9...
static constexpr uint64_t ARRAY_TAG_BITS =
    QNAN | (EXT_TAG << 48) | (0x1ULL << 43);  // 5-bit tag shift

}  // namespace havel::compiler
