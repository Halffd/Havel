// TimeModule.cpp
// Time and date operations for Havel standard library

#include "TimeModule.hpp"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <thread>

namespace havel::stdlib {

void registerTimeModule(Environment &env) {
  // ==========================================================================
  // time.now() - Get current timestamp (seconds since epoch)
  // ==========================================================================
  env.Define("now",
             HavelValue(makeBuiltinFunction(
                 [](const std::vector<HavelValue> &) -> HavelResult {
                   auto now = std::chrono::system_clock::now();
                   auto epoch = now.time_since_epoch();
                   auto seconds =
                       std::chrono::duration_cast<std::chrono::seconds>(epoch);
                   return HavelValue(static_cast<double>(seconds.count()));
                 })));

  // ==========================================================================
  // time.nowMs() - Get current timestamp in milliseconds
  // ==========================================================================
  env.Define("nowMs",
             HavelValue(makeBuiltinFunction(
                 [](const std::vector<HavelValue> &) -> HavelResult {
                   auto now = std::chrono::system_clock::now();
                   auto epoch = now.time_since_epoch();
                   auto ms =
                       std::chrono::duration_cast<std::chrono::milliseconds>(
                           epoch);
                   return HavelValue(static_cast<double>(ms.count()));
                 })));

  // ==========================================================================
  // time.sleep(seconds) - Sleep for specified seconds
  // ==========================================================================
  env.Define(
      "sleep",
      HavelValue(makeBuiltinFunction(
          [](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.empty()) {
              return HavelRuntimeError("sleep() requires duration in seconds");
            }

            double seconds = args[0].asNumber();
            if (seconds < 0) {
              return HavelRuntimeError("sleep() duration must be non-negative");
            }

            std::this_thread::sleep_for(
                std::chrono::milliseconds(static_cast<int>(seconds * 1000)));
            return HavelValue(true);
          })));

  // ==========================================================================
  // time.sleepMs(milliseconds) - Sleep for specified milliseconds
  // ==========================================================================
  env.Define("sleepMs",
             HavelValue(makeBuiltinFunction(
                 [](const std::vector<HavelValue> &args) -> HavelResult {
                   if (args.empty()) {
                     return HavelRuntimeError(
                         "sleepMs() requires duration in milliseconds");
                   }

                   double ms = args[0].asNumber();
                   if (ms < 0) {
                     return HavelRuntimeError(
                         "sleepMs() duration must be non-negative");
                   }

                   std::this_thread::sleep_for(
                       std::chrono::milliseconds(static_cast<int>(ms)));
                   return HavelValue(true);
                 })));

  // ==========================================================================
  // time.format(timestamp, format) - Format timestamp to string
  // ==========================================================================
  env.Define("format",
             HavelValue(makeBuiltinFunction(
                 [](const std::vector<HavelValue> &args) -> HavelResult {
                   if (args.empty()) {
                     return HavelRuntimeError("format() requires timestamp");
                   }

                   double timestamp = args[0].asNumber();
                   std::string format = args.size() > 1 ? args[1].asString()
                                                        : "%Y-%m-%d %H:%M:%S";

                   time_t time = static_cast<time_t>(timestamp);
                   struct tm *tm_info = localtime(&time);

                   char buffer[256];
                   strftime(buffer, sizeof(buffer), format.c_str(), tm_info);

                   return HavelValue(std::string(buffer));
                 })));

  // ==========================================================================
  // time.parse(string, format) - Parse string to timestamp
  // ==========================================================================
  env.Define("parse",
             HavelValue(makeBuiltinFunction(
                 [](const std::vector<HavelValue> &args) -> HavelResult {
                   if (args.empty()) {
                     return HavelRuntimeError("parse() requires time string");
                   }

                   std::string timeStr = args[0].asString();
                   std::string format = args.size() > 1 ? args[1].asString()
                                                        : "%Y-%m-%d %H:%M:%S";

                   struct tm tm = {};
                   std::istringstream ss(timeStr);
                   ss >> std::get_time(&tm, format.c_str());

                   if (ss.fail()) {
                     return HavelRuntimeError("parse() failed to parse: " +
                                              timeStr);
                   }

                   time_t timestamp = mktime(&tm);
                   return HavelValue(static_cast<double>(timestamp));
                 })));

