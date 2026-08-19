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

#endif
