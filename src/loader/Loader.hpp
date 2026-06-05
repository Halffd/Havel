#pragma once

/*
 * Loader.hpp - C++ wrapper over Loader.h
 *
 * Python-style: loadByName("foo") -> finds libfoo.so, dlopens.
 * import("foo", api) -> finds, dlopens, calls havel_extension_init(api).
 *
 * Also replaces DynamicLoader.hpp - use havel::Dynamic for
 * single-library dlopen/dlsym usage.
 */

#include "c/Loader.h"
#include "c/ModulePlugin.h"
#include "c/ToolkitPlugin.h"
#include "extensions/HavelCAPI.h"
#include "havel-lang/core/Value.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <dlfcn.h>

namespace havel {

struct ModulePlugin {
    std::string name;
    std::string version;
    std::string description;
    void *dl_handle = nullptr;
    const HavelModuleABI *abi = nullptr;
    void (*register_fn)(void *) = nullptr;
    void (*cleanup_fn)(void) = nullptr;
};

struct ToolkitPlugin {
    std::string name;
    std::string version;
    std::string description;
    void *dl_handle = nullptr;
    const HavelToolkitABI *abi = nullptr;
};

class Loader {
public:
  enum class SourceType {
    UserSource,
    StdlibSource,
    PackageSource,
    BytecodeCache,
    Cached,
    NativeExtension,
  };

  struct ResolvedModule {
    SourceType type;
    std::string resolvedPath;
    std::string originalName;
  };

  Loader() : handle_(havel_loader_create()) {}

    ~Loader() {
        if (handle_) {
            havel_loader_destroy(handle_);
        }
    }

    Loader(const Loader &) = delete;
    Loader &operator=(const Loader &) = delete;

    Loader(Loader &&other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
    }

    Loader &operator=(Loader &&other) noexcept {
        if (this != &other) {
            if (handle_) havel_loader_destroy(handle_);
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    void addSearchPath(const std::string &path) {
        havel_loader_add_search_path(handle_, path.c_str());
    }

    static std::vector<std::string> getStandardSearchPaths() {
        return {
            ".",
            "/usr/lib/havel/extensions",
            "/usr/local/lib/havel/extensions",
        };
    }

    static std::vector<std::string> getStandardToolkitPaths() {
        return {
            "toolkits",
            "/usr/lib/havel/toolkits",
            "/usr/local/lib/havel/toolkits",
        };
    }

    void *open(const std::string &path) {
        return havel_loader_open(handle_, path.c_str());
    }

    static void *sym(void *dl_handle, const std::string &symbol) {
        return havel_loader_sym(dl_handle, symbol.c_str());
    }

    template <typename T>
    static T getSymbol(void *dl_handle, const char *name) {
        return reinterpret_cast<T>(havel_loader_sym(dl_handle, name));
    }

    bool load(const std::string &path) {
        void *h = havel_loader_open(handle_, path.c_str());
        return h != nullptr;
    }

    bool loadByName(const std::string &name) {
        void *h = havel_loader_load(handle_, name.c_str());
        return h != nullptr;
    }

    bool import(const std::string &name, HavelAPI *api = nullptr) {
        void *h = havel_loader_import(handle_, name.c_str(),
            api ? static_cast<void *>(api) : nullptr);
        return h != nullptr;
    }

    bool loadExtension(const std::string &path) {
        return havel_loader_open(handle_, path.c_str()) != nullptr;
    }

    bool loadExtensionByName(const std::string &name) {
        return havel_loader_load(handle_, name.c_str()) != nullptr;
    }

    void loadExtensionWithInit(const std::string &name, HavelAPI *api) {
        havel_loader_import(handle_, name.c_str(),
            api ? static_cast<void *>(api) : nullptr);
    }

    std::optional<ModulePlugin> loadModulePlugin(const std::string &name) {
        const HavelModuleABI *abi = havel_loader_load_module(handle_, name.c_str());
        if (!abi) return std::nullopt;

        void *dl = havel_loader_get_handle(handle_, ("havel_mod_" + name).c_str());
        ModulePlugin plugin;
        plugin.name = abi->name ? abi->name : name;
        plugin.version = abi->version ? abi->version : "0.0.0";
        plugin.description = abi->description ? abi->description : "";
        plugin.dl_handle = dl;
        plugin.abi = abi;
        plugin.register_fn = abi->register_fn;
        plugin.cleanup_fn = abi->cleanup_fn;
        return plugin;
    }

    std::optional<ToolkitPlugin> loadToolkitPlugin(const std::string &name) {
        const HavelToolkitABI *abi = havel_loader_load_toolkit(handle_, name.c_str());
        if (!abi) return std::nullopt;

        void *dl = havel_loader_get_handle(handle_, ("havel_toolkit_" + name).c_str());
        ToolkitPlugin plugin;
        plugin.name = abi->name ? abi->name : name;
        plugin.version = abi->version ? abi->version : "0.0.0";
        plugin.description = abi->description ? abi->description : "";
        plugin.dl_handle = dl;
        plugin.abi = abi;
        return plugin;
    }

    void addModulePaths() {
        havel_loader_add_module_paths(handle_);
    }

    void addToolkitPaths() {
        havel_loader_add_toolkit_paths(handle_);
    }

    bool isLoaded(const std::string &name) const {
        return havel_loader_is_loaded(handle_, name.c_str());
    }

    void *getHandle(const std::string &name) const {
        return havel_loader_get_handle(handle_, name.c_str());
    }

  std::vector<std::string> getLoadedExtensions() const {
    int count = 0;
    char **names = havel_loader_list_loaded(handle_, &count);
    std::vector<std::string> result;
    result.reserve(count);
    for (int i = 0; i < count; i++) {
      result.emplace_back(names[i]);
    }
    havel_loader_free_names(names, count);
    std::sort(result.begin(), result.end());
    return result;
  }

 std::optional<ResolvedModule> resolve(const std::string &modulePath,
 const std::string &scriptDir) const;
 bool isCached(const std::string &key) const;
 bool getCached(const std::string &key, core::Value *outValue) const;
 void putCache(const std::string &key, core::Value value);
 void clearCache();

  void setStdlibPath(const std::string &path) { stdlibPath_ = path; }
  void addScriptSearchPath(const std::string &path) { scriptSearchPaths_.push_back(path); }

  HavelLoader *raw() const { return handle_; }

private:
  HavelLoader *handle_;
  std::string stdlibPath_;
  std::vector<std::string> scriptSearchPaths_;
  std::map<std::string, core::Value> scriptCache_;
};

class Dynamic {
public:
    Dynamic() = default;
    ~Dynamic() = default;

    Dynamic(const Dynamic &) = delete;
    Dynamic &operator=(const Dynamic &) = delete;

    Dynamic(Dynamic &&other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
    }

    Dynamic &operator=(Dynamic &&other) noexcept {
        if (this != &other) {
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    bool load(const char *path) {
        if (!path) return false;
        dlerror();
        handle_ = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        return handle_ != nullptr;
    }

    bool load(const std::string &path) {
        return load(path.c_str());
    }

    template <typename T>
    T getSymbol(const char *name) const {
        if (!handle_ || !name) return nullptr;
        return reinterpret_cast<T>(havel_loader_sym(handle_, name));
    }

    void *rawHandle() const { return handle_; }

    bool isLoaded() const { return handle_ != nullptr; }
    explicit operator bool() const { return handle_ != nullptr; }

    static const char *suffix() { return havel_loader_suffix(); }

private:
    void *handle_ = nullptr;
};

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

} // namespace havel
