#pragma once

/*
 * platform_win.hpp - Win32 half of the Havel platform shim (PLAT-001).
 *
 * Nothing Linux-specific lives here. On a POSIX build this header expands to
 * nothing at all, so including it is always safe and never changes what a
 * Linux translation unit sees.
 *
 * Convention note: the tree already includes <windows.h> directly in ~20
 * files (KeyMap.hpp, DynamicLibrary.hpp, Loader.c, ...). Those keep working
 * unchanged. This header exists so that code which needs a *specific*
 * Win32 primitive can get one without repeating the guard dance.
 */

#include "havel_platform.h"

#if HAVEL_PLATFORM_WINDOWS

// WIN32_LEAN_AND_MEAN keeps winsock.h and the GDI/COM trivia out of every
// translation unit that pulls this in. Existing files that include
// <windows.h> plainly are unaffected: whichever include happens first
// decides, and an existing plain include still behaves exactly as before.
//
// NOMINMAX is required because <windows.h> defines min/max macros that
// otherwise collide with std::min/std::max at every call site.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#endif  // HAVEL_PLATFORM_WINDOWS
