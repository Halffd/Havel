#pragma once

/*
 * dl.hpp - Dynamic library loading (PLAT-001 section 5).
 *
 * Maps the POSIX dl* trio onto the Win32 LoadLibrary trio. Both are
 * load-by-path, resolve-by-symbol, close, last-error.
 *
 * Scope: this is the low-level primitive. It is NOT a replacement for
 * src/dl/Loader.h, which layers module search paths, the plugin ABI and
 * the loaded-module registry on top and stays the entry point for module
 * plugins. dl.hpp is what a new consumer should reach for when all it
 * needs is "open a library, find a symbol".
 */

#include "havel_platform.h"

#include <string>

#if HAVEL_PLATFORM_WINDOWS
#include "havel-lang/compiler/platform/platform_win.hpp"
#else
#include <dlfcn.h>
#endif

namespace havel::platform {

/**
 * Opaque library handle.
 *
 * Kept as void* on both platforms rather than exposing HMODULE/Dl_handle,
 * so callers never have to know which one they hold.
 */
using library_handle = void *;

/** Symbol address, used as `reinterpret_cast<T>(sym)`. */
using library_symbol = void *;

/**
 * The platform's shared-library file extension, including the dot.
 * ".dll" / ".so" / ".dylib".
 */
inline const char *library_suffix() {
#if HAVEL_PLATFORM_WINDOWS
  return ".dll";
#elif HAVEL_PLATFORM_MACOS
  return ".dylib";
#else
  return ".so";
#endif
}

/**
 * Open a shared library.
 *
 * Equivalent to dlopen(path, RTLD_NOW | RTLD_LOCAL) / LoadLibraryA(path):
 * symbols are resolved eagerly, and the library's symbols do not enter the
 * global namespace and shadow the host's.
 *
 * Returns nullptr on failure; call last_error_message() for the reason.
 */
inline library_handle library_open(const char *path) {
  if (!path) return nullptr;
#if HAVEL_PLATFORM_WINDOWS
  return static_cast<library_handle>(::LoadLibraryA(path));
#else
  return ::dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
}

/**
 * Resolve a symbol in a library, or nullptr if it is absent.
 *
 * On Windows GetProcAddress reports "not found" via GetLastError rather
 * than through a dedicated error channel, so this returns nullptr for a
 * missing symbol without clearing the last-error state.
 */
inline library_symbol library_symbol_get(library_handle handle,
                                         const char *name) {
  if (!handle || !name) return nullptr;
#if HAVEL_PLATFORM_WINDOWS
  // GetProcAddress already returns FARPROC, which is a function pointer;
  // round-tripping through FARPROC keeps the reinterpret_cast explicit.
  return reinterpret_cast<library_symbol>(
      reinterpret_cast<void *>(::GetProcAddress(
          static_cast<HMODULE>(handle), name)));
#else
  return ::dlsym(handle, name);
#endif
}

/** Release a library handle. Safe on nullptr. */
inline void library_close(library_handle handle) {
  if (!handle) return;
#if HAVEL_PLATFORM_WINDOWS
  ::FreeLibrary(static_cast<HMODULE>(handle));
#else
  ::dlclose(handle);
#endif
}

/**
 * Human-readable reason for the most recent library_open failure.
 *
 * POSIX dlerror() both reports and clears; GetLastError does not, so the
 * Windows branch reads the code the call before us left behind. Returns an
 * empty string when there is no error to report.
 */
inline std::string last_error_message() {
#if HAVEL_PLATFORM_WINDOWS
  const DWORD err = ::GetLastError();
  if (err == 0) return {};
  char *buf = nullptr;
  // FormatMessage's own allocation is avoided: converting to a std::string
  // here would need another buffer and another error path to leak.
  const DWORD n = ::FormatMessageA(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, err, 0, reinterpret_cast<char *>(&buf), 0, nullptr);
  if (n == 0 || !buf) return "unknown error (" + std::to_string(err) + ")";
  std::string out(buf, n);
  ::LocalFree(buf);
  // FormatMessage appends CRLF; callers log this verbatim.
  while (!out.empty() && (out.back() == '\r' || out.back() == '\n')) {
    out.pop_back();
  }
  return out;
#else
  const char *msg = ::dlerror();
  return msg ? std::string(msg) : std::string();
#endif
}

/**
 * RAII owner for a shared library.
 *
 * Move-only, like std::unique_ptr: two owners would double-close. Does not
 * throw on open - check is_open(), because the existing call sites in
 * src/dl/Loader.h all report load failure by returning false rather than
 * by throwing, and a throwing helper here would force every one of them to
 * grow a try/catch.
 */
class library {
 public:
  library() = default;

  explicit library(const char *path) : handle_(library_open(path)) {}

  explicit library(const std::string &path) : handle_(library_open(path.c_str())) {}

  ~library() { library_close(handle_); }

  library(const library &) = delete;
  library &operator=(const library &) = delete;

  library(library &&other) noexcept : handle_(other.handle_) {
    other.handle_ = nullptr;
  }

  library &operator=(library &&other) noexcept {
    if (this != &other) {
      library_close(handle_);
      handle_ = other.handle_;
      other.handle_ = nullptr;
    }
    return *this;
  }

  bool load(const char *path) {
    library_close(handle_);
    handle_ = library_open(path);
    return is_open();
  }

  bool load(const std::string &path) { return load(path.c_str()); }

  library_symbol sym(const char *name) const {
    return library_symbol_get(handle_, name);
  }

  template <typename T>
  T sym_as(const char *name) const {
    return reinterpret_cast<T>(sym(name));
  }

  bool is_open() const { return handle_ != nullptr; }
  explicit operator bool() const { return is_open(); }
  library_handle handle() const { return handle_; }

  void close() {
    library_close(handle_);
    handle_ = nullptr;
  }

 private:
  library_handle handle_ = nullptr;
};

}  // namespace havel::platform
