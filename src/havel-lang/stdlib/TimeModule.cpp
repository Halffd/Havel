/* TimeModule.cpp - VM-native stdlib module */
#include "TimeModule.hpp"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <thread>

using havel::compiler::BytecodeValue;
using havel::compiler::VMApi;

namespace havel::stdlib {

// Register time module with VMApi (stable API layer)
void registerTimeModule(VMApi &api) {
  // Get current timestamp
  api.registerFunction("time.now", [](const std::vector<BytecodeValue> &args) {
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                         now.time_since_epoch())
                         .count();
    return BytecodeValue(static_cast<int64_t>(timestamp));
  });

  // Format timestamp as string
  api.registerFunction(
      "time.format", [](const std::vector<BytecodeValue> &args) {
        if (args.empty())
          throw std::runtime_error("time.format() requires timestamp");

        int64_t timestamp = 0;
        if (std::holds_alternative<int64_t>(args[0])) {
          timestamp = std::get<int64_t>(args[0]);
        } else if (std::holds_alternative<double>(args[0])) {
          timestamp = static_cast<int64_t>(std::get<double>(args[0]));
        } else if (std::holds_alternative<std::string>(args[0])) {
          timestamp = std::stoll(std::get<std::string>(args[0]));
        } else {
          throw std::runtime_error("time.format() requires numeric timestamp");
        }

        std::time_t time = timestamp / 1000;
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
        return BytecodeValue(ss.str());
      });

  // Sleep for specified milliseconds
  api.registerFunction(
      "time.sleep", [](const std::vector<BytecodeValue> &args) {
        if (args.empty())
          throw std::runtime_error("time.sleep() requires milliseconds");

        int64_t ms = 0;
        if (std::holds_alternative<int64_t>(args[0])) {
          ms = std::get<int64_t>(args[0]);
        } else if (std::holds_alternative<double>(args[0])) {
          ms = static_cast<int64_t>(std::get<double>(args[0]));
        } else if (std::holds_alternative<std::string>(args[0])) {
          ms = std::stoll(std::get<std::string>(args[0]));
        } else {
          throw std::runtime_error(
              "time.sleep() requires numeric milliseconds");
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        return BytecodeValue(nullptr);
      });

  // Advanced: time.hour - get current hour
  api.registerFunction("time.hour", [](const std::vector<BytecodeValue> &args) {
    auto now = std::chrono::system_clock::now();
    std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm *tm = std::localtime(&time);
    return BytecodeValue(static_cast<int64_t>(tm->tm_hour));
  });

  // Advanced: time.minute - get current minute
  api.registerFunction(
      "time.minute", [](const std::vector<BytecodeValue> &args) {
        auto now = std::chrono::system_clock::now();
        std::time_t time = std::chrono::system_clock::to_time_t(now);
        std::tm *tm = std::localtime(&time);
        return BytecodeValue(static_cast<int64_t>(tm->tm_min));
      });

  // Advanced: time.second - get current second
  api.registerFunction(
      "time.second", [](const std::vector<BytecodeValue> &args) {
        auto now = std::chrono::system_clock::now();
        std::time_t time = std::chrono::system_clock::to_time_t(now);
        std::tm *tm = std::localtime(&time);
        return BytecodeValue(static_cast<int64_t>(tm->tm_sec));
      });

  // Register time object
  auto timeObj = api.makeObject();
  api.setField(timeObj, "now", api.makeFunctionRef("time.now"));
  api.setField(timeObj, "format", api.makeFunctionRef("time.format"));
  api.setField(timeObj, "sleep", api.makeFunctionRef("time.sleep"));
  api.setField(timeObj, "hour", api.makeFunctionRef("time.hour"));
  api.setField(timeObj, "minute", api.makeFunctionRef("time.minute"));
  api.setField(timeObj, "second", api.makeFunctionRef("time.second"));
  api.setGlobal("Time", timeObj);
}

} // namespace havel::stdlib
