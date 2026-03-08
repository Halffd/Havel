/*
 * DynamicLibrary.hpp
 *
 * Dynamic library loading for FFI module.
 * Wraps dlopen/dlsym for safe library access.
 */
#pragma once

#include <string>
#include <stdexcept>
#include <dlfcn.h>

namespace havel::modules::ffi {

/**
 * DynamicLibrary - RAII wrapper for dlopen/dlsym
 * 
 * Usage:
 *   auto lib = DynamicLibrary::open("libc.so");
 *   void* sym = lib.symbol("printf");
 */
class DynamicLibrary {
    void* handle;
    std::string path;

    DynamicLibrary(void* h, const std::string& p) 
        : handle(h), path(p) {}

public:
    // Non-copyable, movable
    DynamicLibrary(const DynamicLibrary&) = delete;
    DynamicLibrary& operator=(const DynamicLibrary&) = delete;
    DynamicLibrary(DynamicLibrary&& other) noexcept 
        : handle(other.handle), path(std::move(other.path)) {
        other.handle = nullptr;
    }
    DynamicLibrary& operator=(DynamicLibrary&& other) noexcept {
        if (this != &other) {
            close();
            handle = other.handle;
            path = std::move(other.path);
            other.handle = nullptr;
        }
        return *this;
    }

    ~DynamicLibrary() {
        close();
    }

    /**
     * Open a dynamic library
     * @param path Library path or name (e.g., "libc.so", "libsqlite3.so")
     * @return DynamicLibrary handle
     * @throws std::runtime_error if library cannot be loaded
     */
    static DynamicLibrary open(const std::string& path) {
        void* handle = dlopen(path.c_str(), RTLD_LAZY);
        if (!handle) {
            // Try with lib prefix if not already present
            if (path.find("lib") != 0) {
                std::string altPath = "lib" + path;
                handle = dlopen(altPath.c_str(), RTLD_LAZY);
            }
        }
        if (!handle) {
            throw std::runtime_error(std::string("Failed to load library: ") + dlerror());
        }
        return DynamicLibrary(handle, path);
    }

    /**
     * Look up a symbol in the library
     * @param name Symbol name (e.g., "printf", "getpid")
     * @return Function pointer (void*)
     * @throws std::runtime_error if symbol not found
     */
    void* symbol(const std::string& name) {
        void* sym = dlsym(handle, name.c_str());
        if (!sym) {
            throw std::runtime_error(std::string("Symbol not found: ") + name + " - " + dlerror());
        }
        return sym;
    }

    /**
     * Check if library is valid
     */
    bool isValid() const { return handle != nullptr; }

    /**
     * Get library path
     */
    const std::string& getPath() const { return path; }

private:
    void close() {
        if (handle) {
            dlclose(handle);
            handle = nullptr;
        }
    }
};

} // namespace havel::modules::ffi
