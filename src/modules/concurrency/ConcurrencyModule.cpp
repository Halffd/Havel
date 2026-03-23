/*
 * ConcurrencyModule.cpp
 *
 * Thread, interval, timeout, and range support for Havel language.
 */
#include "ConcurrencyModule.hpp"
#include "../../havel-lang/runtime/Environment.hpp"
#include "../../havel-lang/runtime/concurrency/Thread.hpp"
#include <memory>

namespace havel::modules {

void registerConcurrencyModule(Environment &env, std::shared_ptr<IHostAPI>) {
  // Store active threads/intervals to keep them alive
  static std::vector<std::shared_ptr<havel::Thread>> activeThreads;
  static std::vector<std::shared_ptr<havel::Interval>> activeIntervals;
  static std::vector<std::shared_ptr<havel::Timeout>> activeTimeouts;

  // thread { ... } - Create a new thread
  // MIGRATED TO BYTECODE VM: env.Define(
      "thread",
      HavelValue(BuiltinFunction(
          [](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.empty()) {
              return HavelRuntimeError("thread() requires a callback function");
            }

            // Create thread
            auto thread = std::make_shared<havel::Thread>();

            // TODO: Extract callback from HavelValue and start thread
            // For now, just store it
            activeThreads.push_back(thread);

            // Return thread as object
            auto obj =
                std::make_shared<std::unordered_map<std::string, HavelValue>>();
            (*obj)["type"] = HavelValue("thread");
            return HavelValue(obj);
          })));

  // interval ms { ... } - Create a repeating timer
  // MIGRATED TO BYTECODE VM: env.Define(
      "interval",
      HavelValue(BuiltinFunction(
          [](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.size() < 2) {
              return HavelRuntimeError("interval() requires ms and callback");
            }

            int intervalMs = static_cast<int>(args[0].asNumber());

            // Create interval
            auto callback = []() {
              // TODO: Execute Havel callback
            };

            auto interval =
                std::make_shared<havel::Interval>(intervalMs, callback);
            activeIntervals.push_back(interval);

            // Return interval as object
            auto obj =
                std::make_shared<std::unordered_map<std::string, HavelValue>>();
            (*obj)["type"] = HavelValue("interval");
            return HavelValue(obj);
          })));

  // timeout ms { ... } - One-shot delayed execution
  // MIGRATED TO BYTECODE VM: env.Define(
      "timeout",
      HavelValue(BuiltinFunction(
          [](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.size() < 2) {
              return HavelRuntimeError("timeout() requires ms and callback");
            }

            int timeoutMs = static_cast<int>(args[0].asNumber());

            // Create timeout
            auto callback = []() {
              // TODO: Execute Havel callback
            };

            auto timeout =
                std::make_shared<havel::Timeout>(timeoutMs, callback);
            activeTimeouts.push_back(timeout);

            // Return timeout as object
            auto obj =
                std::make_shared<std::unordered_map<std::string, HavelValue>>();
            (*obj)["type"] = HavelValue("timeout");
            return HavelValue(obj);
          })));

  // Range type: start..end
  // MIGRATED TO BYTECODE VM: env.Define(
      "Range",
      HavelValue(BuiltinFunction(
          [](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.size() < 2) {
              return HavelRuntimeError("Range() requires start and end");
            }

            int start = static_cast<int>(args[0].asNumber());
            int end = static_cast<int>(args[1].asNumber());

            auto range = std::make_shared<havel::TimeRange>(start, end);
            auto obj =
                std::make_shared<std::unordered_map<std::string, HavelValue>>();
            (*obj)["type"] = HavelValue("range");
            (*obj)["start"] = HavelValue(start);
            (*obj)["end"] = HavelValue(end);
            return HavelValue(obj);
          })));

  // Helper: first_existing file path
  // MIGRATED TO BYTECODE VM: env.Define("firstExisting",
             HavelValue(BuiltinFunction(
                 [](const std::vector<HavelValue> &args) -> HavelResult {
                   for (const auto &arg : args) {
                     if (arg.isString()) {
                       std::string path = arg.asString();
                       // TODO: Check if file exists
                       // For now, return first non-empty string
                       if (!path.empty()) {
                         return HavelValue(path);
                       }
                     }
                   }
                   return HavelValue("");
                 })));
}

} // namespace havel::modules
