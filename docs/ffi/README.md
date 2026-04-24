# FFI Module

The `ffi` module provides foreign function interface bindings for calling C
library functions directly from Havel scripts. It wraps
[libffi](https://sourceware.org/libffi/) for portable, ABI-correct function
calls.

Requires: `libffi` development headers at build time. If `libffi` is not found,
all `ffi.*` functions are no-ops.

## Quick Start

```havel
use ffi

# Load a shared library
let libc = ffi.open("libc.so.6")

# Look up a symbol
let getpid = ffi.sym(libc, "getpid")

# Call it: ffi.call(fn_ptr, return_type, [arg_types], args...)
let pid = ffi.call(getpid, "int64", [])
print("PID: " + pid)

# Clean up
ffi.close(libc)
```

## Type Names

Type names are strings recognized by the FFI type registry:

| Havel type name | C type | Size |
|---|---|---|
| `"void"` | void | 0 |
| `"bool"`, `"_Bool"` | _Bool | 1 |
| `"int8"`, `"int8_t"`, `"char"` | int8_t | 1 |
| `"int16"`, `"int16_t"`, `"short"` | int16_t | 2 |
| `"int32"`, `"int32_t"`, `"int"` | int32_t | 4 |
| `"int64"`, `"int64_t"`, `"long"` | int64_t | 8 |
| `"uint8"`, `"uint8_t"`, `"unsigned char"` | uint8_t | 1 |
| `"uint16"`, `"uint16_t"`, `"unsigned short"` | uint16_t | 2 |
| `"uint32"`, `"uint32_t"`, `"unsigned int"` | uint32_t | 4 |
| `"uint64"`, `"uint64_t"`, `"unsigned long"` | uint64_t | 8 |
| `"float"`, `"f32"` | float | 4 |
| `"double"`, `"f64"` | double | 8 |
| `"pointer"`, `"void*"` | void* | 8 |
| `"string"`, `"char*"` | char* | 8 |

Additional types can be resolved via `FFITypeRegistry::from_name()` which
accepts C-style names like `int32_t`, `char*`, `int[10]`.

## Function Reference

### Library Management

#### ffi.open(path) -> pointer

Load a shared library. Returns an opaque library handle.

```havel
let libc = ffi.open("libc.so.6")
let xlib = ffi.open("libX11.so.6")
```

#### ffi.close(handle) -> null

Unload a shared library previously opened with `ffi.open`.

```havel
ffi.close(libc)
```

#### ffi.sym(handle, name) -> pointer

Look up a symbol (function or variable) in a loaded library.

```havel
let getpid = ffi.sym(libc, "getpid")
let printf = ffi.sym(libc, "printf")
```

### Function Calls

#### ffi.call(fn_ptr, return_type, arg_types, args...) -> value

Call a C function through libffi.

- `fn_ptr` -- function pointer obtained from `ffi.sym`
- `return_type` -- type name string for the return value
- `arg_types` -- array of type name strings, one per argument
- `args...` -- the actual arguments to pass

The number of `args` must match the length of `arg_types`.

```havel
# No arguments
let pid = ffi.call(getpid, "int64", [])

# One argument
let result = ffi.call(close_fn, "int32", ["int32"], fd)

# Multiple arguments with pointer output parameters
let focus_ptr = ffi.allocBytes(8)
let revert_ptr = ffi.allocBytes(4)
let status = ffi.call(XGetInputFocus, "int32",
    ["pointer", "pointer", "pointer"],
    dpy, focus_ptr, revert_ptr)
let focus = ffi.get_u64(focus_ptr)
```

Return type `"void"` returns `null`.

Pointer arguments can be integers (treated as addresses), pointers from
`ffi.sym`/`ffi.alloc`, or `0` for NULL.

String arguments (type `"string"` or `"char*"`) are automatically copied
to a persistent C string buffer for the duration of the call.

### C Definition Parser

#### ffi.cdef(cdef_string, handle?) -> array

Parse C-style declarations and optionally resolve symbols from a library
handle. Returns an array of declaration objects.

Each declaration object has:

- `.kind` -- one of `"function"`, `"constant"`, `"variable"`, `"struct"`,
  `"typedef"`, `"union"`
- `.name` -- the declared name
- `.value` -- (constants only) the parsed integer value
- `.address` -- (functions/variables with handle) the resolved symbol pointer

Constants (`#define`) are automatically installed as VM globals.

Variables (`extern`) with a library handle are resolved via `dlsym` and
their current values are read and installed as VM globals.

Functions with a library handle have their address resolved and set in
the declaration's `.address` field.

```havel
let libc = ffi.open("libc.so.6")

let decls = ffi.cdef("
    #define EINTR 4
    #define EINVAL 22
    #define PATH_MAX 4096
    #define O_RDONLY 0
    #define O_CREAT 64
    int getpid(void);
    int close(int fd);
    extern int errno;
", libc)

# Constants are now VM globals
print("EINTR = " + EINTR)       # 4
print("EINVAL = " + EINVAL)     # 22
print("PATH_MAX = " + PATH_MAX) # 4096
print("O_RDONLY = " + O_RDONLY) # 0

# errno is read from libc and available as a global
print("errno = " + errno)
```

Constant values support decimal, hex (`0x`), binary (`0b`), and octal
literals. C integer suffixes (`u`, `U`, `l`, `L`) are stripped before
parsing.

```havel
ffi.cdef("
    #define MAX_SIZE 4096UL
    #define FLAGS 0xFF
    #define MASK 0b11111111
", libc)
```

### Memory Management

#### ffi.alloc(type_or_size) -> pointer

Allocate memory. If the argument is an integer, allocates that many bytes
(equivalent to `ffi.allocBytes`). If it's a type name string, allocates
`sizeof(type)` bytes and zero-initializes.

```havel
# By type name
let point = ffi.alloc("int64")

# By byte count (integer argument)
let buf = ffi.alloc(256)
```

#### ffi.allocBytes(size) -> pointer

Allocate `size` bytes of zero-initialized memory.

```havel
let buf = ffi.allocBytes(1024)
```

#### ffi.free(ptr) -> null

Free memory allocated by `ffi.alloc`, `ffi.allocBytes`, or `ffi.cstring`.

```havel
ffi.free(buf)
```

#### ffi.sizeof(type) -> int

Return the size in bytes of the given type.

```havel
print(ffi.sizeof("int32"))    # 4
print(ffi.sizeof("pointer"))  # 8
print(ffi.sizeof("int64"))    # 8
```

#### ffi.alignof(type) -> int

Return the alignment in bytes of the given type.

```havel
print(ffi.alignof("int64"))   # 8
print(ffi.alignof("int32"))   # 4
```

### Type Conversion

#### ffi.string(ptr) -> string

Read a null-terminated C string at `ptr` and return it as a Havel string.

```havel
let title = ffi.string(title_ptr)
print("Window title: " + title)
```

#### ffi.cstring(str) -> pointer

Allocate a C string copy of a Havel string. The returned pointer must be
freed with `ffi.free` when no longer needed.

```havel
let cstr = ffi.cstring("hello world")
# ... pass cstr to a C function ...
ffi.free(cstr)
```

#### ffi.array(ptr, type, length) -> array

Read `length` elements of `type` starting at `ptr` and return them as a
Havel array.

```havel
let values = ffi.array(int_buf, "int32", 10)
print(values[0])
```

#### ffi.cast(ptr, type) -> pointer

Reinterpret a pointer as a different type.

```havel
let int_ptr = ffi.cast(raw_ptr, "int32")
```

### Struct Operations

Structs require building a type definition first, then allocating and
accessing fields. Note: `ffi.newStruct` is used instead of `ffi.struct`
because `struct` is a reserved keyword in Havel.

#### ffi.newStruct(name, fields) -> pointer

Define a struct type. `fields` is an array of `[name, type]` pairs.

```havel
let fields = []
fields.push(["x", "int32"])
fields.push(["y", "int32"])
let point_type = ffi.newStruct("Point", fields)
```

#### ffi.field(ptr, type, field_name) -> value

Read a field value from a struct at `ptr`. `type` must be the struct type
name.

```havel
let x = ffi.field(point_ptr, "Point", "x")
```

#### ffi.setField(ptr, type, field_name, value) -> null

Write a value to a struct field.

```havel
ffi.setField(point_ptr, "Point", "x", 42)
ffi.setField(point_ptr, "Point", "y", 100)
```

### Callbacks

#### ffi.callback(closure, return_type, arg_types) -> pointer

Create a C-callable function pointer that invokes a Havel closure when
called from C. `arg_types` is an array of type name strings.

```havel
let my_callback = ffi.callback(fn, "int32", ["int32", "int32"])
```

#### ffi.closure(ptr) -> pointer

Attach closure context to a callback. Currently a stub that returns the
input pointer unchanged.

### Global Variables

#### ffi.var(handle, name) -> pointer

Look up a global variable symbol in a loaded library. Equivalent to
`ffi.sym` but semantically marks it as a variable address.

```havel
let errno_ptr = ffi.var(libc, "errno")
```

#### ffi.get(ptr, type) -> value

Read a value of the given type from the memory at `ptr`.

```havel
let errno_val = ffi.get(errno_ptr, "int32")
```

#### ffi.set(ptr, type, value) -> null

Write a value of the given type to the memory at `ptr`.

```havel
ffi.set(some_ptr, "int32", 42)
```

### Typed Accessors

Typed accessors provide direct memory read/write at specific widths without
going through the type registry. They are useful for reading output
parameters after FFI calls.

#### Integer Readers

| Function | C type read | Returns |
|---|---|---|
| `ffi.get_i8(ptr)` | int8_t | int64 |
| `ffi.get_i16(ptr)` | int16_t | int64 |
| `ffi.get_i32(ptr)` | int32_t | int64 |
| `ffi.get_i64(ptr)` | int64_t | int64 |
| `ffi.get_u8(ptr)` | uint8_t | int64 |
| `ffi.get_u16(ptr)` | uint16_t | int64 |
| `ffi.get_u32(ptr)` | uint32_t | int64 |
| `ffi.get_u64(ptr)` | uint64_t | int64 |

#### Integer Writers

| Function | C type written | Value from |
|---|---|---|
| `ffi.set_i8(ptr, v)` | int8_t | int64 |
| `ffi.set_i16(ptr, v)` | int16_t | int64 |
| `ffi.set_i32(ptr, v)` | int32_t | int64 |
| `ffi.set_i64(ptr, v)` | int64_t | int64 |
| `ffi.set_u8(ptr, v)` | uint8_t | int64 |
| `ffi.set_u16(ptr, v)` | uint16_t | int64 |
| `ffi.set_u32(ptr, v)` | uint32_t | int64 |
| `ffi.set_u64(ptr, v)` | uint64_t | int64 |

#### Float Readers/Writers

| Function | C type | Notes |
|---|---|---|
| `ffi.get_f32(ptr)` | float | Returns double |
| `ffi.get_f64(ptr)` | double | Returns double |
| `ffi.set_f32(ptr, v)` | float | v cast from double |
| `ffi.set_f64(ptr, v)` | double | v is double |

#### Pointer Reader/Writer

| Function | C type | Notes |
|---|---|---|
| `ffi.get_ptr(ptr)` | void* | Dereferences ptr as void** |
| `ffi.set_ptr(ptr, v)` | void* | Writes pointer v at ptr |

### Platform

#### ffi.lastError() -> int

Return the current value of `errno`.

```havel
ffi.clearError()
# ... call that might set errno ...
let err = ffi.lastError()
if err != 0 {
    print("Error: " + err)
}
```

#### ffi.clearError() -> null

Set `errno` to 0.
