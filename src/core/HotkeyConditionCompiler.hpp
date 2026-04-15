#pragma once

#include <string>
#include <cstdint>
#include <memory>
#include <unordered_map>

namespace havel {

// Forward declarations
namespace compiler { class VM; }

/**
 * Phase 2H: HotkeyConditionCompiler
 * 
 * Compiles hotkey condition strings to bytecode for efficient reactive evaluation.
 * 
 * Instead of repeatedly parsing and patternmatching condition strings at runtime,
 * we compile them once to bytecode and cache the result.
 * 
 * Example:
 *   "mode == 'gaming' && window.exe == 'steam.exe'"
 *   →
 *   Bytecode: LOAD_GLOBAL "mode", CONST "gaming", EQ, LOAD_GLOBAL "window", ...
 *   Cached in: compiled_conditions_
 * 
 * Usage:
 *   auto [func_id, ip] = compiler.compileCondition(vm, "mode == 'gaming'");
 *   // Register with WatcherRegistry for efficient re-evaluation
 */
class HotkeyConditionCompiler {
public:
  HotkeyConditionCompiler() = default;
  ~HotkeyConditionCompiler() = default;
  
  /**
   * Compiled condition reference
   * Stores the bytecode location for efficient evaluation
   */
  struct CompiledCondition {
    uint32_t function_id;      // Function in VM bytecode
    uint32_t instruction_ptr;  // IP within that function
    std::string original_source; // For debugging
  };
  
  /**
   * Compile a condition string to bytecode
   * 
   * Phase 2H Strategy:
   * - Try to parse as simple condition expressions first
   * - Fall back to pattern matching for complex conditions  
   * - Cache compiled bytecode for reuse
   * 
   * @param vm The bytecode VM
   * @param condition_str The condition string (e.g., "mode == 'gaming'")
   * @return CompiledCondition with function_id and IP, or nullopt if fails
   * 
   * Note: In Phase 2H, we focus on string pattern compilation
   *       Full expression parsing to bytecode is Phase 2H+ enhancement
   */
  std::unique_ptr<CompiledCondition> compileCondition(
      compiler::VM* vm,
      const std::string& condition_str);
  
  /**
   * Get cached compiled condition (Phase 2H optimization)
   */
  const CompiledCondition* getCached(const std::string& condition_str) const;
  
  /**
   * Clear cache (if conditions change dynamically)
   */
  void clearCache() { compiled_conditions_.clear(); }
  
  /**
   * Get cache size (debugging)
   */
  size_t getCacheSize() const { return compiled_conditions_.size(); }

private:
  // Phase 2H: Cache compiled conditions to avoid recompilation
  // Key: condition string, Value: compiled bytecode reference
  std::unordered_map<std::string, std::unique_ptr<CompiledCondition>> compiled_conditions_;
  
  /**
   * Internal helper: Try pattern-based condition compilation
   * 
   * Phase 2H uses pattern matching to avoid needing full parser
   * Recognizes: "mode == 'X'", "window.exe == 'Y'", "window.title ~= 'Z'", etc.
   */
  std::unique_ptr<CompiledCondition> compilePatternCondition(
      compiler::VM* vm,
      const std::string& condition_str);
};

}  // namespace havel
