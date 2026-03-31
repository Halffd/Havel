/*
 * DynamicLoader.hpp - Helper for runtime dynamic library loading
 *
 * Usage:
 *   DynamicLoader loader;
 *   if (loader.load("libgtk-4.so")) {
 *       auto fn = loader.getSymbol<GtkInitFn>("gtk_init");
 *       if (fn) fn();
 *   }
 */

#pragma once

#include <string>
#include <unordered_map>
#include <functional>
#include <memory>
#include <cstdio>

#ifdef HAVE_DLFCN_H
#include <dlfcn.h>
#endif

/**
 * DynamicLoader - RAII wrapper for dlopen/dlsym
 * 
 * Allows extensions to load libraries at runtime only when needed.
 * Libraries are unloaded when the loader is destroyed.
 */
class DynamicLoader {
public:
    DynamicLoader() : handle_(nullptr) {}
    
    ~DynamicLoader() {
        unload();
    }
    
    /* Non-copyable */
    DynamicLoader(const DynamicLoader&) = delete;
    DynamicLoader& operator=(const DynamicLoader&) = delete;
    
    /* Movable */
    DynamicLoader(DynamicLoader&& other) noexcept 
        : handle_(other.handle_), path_(std::move(other.path_)) {
        other.handle_ = nullptr;
    }
    
    DynamicLoader& operator=(DynamicLoader&& other) noexcept {
        if (this != &other) {
            unload();
            handle_ = other.handle_;
            path_ = std::move(other.path_);
            other.handle_ = nullptr;
        }
        return *this;
    }
    
    /**
     * Load a shared library
     * 
     * @param path Path to the .so file or library name
     * @return true if loaded successfully
     */
    bool load(const std::string& path) {
#ifdef HAVE_DLFCN_H
        if (handle_) {
            if (path_ == path) {
                return true;  /* Already loaded */
            }
            unload();
        }
        
        /* Try loading with RTLD_LAZY (resolve symbols on first use) */
        handle_ = dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL);
        if (!handle_) {
            const char* err = dlerror();
            fprintf(stderr, "[DynamicLoader] Failed to load %s: %s\n",
                    path.c_str(), err ? err : "unknown error");
            return false;
        }
        
        path_ = path;
        return true;
#else
        (void)path;
        fprintf(stderr, "[DynamicLoader] dlopen not available\n");
        return false;
#endif
    }
    
    /**
     * Get a symbol (function) from the loaded library
     * 
     * @param name Symbol name
     * @return Function pointer or nullptr if not found
     */
    template<typename T>
    T getSymbol(const char* name) {
#ifdef HAVE_DLFCN_H
        if (!handle_) {
            return nullptr;
        }
        
        dlerror();  /* Clear errors */
        void* sym = dlsym(handle_, name);
        const char* err = dlerror();
        if (err) {
            fprintf(stderr, "[DynamicLoader] Symbol %s not found: %s\n",
                    name, err);
            return nullptr;
        }
        
        return reinterpret_cast<T>(sym);
#else
        (void)name;
        return nullptr;
#endif
    }
    
    /**
     * Check if library is loaded
     */
    bool isLoaded() const {
        return handle_ != nullptr;
    }
    
    /**
     * Unload the library
     */
    void unload() {
#ifdef HAVE_DLFCN_H
        if (handle_) {
            dlclose(handle_);
            handle_ = nullptr;
            path_.clear();
        }
#endif
    }
    
    /**
     * Get the loaded library path
     */
    const std::string& getPath() const {
        return path_;
    }
    
private:
    void* handle_;
    std::string path_;
};

/**
 * LibraryLoader - Singleton manager for shared library instances
 * 
 * Ensures libraries are only loaded once and shared across the extension.
 */
template<typename Loader = DynamicLoader>
class LibraryLoader {
public:
    static LibraryLoader& instance() {
        static LibraryLoader inst;
        return inst;
    }
    
    /**
     * Get or load a library
     * 
     * @param name Library name/key
     * @param path Path to load from
     * @return Loader pointer or nullptr if failed
     */
    Loader* get(const std::string& name, const std::string& path) {
        auto it = loaders_.find(name);
        if (it != loaders_.end()) {
            return it->second.get();
        }
        
        auto loader = std::make_unique<Loader>();
        if (loader->load(path)) {
            Loader* ptr = loader.get();
            loaders_[name] = std::move(loader);
            return ptr;
        }
        
        return nullptr;
    }
    
    /**
     * Check if a library is loaded
     */
    bool isLoaded(const std::string& name) const {
        return loaders_.find(name) != loaders_.end();
    }
    
    /**
     * Unload a specific library
     */
    void unload(const std::string& name) {
        loaders_.erase(name);
    }
    
    /**
     * Unload all libraries
     */
    void unloadAll() {
        loaders_.clear();
    }
    
private:
    LibraryLoader() = default;
    ~LibraryLoader() = default;
    
    std::unordered_map<std::string, std::unique_ptr<Loader>> loaders_;
};

/* Common library names for extensions */
namespace LibNames {
    constexpr const char* GTK4 = "libgtk-4.so";
    constexpr const char* GDK4 = "libgdk-4.so";
    constexpr const char* GLIB2 = "libglib-2.0.so";
    constexpr const char* GOBJECT2 = "libgobject-2.0.so";
    
    constexpr const char* GLFW3 = "libglfw.so.3";
    constexpr const char* GL = "libGL.so.1";
    constexpr const char* GLU = "libGLU.so.1";
}
