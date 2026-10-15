#pragma once

/*
 * platform.hpp - Umbrella header for the Havel platform shim (PLAT-001).
 *
 * Platform *detection* is not defined here. It already lives in
 * include/havel_platform.h, which every existing Windows-aware file in the
 * tree includes, and this shim deliberately builds on it rather than
 * introducing a second source of truth. Nothing in the codebase may
 * redefine the platform macros.
 *
 * What this shim adds is the small set of primitives that had no portable
 * spelling. Each one is header-only and inline, so there is no library to
 * wire into a target and no link order to get wrong - a shim that needs
 * build-system changes before it can be used is a shim nobody adopts.
 *
 * Deliberately NOT here, because a portable helper would be a stub:
 *
 *   mmap/munmap/mprotect   -> VirtualAlloc/VirtualFree/VirtualProtect.
 *                             Different allocation granularity and no
 *                             analogue of an over-committed file mapping,
 *                             so callers keep their own OS branch.
 *   eventfd/signalfd       -> no file-descriptor equivalent at all; needs
 *                             a redesign of what "wakeup" means (HANDLE +
 *                             WaitForMultipleObjects), not a wrapper.
 *   fork/execv/waitpid     -> CreateProcess is not the same shape. Lives in
 *                             src/core/process, behind its own backend.
 *   termios/isatty         -> GetConsoleMode/SetConsoleMode, and only when
 *                             attached to a real console.
 *
 * Include the narrow header you need rather than this one where you can;
 * dl.hpp and file.hpp pull in <windows.h> on Windows, which is a lot of
 * macro surface to hand to a file that only wanted a clock.
 */

#include "havel_platform.h"

#include "havel-lang/compiler/platform/time.hpp"

// dl.hpp, file.hpp and thread.hpp are intentionally NOT included here.
// Each pulls in <windows.h> on a Windows build, and a file that wants a
// clock should not inherit that macro surface. Include the one you need:
//
//   #include "havel-lang/compiler/platform/dl.hpp"     // LoadLibrary etc.
//   #include "havel-lang/compiler/platform/file.hpp"   // readdir, mkdtemp
//   #include "havel-lang/compiler/platform/thread.hpp" // thread naming
