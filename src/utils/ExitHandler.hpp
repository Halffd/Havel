#pragma once

#include <csignal>
#include <functional>

namespace havel {

enum class ExitReason {
    Normal,
    SignalInt,
    SignalTerm,
    SignalQuit,
    SignalCrash,
    Exception,
    Forced,
    VmExit,
};

// Exported: plugins (havel_mod_*.so) call havel::exit() to terminate the
// program; release builds hide symbols by default, so without the explicit
// visibility the plugin dlopen fails with an undefined symbol error.
[[noreturn]] __attribute__((visibility("default"))) void exit(ExitReason reason, int code = 0);

void registerExitCleanup(std::function<void()> fn);

void runExitCleanups();

bool isExiting();

} // namespace havel
