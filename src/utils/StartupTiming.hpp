#pragma once
#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace havel {

inline bool startup_timing_enabled() {
    static bool enabled = (std::getenv("HAVEL_STARTUP_TIMING") != nullptr);
    return enabled;
}

inline void startup_timing_report(const char* label, std::chrono::steady_clock::time_point since) {
    if (!startup_timing_enabled()) return;
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - since).count();
    fprintf(stderr, "[startup] %s = %.2fms\n", label, us / 1000.0);
    fflush(stderr);
}

inline std::chrono::steady_clock::time_point startup_now() {
    return std::chrono::steady_clock::now();
}

}
