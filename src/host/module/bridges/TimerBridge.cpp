// TimerBridge implementation — extracted from ModularHostBridges.cpp when
// the file was split per-domain. No behavior change.

#include "../ModularHostBridges.hpp"
#include "BridgesInternal.hpp"

namespace havel::compiler {
void TimerBridge::install(PipelineOptions &options) {
  // Timer functions are available but require closure support for callbacks
  // For now, use sleep() for simple delays
  options.host_functions["timer.after"] = [ctx = ctx_](const auto &args) {
    return handleAfter(args, ctx);
  };
  options.host_functions["timer.every"] = [ctx = ctx_](const auto &args) {
    return handleEvery(args, ctx);
  };
}


Value TimerBridge::handleAfter(const std::vector<Value> &args,
                                       const HostContext *ctx) {
  if (args.empty()) {
    throw std::runtime_error("timer.after() requires delay_ms");
  }

  if (!args[0].isInt()) {
    throw std::runtime_error("timer.after() delay must be an integer");
  }

  int64_t delay_ms = args[0].asInt();

  // Legacy shim: sleep for delay_ms (callback argument is ignored —
  // scripts use timeout{} for real callbacks). The sleep runs on a
  // worker under the A+C model so goroutines park instead of stalling
  // the VM thread; top-level calls block inline exactly as before.
  if (!ctx || !ctx->vm) {
    std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    return Value::makeNull();
  }
  auto *vm = static_cast<VM *>(ctx->vm);
  compiler::VMApi api(*vm);
  return api.runBlocking(
      [delay_ms]() -> compiler::AsyncCxxResult {
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        return nullptr;
      },
      [](const compiler::AsyncCxxResult &) -> Value {
        return Value::makeNull();
      });
}


Value TimerBridge::handleEvery(const std::vector<Value> &args,
                                       const HostContext *ctx) {
  (void)args;
  (void)ctx;
  // timer.every requires closure/callback support
  // For now, just return without doing anything
  // Users should use a while loop with sleep instead:
  // while (true) { body; sleep(interval); }
  throw std::runtime_error("timer.every() requires closure support - use while "
                           "loop with sleep() instead");
}

// ============================================================================
// AppBridge Implementation
// ============================================================================

} // namespace havel::compiler
