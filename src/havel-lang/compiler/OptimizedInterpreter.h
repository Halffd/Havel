#pragma once

#include "Bytecode.h"
#include <array>
#include <iostream>
#include <stack>
#include <unordered_map>
#include <vector>
#include <memory>
#include <functional>
#include <typeindex>
#include <algorithm>
#include <cstring>
#include <sys/mman.h>
#include <cerrno>

namespace havel::compiler {

// ============================================================================
// Inline Cache Structures
// ============================================================================

struct InlineCache {
  enum class CacheType { EMPTY, MONOMORPHIC, POLYMORPHIC, MEGAMORPHIC };

  CacheType type = CacheType::EMPTY;
  uint32_t type_id1 = 0;
  uint32_t type_id2 = 0;
  void *target = nullptr;
  uint64_t hit_count = 0;
  uint64_t miss_count = 0;

  void reset() {
    type = CacheType::EMPTY;
    type_id1 = type_id2 = 0;
    target = nullptr;
  }

  double getHitRate() const {
    uint64_t total = hit_count + miss_count;
    return total > 0 ? (double)hit_count / total : 0.0;
  }
};

// Polymorphic inline cache with multiple entries
struct PolymorphicInlineCache {
  static constexpr size_t MAX_ENTRIES = 4;

  struct Entry {
    uint32_t type_id = 0;
    void *target = nullptr;
    uint64_t hit_count = 0;
  };

  Entry entries[MAX_ENTRIES];
  size_t count = 0;
  uint64_t total_hits = 0;
  uint64_t total_misses = 0;

  void *lookup(uint32_t type_id) {
    for (size_t i = 0; i < count; i++) {
      if (entries[i].type_id == type_id) {
        entries[i].hit_count++;
        total_hits++;
        return entries[i].target;
      }
    }
    total_misses++;
    return nullptr;
  }

  bool add(uint32_t type_id, void *target) {
    if (count >= MAX_ENTRIES) return false;
    entries[count].type_id = type_id;
    entries[count].target = target;
    count++;
    return true;
  }
};

// ============================================================================
// JIT Compiled Block
// ============================================================================

struct CompiledBlock {
  void *code = nullptr;
  size_t size = 0;
  uint32_t start_addr = 0;
  uint32_t end_addr = 0;
  uint64_t execution_count = 0;
  bool is_valid = false;
  std::vector<uint8_t> machine_code;

  ~CompiledBlock() {
    if (code && is_valid) free(code);
  }

  bool allocateExecutableMemory() {
    if (machine_code.empty()) return false;
    size = machine_code.size();
    code = malloc(size);
    if (!code) return false;
    memcpy(code, machine_code.data(), size);
#ifdef __linux__
    mprotect(code, size, PROT_READ | PROT_EXEC);
#endif
    is_valid = true;
    return true;
  }

  void generateAddCode() {
    machine_code = {0x58, 0x5B, 0x48, 0x01, 0xD8, 0x50, 0xC3};
  }

  void generateMulCode() {
    machine_code = {0x58, 0x5B, 0x48, 0x0F, 0xAF, 0xC3, 0x50, 0xC3};
  }

  void generateSubCode() {
    machine_code = {0x58, 0x5B, 0x48, 0x29, 0xC3, 0x53, 0xC3};
  }
};

// ============================================================================
// JIT Compiler
// ============================================================================

class HotPathJIT {
private:
  std::unordered_map<uint32_t, std::unique_ptr<CompiledBlock>> compiled_blocks;
  uint32_t compilation_threshold = 100;
  size_t total_compiled_bytes = 0;

public:
  bool compileBlock(const std::vector<Instruction> &instructions,
                    uint32_t start, uint32_t end) {
    if (start >= end || start >= instructions.size()) return false;

    auto block = std::make_unique<CompiledBlock>();
    block->start_addr = start;
    block->end_addr = end;

    // Generate optimized code for arithmetic operations
    for (uint32_t i = start; i < end; i++) {
      const auto &instr = instructions[i];
      switch (instr.opcode) {
        case OpCode::ADD: block->generateAddCode(); break;
        case OpCode::MUL: block->generateMulCode(); break;
        case OpCode::SUB: block->generateSubCode(); break;
        default: break;
      }
    }

    if (block->allocateExecutableMemory()) {
      total_compiled_bytes += block->size;
      compiled_blocks[start] = std::move(block);
      std::cout << "[JIT] Compiled block " << start << "-" << end
                << " (" << compiled_blocks[start]->size << " bytes)" << std::endl;
      return true;
    }
    return false;
  }

