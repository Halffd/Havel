#ifndef HAVEL_PLATFORM_H
#define HAVEL_PLATFORM_H

#if defined(_WIN32) || defined(_WIN64) || defined(__CYGWIN__) || defined(__MINGW32__) || defined(__MSYS__)
#  define HAVEL_PLATFORM_WINDOWS 1
#  define HAVEL_PLATFORM_POSIX   0
#  define HAVEL_PLATFORM_LINUX   0
#  define HAVEL_PLATFORM_MACOS   0
#  define HAVEL_PLATFORM_NAME    "windows"
#elif defined(__APPLE__) && defined(__MACH__)
#  define HAVEL_PLATFORM_WINDOWS 0
#  define HAVEL_PLATFORM_POSIX   1
#  define HAVEL_PLATFORM_LINUX   0
#  define HAVEL_PLATFORM_MACOS   1
#  define HAVEL_PLATFORM_NAME    "macos"
#elif defined(__linux__)
#  define HAVEL_PLATFORM_WINDOWS 0
#  define HAVEL_PLATFORM_POSIX   1
#  define HAVEL_PLATFORM_LINUX   1
#  define HAVEL_PLATFORM_MACOS   0
#  define HAVEL_PLATFORM_NAME    "linux"
#else
#  define HAVEL_PLATFORM_WINDOWS 0
#  define HAVEL_PLATFORM_POSIX   1
#  define HAVEL_PLATFORM_LINUX   0
#  define HAVEL_PLATFORM_MACOS   0
#  define HAVEL_PLATFORM_NAME    "posix"
#endif

#if HAVEL_PLATFORM_WINDOWS
/* <windows.h> defines min and max as function-like macros. Wherever they are
 * live, every std::min / std::max call site expands to something the compiler
 * rejects ("'(' illegal token on right side of '::'"), and the genuine
 * min()/max() helpers in src/utils/Util.hpp would be mangled at their
 * definition. Defining NOMINMAX here, ahead of every other include in the
 * tree, is the only point guaranteed to precede every <windows.h>. Doing it
 * per-file does not work: windows.h is reached transitively through a dozen
 * headers before the file that wants Win32 gets around to asking for it.
 * WIN32_LEAN_AND_MEAN is deliberately NOT set here: it would strip winsock
 * and COM from translation units that legitimately need them, and nothing
 * needs that trimming to compile. */
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif

/* MSVC has no pid_t. The CRT's _getpid() returns int, and every pid_t use in
 * this tree is either a comparison against that or a value stored in a struct
 * that also gets serialised, so int is the correct width rather than a
 * stand-in that would truncate later. Defined here, in the one platform
 * header everything already includes, so each consumer does not grow its own
 * typedef and the tree keeps a single source of truth. */
#  ifndef HAVEL_PID_T_DEFINED
#    define HAVEL_PID_T_DEFINED
     typedef int pid_t;
#  endif
#endif

#endif
