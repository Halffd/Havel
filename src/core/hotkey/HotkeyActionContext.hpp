#pragma once

#include <string>
#include <cstdint>
#include <unordered_map>
#include <chrono>
#include <memory>

namespace havel {

/**
 * Phase 2J: HotkeyActionContext
 * 
 * Provides state context to hotkey action Fibers, allowing them to know:
 * - Which condition triggered them (result: bool)
 * - Which variable changed (var_name: string)
 * - When the change occurred (timestamp)
 * - Any additional metadata
 * 
 * Example usage in Havel code:
 *   on_hotkey(HotKey.F1, hotkey.mode == "gaming", {
 *       let context = hotkey_context();
 *       print("Condition: " + context.condition);    // true/false
 *       print("Variable: " + context.variable);      // "mode"
 *       brightness(50);
 *   });
 * 
 * Architecture:
 * - ExecutionEngine stores current context when calling action Fiber
 * - Action Fiber can retrieve context via hotkey_context() builtin
 * - Context persists for the duration of Fiber execution
 * - Fiber can read but not modify context (read-only snapshot)
 */
class HotkeyActionContext {
public:
  // Context data available to action Fibers
  struct ContextData {
    bool condition_result = false;        // Did condition evaluate to true/false?
    std::string changed_variable;         // Which variable triggered this? ("mode", etc)
    uint64_t timestamp_ns = 0;            // When did variable change? (nanoseconds)
    std::string hotkey_name;              // For debugging: which hotkey fired?
    std::string condition_source;         // For debugging: original condition string
  };
  
  /**
   * Set current context (called when hotkey action triggers)
   * 
   * Phase 2J: When ExecutionEngine fires a watcher:
   * 1. Create ContextData with condition result and variable name
   * 2. Call setContext() to make it available to the Fiber
   * 3. Resume action Fiber
   * 4. Fiber calls hotkey_context() to read
   */
  static void setContext(const ContextData& context);
  
  /**
   * Get current context (called from Fiber)
   */
  static const ContextData& getContext();
  
  /**
   * Check if context is available
   */
  static bool hasContext();
  
  /**
   * Clear context (called when action Fiber completes)
   */
  static void clearContext();

private:
static thread_local ContextData current_context_;
static thread_local bool context_available_;
};

} // namespace havel