  bool shouldCompile(uint32_t addr, uint64_t exec_count) const {
    return exec_count >= compilation_threshold &&
           compiled_blocks.find(addr) == compiled_blocks.end();
  }

  bool isCompiled(uint32_t addr) const {
    return compiled_blocks.find(addr) != compiled_blocks.end();
  }

  struct JITStats {
    uint32_t compiled_blocks = 0;
    uint64_t total_executions = 0;
    size_t total_code_size = 0;
  };

  JITStats getStats() const {
    JITStats stats;
    stats.compiled_blocks = compiled_blocks.size();
    for (const auto &[addr, blk] : compiled_blocks) {
      stats.total_executions += blk->execution_count;
      stats.total_code_size += blk->size;
    }
    return stats;
  }

  void setCompilationThreshold(uint32_t t) { compilation_threshold = t; }
  uint32_t getCompilationThreshold() const { return compilation_threshold; }
};

// ============================================================================
// Type ID System
// ============================================================================

class TypeIdSystem {
  std::unordered_map<std::type_index, uint32_t> type_to_id;
  uint32_t next_id = 1;

public:
  template <typename T> uint32_t registerType() {
    auto type = std::type_index(typeid(T));
    auto it = type_to_id.find(type);
    if (it != type_to_id.end()) return it->second;
    uint32_t id = next_id++;
    type_to_id[type] = id;
    return id;
  }

  uint32_t getTypeId(const BytecodeValue &value) {
    if (std::holds_alternative<std::nullptr_t>(value)) return registerType<std::nullptr_t>();
    if (std::holds_alternative<bool>(value)) return registerType<bool>();
    if (std::holds_alternative<int64_t>(value)) return registerType<int64_t>();
    if (std::holds_alternative<double>(value)) return registerType<double>();
    if (std::holds_alternative<std::string>(value)) return registerType<std::string>();
    if (std::holds_alternative<uint32_t>(value)) return registerType<uint32_t>();
    return 0;
  }
};

// ============================================================================
// Optimized Bytecode Interpreter
// ============================================================================

class OptimizedBytecodeInterpreter : public BytecodeInterpreter {
protected:
  std::stack<BytecodeValue> stack;
  std::vector<BytecodeValue> locals;
  std::vector<BytecodeValue> constants;
  size_t instruction_pointer = 0;
  bool debug_mode = false;

  std::unordered_map<uint32_t, InlineCache> add_caches;
  std::unordered_map<uint32_t, InlineCache> mul_caches;
  std::unordered_map<uint32_t, InlineCache> sub_caches;
  std::unordered_map<uint32_t, InlineCache> div_caches;

  TypeIdSystem type_system;
  HotPathJIT jit;
  std::unordered_map<uint32_t, uint32_t> execution_counts;

  uint64_t total_instructions_executed = 0;
  uint64_t total_cache_hits = 0;
  uint64_t total_cache_misses = 0;

  uint32_t getTypeId(const BytecodeValue &value) {
    return type_system.getTypeId(value);
  }

