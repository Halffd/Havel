#pragma once

/*
 * thread.hpp - Thread naming and affinity hints (PLAT-001 section 4).
 *
 * The scheduler, fibers and the event-queue worker pool all run on
 * std::thread already, which is portable as-is, so this header deliberately
 * does NOT re-wrap thread creation. The one thing std::thread cannot do
 * portably is label a thread for the debugger and the OS profiler, and the
 * tree does that on POSIX via pthread_setname_np. This is the equivalent.
 *
 * Affinity is intentionally absent: pthread_setaffinity is Linux-only,
 * has no portable spelling, and nothing in the tree calls it yet. Adding a
 * stub for it would be worse than its absence.
 */

#include "havel_platform.h"

#include <cstdint>
#include <string>

#if HAVEL_PLATFORM_WINDOWS
#include "havel-lang/compiler/platform/platform_win.hpp"
#else
#include <pthread.h>
#endif

namespace havel::platform {

/**
 * Name the calling thread, best-effort.
 *
 * Purely diagnostic: never called for synchronisation, so a failure is
 * ignored by design and returns false. Windows truncates at 63 UTF-16
 * code units, Linux at 15 bytes plus NUL, so short names are the portable
 * habit.
 */
inline bool set_current_thread_name(const char *name) {
  if (!name) return false;
#if HAVEL_PLATFORM_WINDOWS
  // Resolved dynamically rather than called directly: SetThreadDescription
  // only exists on Windows 10 1607+ and calling it by name makes the binary
  // refuse to load on anything older. GetModuleHandleW never fails for
  // kernel32 in practice, but a null module is handled anyway.
  using SetThreadDescriptionFn = HRESULT(WINAPI *)(HANDLE, PCWSTR);
  static SetThreadDescriptionFn fn = []() -> SetThreadDescriptionFn {
    HMODULE k32 = ::GetModuleHandleW(L"kernel32.dll");
    if (!k32) return nullptr;
    return reinterpret_cast<SetThreadDescriptionFn>(
        reinterpret_cast<void *>(::GetProcAddress(k32, "SetThreadDescription")));
  }();
  if (!fn) return false;

  const int wlen = ::MultiByteToWideChar(CP_UTF8, 0, name, -1, nullptr, 0);
  if (wlen <= 0) return false;

  std::wstring wname(static_cast<size_t>(wlen), L'\0');
  if (::MultiByteToWideChar(CP_UTF8, 0, name, -1, wname.data(), wlen) <= 0) {
    return false;
  }
  return SUCCEEDED(fn(::GetCurrentThread(), wname.c_str()));
#elif HAVEL_PLATFORM_MACOS
  // Darwin's pthread_setname_np takes only the name and names the caller;
  // Linux and the BSDs take (thread, name).
  return ::pthread_setname_np(name) == 0;
#else
  return ::pthread_setname_np(::pthread_self(), name) == 0;
#endif
}

/** The calling thread's id, from the same primitive both platforms expose. */
inline uint64_t current_thread_id() {
#if HAVEL_PLATFORM_WINDOWS
  // Hash the HANDLE into a stable number. It is not the OS TID, and callers
  // must only ever use it for equality/identity, never for display.
  return static_cast<uint64_t>(
      reinterpret_cast<uintptr_t>(::GetCurrentThread()));
#else
  return static_cast<uint64_t>(
      reinterpret_cast<uintptr_t>(::pthread_self()));
#endif
}

}  // namespace havel::platform
