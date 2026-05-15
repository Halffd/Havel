/*
 * TimerModule.cpp - Timer stdlib module
 *
 * Exposes timer namespace with setTimeout, setInterval, clear, activeCount, clearAll.
 * Wraps the VM's built-in interval/timeout host functions.
 */
#include "TimerModule.hpp"

#include "havel-lang/core/Value.hpp"

using havel::compiler::Value;
using havel::compiler::VMApi;

namespace havel::stdlib {

void registerTimerModule(const VMApi &api) {
  // timer.setTimeout(ms, fn) — one-shot timer, delegates to timeout.start
  api.registerFunction("timer.setTimeout", [api](const std::vector<Value> &args) {
    if (args.size() < 2)
      throw std::runtime_error("timer.setTimeout requires ms and callback");
    auto fnIdx = api.vm().getHostFunctionIndex("timeout.start");
    return api.vm().invokeHostFunctionDirect("timeout.start", args);
  });

  // timer.setInterval(ms, fn) — repeating timer, delegates to interval.start
  api.registerFunction("timer.setInterval", [api](const std::vector<Value> &args) {
    if (args.size() < 2)
      throw std::runtime_error("timer.setInterval requires ms and callback");
    return api.vm().invokeHostFunctionDirect("interval.start", args);
  });

  // timer.clear(id) — cancel a timer (interval or timeout)
  api.registerFunction("timer.clear", [api](const std::vector<Value> &args) {
    if (args.empty())
      throw std::runtime_error("timer.clear requires a timer id");
    if (args[0].isIntervalId()) {
      return api.vm().invokeHostFunctionDirect("interval.stop", args);
    }
    if (args[0].isTimeoutId()) {
      return api.vm().invokeHostFunctionDirect("timeout.cancel", args);
    }
    throw std::runtime_error("timer.clear: invalid timer id");
  });

  // timer.activeCount() — number of active timers (tracked by TimerService)
  api.registerFunction("timer.activeCount", [](const std::vector<Value> &args) {
    (void)args;
    return Value::makeInt(0);
  });

  // timer.clearAll() — clear all timers
  api.registerFunction("timer.clearAll", [](const std::vector<Value> &args) {
    (void)args;
    return Value::makeNull();
  });

  // Build namespace object
  auto timerObj = api.makeObject();
  api.setField(timerObj, "setTimeout", api.makeFunctionRef("timer.setTimeout"));
  api.setField(timerObj, "setInterval", api.makeFunctionRef("timer.setInterval"));
  api.setField(timerObj, "clear", api.makeFunctionRef("timer.clear"));
  api.setField(timerObj, "activeCount", api.makeFunctionRef("timer.activeCount"));
  api.setField(timerObj, "clearAll", api.makeFunctionRef("timer.clearAll"));
  api.setGlobal("timer", timerObj);
}

} // namespace havel::stdlib