  // ==========================================================================
  // time.year(timestamp) - Get year from timestamp
  // ==========================================================================
  env.Define("year",
             HavelValue(makeBuiltinFunction(
                 [](const std::vector<HavelValue> &args) -> HavelResult {
                   double timestamp =
                       args.empty() ? time(nullptr) : args[0].asNumber();
                   time_t time = static_cast<time_t>(timestamp);
                   struct tm *tm_info = localtime(&time);
                   return HavelValue(tm_info->tm_year + 1900);
                 })));

  // ==========================================================================
  // time.month(timestamp) - Get month from timestamp (1-12)
  // ==========================================================================
  env.Define("month",
             HavelValue(makeBuiltinFunction(
                 [](const std::vector<HavelValue> &args) -> HavelResult {
                   double timestamp =
                       args.empty() ? time(nullptr) : args[0].asNumber();
                   time_t time = static_cast<time_t>(timestamp);
                   struct tm *tm_info = localtime(&time);
                   return HavelValue(tm_info->tm_mon + 1);
                 })));

  // ==========================================================================
  // time.day(timestamp) - Get day from timestamp (1-31)
  // ==========================================================================
  env.Define("day", HavelValue(makeBuiltinFunction(
                        [](const std::vector<HavelValue> &args) -> HavelResult {
                          double timestamp =
                              args.empty() ? time(nullptr) : args[0].asNumber();
                          time_t time = static_cast<time_t>(timestamp);
                          struct tm *tm_info = localtime(&time);
                          return HavelValue(tm_info->tm_mday);
                        })));

  // ==========================================================================
  // time.hour(timestamp) - Get hour from timestamp (0-23)
  // ==========================================================================
  env.Define("hour",
             HavelValue(makeBuiltinFunction(
                 [](const std::vector<HavelValue> &args) -> HavelResult {
                   double timestamp =
                       args.empty() ? time(nullptr) : args[0].asNumber();
                   time_t time = static_cast<time_t>(timestamp);
                   struct tm *tm_info = localtime(&time);
                   return HavelValue(tm_info->tm_hour);
                 })));

  // ==========================================================================
  // time.minute(timestamp) - Get minute from timestamp (0-59)
  // ==========================================================================
  env.Define("minute",
             HavelValue(makeBuiltinFunction(
                 [](const std::vector<HavelValue> &args) -> HavelResult {
                   double timestamp =
                       args.empty() ? time(nullptr) : args[0].asNumber();
                   time_t time = static_cast<time_t>(timestamp);
                   struct tm *tm_info = localtime(&time);
                   return HavelValue(tm_info->tm_min);
                 })));

  // ==========================================================================
  // time.second(timestamp) - Get second from timestamp (0-59)
  // ==========================================================================
  env.Define("second",
             HavelValue(makeBuiltinFunction(
                 [](const std::vector<HavelValue> &args) -> HavelResult {
                   double timestamp =
                       args.empty() ? time(nullptr) : args[0].asNumber();
                   time_t time = static_cast<time_t>(timestamp);
                   struct tm *tm_info = localtime(&time);
                   return HavelValue(tm_info->tm_sec);
                 })));

  // ==========================================================================
  // time.weekday(timestamp) - Get weekday from timestamp (0=Sunday, 6=Saturday)
  // ==========================================================================
  env.Define("weekday",
             HavelValue(makeBuiltinFunction(
                 [](const std::vector<HavelValue> &args) -> HavelResult {
                   double timestamp =
                       args.empty() ? time(nullptr) : args[0].asNumber();
                   time_t time = static_cast<time_t>(timestamp);
                   struct tm *tm_info = localtime(&time);
                   return HavelValue(tm_info->tm_wday);
                 })));

