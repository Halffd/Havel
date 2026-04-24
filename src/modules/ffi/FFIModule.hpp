/*
 * FFIModule.hpp
 *
 * FFI module for Havel language.
 * Provides dynamic library loading and C function calls.
 */
#pragma once

#include "havel-lang/compiler/vm/VMApi.hpp"

namespace havel::modules::ffi {

/**
 * Register FFI module with the VM.
 *
 * Exposes:
 * - ffi.open(path) -> library handle
 * - ffi.close(handle) -> void
 * - ffi.sym(handle, name) -> function pointer
 * - ffi.call(fn_ptr, ret_type, arg_types, args...) -> result
 * - ffi.cdef(decl_string) -> parsed declarations
 *
 * Usage:
 *   import ffi
 *   libc = ffi.open("libc.so.6")
 *   printf = ffi.sym(libc, "printf")
 *   ffi.call(printf, "int32", ["string", "int32"], "hello %d\n", 42)
 */
void registerFFIModule(compiler::VMApi& api);

} // namespace havel::modules::ffi
