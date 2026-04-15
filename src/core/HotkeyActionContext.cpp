#include "HotkeyActionContext.hpp"
#include "utils/Logger.hpp"

namespace havel {

// Static member initialization
thread_local HotkeyActionContext::ContextData HotkeyActionContext::current_context_;
thread_local bool HotkeyActionContext::context_available_ = false;

std::unordered_map<std::string, HotkeyActionStateSync::StateValue> 
    HotkeyActionStateSync::state_;

// ============================================================================
// Phase 2J: HotkeyActionContext Implementation
// ============================================================================

void HotkeyActionContext::setContext(const ContextData& context) {
  current_context_ = context;
  context_available_ = true;
  
  debug("HotkeyActionContext: Set context for hotkey '{}', var='{}', result={}",
        context.hotkey_name, context.changed_variable, context.condition_result);
}

const HotkeyActionContext::ContextData& HotkeyActionContext::getContext() {
  if (!context_available_) {
    warn("HotkeyActionContext: Accessing context when not available");
  }
  return current_context_;
}

bool HotkeyActionContext::hasContext() {
  return context_available_;
}

void HotkeyActionContext::clearContext() {
  context_available_ = false;
  debug("HotkeyActionContext: Cleared context");
}

// ============================================================================
// Phase 2J: HotkeyActionStateSync Implementation
// ============================================================================

void HotkeyActionStateSync::setState(const std::string& key, const StateValue& value) {
  state_[key] = value;
  debug("HotkeyActionStateSync: Set state: {} = {}", key, value);
}

HotkeyActionStateSync::StateValue HotkeyActionStateSync::getState(const std::string& key) {
  auto it = state_.find(key);
  if (it != state_.end()) {
    return it->second;
  }
  warn("HotkeyActionStateSync: State key not found: {}", key);
  return "";
}

bool HotkeyActionStateSync::hasState(const std::string& key) {
  return state_.find(key) != state_.end();
}

std::unordered_map<std::string, HotkeyActionStateSync::StateValue> 
HotkeyActionStateSync::getAllState() {
  return state_;
}

void HotkeyActionStateSync::clearAll() {
  state_.clear();
  info("HotkeyActionStateSync: Cleared all state");
}

}  // namespace havel