  // ==========================================================================
  // time.yearday(timestamp) - Get day of year (0-365)
  // ==========================================================================
  env.Define("yearday",
             HavelValue(makeBuiltinFunction(
                 [](const std::vector<HavelValue> &args) -> HavelResult {
                   double timestamp =
                       args.empty() ? time(nullptr) : args[0].asNumber();
                   time_t time = static_cast<time_t>(timestamp);
                   struct tm *tm_info = localtime(&time);
                   return HavelValue(tm_info->tm_yday);
                 })));

  // ==========================================================================
  // time.isLeapYear(year) - Check if year is a leap year
  // ==========================================================================
  env.Define("isLeapYear",
             HavelValue(makeBuiltinFunction(
                 [](const std::vector<HavelValue> &args) -> HavelResult {
                   if (args.empty()) {
                     return HavelRuntimeError("isLeapYear() requires year");
                   }

                   int year = static_cast<int>(args[0].asNumber());
                   bool isLeap =
                       (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
                   return HavelValue(isLeap);
                 })));

  // ==========================================================================
  // time.daysInMonth(year, month) - Get number of days in month
  // ==========================================================================
  env.Define(
      "daysInMonth",
      HavelValue(makeBuiltinFunction([](const std::vector<HavelValue> &args)
                                         -> HavelResult {
        if (args.size() < 2) {
          return HavelRuntimeError("daysInMonth() requires year and month");
        }

        int year = static_cast<int>(args[0].asNumber());
        int month = static_cast<int>(args[1].asNumber());

        if (month < 1 || month > 12) {
          return HavelRuntimeError("daysInMonth() month must be 1-12");
        }

        int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        int days = daysInMonth[month - 1];

        // Check for leap year if February
        if (month == 2) {
          bool isLeap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
          if (isLeap) {
            days = 29;
          }
        }

        return HavelValue(days);
      })));

  // ==========================================================================
  // time.diff(timestamp1, timestamp2) - Get difference in seconds
  // ==========================================================================
  env.Define("diff",
             HavelValue(makeBuiltinFunction(
                 [](const std::vector<HavelValue> &args) -> HavelResult {
                   if (args.size() < 2) {
                     return HavelRuntimeError("diff() requires two timestamps");
                   }

                   double t1 = args[0].asNumber();
                   double t2 = args[1].asNumber();

                   return HavelValue(t2 - t1);
                 })));

  // ==========================================================================
  // time.diffDays(timestamp1, timestamp2) - Get difference in days
  // ==========================================================================
  env.Define("diffDays",
             HavelValue(makeBuiltinFunction(
                 [](const std::vector<HavelValue> &args) -> HavelResult {
                   if (args.size() < 2) {
                     return HavelRuntimeError(
                         "diffDays() requires two timestamps");
                   }

                   double t1 = args[0].asNumber();
                   double t2 = args[1].asNumber();

                   double diffSeconds = t2 - t1;
                   double diffDays = diffSeconds / (24 * 60 * 60);

                   return HavelValue(diffDays);
                 })));

  // ==========================================================================
  // time.toISO(timestamp) - Convert to ISO 8601 format
  // ==========================================================================
  env.Define("toISO",
             HavelValue(makeBuiltinFunction(
                 [](const std::vector<HavelValue> &args) -> HavelResult {
                   double timestamp =
                       args.empty() ? time(nullptr) : args[0].asNumber();
                   time_t time = static_cast<time_t>(timestamp);
                   struct tm *tm_info = gmtime(&time);

                   char buffer[256];
                   strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ",
                            tm_info);

                   return HavelValue(std::string(buffer));
                 })));

  // ==========================================================================
  // time.fromISO(string) - Parse ISO 8601 format
  // ==========================================================================
  env.Define(
      "fromISO",
      HavelValue(makeBuiltinFunction(
          [](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.empty()) {
              return HavelRuntimeError("fromISO() requires ISO 8601 string");
            }

            std::string isoStr = args[0].asString();
            struct tm tm = {};
            std::istringstream ss(isoStr);
            ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");

            if (ss.fail()) {
              return HavelRuntimeError("fromISO() failed to parse: " + isoStr);
            }

            time_t timestamp = mktime(&tm);
            return HavelValue(static_cast<double>(timestamp));
          })));
}

} // namespace havel::stdlib