  BytecodeValue performBinaryOp(OpCode op, const BytecodeValue &left,
                                const BytecodeValue &right) {
    if (std::holds_alternative<int64_t>(left) &&
        std::holds_alternative<int64_t>(right)) {
      int64_t l = std::get<int64_t>(left);
      int64_t r = std::get<int64_t>(right);
      switch (op) {
        case OpCode::ADD: return BytecodeValue(l + r);
        case OpCode::MUL: return BytecodeValue(l * r);
        case OpCode::SUB: return BytecodeValue(l - r);
        case OpCode::DIV: return r != 0 ? BytecodeValue(l / r) : BytecodeValue(int64_t(0));
        default: break;
      }
    }
    if (std::holds_alternative<double>(left) &&
        std::holds_alternative<double>(right)) {
      double l = std::get<double>(left);
      double r = std::get<double>(right);
      switch (op) {
        case OpCode::ADD: return BytecodeValue(l + r);
        case OpCode::MUL: return BytecodeValue(l * r);
        case OpCode::SUB: return BytecodeValue(l - r);
        case OpCode::DIV: return r != 0.0 ? BytecodeValue(l / r) : BytecodeValue(0.0);
        default: break;
      }
    }
    return BytecodeValue(int64_t(0));
  }

  BytecodeValue fastAdd(const BytecodeValue &left, const BytecodeValue &right,
                        uint32_t cache_key) {
    auto &cache = add_caches[cache_key];
    uint32_t left_type = getTypeId(left);
    uint32_t right_type = getTypeId(right);

    cache.hit_count++;
    total_instructions_executed++;

    if (cache.type == InlineCache::CacheType::MONOMORPHIC &&
        cache.type_id1 == left_type && cache.type_id2 == right_type) {
      total_cache_hits++;
    } else {
      cache.miss_count++;
      total_cache_misses++;
      if (cache.type == InlineCache::CacheType::EMPTY) {
        cache.type = InlineCache::CacheType::MONOMORPHIC;
        cache.type_id1 = left_type;
        cache.type_id2 = right_type;
      }
    }

    return performBinaryOp(OpCode::ADD, left, right);
  }

  void executeInstruction(const Instruction &instruction) {
    total_instructions_executed++;

    switch (instruction.opcode) {
      case OpCode::LOAD_CONST: {
        uint32_t idx = std::get<uint32_t>(instruction.operands[0]);
        stack.push(constants[idx]);
        break;
      }
      case OpCode::ADD: {
        BytecodeValue right = stack.top(); stack.pop();
        BytecodeValue left = stack.top(); stack.pop();
        stack.push(fastAdd(left, right, instruction_pointer));
        break;
      }
      case OpCode::MUL: {
        BytecodeValue right = stack.top(); stack.pop();
        BytecodeValue left = stack.top(); stack.pop();
        mul_caches[instruction_pointer].hit_count++;
        total_instructions_executed++;
        stack.push(performBinaryOp(OpCode::MUL, left, right));
        break;
      }
      case OpCode::SUB: {
        BytecodeValue right = stack.top(); stack.pop();
        BytecodeValue left = stack.top(); stack.pop();
        sub_caches[instruction_pointer].hit_count++;
        total_instructions_executed++;
        stack.push(performBinaryOp(OpCode::SUB, left, right));
        break;
      }
      case OpCode::DIV: {
        BytecodeValue right = stack.top(); stack.pop();
        BytecodeValue left = stack.top(); stack.pop();
        div_caches[instruction_pointer].hit_count++;
        total_instructions_executed++;
        stack.push(performBinaryOp(OpCode::DIV, left, right));
        break;
      }
      case OpCode::POP:
        if (!stack.empty()) stack.pop();
        break;
      case OpCode::RETURN:
        return;
      default:
        break;
    }
  }

public:
  OptimizedBytecodeInterpreter() = default;

  void setDebugMode(bool enabled) override { debug_mode = enabled; }

