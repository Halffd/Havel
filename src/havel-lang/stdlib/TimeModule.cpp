/* TimeModule.cpp - VM-native stdlib module (time/date operations) */
#include "TimeModule.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <thread>

#include "havel-lang/core/Value.hpp"
#include "havel-lang/runtime/concurrency/Fiber.hpp"
#include "havel-lang/runtime/concurrency/Scheduler.hpp"

using havel::compiler::Value;
using havel::compiler::VMApi;

namespace havel::stdlib {

// Helper: get current tm struct
static std::tm getLocalTm() {
  auto now = std::chrono::system_clock::now();
  std::time_t time = std::chrono::system_clock::to_time_t(now);
  return *std::localtime(&time);
}

// Helper: get timestamp in milliseconds from args
static int64_t getTimestampMs(const std::vector<Value> &args) {
  if (args.empty())
    throw std::runtime_error("time: requires timestamp argument");
  if (args[0].isInt())
    return args[0].asInt();
  if (args[0].isDouble())
    return static_cast<int64_t>(args[0].asDouble());
  throw std::runtime_error("time: timestamp must be numeric");
}

void registerTimeModule(const VMApi &api) {
  // time.now() — current epoch in milliseconds
  api.registerFunction("time.now",
                       [](const std::vector<Value> &args) {
                         (void)args;
                         auto now = std::chrono::system_clock::now();
                         auto ms = std::chrono::duration_cast<
                                       std::chrono::milliseconds>(
                                       now.time_since_epoch())
                                       .count();
                         return Value(static_cast<int64_t>(ms));
                       });

  // time.epoch() — Unix epoch in seconds
  api.registerFunction("time.epoch",
                       [](const std::vector<Value> &args) {
                         (void)args;
                         auto now = std::chrono::system_clock::now();
                         auto secs = std::chrono::duration_cast<
                                        std::chrono::seconds>(
                                        now.time_since_epoch())
                                        .count();
                         return Value(static_cast<int64_t>(secs));
                       });

  // time.millis() — same as time.now (milliseconds since epoch)
  api.registerFunction("time.millis",
                       [](const std::vector<Value> &args) {
                         (void)args;
                         auto now = std::chrono::system_clock::now();
                         auto ms = std::chrono::duration_cast<
                                       std::chrono::milliseconds>(
                                       now.time_since_epoch())
                                       .count();
                         return Value(static_cast<int64_t>(ms));
                       });

  // time.format(timestamp, format?) — format timestamp as string
  api.registerFunction(
      "time.format", [api](const std::vector<Value> &args) {
        int64_t timestamp = getTimestampMs(args);
        std::time_t time = timestamp / 1000;
        std::string fmt = "%Y-%m-%d %H:%M:%S";
        if (args.size() > 1) {
          fmt = api.resolveString(args[1]);
        }
        std::tm *tm = std::localtime(&time);
        char buf[256];
        std::strftime(buf, sizeof(buf), fmt.c_str(), tm);
        return api.makeString(buf);
      });

  // time.parse(datestr, format?) — parse date string to millisecond
  // timestamp
  api.registerFunction(
      "time.parse", [api](const std::vector<Value> &args) {
        if (args.empty())
          throw std::runtime_error("time.parse() requires date string");
        std::string datestr = api.resolveString(args[0]);
        std::string fmt = "%Y-%m-%d %H:%M:%S";
        if (args.size() > 1) {
          fmt = api.resolveString(args[1]);
        }
        std::tm tm = {};
#ifndef _WIN32
        char *result = strptime(datestr.c_str(), fmt.c_str(), &tm);
        if (!result)
          return Value::makeNull();
#else
        std::istringstream ss(datestr);
        ss >> std::get_time(&tm, fmt.c_str());
        if (ss.fail())
          return Value::makeNull();
#endif
        std::time_t time = std::mktime(&tm);
        return Value(static_cast<int64_t>(time) * 1000);
      });

  // time.sleep(ms) — non-blocking if in a goroutine
  api.registerFunction(
      "time.sleep", [api](const std::vector<Value> &args) {
        if (args.empty())
          throw std::runtime_error("time.sleep() requires milliseconds");
        int64_t ms = 0;
        if (args[0].isInt())
          ms = args[0].asInt();
        else if (args[0].isDouble())
          ms = static_cast<int64_t>(args[0].asDouble());
        else
          throw std::runtime_error("time.sleep() requires numeric argument");
        
        
        // If we are running in a goroutine, suspend instead of blocking the thread
  if (api.isInGoroutine()) {
    api.requestSuspension(static_cast<uint8_t>(havel::compiler::SuspensionReason::SLEEP),
                                   reinterpret_cast<void*>(static_cast<intptr_t>(ms)));
            return Value::makeNull();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        return Value::makeNull();
      });

  // time.year() — current year
  api.registerFunction("time.year", [](const std::vector<Value> &args) {
    (void)args;
    auto tm = getLocalTm();
    return Value(static_cast<int64_t>(tm.tm_year + 1900));
  });

  // time.month() — current month (1-12)
  api.registerFunction("time.month", [](const std::vector<Value> &args) {
    (void)args;
    auto tm = getLocalTm();
    return Value(static_cast<int64_t>(tm.tm_mon + 1));
  });

  // time.day() — current day of month (1-31)
  api.registerFunction("time.day", [](const std::vector<Value> &args) {
    (void)args;
    auto tm = getLocalTm();
    return Value(static_cast<int64_t>(tm.tm_mday));
  });

  // time.hour() — current hour (0-23)
  api.registerFunction("time.hour", [](const std::vector<Value> &args) {
    (void)args;
    auto tm = getLocalTm();
    return Value(static_cast<int64_t>(tm.tm_hour));
  });

  // time.minute() — current minute (0-59)
  api.registerFunction("time.minute", [](const std::vector<Value> &args) {
    (void)args;
    auto tm = getLocalTm();
    return Value(static_cast<int64_t>(tm.tm_min));
  });

  // time.second() — current second (0-59)
  api.registerFunction("time.second", [](const std::vector<Value> &args) {
    (void)args;
    auto tm = getLocalTm();
    return Value(static_cast<int64_t>(tm.tm_sec));
  });

  // time.weekday() — day of week (0=Sunday, 6=Saturday)
  api.registerFunction("time.weekday", [](const std::vector<Value> &args) {
    (void)args;
    auto tm = getLocalTm();
    return Value(static_cast<int64_t>(tm.tm_wday));
  });

  // time.date() — current date as "YYYY-MM-DD" string
  api.registerFunction("time.date", [api](const std::vector<Value> &args) {
    (void)args;
    auto tm = getLocalTm();
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return api.makeString(buf);
  });

  // time.time() — current time as "HH:MM:SS" string
  api.registerFunction("time.time", [api](const std::vector<Value> &args) {
    (void)args;
    auto tm = getLocalTm();
    char buf[32];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
    return api.makeString(buf);
  });

  // Build namespace object
  auto timeObj = api.makeObject();
  api.setField(timeObj, "now", api.makeFunctionRef("time.now"));
  api.setField(timeObj, "epoch", api.makeFunctionRef("time.epoch"));
  api.setField(timeObj, "millis", api.makeFunctionRef("time.millis"));
  api.setField(timeObj, "format", api.makeFunctionRef("time.format"));
  api.setField(timeObj, "parse", api.makeFunctionRef("time.parse"));
  api.setField(timeObj, "sleep", api.makeFunctionRef("time.sleep"));
  api.setField(timeObj, "year", api.makeFunctionRef("time.year"));
  api.setField(timeObj, "month", api.makeFunctionRef("time.month"));
  api.setField(timeObj, "day", api.makeFunctionRef("time.day"));
  api.setField(timeObj, "hour", api.makeFunctionRef("time.hour"));
  api.setField(timeObj, "minute", api.makeFunctionRef("time.minute"));
  api.setField(timeObj, "second", api.makeFunctionRef("time.second"));
  api.setField(timeObj, "weekday", api.makeFunctionRef("time.weekday"));
  api.setField(timeObj, "date", api.makeFunctionRef("time.date"));
  api.setField(timeObj, "time", api.makeFunctionRef("time.time"));
  api.setGlobal("time", timeObj);
}

} // namespace havel::stdlib
