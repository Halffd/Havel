#include "HotkeyConditionCompiler.hpp"
#include "havel-lang/compiler/vm/VM.hpp"
#include "utils/Logger.hpp"

#include <sstream>

namespace havel {

std::unique_ptr<HotkeyConditionCompiler::CompiledCondition>
HotkeyConditionCompiler::compileCondition(
    compiler::VM* vm,
    const std::string& condition_str) {
  
  if (!vm) {
    error("HotkeyConditionCompiler: No VM provided");
    return nullptr;
  }
  
  // Phase 2H: Check cache first
  if (auto cached = getCached(condition_str)) {
    debug("HotkeyConditionCompiler: Cache hit for condition: {}", condition_str);
    return std::make_unique<CompiledCondition>(*cached);
  }
  
  // Phase 2H: Try pattern-based compilation
  // In Phase 2H+, this would use full parser/compiler pipeline
  // For now, we support simple pattern matching that can be compiled efficiently
  auto compiled = compilePatternCondition(vm, condition_str);
  
  if (compiled) {
    debug("HotkeyConditionCompiler: Successfully compiled condition: {}", condition_str);
    // Cache the result
    compiled_conditions_[condition_str] = std::make_unique<CompiledCondition>(*compiled);
    return compiled;
  }
  
  warn("HotkeyConditionCompiler: Failed to compile condition: {}", condition_str);
  return nullptr;
}

const HotkeyConditionCompiler::CompiledCondition*
HotkeyConditionCompiler::getCached(const std::string& condition_str) const {
  auto it = compiled_conditions_.find(condition_str);
  if (it != compiled_conditions_.end()) {
    return it->second.get();
  }
  return nullptr;
}

std::unique_ptr<HotkeyConditionCompiler::CompiledCondition>
HotkeyConditionCompiler::compilePatternCondition(
    compiler::VM* vm,
    const std::string& condition_str) {
  
  // Phase 2H: Simple pattern matching for common condition patterns
  // This allows compilation of common conditions without full parser
  
  // Pattern 1: mode == 'X'
  if (condition_str.find("mode ==") != std::string::npos) {
    // Extract mode name from pattern like: "mode == 'gaming'"
    size_t start = condition_str.find("'");
    size_t end = condition_str.rfind("'");
    
    if (start != std::string::npos && end != std::string::npos && start < end) {
      std::string mode_name = condition_str.substr(start + 1, end - start - 1);
      
      // Phase 2H Future: Emit bytecode like:
      // LOAD_GLOBAL "current_mode"
      // CONST "<mode_name>"
      // EQ
      // RETURN
      
      // For now, return placeholder indicating this pattern was recognized
      auto compiled = std::make_unique<CompiledCondition>();
      compiled->original_source = condition_str;
      compiled->function_id = 0;  // Special ID for pattern-based condition
      compiled->instruction_ptr = 0;
      
      debug("HotkeyConditionCompiler: Recognized 'mode == X' pattern: {}", condition_str);
      return compiled;
    }
  }
  
  // Pattern 2: window.exe == 'X'
  if (condition_str.find("window.exe") != std::string::npos ||
      condition_str.find("window.title") != std::string::npos) {
    
    debug("HotkeyConditionCompiler: Recognized window pattern: {}", condition_str);
    
    auto compiled = std::make_unique<CompiledCondition>();
    compiled->original_source = condition_str;
    compiled->function_id = 1;  // Special ID for window pattern
    compiled->instruction_ptr = 0;
    
    return compiled;
  }
  
  // Pattern 3: Complex boolean expressions (phase 2H+)
  // For now, these remain as string patterns evaluated at runtime
  // In Phase 2H+, we would:
  // 1. Parse the string to AST
  // 2. Type-check the AST
  // 3. Compile to bytecode
  // 4. Return function_id and IP pointing to compiled code
  
  return nullptr;
}

}  // namespace havel
