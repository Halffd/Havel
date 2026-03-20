/*
 * TimerModule.cpp
 *
 * Timer module for Havel language.
 * Uses existing TimerManager from src/utils/Timer.hpp
 */
#include "TimerModule.hpp"
#include "../../havel-lang/runtime/Environment.hpp"
#include "utils/Timer.hpp"

namespace havel::modules {

void registerTimerModule(Environment &env, std::shared_ptr<IHostAPI>) {
  // Create timer module object
  auto timerObj =
      std::make_shared<std::unordered_map<std::string, HavelValue>>();

  // =========================================================================
  // setTimeout(callback, delayMs) - One-time timer
  // =========================================================================

  (*timerObj)["setTimeout"] = HavelValue(BuiltinFunction(
      [](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2) {
          return HavelRuntimeError("setTimeout requires callback and delay");
        }

        int delayMs = static_cast<int>(args[0].asNumber());

        // Validate callback
        if (!args[1].is<std::shared_ptr<HavelFunction>>()) {
          return HavelRuntimeError(
              "setTimeout second argument must be a function");
        }

        auto callback = args[1].get<std::shared_ptr<HavelFunction>>();

        // Create timer - note: full execution requires interpreter context
        // This is a simplified version that creates the timer but can't execute
        // the callback without interpreter access
        try {
          auto timer = havel::SetTimeout(delayMs, [callback]() {
            // TODO: This needs interpreter context to evaluate callback
            // For now, just log that the timer fired
            info("Timer callback would execute function '{}', but interpreter "
                 "context not available",
                 callback->declaration->name->symbol);
          });

          // Return timer ID (use pointer address as ID)
          return HavelValue(
              static_cast<double>(reinterpret_cast<uintptr_t>(timer.get())));
        } catch (const std::exception &e) {
          return HavelRuntimeError("Failed to create timer: " +
                                   std::string(e.what()));
        }
      }));

  // =========================================================================
  // setInterval(callback, intervalMs) - Repeating timer
  // =========================================================================

  (*timerObj)["setInterval"] = HavelValue(BuiltinFunction(
      [](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2) {
          return HavelRuntimeError(
              "setInterval requires callback and interval");
        }

        int intervalMs = static_cast<int>(args[0].asNumber());

        // Validate callback
        if (!args[1].is<std::shared_ptr<HavelFunction>>()) {
          return HavelRuntimeError(
              "setInterval second argument must be a function");
        }

        auto callback = args[1].get<std::shared_ptr<HavelFunction>>();

        try {
          auto timer = havel::SetInterval(intervalMs, [callback]() {
            // TODO: This needs interpreter context to evaluate callback
            info("Interval callback would execute function '{}', but "
                 "interpreter context not available",
                 callback->declaration->name->symbol);
          });

          return HavelValue(
              static_cast<double>(reinterpret_cast<uintptr_t>(timer.get())));
        } catch (const std::exception &e) {
          return HavelRuntimeError("Failed to create interval: " +
                                   std::string(e.what()));
        }
      }));

  // =========================================================================
  // clearTimeout(timerId) - Cancel timeout
  // =========================================================================

  (*timerObj)["clearTimeout"] = HavelValue(BuiltinFunction(
      [](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty()) {
          return HavelRuntimeError("clearTimeout requires timer ID");
        }

        // Convert ID back to pointer
        double timerId = args[0].asNumber();
        auto timer = std::shared_ptr<std::atomic<bool>>(
            reinterpret_cast<std::atomic<bool> *>(
                static_cast<uintptr_t>(timerId)),
            [](std::atomic<bool> *) {}
            // No-op deleter, we don't own the pointer
        );

        havel::StopTimer(timer);
        return HavelValue(true);
      }));

  // =========================================================================
  // clearInterval(timerId) - Cancel interval
  // =========================================================================

  (*timerObj)["clearInterval"] = HavelValue(BuiltinFunction(
      [](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty()) {
          return HavelRuntimeError("clearInterval requires timer ID");
        }

        double timerId = args[0].asNumber();
        auto timer = std::shared_ptr<std::atomic<bool>>(
            reinterpret_cast<std::atomic<bool> *>(
                static_cast<uintptr_t>(timerId)),
            [](std::atomic<bool> *) {});

        havel::StopTimer(timer);
        return HavelValue(true);
      }));

  // =========================================================================
  // stopTimer(timerId) - Unified cancel function
  // =========================================================================

  (*timerObj)["stopTimer"] = HavelValue(BuiltinFunction(
      [](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty()) {
          return HavelRuntimeError("stopTimer requires timer ID");
        }

        double timerId = args[0].asNumber();
        auto timer = std::shared_ptr<std::atomic<bool>>(
            reinterpret_cast<std::atomic<bool> *>(
                static_cast<uintptr_t>(timerId)),
            [](std::atomic<bool> *) {});

        havel::StopTimer(timer);
        return HavelValue(true);
      }));

  // Register timer module
  env.Define("timer", HavelValue(timerObj));
}

} // namespace havel::modules
