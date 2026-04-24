# FFI Internals

Implementation details of the FFI module for contributors.

## Architecture

```
Havel Script
    |
    v
FFIModule.cpp          -- VM-facing bindings (registerFFIModule)
    |
    v
FFICall.cpp/hpp        -- dlopen/dlsym, libffi call_native, parse_cdef
FFIMemory.cpp/hpp      -- alloc/free, to_native, to_havel, GC
FFITypes.cpp/hpp       -- type registry, layout computation
FFIAccessors.hpp       -- inline typed pointer read/write
```

## Build Guard

All FFI code is gated behind `#ifdef HAVE_LIBFFI`. When libffi is not
available at build time, `registerFFIModule` is a no-op and none of the
FFI functions are registered.

CMake sets `HAVE_LIBFFI` via `pkg_check_modules(LIBFFI)` and propagates it
to both the `havel_lang` static library and the `havel` executable target
via generator expressions:

```cmake
target_compile_definitions(havel PRIVATE
    $<$<BOOL:${HAVE_LIBFFI}>:HAVE_LIBFFI>
)
target_link_libraries(havel PRIVATE
    $<$<BOOL:${HAVE_LIBFFI}>:ffi>
)
```

## Module Registration

`registerFFIModule(VMApi& api)` is called from `registerStdLibWithVM()` in
`src/havel-lang/runtime/StdLibModules.cpp`. The `ffi` identifier is added
to `host_global_names` so the compiler treats it as a known global.

Registration follows the VMApi pattern:

1. `api.registerFunction("ffi.open", lambda)` -- registers host function
2. `api.makeObject()` -- creates the `ffi` module object
3. `api.setField(ffiObj, "open", api.makeFunctionRef("ffi.open"))` -- binds method
4. `api.setGlobal("ffi", ffiObj)` -- publishes as global

## Value Marshaling

### Havel -> C (to_native)

`FFIMemory::to_native(Value, FFIType)` converts a Havel Value to a C-native
buffer. It allocates a temporary buffer via `alloc_bytes`, writes the value,
and returns the buffer pointer. The caller must `FFIMemory::free()` the
buffer after use.

For `ffi.call`, the module copies the native buffer into a per-argument
`unique_ptr<uint8_t[]>` and immediately frees the temporary. This ensures
each argument has its own stable storage for the duration of the `ffi_call`.

### C -> Havel (to_havel)

`FFIMemory::to_havel(void* ptr, FFIType)` reads a value from a C memory
location and returns a Havel Value. The ptr points to the return value
buffer (alloca'd in `call_native`) or to a user-provided buffer.

Key fix: sub-64-bit integers must read their exact width (int8_t reads 1
byte, not 8) to avoid stack garbage from the alloca buffer.

## call_native Flow

```
1. Build ffi_cif with ffi_prep_cif(ret_type, param_types)
2. alloca(ret_size) for return value buffer (nullptr for void)
3. ffi_call(cif, fn_ptr, ret_buf, arg_ptrs)
4. FFIMemory::to_havel(ret_buf, ret_type) -> Value
```

`call_function` is the higher-level API that takes Havel Values and
marshals them through `to_native` before calling `call_native`.

`ffi.call` in FFIModule.cpp handles the string and pointer marshaling
itself (not through `call_function`) because it needs to resolve Havel
string IDs to actual content via `api.toString()`.

## parse_cdef

Parses C declarations from a string using regex:

- `#define NAME (expr)` -- parenthesized constant expressions
- `#define NAME 0xHEX[uUlL]*` -- hex constants with optional suffixes
- `#define NAME INTEGER[uUlL]*` -- decimal constants with optional suffixes
- `extern type name;` -- variable declarations
- `type name(params);` -- function declarations
- `struct name {` -- struct declarations
- `typedef old new;` -- typedef declarations

Constant values are stored in `FFIDeclaration::constant_value` as uint64_t.
The parser strips C integer suffixes (u, U, l, L) before conversion.

When `ffi.cdef` receives a library handle:
- Constants: `api.setGlobal(name, Value(constant_value))` -- creates VM global
- Variables: `dlsym(handle, name)` then `FFIMemory::to_havel(sym, type)` -- reads and creates VM global
- Functions: `dlsym(handle, name)` then stores address in declaration

## FFIMemory Allocation Tracking

`FFIMemory` tracks all allocations in an `unordered_map<void*, Allocation>`
with mutex protection. Each `Allocation` records:

- `ptr` -- the allocation address
- `type` -- the FFIType (if allocated via `alloc(type)`)
- `size` -- bytes allocated
- `gc_mark` -- mark bit for sweep
- `finalizer` -- optional cleanup function

`alloc_bytes` creates untyped allocations (type is nullptr).
`alloc(type)` creates typed allocations zero-initialized to `sizeof(type)`.

## GC Integration

`FFIMemory::mark(ptr)` sets the gc_mark on an allocation.
`FFIMemory::sweep()` frees all allocations with gc_mark == 0 and resets
marks on surviving allocations.

The GC integration is not yet wired to the VM's garbage collector. This
is a future improvement -- currently FFI memory must be manually freed
with `ffi.free()`.

## Known Limitations

- No variadic function support in `ffi.call` (arg_types array must match exactly)
- `ffi.callback` works but the closure lifetime is not GC-managed
- `ffi.closure` is a stub
- Struct field access requires the struct type name as a string, not a
  type pointer (limitation of how `resolveType` works in the VM binding)
- `ffi.alloc(integer)` is ambiguous with `ffi.alloc("int64")` -- the integer
  path allocates raw bytes, the string path allocates a typed zero-init block
