---
title: "FFI Reference"
description: "Complete FFI API reference: library management, function calls, memory, structs, callbacks, and typed accessors."
---

# FFI Reference

```hv
use ffi
```

Requires `libffi` development headers at build time.

---

## Library Management

### `ffi.open(path: str) -> pointer`

Load a shared library. Returns opaque handle.

```hv
# libc = ffi.open("libc.so.6")
# xlib = ffi.open("libX11.so.6")
```

### `ffi.close(handle: pointer) -> nil`

Unload library.

```hv
ffi.close(libc)
```

### `ffi.sym(handle: pointer, name: str) -> pointer`

Look up symbol (function or variable).

```hv
# getpid = ffi.sym(libc, "getpid")
# printf = ffi.sym(libc, "printf")
```

---

## Function Calls

### `ffi.call(fn_ptr: pointer, return_type: str, arg_types: array, args...) -> value`

Call C function through libffi.

```hv
# No arguments
# pid = ffi.call(getpid, "int64", [])

# One argument
# result = ffi.call(close_fn, "int32", ["int32"], fd)

# Multiple arguments
ffi.call(printf, "int32", ["string", "int32"], "Value: %d\n", 42)

# Output parameters
# focus_ptr = ffi.allocBytes(8)
# revert_ptr = ffi.allocBytes(4)
ffi.call(XGetInputFocus, "int32",
    ["pointer", "pointer", "pointer"],
    dpy, focus_ptr, revert_ptr)
```

- Return type `"void"` returns `nil`
- Pointer args: integers (addresses), pointers from `ffi.sym`/`ffi.alloc`, or `0` for NULL
- String args (`"string"`/`"char*"`) auto-copied to persistent C string buffer

---

## C Definition Parser

### `ffi.cdef(cdef_string: str, handle?: pointer) -> array`

Parse C declarations, optionally resolve symbols.

```hv
# libc = ffi.open("libc.so.6")
# decls = ffi.cdef("
    #define EINTR 4
    #define PATH_MAX 4096
    int open(const char*, int, ...);
    extern int errno;
", libc)
```

Returns array of declaration objects:
- `.kind`: `"function"`, `"constant"`, `"variable"`, `"struct"`, `"typedef"`, `"union"`
- `.name`: declared name
- `.value`: (constants) parsed integer value
- `.address`: (functions/variables with handle) resolved symbol pointer

Constants (`#define`) auto-installed as VM globals.
Variables (`extern`) with handle resolved via `dlsym`, values installed as globals.

---

## Memory Management

| Function | Signature | Description |
|----------|-----------|-------------|
| `ffi.alloc` | `(type_or_size: any) -> pointer` | Allocate by type name or byte count |
| `ffi.allocBytes` | `(size: int) -> pointer` | Allocate bytes, zero-initialized |
| `ffi.free` | `(ptr: pointer) -> nil` | Free allocated memory |
| `ffi.sizeof` | `(type: str) -> int` | Size in bytes |
| `ffi.alignof` | `(type: str) -> int` | Alignment in bytes |

```hv
# point = ffi.alloc("int64")    // by type
# buf = ffi.alloc(256)          // by bytes
# buf = ffi.allocBytes(1024)    // explicit
ffi.free(buf)
```

---

## Type Conversion

| Function | Signature | Description |
|----------|-----------|-------------|
| `ffi.string` | `(ptr: pointer) -> str` | Read null-terminated C string |
| `ffi.cstring` | `(str: str) -> pointer` | Allocate C string copy (must free) |
| `ffi.array` | `(ptr: pointer, type: str, length: int) -> array` | Read array from memory |
| `ffi.cast` | `(ptr: pointer, type: str) -> pointer` | Reinterpret pointer as type |

```hv
# title = ffi.string(title_ptr)
# cstr = ffi.cstring("hello")
# values = ffi.array(int_buf, "int32", 10)
# int_ptr = ffi.cast(raw_ptr, "int32")
```

