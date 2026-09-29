/* SleepUntilParser.hpp - wall-clock target parsing for sleepUntil() */
#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace havel::compiler {

// Parse a sleepUntil() target and return the delay from now until its next
// occurrence, in milliseconds. Accepts an optional leading weekday name (full
// or three-letter abbreviation, case-insensitive) followed by a wall-clock
// time:
//
//   "13:10", "23:59:59", "0:0:30.500", "thursday 8:00", "mon 9:00"
//
// A time-only spec targets today when it is still in the future, otherwise
// tomorrow. A weekday-qualified spec targets that weekday, rolling to the
// following week once the time has passed. Returns nullopt when the spec is
// malformed (unknown weekday, missing or out-of-range time, trailing junk).
std::optional<int64_t> computeSleepUntilDelayMs(const std::string &spec);

} // namespace havel::compiler
