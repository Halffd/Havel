#include "../../havel-lang/runtime/HostAPI.hpp"
/*
 * FFIModule.hpp
 *
 * FFI module for Havel language.
 * Provides dynamic library loading and C function calls.
 */
#pragma once


namespace havel::modules::ffi {

/**
 * Register FFI module in environment
 * 
 * Exposes:
 * - ffi.dl(path) -> library handle
 * - ffi.sym(lib, name) -> function pointer
 * - ffi.call(fn, ...) -> result
 * - ffi.close(lib) -> void
 * 
 * Usage:
 *   import ffi
 *   libc = ffi.dl("libc.so")
 *   printf = ffi.sym(libc, "printf")
 *   ffi.call(printf, "hello\n")
 */
void registerFFIModule(Environment& env);

} // namespace havel::modules::ffi