---

## Struct Operations

| Function | Signature | Description |
|----------|-----------|-------------|
| `ffi.newStruct` | `(name: str, fields: array) -> pointer` | Define struct type |
| `ffi.field` | `(ptr: pointer, type: str, field_name: str) -> value` | Read struct field |
| `ffi.setField` | `(ptr: pointer, type: str, field_name: str, value: any) -> nil` | Write struct field |

```hv
# fields = []
fields.push(["x", "int32"])
fields.push(["y", "int32"])
# Point = ffi.newStruct("Point", fields)

# point_ptr = ffi.alloc("Point")
ffi.setField(point_ptr, "Point", "x", 10)
ffi.setField(point_ptr, "Point", "y", 20)
# x = ffi.field(point_ptr, "Point", "x")
```

---

## Callbacks

| Function | Signature | Description |
|----------|-----------|-------------|
| `ffi.callback` | `(closure: fn, return_type: str, arg_types: array) -> pointer` | Create C-callable function pointer |
| `ffi.closure` | `(ptr: pointer) -> pointer` | Attach closure context (stub) |

```hv
# callback = ffi.callback(fn(a, b) { a + b }, "int32", ["int32", "int32"])
```

---

## Global Variables

| Function | Signature | Description |
|----------|-----------|-------------|
| `ffi.var` | `(handle: pointer, name: str) -> pointer` | Look up global variable symbol |
| `ffi.get` | `(ptr: pointer, type: str) -> value` | Read value from memory |
| `ffi.set` | `(ptr: pointer, type: str, value: any) -> nil` | Write value to memory |

```hv
# errno_ptr = ffi.var(libc, "errno")
# errno_val = ffi.get(errno_ptr, "int32")
ffi.set(some_ptr, "int32", 42)
```

---

## Typed Accessors (Direct Memory)

### Integer Readers

| Function | C Type | Returns |
|----------|--------|---------|
| `ffi.get_i8` | int8_t | int64 |
| `ffi.get_i16` | int16_t | int64 |
| `ffi.get_i32` | int32_t | int64 |
| `ffi.get_i64` | int64_t | int64 |
| `ffi.get_u8` | uint8_t | int64 |
| `ffi.get_u16` | uint16_t | int64 |
| `ffi.get_u32` | uint32_t | int64 |
| `ffi.get_u64` | uint64_t | int64 |

### Integer Writers

| Function | C Type | Value From |
|----------|--------|------------|
| `ffi.set_i8` | int8_t | int64 |
| `ffi.set_i16` | int16_t | int64 |
| `ffi.set_i32` | int32_t | int64 |
| `ffi.set_i64` | int64_t | int64 |
| `ffi.set_u8` | uint8_t | int64 |
| `ffi.set_u16` | uint16_t | int64 |
| `ffi.set_u32` | uint32_t | int64 |
| `ffi.set_u64` | uint64_t | int64 |

### Float Readers/Writers

| Function | C Type | Notes |
|----------|--------|-------|
| `ffi.get_f32` | float | Returns double |
| `ffi.get_f64` | double | Returns double |
| `ffi.set_f32` | float | v cast from double |
| `ffi.set_f64` | double | v is double |

### Pointer Reader/Writer

| Function | C Type | Notes |
|----------|--------|-------|
| `ffi.get_ptr` | void* | Dereferences as void** |
| `ffi.set_ptr` | void* | Writes pointer v at ptr |

---

## Platform

| Function | Signature | Description |
|----------|-----------|-------------|
| `ffi.lastError` | `() -> int` | Current `errno` value |
| `ffi.clearError` | `() -> nil` | Set `errno` to 0 |

```hv
ffi.clearError()
# ... call that might set errno ...
# err = ffi.lastError()
```

---

## FFI Internals

### Architecture

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

### Build Guard

All FFI code is gated behind `#ifdef HAVE_LIBFFI`. When libffi is not available at build time, `registerFFIModule` is a no-op and none of the FFI functions are registered.

