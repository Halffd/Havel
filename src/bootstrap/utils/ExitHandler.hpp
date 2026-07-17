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

void exit(ExitReason reason, int code = 0);

void registerExitCleanup(std::function<void()> fn);

bool isExiting();

} // namespace havel
