/* TimeModule.cpp - VM-native stdlib module (pure timestamp operations) */
#include "TimeModule.hpp"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

using havel::compiler::Value;
using havel::compiler::VMApi;

namespace havel::stdlib {

// Register time module with VMApi (stable API layer)
// NOTE: time.sleep is now provided by the host layer (AsyncService)
void registerTimeModule(VMApi &api) {
  // Get current timestamp
  api.registerFunction("time.now", [](const std::vector<Value> &args) {
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                         now.time_since_epoch())
                         .count();
    return Value(static_cast<int64_t>(timestamp));
  });

  // Format timestamp as string
  api.registerFunction(
      "time.format", [](const std::vector<Value> &args) {
        if (args.empty())
          throw std::runtime_error("time.format() requires timestamp");

        int64_t timestamp = 0;
        if (args[0].isInt()) {
          timestamp = args[0].asInt();
        } else if (args[0].isDouble()) {
          timestamp = static_cast<int64_t>(args[0].asDouble());
        } else if (args[0].isStringValId()) {
          // TODO: string pool lookup
          timestamp = std::stoll("<string:" + std::to_string(args[0].asStringValId()) + ">");
        } else {
          throw std::runtime_error("time.format() requires numeric timestamp");
        }

        std::time_t time = timestamp / 1000;
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
        // TODO: string pool integration - for now return null
        (void)ss;
        return Value::makeNull();
      });

  // Advanced: time.hour - get current hour
  api.registerFunction("time.hour", [](const std::vector<Value> &args) {
    auto now = std::chrono::system_clock::now();
    std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm *tm = std::localtime(&time);
    return Value(static_cast<int64_t>(tm->tm_hour));
  });

  // Advanced: time.minute - get current minute
  api.registerFunction(
      "time.minute", [](const std::vector<Value> &args) {
        auto now = std::chrono::system_clock::now();
        std::time_t time = std::chrono::system_clock::to_time_t(now);
        std::tm *tm = std::localtime(&time);
        return Value(static_cast<int64_t>(tm->tm_min));
      });

  // Advanced: time.second - get current second
  api.registerFunction(
      "time.second", [](const std::vector<Value> &args) {
        auto now = std::chrono::system_clock::now();
        std::time_t time = std::chrono::system_clock::to_time_t(now);
        std::tm *tm = std::localtime(&time);
        return Value(static_cast<int64_t>(tm->tm_sec));
      });

  // Get current date as string
  api.registerFunction(
      "time.date", [](const std::vector<Value> &args) {
        auto now = std::chrono::system_clock::now();
        std::time_t time = std::chrono::system_clock::to_time_t(now);
        std::tm *tm = std::localtime(&time);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d", tm);
        // For now return a string via the vm
        return Value::makeNull();
      });

  // Register time object (no sleep - that's in host layer)
  auto timeObj = api.makeObject();
  api.setField(timeObj, "now", api.makeFunctionRef("time.now"));
  api.setField(timeObj, "format", api.makeFunctionRef("time.format"));
  api.setField(timeObj, "hour", api.makeFunctionRef("time.hour"));
  api.setField(timeObj, "minute", api.makeFunctionRef("time.minute"));
  api.setField(timeObj, "second", api.makeFunctionRef("time.second"));
  api.setField(timeObj, "date", api.makeFunctionRef("time.date"));
  api.setGlobal("Time", timeObj);
}

} // namespace havel::stdlib
