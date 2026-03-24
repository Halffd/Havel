/*
 * NewTimerModule.cpp - Simplified version
 *
 * Timer module implementation using VMApi system.
 * Provides timer operations with clean host service integration.
 */
#include "NewTimerModule.hpp"
#include <chrono>
#include <functional>
#include <thread>
#include <unordered_map>
#include <cmath>
#include <sstream>

using namespace havel::compiler;

namespace havel::stdlib {

void registerNewTimerModule(havel::compiler::VMApi &api) {
  // Helper: convert BytecodeValue to string
  auto toString = [](const BytecodeValue &v) -> std::string {
    if (std::holds_alternative<std::string>(v))
      return std::get<std::string>(v);
    if (std::holds_alternative<int64_t>(v))
      return std::to_string(std::get<int64_t>(v));
    if (std::holds_alternative<double>(v)) {
      double val = std::get<double>(v);
      if (val == std::floor(val) && std::abs(val) < 1e15) {
        return std::to_string(static_cast<long long>(val));
      }
      std::ostringstream oss;
      oss.precision(15);
      oss << val;
      return oss.str();
    }
    if (std::holds_alternative<bool>(v))
      return std::get<bool>(v) ? "true" : "false";
    return "";
  };

  // Helper: convert BytecodeValue to number
  auto toNumber = [](const BytecodeValue &v) -> double {
    if (std::holds_alternative<int64_t>(v))
      return static_cast<double>(std::get<int64_t>(v));
    if (std::holds_alternative<double>(v))
      return std::get<double>(v);
    return 0.0;
  };

  // Simple timer implementation using C++ chrono
  static std::unordered_map<int, std::function<void()>> timerCallbacks;
  static std::unordered_map<int, bool> timerActive;
  static std::unordered_map<int, std::thread> timerThreads;
  static int nextTimerId = 1;

  // timer.now() - Get current timestamp in milliseconds
  api.registerFunction("timer.now", [](const std::vector<BytecodeValue> &args) -> BytecodeValue {
    if (!args.empty()) {
      throw std::runtime_error("timer.now() takes no arguments");
    }

    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                         now.time_since_epoch())
                         .count();
    return BytecodeValue(timestamp);
  });

  // timer.sleep(ms) - Sleep for specified milliseconds
  api.registerFunction("timer.sleep", [toNumber](const std::vector<BytecodeValue> &args) -> BytecodeValue {
    if (args.size() != 1) {
      throw std::runtime_error(
          "timer.sleep() requires exactly one argument (milliseconds)");
    }

    int64_t ms = static_cast<int64_t>(toNumber(args[0]));
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));

    return BytecodeValue(true);
  });

  // Create timer object with methods
  auto timerObj = api.makeObject();

  // Add methods
  api.setField(timerObj, "now", api.makeFunctionRef("timer.now"));
  api.setField(timerObj, "sleep", api.makeFunctionRef("timer.sleep"));

  // Register global timer object
  api.setGlobal("timer", timerObj);
}

} // namespace havel::stdlib
