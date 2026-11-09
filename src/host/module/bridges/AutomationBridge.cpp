// AutomationBridge implementation - extracted from ModularHostBridges.cpp when
// the file was split per-domain. The create/hasTask/removeTask/stopAll/
// getTaskNames handlers drive HostContext::automationManager (the engine-owned
// core::automation::AutomationManager, wired by HavelLauncher's createHostAPI).

#include "../ModularHostBridges.hpp"
#include "BridgesInternal.hpp"

#include <stdexcept>

#include "core/automation/AutomationManager.hpp"

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
  options.host_functions["automation.getTaskNames"] =
      [ctx = ctx_](const auto &args) {
        return handleAutomationGetTaskNames(args, ctx);
      };
}

namespace {

// Resolve one positional string arg (StringValId via the VM's string table,
// same pattern as SystemBridge's process.* handlers).
std::string autoStrArg(const std::vector<Value> &args, size_t i,
                       const HostContext *ctx, const char *what) {
  if (i >= args.size())
    throw std::runtime_error(std::string("automation: missing ") + what +
                             " argument");
  const Value &v = args[i];
  if (v.isStringValId() || v.isStringId()) {
    auto *vm = ctx ? static_cast<VM *>(ctx->vm) : nullptr;
    return vm ? vm->resolveStringKey(v) : strVal(v, nullptr);
  }
  throw std::runtime_error(std::string("automation: ") + what +
                           " must be a string");
}

int autoIntArg(const std::vector<Value> &args, size_t i, int fallback) {
  if (i >= args.size())
    return fallback;
  const Value &v = args[i];
  if (v.isNull())
    return fallback;
  if (v.isInt() || v.isNumber())
    return static_cast<int>(v.asNumber());
  return fallback;
}

::havel::automation::AutomationManager *automationMgr(const HostContext *ctx) {
  return ctx ? ctx->automationManager : nullptr;
}

} // namespace

Value AutomationBridge::handleAutomationCreateAutoClicker(
    const std::vector<Value> &args, const HostContext *ctx) {
  auto *mgr = automationMgr(ctx);
  if (!mgr)
    throw std::runtime_error("automation: AutomationManager is not available");
  std::string button = args.empty() ? std::string("left")
                                    : autoStrArg(args, 0, ctx, "button");
  int intervalMs = autoIntArg(args, 1, 100);
  auto task = mgr->createAutoClicker(button, intervalMs);
  if (!task)
    return Value::makeBool(false);
  task->start();
  return Value::makeBool(true);
}

Value AutomationBridge::handleAutomationCreateAutoRunner(
    const std::vector<Value> &args, const HostContext *ctx) {
  auto *mgr = automationMgr(ctx);
  if (!mgr)
    throw std::runtime_error("automation: AutomationManager is not available");
  std::string direction = args.empty() ? std::string("right")
                                       : autoStrArg(args, 0, ctx, "direction");
  int intervalMs = autoIntArg(args, 1, 50);
  auto task = mgr->createAutoRunner(direction, intervalMs);
  if (!task)
    return Value::makeBool(false);
  task->start();
  return Value::makeBool(true);
}

Value AutomationBridge::handleAutomationCreateAutoKeyPresser(
    const std::vector<Value> &args, const HostContext *ctx) {
  auto *mgr = automationMgr(ctx);
  if (!mgr)
    throw std::runtime_error("automation: AutomationManager is not available");
  std::string key = args.empty() ? std::string("space")
                                 : autoStrArg(args, 0, ctx, "key");
  int intervalMs = autoIntArg(args, 1, 100);
  auto task = mgr->createAutoKeyPresser(key, intervalMs);
  if (!task)
    return Value::makeBool(false);
  task->start();
  return Value::makeBool(true);
}

Value AutomationBridge::handleAutomationHasTask(const std::vector<Value> &args,
                                                const HostContext *ctx) {
  auto *mgr = automationMgr(ctx);
  if (!mgr)
    return Value::makeBool(false);
  std::string name = autoStrArg(args, 0, ctx, "task name");
  return Value::makeBool(mgr->hasTask(name));
}

Value AutomationBridge::handleAutomationRemoveTask(
    const std::vector<Value> &args, const HostContext *ctx) {
  auto *mgr = automationMgr(ctx);
  if (!mgr)
    return Value::makeBool(false);
  std::string name = autoStrArg(args, 0, ctx, "task name");
  bool had = mgr->hasTask(name);
  mgr->removeTask(name);
  return Value::makeBool(had);
}

Value AutomationBridge::handleAutomationStopAll(const std::vector<Value> &args,
                                                const HostContext *ctx) {
  (void)args;
  auto *mgr = automationMgr(ctx);
  if (!mgr)
    return Value::makeBool(false);
  mgr->stopAll();
  return Value::makeBool(true);
}

Value AutomationBridge::handleAutomationGetTaskNames(
    const std::vector<Value> &args, const HostContext *ctx) {
  (void)args;
  auto *mgr = automationMgr(ctx);
  auto *vm = ctx ? static_cast<VM *>(ctx->vm) : nullptr;
  if (!mgr || !vm)
    return Value::makeNull();
  auto arr = vm->createHostArray();
  for (const auto &name : mgr->getTaskNames()) {
    auto strRef = vm->createRuntimeString(name);
    vm->pushHostArrayValue(arr, Value::makeStringId(strRef.id));
  }
  return Value::makeArrayId(arr.id);
}

} // namespace havel::compiler
