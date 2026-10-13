#pragma once

/*
 * time.hpp - Clocks, sleeping and yielding (PLAT-001 section 3).
 *
 * Deliberately built on <chrono> and <thread> rather than raw OS calls.
 * Rationale: this header is meant to be included widely, and on Windows a
 * raw clock primitive means dragging <windows.h> into every translation
 * unit that measures a span. std::chrono::steady_clock is already
 * QueryPerformanceCounter-backed on the MSVC/Windows target and
 * CLOCK_MONOTONIC-backed on glibc, so the native clock we want is what we
 * get without the macro pollution. std::this_thread::sleep_for lowers to
 * nanosleep(2) on POSIX and to a waitable-timer-backed Sleep on Windows.
 *
 * Callers that genuinely need an OS handle (signalfd, timerfd, a
 * Windows waitable timer for sub-millisecond precision) must keep using
 * the OS primitive at their own call site, guarded by
 * HAVEL_PLATFORM_WINDOWS. That is not a gap here; it is the shim being
 * honest about what a portable helper can promise.
 */

#include "havel_platform.h"

#include <chrono>
#include <cstdint>
#include <thread>

namespace havel::platform {

/**
 * Monotonic nanoseconds since an unspecified epoch.
 *
 * Only differences are meaningful - the absolute origin is deliberately
 * unspecified because CLOCK_MONOTONIC and QueryPerformanceCounter disagree
 * about it and code that depends on the origin is already a bug.
 */
inline uint64_t monotonic_now_ns() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

/**
 * Wall-clock nanoseconds since the Unix epoch.
 *
 * Unlike monotonic_now_ns() this can jump when the system clock is adjusted,
 * so use it for timestamps that a user reads, never for measuring elapsed
 * time. For elapsed time use monotonic_now_ns().
 */
inline uint64_t realtime_now_ns() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
}

/** Sleep for at least the given number of nanoseconds. */
inline void sleep_ns(uint64_t ns) {
  std::this_thread::sleep_for(std::chrono::nanoseconds(ns));
}

/** Sleep for at least the given number of milliseconds. */
inline void sleep_ms(uint64_t ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

/**
 * Yield the remainder of the current time slice.
 *
 * sched_yield() on POSIX, SwitchToThread() on Windows. Both are hints; a
 * spinning loop that depends on the yield actually happening is wrong on
 * both platforms.
 */
inline void yield() { std::this_thread::yield(); }

}  // namespace havel::platform
