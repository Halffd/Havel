// sleepUntil() target parsing. Tests computeSleepUntilDelayMs(), the wall-clock
// arithmetic behind the sleepUntil() global verb documented in
// docs/specs/Havel.md.
//
// The function is a pure string -> milliseconds-until-target mapping, so the
// weekday rollover logic is testable without a VM or an actual sleep.
#include "SleepUntilParser.hpp"

#include <chrono>
#include <ctime>
#include <gtest/gtest.h>

using havel::compiler::computeSleepUntilDelayMs;

namespace {
constexpr int64_t kMinuteMs = 60 * 1000;
constexpr int64_t kHourMs = 60 * kMinuteMs;
constexpr int64_t kDayMs = 24 * kHourMs;

int currentWday() {
  const std::time_t now = std::chrono::system_clock::to_time_t(
      std::chrono::system_clock::now());
  std::tm local{};
  localtime_r(&now, &local);
  return local.tm_wday;
}

// Wall-clock time two hours ago, formatted as HH:MM. Used with the matching
// weekday to build a target that has definitely already passed.
std::string timeOfDayTwoHoursAgo() {
  const std::time_t past = std::chrono::system_clock::to_time_t(
      std::chrono::system_clock::now() - std::chrono::hours(2));
  std::tm local{};
  localtime_r(&past, &local);
  char buf[8];
  std::snprintf(buf, sizeof(buf), "%02d:%02d", local.tm_hour, local.tm_min);
  return buf;
}
} // namespace

// A time-only spec is either still ahead today (delay within 24h) or has
// already passed (delay rolls into tomorrow, so just under 24h).
TEST(SleepUntilParser, TimeOnlyIsWithinOneDay) {
  const auto delay = computeSleepUntilDelayMs("13:10");
  ASSERT_TRUE(delay.has_value());
  EXPECT_GT(*delay, 0);
  EXPECT_LE(*delay, kDayMs);
}

// "HH:MM:SS" parses, as does the ".mmm" fraction Havel.md documents for sleep().
TEST(SleepUntilParser, AcceptsSecondsAndMilliseconds) {
  EXPECT_TRUE(computeSleepUntilDelayMs("23:59:59").has_value());
  EXPECT_TRUE(computeSleepUntilDelayMs("0:0:30.500").has_value());
  EXPECT_TRUE(computeSleepUntilDelayMs("0:0:30.5").has_value());
}

// A weekday-qualified target whose time has already passed must roll a full
// seven days, not one. The spec names the weekday of (now - 2h) together with
// (now - 2h)'s own time-of-day, so the target passed exactly 2h ago and its
// next occurrence is 7d - 2h from now. A one-day roll (the bug this pins)
// would yield ~22h instead.
TEST(SleepUntilParser, PassedWeekdayRollsSevenDaysNotOne) {
  const std::time_t past = std::chrono::system_clock::to_time_t(
      std::chrono::system_clock::now() - std::chrono::hours(2));
  std::tm local{};
  localtime_r(&past, &local);
  static const char *kNames[] = {"sunday",   "monday",  "tuesday", "wednesday",
                                 "thursday", "friday",  "saturday"};
  const std::string spec =
      std::string(kNames[local.tm_wday]) + " " + timeOfDayTwoHoursAgo();
  const auto delay = computeSleepUntilDelayMs(spec);
  ASSERT_TRUE(delay.has_value());
  EXPECT_GT(*delay, 7 * kDayMs - 3 * kHourMs);
  EXPECT_LT(*delay, 7 * kDayMs - 1 * kHourMs);
}

// Every full weekday name and three-letter abbreviation must be accepted, and
// must resolve to the same target as each other.
TEST(SleepUntilParser, AllWeekdaySpellingsParse) {
  static const char *kSpecs[] = {
      "sunday 8:00",   "sun 8:00",   "monday 8:00",   "mon 8:00",
      "tuesday 8:00",  "tue 8:00",   "wednesday 8:00", "wed 8:00",
      "thursday 8:00", "thu 8:00",   "friday 8:00",   "fri 8:00",
      "saturday 8:00", "sat 8:00",
  };
  for (const char *spec : kSpecs) {
    const auto delay = computeSleepUntilDelayMs(spec);
    EXPECT_TRUE(delay.has_value()) << spec;
  }
}

// Weekday names are case-insensitive.
TEST(SleepUntilParser, WeekdayIsCaseInsensitive) {
  EXPECT_TRUE(computeSleepUntilDelayMs("THURSDAY 8:00").has_value());
  EXPECT_TRUE(computeSleepUntilDelayMs("Thu 8:00").has_value());
}

// A weekday-qualified target for tomorrow's weekday at 23:59 is between 0 and
// 48h away (now just after midnight => nearly 48h), so the bound is 2 days.
TEST(SleepUntilParser, WeekdayAheadOfTodayIsUnderSevenDays) {
  const int tomorrow = (currentWday() + 1) % 7;
  static const char *kNames[] = {"sunday",   "monday", "tuesday", "wednesday",
                                 "thursday", "friday", "saturday"};
  const std::string spec =
      std::string(kNames[tomorrow]) + " 23:59";
  const auto delay = computeSleepUntilDelayMs(spec);
  ASSERT_TRUE(delay.has_value());
  EXPECT_GT(*delay, 0);
  EXPECT_LE(*delay, 2 * kDayMs);
}

// Malformed specs must be rejected rather than silently defaulted.
TEST(SleepUntilParser, RejectsMalformedSpecs) {
  EXPECT_FALSE(computeSleepUntilDelayMs("").has_value());
  EXPECT_FALSE(computeSleepUntilDelayMs("garbage").has_value());
  EXPECT_FALSE(computeSleepUntilDelayMs("25:00").has_value());
  EXPECT_FALSE(computeSleepUntilDelayMs("13:99").has_value());
  EXPECT_FALSE(computeSleepUntilDelayMs("13:10:99").has_value());
  EXPECT_FALSE(computeSleepUntilDelayMs("13").has_value());
  EXPECT_FALSE(computeSleepUntilDelayMs("thursday").has_value());
  EXPECT_FALSE(computeSleepUntilDelayMs("funday 8:00").has_value());
  EXPECT_FALSE(computeSleepUntilDelayMs("13:10junk").has_value());
  EXPECT_FALSE(computeSleepUntilDelayMs("13:10.abc").has_value());
}