CMake sets `HAVE_LIBFFI` via `pkg_check_modules(LIBFFI)` and propagates it to both the `havel_lang` static library and the `havel` executable target via generator expressions:

```cmake
target_compile_definitions(havel PRIVATE
    $<$<BOOL:${HAVE_LIBFFI}>:HAVE_LIBFFI>
)
target_link_libraries(havel PRIVATE
    $<$<BOOL:${HAVE_LIBFFI}>:ffi>
)
```

### Module Registration

`registerFFIModule(VMApi& api)` is called from `registerStdLibWithVM()` in `src/havel-lang/runtime/StdLibModules.cpp`. The `ffi` module is registered as a global object via `api.setGlobal("ffi", ffiObj)` — host functions are dispatched at runtime via `host_function_globals_` lookup.

Registration follows the VMApi pattern:

1. `api.registerFunction("ffi.open", lambda)` — registers host function
2. `api.makeObject()` — creates the `ffi` module object
3. `api.setField(ffiObj, "open", api.makeFunctionRef("ffi.open"))` — binds method
4. `api.setGlobal("ffi", ffiObj)` — publishes as global

### Value Marshaling

#### Havel -> C (to_native)

`FFIMemory::to_native(Value, FFIType)` converts a Havel Value to a C-native buffer. It allocates a temporary buffer via `alloc_bytes`, writes the value, and returns the buffer pointer. The caller must `FFIMemory::free()` the buffer after use.

For `ffi.call`, the module copies the native buffer into a per-argument `unique_ptr<uint8_t[]>` and immediately frees the temporary. This ensures each argument has its own stable storage for the duration of the `ffi_call`.

#### C -> Havel (to_havel)

