#pragma once

#ifndef HAVEL_EXPORT_HPP
#define HAVEL_EXPORT_HPP

/*
 * Havel export macro for plugin symbol visibility.
 *
 * When building with -fvisibility=hidden (default for plugin builds),
 * classes and functions that need to be visible to dynamically loaded
 * .so plugins must be explicitly marked with HAVEL_EXPORT.
 *
 * Usage:
 *   class HAVEL_EXPORT MyClass { ... };
 *   HAVEL_EXPORT void myFunction();
 *   HAVEL_EXPORT MyClass& getInstance();
 */

#if defined(__GNUC__) || defined(__clang__)
#define HAVEL_EXPORT __attribute__((visibility("default")))
#else
#define HAVEL_EXPORT
#endif

#endif // HAVEL_EXPORT_HPP