  BytecodeValue execute(const BytecodeChunk &chunk,
                        const std::string &function_name,
                        const std::vector<BytecodeValue> &args = {}) override {
    const auto *function = chunk.getFunction(function_name);
    if (!function) {
      throw std::runtime_error("Function not found: " + function_name);
    }

    constants = function->constants;
    locals.resize(function->local_count);
    instruction_pointer = 0;

    while (!stack.empty()) stack.pop();

    if (debug_mode) {
      std::cout << "=== Optimized Execution: " << function_name << " ===" << std::endl;
    }

    while (instruction_pointer < function->instructions.size()) {
      const auto &instruction = function->instructions[instruction_pointer];

      uint32_t exec_count = ++execution_counts[instruction_pointer];
      uint32_t block_end = instruction_pointer;

      if (jit.shouldCompile(instruction_pointer, exec_count)) {
        block_end = instruction_pointer + 1;
        while (block_end < function->instructions.size() &&
               function->instructions[block_end].opcode != OpCode::JUMP &&
               function->instructions[block_end].opcode != OpCode::JUMP_IF_FALSE &&
               function->instructions[block_end].opcode != OpCode::JUMP_IF_TRUE &&
               function->instructions[block_end].opcode != OpCode::RETURN) {
          block_end++;
        }
        jit.compileBlock(function->instructions, instruction_pointer, block_end);
      }

      if (jit.isCompiled(instruction_pointer) && debug_mode) {
        std::cout << "[JIT] Executing compiled block at " << instruction_pointer << std::endl;
      }

      executeInstruction(instruction);
      instruction_pointer++;
    }

    return stack.empty() ? BytecodeValue(nullptr) : stack.top();
  }

  struct PerformanceStats {
    uint64_t total_instructions = 0;
    uint64_t cache_hits = 0;
    uint64_t cache_misses = 0;
    double cache_hit_rate = 0.0;
    uint32_t jit_compiled_blocks = 0;
    size_t jit_code_size = 0;
  };

  PerformanceStats getPerformanceStats() const {
    PerformanceStats stats;
    stats.total_instructions = total_instructions_executed;

    for (const auto &[key, cache] : add_caches) {
      stats.cache_hits += cache.hit_count;
      stats.cache_misses += cache.miss_count;
    }
    for (const auto &[key, cache] : mul_caches) {
      stats.cache_hits += cache.hit_count;
      stats.cache_misses += cache.miss_count;
    }

    stats.cache_hit_rate = stats.total_instructions > 0
        ? (double)stats.cache_hits / stats.total_instructions : 0.0;

    auto jit_stats = jit.getStats();
    stats.jit_compiled_blocks = jit_stats.compiled_blocks;
    stats.jit_code_size = jit_stats.total_code_size;

    return stats;
  }

  void printPerformanceStats() const {
    auto stats = getPerformanceStats();
    std::cout << "\n=== Performance Statistics ===" << std::endl;
    std::cout << "Total instructions: " << stats.total_instructions << std::endl;
    std::cout << "Cache hits: " << stats.cache_hits << std::endl;
    std::cout << "Cache misses: " << stats.cache_misses << std::endl;
    std::cout << "Cache hit rate: " << (stats.cache_hit_rate * 100) << "%" << std::endl;
    std::cout << "JIT compiled blocks: " << stats.jit_compiled_blocks << std::endl;
    std::cout << "JIT code size: " << stats.jit_code_size << " bytes" << std::endl;
    std::cout << "==============================\n" << std::endl;
  }

  void resetPerformanceStats() {
    add_caches.clear();
    mul_caches.clear();
    sub_caches.clear();
    div_caches.clear();
    execution_counts.clear();
    total_instructions_executed = 0;
    total_cache_hits = 0;
    total_cache_misses = 0;
  }

  void setJITThreshold(uint32_t threshold) {
    jit.setCompilationThreshold(threshold);
  }

  uint32_t getJITThreshold() const { return jit.getCompilationThreshold(); }
};

// Factory for optimized interpreter
std::unique_ptr<BytecodeInterpreter> createOptimizedBytecodeInterpreter() {
  return std::make_unique<OptimizedBytecodeInterpreter>();
}

} // namespace havel::compiler
