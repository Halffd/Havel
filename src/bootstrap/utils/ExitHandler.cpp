#include "ExitHandler.hpp"
#include "Logger.hpp"
#include <atomic>
#include <cstdlib>
#include <unistd.h>
#include <cstring>

namespace havel {

void EmergencyUngrabAllEvdevSignalSafe();

static std::atomic<bool> g_exiting{false};
static std::function<void()> g_cleanup_fn;

void registerExitCleanup(std::function<void()> fn) {
  g_cleanup_fn = std::move(fn);
}

bool isExiting() { return g_exiting.load(std::memory_order_acquire); }

static const char *reasonStr(ExitReason r) {
  switch (r) {
  case ExitReason::Normal:      return "normal";
  case ExitReason::SignalInt:   return "SIGINT";
  case ExitReason::SignalTerm:  return "SIGTERM";
  case ExitReason::SignalQuit:  return "SIGQUIT";
  case ExitReason::SignalCrash: return "crash";
  case ExitReason::Exception:   return "exception";
  case ExitReason::Forced:      return "forced";
  case ExitReason::VmExit:      return "vm_exit";
  }
  return "?";
}

static bool isSignalContext(ExitReason reason) {
  return reason == ExitReason::SignalInt ||
         reason == ExitReason::SignalTerm ||
         reason == ExitReason::SignalQuit ||
         reason == ExitReason::SignalCrash;
}

static void writeStderr(const char *msg) {
  auto len = strlen(msg);
  ssize_t unused = write(STDERR_FILENO, msg, len);
  (void)unused;
}

void exit(ExitReason reason, int code) {
  if (g_exiting.exchange(true)) {
    if (isSignalContext(reason)) {
      _exit(code);
    }
    return;
  }

  if (isSignalContext(reason)) {
    writeStderr("havel::exit reason=");
    writeStderr(reasonStr(reason));
    writeStderr("\n");
    EmergencyUngrabAllEvdevSignalSafe();
    _exit(code);
  }

  info("havel::exit reason={} code={}", reasonStr(reason), code);

  EmergencyUngrabAllEvdevSignalSafe();

  if (g_cleanup_fn) {
    g_cleanup_fn();
  }

  std::exit(code);
}

} // namespace havel
