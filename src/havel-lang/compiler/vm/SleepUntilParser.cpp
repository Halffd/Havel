/* SleepUntilParser.cpp - wall-clock target parsing for sleepUntil() */

#include "SleepUntilParser.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <sstream>
#include <utility>

namespace havel::compiler {
namespace {

std::string toLowerAscii(std::string s) {
  for (char &c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

// Split a spec into an optional weekday index (0 = Sunday, matching struct tm)
// and the remaining time text. target_wday stays -1 when no weekday is given.
bool splitWeekday(const std::string &spec, int &target_wday,
                  std::string &time_part) {
  target_wday = -1;
  time_part = spec;

  std::istringstream in(spec);
  std::string head;
  if (!(in >> head)) {
    return false;
  }

  static const std::pair<const char *, int> kWeekdays[] = {
      {"sunday", 0},   {"sun", 0},   {"monday", 1},   {"mon", 1},
      {"tuesday", 2},  {"tue", 2},   {"wednesday", 3}, {"wed", 3},
      {"thursday", 4}, {"thu", 4},   {"friday", 5},   {"fri", 5},
      {"saturday", 6}, {"sat", 6},
  };
  const std::string lower = toLowerAscii(head);
  for (const auto &entry : kWeekdays) {
    if (lower == entry.first) {
      target_wday = entry.second;
      break;
    }
  }

  if (target_wday < 0) {
    return true;
  }
  // Weekday present: the time must follow it.
  if (!(in >> time_part)) {
    return false;
  }
  return true;
}

// Parse H:MM[:SS[.mmm]] into hour/minute/second plus the fractional part kept
// out of band, because mktime() only has second resolution.
bool parseClockTime(const std::string &text, int &hour, int &minute,
                    int &second, int &millis) {
  hour = 0;
  minute = 0;
  second = 0;
  millis = 0;

  std::istringstream in(text);
  in >> hour;
  if (in.fail() || hour < 0 || hour > 23) {
    return false;
  }

  if (in.peek() == ':') {
    in.get();
    in >> minute;
    if (in.fail() || minute < 0 || minute > 59) {
      return false;
    }
  } else {
    return false; // a bare hour is not a valid target
  }

  if (in.peek() == ':') {
    in.get();
    in >> second;
    if (in.fail() || second < 0 || second > 59) {
      return false;
    }
    if (in.peek() == '.') {
      in.get();
      std::string frac;
      if (!(in >> frac) || frac.empty()) {
        return false;
      }
      for (char c : frac) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
          return false;
        }
      }
      frac.resize(std::min<size_t>(3, frac.size()), '0');
      millis = std::stoi(frac);
    }
  }

  std::string trailing;
  if (in >> trailing) {
    return false; // junk after the time
  }
  return true;
}

} // namespace

std::optional<int64_t> computeSleepUntilDelayMs(const std::string &spec) {
  int target_wday = -1;
  std::string time_part;
  if (!splitWeekday(spec, target_wday, time_part)) {
    return std::nullopt;
  }

  int hour = 0;
  int minute = 0;
  int second = 0;
  int millis = 0;
  if (!parseClockTime(time_part, hour, minute, second, millis)) {
    return std::nullopt;
  }

  const auto now = std::chrono::system_clock::now();
  const std::time_t now_t = std::chrono::system_clock::to_time_t(now);
  std::tm today{};
  if (!localtime_r(&now_t, &today)) {
    return std::nullopt;
  }

  // Build the target for `day_offset` days from today. mktime() normalises
  // month/year rollover and resolves DST for the resulting wall-clock time.
  auto build_at = [&](int day_offset) -> std::time_t {
    std::tm t = today;
    t.tm_mday += day_offset;
    t.tm_hour = hour;
    t.tm_min = minute;
    t.tm_sec = second;
    t.tm_isdst = -1;
    return std::mktime(&t);
  };

  int day_offset = 0;
  if (target_wday >= 0) {
    day_offset = (target_wday - today.tm_wday + 7) % 7;
  }
  std::time_t target_t = build_at(day_offset);
  if (target_t <= now_t) {
    // Already passed: roll forward to the next matching day.
    day_offset += (target_wday >= 0) ? 7 : 1;
    target_t = build_at(day_offset);
  }

  const int64_t target_ms = static_cast<int64_t>(target_t) * 1000 + millis;
  const int64_t now_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch())
          .count();
  const int64_t delta_ms = target_ms - now_ms;
  if (delta_ms < 0) {
    return std::nullopt;
  }
  return delta_ms;
}

} // namespace havel::compiler
