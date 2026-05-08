/*
 * DynamicLibrary.hpp
 *
 * Dynamic library loading for FFI module.
 * Wraps dlopen/dlsym for safe library access.
 */
#pragma once

#include <string>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

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
#ifdef _WIN32
        HMODULE handle = LoadLibraryA(path.c_str());
        if (!handle && path.find(".dll") == std::string::npos) {
            std::string altPath = path + ".dll";
            handle = LoadLibraryA(altPath.c_str());
        }
        if (!handle) {
            throw std::runtime_error("Failed to load library (Win32 error " +
                                     std::to_string(static_cast<unsigned long>(GetLastError())) + ")");
        }
        return DynamicLibrary(reinterpret_cast<void*>(handle), path);
#else
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
#endif
    }

    /**
     * Look up a symbol in the library
     * @param name Symbol name (e.g., "printf", "getpid")
     * @return Function pointer (void*)
     * @throws std::runtime_error if symbol not found
     */
    void* symbol(const std::string& name) {
#ifdef _WIN32
        FARPROC sym = GetProcAddress(reinterpret_cast<HMODULE>(handle), name.c_str());
        if (!sym) {
            throw std::runtime_error("Symbol not found: " + name + " (Win32 error " +
                                     std::to_string(static_cast<unsigned long>(GetLastError())) + ")");
        }
        return reinterpret_cast<void*>(sym);
#else
        void* sym = dlsym(handle, name.c_str());
        if (!sym) {
            throw std::runtime_error(std::string("Symbol not found: ") + name + " - " + dlerror());
        }
        return sym;
#endif
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
#ifdef _WIN32
            FreeLibrary(reinterpret_cast<HMODULE>(handle));
#else
            dlclose(handle);
#endif
            handle = nullptr;
        }
    }
};

} // namespace havel::modules::ffi
