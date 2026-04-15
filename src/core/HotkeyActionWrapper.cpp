#include "HotkeyActionWrapper.hpp"
#include "utils/Logger.hpp"
#include <unordered_map>
#include <memory>

namespace havel {

// Static member initialization
std::unordered_map<uint32_t, HotkeyActionWrapper::ActionCallback> 
    HotkeyActionWrapper::action_callbacks_;

compiler::Fiber* HotkeyActionWrapper::createActionFiber(
    const std::string& fiber_name,
    ActionCallback action) {
  
  if (!action) {
    error("HotkeyActionWrapper: Cannot create Fiber with null callback");
    return nullptr;
  }
  
  // Phase 2I: Create a Fiber with special marker function ID
  // The Fiber will be executed by ExecutionEngine which will call the callback
  static uint32_t next_fiber_id = 1000;  // Non-overlapping with regular fiber IDs
  uint32_t fiber_id = next_fiber_id++;
  
  // Create Fiber with special function ID
  // When ExecutionEngine sees this, it will call the callback instead of executing bytecode
  auto fiber = std::make_unique<compiler::Fiber>(
      fiber_id,
      HOTKEY_ACTION_FUNCTION_ID,  // Special marker for hotkey actions
      0,  // parent_id
      fiber_name
  );
  
  if (!fiber) {
    error("HotkeyActionWrapper: Failed to create Fiber");
    return nullptr;
  }
  
  // Store the action callback for later execution
  registerCallback(fiber_id, action);
  
  debug("HotkeyActionWrapper: Created action Fiber '{}' with ID {}", fiber_name, fiber_id);
  
  // Return raw pointer (Fiber ownership is transferred to Scheduler)
  return fiber.release();
}

HotkeyActionWrapper::ActionCallback* HotkeyActionWrapper::getCallback(uint32_t fiber_id) {
  auto it = action_callbacks_.find(fiber_id);
  if (it != action_callbacks_.end()) {
    return &it->second;
  }
  return nullptr;
}

void HotkeyActionWrapper::registerCallback(uint32_t fiber_id, ActionCallback callback) {
  action_callbacks_[fiber_id] = callback;
  debug("HotkeyActionWrapper: Registered callback for Fiber {}", fiber_id);
}

void HotkeyActionWrapper::unregisterCallback(uint32_t fiber_id) {
  action_callbacks_.erase(fiber_id);
  debug("HotkeyActionWrapper: Unregistered callback for Fiber {}", fiber_id);
}

void HotkeyActionWrapper::clearAll() {
  action_callbacks_.clear();
  info("HotkeyActionWrapper: Cleared all callbacks");
}

}  // namespace havel
