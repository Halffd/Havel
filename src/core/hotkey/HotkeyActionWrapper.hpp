#pragma once

#include "havel-lang/runtime/concurrency/Fiber.hpp"
#include <functional>
#include <memory>
#include <cstdint>
#include <atomic>

namespace havel {

/**
 * Phase 2I: HotkeyActionWrapper
 * 
 * Wraps C++ hotkey action callbacks (void functions) in Fibers for full integration
 * with the goroutine execution model.
 * 
 * Before Phase 2I:
 *   Hotkey action = void callback()
 *   When condition fires → call callback directly (blocking if slow)
 * 
 * After Phase 2I:
 *   Hotkey action = Fiber wrapping callback
 *   When condition fires → resume Fiber in Scheduler (non-blocking)
 *   Fiber yields to main loop when done
 * 
 * Benefits:
 * - Hotkey detection never blocks (non-cooperative)
 * - Long-running actions are scheduled like other goroutines
 * - Actions can suspend on sleep(), channels, etc
 * - Main loop coordinates everything seamlessly
 * 
 * Example:
 *   auto wrapper = HotkeyActionWrapper::create(
 *       "brightness_control",
 *       [](){ brightness(50); }
 *   );
 *   // Returns Fiber that can be scheduled and executed
 */
class HotkeyActionWrapper {
public:
  using ActionCallback = std::function<void()>;
  
  /**
   * Create a Fiber wrapping a hotkey action callback
   * 
   * @param fiber_name Debug name for the Fiber
   * @param action Callback to execute (void function)
   * @return Fiber* ready for scheduling, or nullptr if creation failed
   * 
   * Phase 2I Note:
   * In a full implementation, we would:
   * 1. Compile the action to a bytecode function
   * 2. Create a Fiber with that function_id
   * 3. Store the C++ callback in a thread-local registry
   * 4. When Fiber executes, call the C++ callback
   * 
   * For Phase 2I MVP, we use a simpler approach:
   * - Create a Fiber with special marker function_id
   * - Store callback in a map indexed by Fiber ID
   * - ExecutionEngine::executeFrame() special-cases these Fibers
   */
  static compiler::Fiber* createActionFiber(
      const std::string& fiber_name,
      ActionCallback action);
  
  /**
   * Get callback for a Fiber (for execution)
   */
  static ActionCallback* getCallback(uint32_t fiber_id);
  
  /**
   * Register a callback for a Fiber ID
   * (Called internally by createActionFiber)
   */
  static void registerCallback(uint32_t fiber_id, ActionCallback callback);
  
  /**
   * Unregister and cleanup (called when Fiber completes)
   */
  static void unregisterCallback(uint32_t fiber_id);
  
  /**
   * Clear all callbacks (shutdown)
   */
  static void clearAll();
  
  // Special function ID for hotkey action Fibers
  // When VM sees this function_id, it calls the registered callback instead
  static constexpr uint32_t HOTKEY_ACTION_FUNCTION_ID = 0xFFFFFFFF;

private:
  // Registry: Fiber ID → Action callback
  // Allows ExecutionEngine to find callbacks when executing action Fibers
  static std::unordered_map<uint32_t, ActionCallback> action_callbacks_;
};

}  // namespace havel
