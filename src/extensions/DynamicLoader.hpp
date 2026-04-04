#pragma once

#include <string>

#if defined(_WIN32)
#include <windows.h>
#define HAVEL_DYNLIB_WINDOWS 1
#else
#include <dlfcn.h>
#define HAVEL_DYNLIB_DLFCN 1
#endif

namespace havel {

class DynamicLoader {
  void *handle_ = nullptr;

public:
  DynamicLoader() = default;

  ~DynamicLoader() { unload(); }

  DynamicLoader(const DynamicLoader &) = delete;
  DynamicLoader &operator=(const DynamicLoader &) = delete;

  DynamicLoader(DynamicLoader &&other) noexcept : handle_(other.handle_) {
    other.handle_ = nullptr;
  }

  DynamicLoader &operator=(DynamicLoader &&other) noexcept {
    if (this != &other) {
      unload();
      handle_ = other.handle_;
      other.handle_ = nullptr;
    }
    return *this;
  }

  bool load(const char *path) {
    if (!path) {
      return false;
    }
    unload();
#if defined(HAVEL_DYNLIB_DLFCN)
    handle_ = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    return handle_ != nullptr;
#elif defined(HAVEL_DYNLIB_WINDOWS)
    handle_ = static_cast<void *>(LoadLibraryA(path));
    return handle_ != nullptr;
#else
    (void)path;
    return false;
#endif
  }

  template <typename T>
  T getSymbol(const char *name) const {
    if (!handle_ || !name) {
      return nullptr;
    }
#if defined(HAVEL_DYNLIB_DLFCN)
    void *sym = dlsym(handle_, name);
#elif defined(HAVEL_DYNLIB_WINDOWS)
    void *sym = reinterpret_cast<void *>(
        GetProcAddress(static_cast<HMODULE>(handle_), name));
#else
    void *sym = nullptr;
#endif
    return reinterpret_cast<T>(sym);
  }

  void unload() {
    if (!handle_) {
      return;
    }
#if defined(HAVEL_DYNLIB_DLFCN)
    dlclose(handle_);
#elif defined(HAVEL_DYNLIB_WINDOWS)
    FreeLibrary(static_cast<HMODULE>(handle_));
#endif
    handle_ = nullptr;
  }

  bool isLoaded() const { return handle_ != nullptr; }

  explicit operator bool() const { return handle_ != nullptr; }

  static bool loadLibrary(const std::string &path) {
    DynamicLoader tmp;
    return tmp.load(path.c_str());
  }

  static void unloadAll() {}
};

} // namespace havel

/** Standard shared-library names for dynamic UI extensions (GTK / ImGui). */
struct LibNames {
#if defined(__APPLE__)
  static constexpr const char *GLIB2 = "libglib-2.0.0.dylib";
  static constexpr const char *GOBJECT2 = "libgobject-2.0.0.dylib";
  static constexpr const char *GDK4 = "libgdk-4.0.dylib";
  static constexpr const char *GTK4 = "libgtk-4.0.dylib";
  static constexpr const char *GLFW3 = "libglfw.3.dylib";
  static constexpr const char *GL = "libGL.dylib";
#else
  static constexpr const char *GLIB2 = "libglib-2.0.so.0";
  static constexpr const char *GOBJECT2 = "libgobject-2.0.so.0";
  static constexpr const char *GDK4 = "libgdk-4.so.1";
  static constexpr const char *GTK4 = "libgtk-4.so.1";
  static constexpr const char *GLFW3 = "libglfw.so.3";
  static constexpr const char *GL = "libGL.so.1";
#endif
};
