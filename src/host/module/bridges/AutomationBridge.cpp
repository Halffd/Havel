// AutomationBridge implementation — extracted from ModularHostBridges.cpp when
// the file was split per-domain. No behavior change.

#include "../ModularHostBridges.hpp"
#include "BridgesInternal.hpp"

namespace havel::compiler {
void AutomationBridge::install(PipelineOptions &options) {
  options.host_functions["automation.createAutoClicker"] =
      [ctx = ctx_](const auto &args) {
        return handleAutomationCreateAutoClicker(args, ctx);
      };
  options.host_functions["automation.createAutoRunner"] =
      [ctx = ctx_](const auto &args) {
        return handleAutomationCreateAutoRunner(args, ctx);
      };
  options.host_functions["automation.createAutoKeyPresser"] =
      [ctx = ctx_](const auto &args) {
        return handleAutomationCreateAutoKeyPresser(args, ctx);
      };
  options.host_functions["automation.hasTask"] = [ctx =
                                                      ctx_](const auto &args) {
    return handleAutomationHasTask(args, ctx);
  };
  options.host_functions["automation.removeTask"] =
      [ctx = ctx_](const auto &args) {
        return handleAutomationRemoveTask(args, ctx);
      };
  options.host_functions["automation.stopAll"] = [ctx =
                                                      ctx_](const auto &args) {
    return handleAutomationStopAll(args, ctx);
  };
}


Value AutomationBridge::handleAutomationCreateAutoClicker(
    const std::vector<Value> &args, const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return Value::makeBool(false);
}


Value AutomationBridge::handleAutomationCreateAutoRunner(
    const std::vector<Value> &args, const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return Value::makeBool(false);
}


Value AutomationBridge::handleAutomationCreateAutoKeyPresser(
    const std::vector<Value> &args, const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return Value::makeBool(false);
}


Value AutomationBridge::handleAutomationHasTask(
    const std::vector<Value> &args, const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return Value::makeBool(false);
}


Value AutomationBridge::handleAutomationRemoveTask(
    const std::vector<Value> &args, const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return Value::makeBool(false);
}


Value AutomationBridge::handleAutomationStopAll(
    const std::vector<Value> &args, const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return Value::makeBool(false);
}

// ============================================================================
// BrowserBridge Implementation
// ============================================================================

} // namespace havel::compiler