`FFIMemory::to_havel(void* ptr, FFIType)` reads a value from a C memory location and returns a Havel Value. The ptr points to the return value buffer (alloca'd in `call_native`) or to a user-provided buffer.

Key fix: sub-64-bit integers must read their exact width (int8_t reads 1 byte, not 8) to avoid stack garbage from the alloca buffer.

### call_native Flow

```
1. Build ffi_cif with ffi_prep_cif(ret_type, param_types)
2. alloca(ret_size) for return value buffer (nullptr for void)
3. ffi_call(cif, fn_ptr, ret_buf, arg_ptrs)
4. FFIMemory::to_havel(ret_buf, ret_type) -> Value
```

`call_function` is the higher-level API that takes Havel Values and marshals them through `to_native` before calling `call_native`.

`ffi.call` in FFIModule.cpp handles the string and pointer marshaling itself (not through `call_function`) because it needs to resolve Havel string IDs to actual content via `api.toString()`.

### parse_cdef

Parses C declarations from a string using regex:

- `#define NAME (expr)` — parenthesized constant expressions
- `#define NAME 0xHEX[uUlL]*` — hex constants with optional suffixes
- `#define NAME INTEGER[uUlL]*` — decimal constants with optional suffixes
- `extern type name;` — variable declarations
- `type name(params);` — function declarations
- `struct name {` — struct declarations
- `typedef old new;` — typedef declarations

Constant values are stored in `FFIDeclaration::constant_value` as uint64_t. The parser strips C integer suffixes (u, U, l, L) before conversion.

When `ffi.cdef` receives a library handle:
- Constants: `api.setGlobal(name, Value(constant_value))` — creates VM global
- Variables: `dlsym(handle, name)` then `FFIMemory::to_havel(sym, type)` — reads and creates VM global
- Functions: `dlsym(handle, name)` then stores address in declaration

### FFIMemory Allocation Tracking

`FFIMemory` tracks all allocations in an `unordered_map<void*, Allocation>` with mutex protection. Each `Allocation` records:

- `ptr` — the allocation address
- `type` — the FFIType (if allocated via `alloc(type)`)
- `size` — bytes allocated
- `gc_mark` — mark bit for sweep
- `finalizer` — optional cleanup function

`alloc_bytes` creates untyped allocations (type is nullptr).
`alloc(type)` creates typed allocations zero-initialized to `sizeof(type)`.

### GC Integration

`FFIMemory::mark(ptr)` sets the gc_mark on an allocation.
`FFIMemory::sweep()` frees all allocations with gc_mark == 0 and resets marks on surviving allocations.

The GC integration is not yet wired to the VM's garbage collector. This is a future improvement — currently FFI memory must be manually freed with `ffi.free()`.

### Known Limitations

- No variadic function support in `ffi.call` (arg_types array must match exactly)
- `ffi.callback` works but the closure lifetime is not GC-managed
- `ffi.closure` is a stub
- Struct field access requires the struct type name as a string, not a type pointer (limitation of how `resolveType` works in the VM binding)
- `ffi.alloc(integer)` is ambiguous with `ffi.alloc("int64")` — the integer path allocates raw bytes, the string path allocates a typed zero-init block

---

## FFI Patterns

### Pattern: Output Parameters

Many C APIs use pointer arguments to return values. Allocate a buffer, pass it as a `"pointer"` argument, then read the result with typed accessors.

```hv
use ffi

# libc = ffi.open("libc.so.6")
# xlib = ffi.open("libX11.so.6")

# XGetInputFocus(Display* dpy, Window* focus_return, int* revert_to_return)
# XGetInputFocus = ffi.sym(xlib, "XGetInputFocus")
# dpy = ffi.call(ffi.sym(xlib, "XOpenDisplay"), "pointer", ["pointer"], 0)

# Allocate output buffers
# focus_ptr = ffi.allocBytes(8)   # Window (unsigned long)
# revert_ptr = ffi.allocBytes(4)  # int

# Call with pointer output parameters
ffi.call(XGetInputFocus, "int32",
    ["pointer", "pointer", "pointer"],
    dpy, focus_ptr, revert_ptr)

# Read results
# focus = ffi.get_u64(focus_ptr)
# revert = ffi.get_i32(revert_ptr)

# Clean up
ffi.free(focus_ptr)
ffi.free(revert_ptr)
```

### Pattern: C Constants via cdef

Use `ffi.cdef` with `#define` to import C constants as Havel globals.

```hv
use ffi

# libc = ffi.open("libc.so.6")

ffi.cdef("
    #define O_RDONLY  0
    #define O_WRONLY  1
    #define O_RDWR    2
    #define O_CREAT   64
    #define O_TRUNC   512
    #define SEEK_SET  0
    #define SEEK_CUR  1
    #define SEEK_END  2
", libc)

# O_RDONLY, O_WRONLY, etc. are now available as globals
# fd = ffi.call(open_fn, "int32", ["pointer", "int32", "int32"],
    path_ptr, O_RDWR, 0)
```

### Pattern: Reading Extern Variables

Use `ffi.cdef` with `extern` declarations and a library handle to read global C variables directly into Havel.

```hv
use ffi

# libc = ffi.open("libc.so.6")

ffi.cdef("extern int errno;", libc)
# errno is now a Havel global with the current value

# To re-read errno after a failing call:
# errno_ptr = ffi.var(libc, "errno")
# current_errno = ffi.get(errno_ptr, "int32")
```

### Pattern: String Arguments

C functions expecting `char*` arguments need the string type declared and the Havel string value passed directly.

```hv
use ffi

# libc = ffi.open("libc.so.6")
# puts_fn = ffi.sym(libc, "puts")

# "string" type automatically copies the Havel string to a C buffer
ffi.call(puts_fn, "int32", ["string"], "hello from havel")
```

For functions that modify the string buffer in-place, allocate with `ffi.cstring` and pass as `"pointer"`:

```hv
# buf = ffi.cstring("initial value")
# pass buf as "pointer" type to a function that writes to it
ffi.call(some_fn, "int32", ["pointer"], buf)
# result = ffi.string(buf)
ffi.free(buf)
```

### Pattern: Struct Definition and Access

Define struct layouts with `ffi.newStruct`, allocate instances with `ffi.alloc`, and access fields with `ffi.field`/`ffi.setField`.

```hv
use ffi

# Define struct type
# fields = []
fields.push(["tv_sec", "int64"])
fields.push(["tv_nsec", "int64"])
ffi.newStruct("timespec", fields)

# Allocate and populate
# ts = ffi.alloc("timespec")
ffi.setField(ts, "timespec", "tv_sec", 0)
ffi.setField(ts, "timespec", "tv_nsec", 0)

# Call nanosleep
# libc = ffi.open("libc.so.6")
# nanosleep = ffi.sym(libc, "nanosleep")
ffi.call(nanosleep, "int32", ["pointer", "pointer"], ts, 0)

ffi.free(ts)
```

### Pattern: Pointer Chaining

When C APIs return pointers-to-pointers, use `ffi.get_ptr` to dereference.

```hv
# XGetWindowProperty writes a pointer into prop_return_ptr
# prop_return_ptr = ffi.allocBytes(8)

ffi.call(XGetWindowProperty, "int32", xgwp_types,
    dpy, window, atom, 0, 1024, 0, XA_STRING,
    actual_type, actual_fmt, nitems, bytes_after, prop_return_ptr)

# Dereference the pointer-to-pointer
# data_ptr = ffi.get_ptr(prop_return_ptr)
if data_ptr != 0 {
    value = ffi.string(data_ptr)
    print("Property: " + value)
    ffi.call(XFree, "void", ["pointer"], data_ptr)
}
```

### Pattern: Checking for NULL Returns

C functions that return NULL on failure return `0` as a pointer in Havel. Compare against `0`:

```hv
# dpy = ffi.call(XOpenDisplay, "pointer", ["pointer"], 0)
if dpy == 0 {
    print("Cannot open display")
    exit(1)
}
```

### Pattern: Void Return Functions

Functions with `void` return type return `nil`. Use `"void"` as the return type name:

```hv
ffi.call(XFree, "void", ["pointer"], ptr)
```

### Pattern: Error Handling with errno

Wrap calls that set `errno` with `ffi.clearError`/`ffi.lastError`:

```hv
ffi.clearError()
# result = ffi.call(open_fn, "int32", ["pointer", "int32", "int32"],
    path, O_RDONLY, 0)
if result == -1 {
    err = ffi.lastError()
    print("open failed with errno=" + err)
}
```

---

## Complete Examples

### Example 1: Call libc getpid

```hv
use ffi

# libc = ffi.open("libc.so.6")
# getpid = ffi.sym(libc, "getpid")
# pid = ffi.call(getpid, "int64", [])
print("PID: " + pid)
ffi.close(libc)
```

### Example 2: Typed Memory Read/Write

```hv
use ffi

# ptr = ffi.allocBytes(8)

ffi.set_i32(ptr, 42)
print("i32: " + ffi.get_i32(ptr))

ffi.set_f64(ptr, 3.14159)
print("f64: " + ffi.get_f64(ptr))

ffi.free(ptr)
```

### Example 3: Sizeof and Alignof

```hv
use ffi

print("sizeof(int32) = " + ffi.sizeof("int32"))    # 4
print("sizeof(int64) = " + ffi.sizeof("int64"))    # 8
print("sizeof(pointer) = " + ffi.sizeof("pointer")) # 8
print("alignof(int64) = " + ffi.alignof("int64"))  # 8
```

### Example 4: C String Conversion

```hv
use ffi

# Havel string -> C string
# cstr = ffi.cstring("hello from havel")
# cstr is a pointer to a malloc'd C string

# C string -> Havel string
# havel_str = ffi.string(cstr)
print(havel_str)  # "hello from havel"

ffi.free(cstr)
```

### Example 5: C Constants as Globals

```hv
use ffi

# libc = ffi.open("libc.so.6")

ffi.cdef("
    #define EINTR 4
    #define EINVAL 22
    #define PATH_MAX 4096
    #define O_RDONLY 0
    #define O_CREAT 64
    extern int errno;
", libc)

# All #define values and extern variables are now globals
print("EINTR = " + EINTR)
print("EINVAL = " + EINVAL)
print("PATH_MAX = " + PATH_MAX)
print("O_RDONLY = " + O_RDONLY)
print("O_CREAT = " + O_CREAT)

ffi.close(libc)
```

### Example 6: X11 Get Focused Window

```hv
use ffi

# xlib = ffi.open("libX11.so.6")
if xlib == 0 {
    print("Failed to load libX11.so.6")
    exit(1)
}

# XOpenDisplay = ffi.sym(xlib, "XOpenDisplay")
# XDefaultRootWindow = ffi.sym(xlib, "XDefaultRootWindow")
# XGetInputFocus = ffi.sym(xlib, "XGetInputFocus")
# XCloseDisplay = ffi.sym(xlib, "XCloseDisplay")

# dpy = ffi.call(XOpenDisplay, "pointer", ["pointer"], 0)
if dpy == 0 {
    print("Cannot open display")
    exit(1)
}
print("Display opened")

# root = ffi.call(XDefaultRootWindow, "uint64", ["pointer"], dpy)
print("Root window: " + root)

# focus_ptr = ffi.allocBytes(8)
# revert_ptr = ffi.allocBytes(4)
ffi.call(XGetInputFocus, "int32",
    ["pointer", "pointer", "pointer"],
    dpy, focus_ptr, revert_ptr)

# focus = ffi.get_u64(focus_ptr)
# revert = ffi.get_i32(revert_ptr)
print("Focused window: " + focus + " (revert: " + revert + ")")

ffi.free(focus_ptr)
ffi.free(revert_ptr)
ffi.call(XCloseDisplay, "int32", ["pointer"], dpy)
ffi.close(xlib)
```

### Example 7: Struct Definition and Field Access

```hv
use ffi

# Define a Point struct
# fields = []
fields.push(["x", "int32"])
fields.push(["y", "int32"])
ffi.newStruct("Point", fields)

# Allocate an instance
# point = ffi.alloc("Point")

# Write fields
ffi.setField(point, "Point", "x", 10)
ffi.setField(point, "Point", "y", 20)

# Read fields
# x = ffi.field(point, "Point", "x")
# y = ffi.field(point, "Point", "y")
print("Point(" + x + ", " + y + ")")

ffi.free(point)
```

### Example 8: Errno Handling

```hv
use ffi

ffi.clearError()
# ... perform an operation that might fail ...
# err = ffi.lastError()
if err != 0 {
    print("Operation failed, errno=" + err)
}
```

### Example 9: Reading a C Array

```hv
use ffi

# Allocate an array of 5 int32 values
# arr_ptr = ffi.allocBytes(5 * 4)

# Write some values
ffi.set_i32(arr_ptr, 10)
ffi.set_i32(ffi.alloc(4), 20)  # offset +4 would need pointer arithmetic

# Read the array back as a Havel array
# values = ffi.array(arr_ptr, "int32", 5)
print("First element: " + values[0])

ffi.free(arr_ptr)
```

### Example 10: Global Variable Access

```hv
use ffi

# libc = ffi.open("libc.so.6")

# Look up errno by name
# errno_ptr = ffi.var(libc, "errno")

# Read its current value
# err = ffi.get(errno_ptr, "int32")
print("errno = " + err)

# Write a new value
ffi.set(errno_ptr, "int32", 0)
print("errno cleared: " + ffi.get(errno_ptr, "int32"))

ffi.close(libc)
```

---

**Previous:** [Host Functions Reference](/reference/host-functions)
**Next:** [Error Handling Reference →](/reference/error-handling)