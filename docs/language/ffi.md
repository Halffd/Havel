---
title: "Interoperability (FFI)"
description: "Foreign function interface: ffi.open, ffi.sym, ffi.call, ffi.cdef, memory management, callbacks, and C bindings."
---

# Interoperability (FFI)

The `ffi` module provides foreign function interface bindings for calling C library functions directly from Havel. It wraps **libffi** for portable, ABI-correct function calls.

Requires `libffi` development headers at build time. If not found, all `ffi.*` functions are no-ops.

```hv
use ffi
```

---

## Quick Start

```hv
use ffi

# Load a shared library
libc = ffi.open("libc.so.6")

# Look up a symbol
getpid = ffi.sym(libc, "getpid")

# Call it: ffi.call(fn_ptr, return_type, [arg_types], args...)
pid = ffi.call(getpid, "int64", [])
print("PID: {pid}")

# Clean up
ffi.close(libc)
```

---

## Type Names

Type names are strings recognized by the FFI type registry:

| Havel Name | C Type | Size |
|------------|--------|------|
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

Additional types resolved via `FFITypeRegistry::from_name()`: `int32_t`, `char*`, `int[10]`, etc.

---

## Function Reference

### Library Management

#### `ffi.open(path) -> pointer`

Load a shared library. Returns opaque handle.

```hv
libc = ffi.open("libc.so.6")
xlib = ffi.open("libX11.so.6")
```

#### `ffi.close(handle) -> nil`

Unload library.

```hv
ffi.close(libc)
```

#### `ffi.sym(handle, name) -> pointer`

Look up symbol (function or variable).

```hv
getpid = ffi.sym(libc, "getpid")
printf = ffi.sym(libc, "printf")
```

### Function Calls

#### `ffi.call(fn_ptr, return_type, arg_types, args...) -> value`

Call C function through libffi.

```hv
# No arguments
pid = ffi.call(getpid, "int64", [])

# One argument
result = ffi.call(close_fn, "int32", ["int32"], fd)

# Multiple arguments with output parameters
focus_ptr = ffi.allocBytes(8)
revert_ptr = ffi.allocBytes(4)
status = ffi.call(XGetInputFocus, "int32",
    ["pointer", "pointer", "pointer"],
    dpy, focus_ptr, revert_ptr)
focus = ffi.get_u64(focus_ptr)
```

- Return type `"void"` returns `nil`
- Pointer arguments: integers (addresses), pointers from `ffi.sym`/`ffi.alloc`, or `0` for NULL
- String arguments (`"string"`/`"char*"`) auto-copied to persistent C string buffer

### C Definition Parser

#### `ffi.cdef(cdef_string, handle?) -> array`

Parse C-style declarations, optionally resolve symbols from library. Returns array of declaration objects.

```hv
libc = ffi.open("libc.so.6")

decls = ffi.cdef("
    #define EINTR 4
    #define EINVAL 22
    #define PATH_MAX 4096
    int getpid(void);
    int close(int fd);
    extern int errno;
", libc)

# Constants are now VM globals
print("EINTR = {EINTR}")       # 4
print("errno = {errno}")       # from libc
```

Each declaration object:
- `.kind`: `"function"`, `"constant"`, `"variable"`, `"struct"`, `"typedef"`, `"union"`
- `.name`: declared name
- `.value`: (constants) parsed integer value
- `.address`: (functions/variables with handle) resolved symbol pointer

Constants (`#define`) auto-installed as VM globals.
Variables (`extern`) with handle resolved via `dlsym`, values installed as globals.
Functions with handle have `.address` set.

Constant values support: decimal, hex (`0x`), binary (`0b`), octal. C suffixes (`u`, `U`, `l`, `L`) stripped.

```hv
ffi.cdef("
    #define MAX_SIZE 4096UL
    #define FLAGS 0xFF
    #define MASK 0b11111111
", libc)
```

---

## Memory Management

#### `ffi.alloc(type_or_size) -> pointer`

Allocate memory. Integer = bytes (like `allocBytes`). Type name = `sizeof(type)` bytes, zero-initialized.

```hv
point = ffi.alloc("int64")   # by type
buf = ffi.alloc(256)         # by bytes
```

#### `ffi.allocBytes(size) -> pointer`

Allocate `size` bytes, zero-initialized.

```hv
buf = ffi.allocBytes(1024)
```

#### `ffi.free(ptr) -> nil`

Free memory from `ffi.alloc`, `ffi.allocBytes`, or `ffi.cstring`.

```hv
ffi.free(buf)
```

#### `ffi.sizeof(type) -> int`

Size in bytes.

```hv
print(ffi.sizeof("int32"))    # 4
print(ffi.sizeof("pointer"))  # 8
```

#### `ffi.alignof(type) -> int`

Alignment in bytes.

```hv
print(ffi.alignof("int64"))   # 8
print(ffi.alignof("int32"))   # 4
```

---

## Type Conversion

#### `ffi.string(ptr) -> string`

Read null-terminated C string at `ptr`.

```hv
title = ffi.string(title_ptr)
print("Window title: {title}")
```

#### `ffi.cstring(str) -> pointer`

Allocate C string copy of Havel string. Must `ffi.free` when done.

```hv
cstr = ffi.cstring("hello world")
# ... pass to C function ...
ffi.free(cstr)
```

#### `ffi.array(ptr, type, length) -> array`

Read `length` elements of `type` from `ptr`.

```hv
values = ffi.array(int_buf, "int32", 10)
print(values[0])
```

#### `ffi.cast(ptr, type) -> pointer`

Reinterpret pointer as different type.

```hv
int_ptr = ffi.cast(raw_ptr, "int32")
```

---

## Struct Operations

Uses `ffi.newStruct` (not `ffi.struct` — `struct` is reserved keyword).

#### `ffi.newStruct(name, fields) -> pointer`

Define struct type. `fields` = array of `[name, type]` pairs.

```hv
fields = []
fields.push(["x", "int32"])
fields.push(["y", "int32"])
point_type = ffi.newStruct("Point", fields)
```

#### `ffi.field(ptr, type, field_name) -> value`

Read field from struct.

```hv
x = ffi.field(point_ptr, "Point", "x")
```

#### `ffi.setField(ptr, type, field_name, value) -> nil`

Write field to struct.

```hv
ffi.setField(point_ptr, "Point", "x", 42)
ffi.setField(point_ptr, "Point", "y", 100)
```

---

## Callbacks

#### `ffi.callback(closure, return_type, arg_types) -> pointer`

Create C-callable function pointer invoking Havel closure.

```hv
my_callback = ffi.callback(fn(a, b) { a + b }, "int32", ["int32", "int32"])
```

#### `ffi.closure(ptr) -> pointer`

Attach closure context (currently returns input unchanged).

---

## Global Variables

#### `ffi.var(handle, name) -> pointer`

Look up global variable symbol (semantically marks as variable address).

```hv
errno_ptr = ffi.var(libc, "errno")
```

#### `ffi.get(ptr, type) -> value`

Read value of type from memory at `ptr`.

```hv
errno_val = ffi.get(errno_ptr, "int32")
```

#### `ffi.set(ptr, type, value) -> nil`

Write value of type to memory at `ptr`.

```hv
ffi.set(some_ptr, "int32", 42)
```

---

## Typed Accessors

Direct memory read/write without type registry. Useful for output parameters.

### Integer Readers

| Function | C Type | Returns |
|----------|--------|---------|
| `ffi.get_i8(ptr)` | int8_t | int64 |
| `ffi.get_i16(ptr)` | int16_t | int64 |
| `ffi.get_i32(ptr)` | int32_t | int64 |
| `ffi.get_i64(ptr)` | int64_t | int64 |
| `ffi.get_u8(ptr)` | uint8_t | int64 |
| `ffi.get_u16(ptr)` | uint16_t | int64 |
| `ffi.get_u32(ptr)` | uint32_t | int64 |
| `ffi.get_u64(ptr)` | uint64_t | int64 |

### Integer Writers

| Function | C Type | Value From |
|----------|--------|------------|
| `ffi.set_i8(ptr, v)` | int8_t | int64 |
| `ffi.set_i16(ptr, v)` | int16_t | int64 |
| `ffi.set_i32(ptr, v)` | int32_t | int64 |
| `ffi.set_i64(ptr, v)` | int64_t | int64 |
| `ffi.set_u8(ptr, v)` | uint8_t | int64 |
| `ffi.set_u16(ptr, v)` | uint16_t | int64 |
| `ffi.set_u32(ptr, v)` | uint32_t | int64 |
| `ffi.set_u64(ptr, v)` | uint64_t | int64 |

### Float Readers/Writers

| Function | C Type | Notes |
|----------|--------|-------|
| `ffi.get_f32(ptr)` | float | Returns double |
| `ffi.get_f64(ptr)` | double | Returns double |
| `ffi.set_f32(ptr, v)` | float | v cast from double |
| `ffi.set_f64(ptr, v)` | double | v is double |

### Pointer Reader/Writer

| Function | C Type | Notes |
|----------|--------|-------|
| `ffi.get_ptr(ptr)` | void* | Dereferences as void** |
| `ffi.set_ptr(ptr, v)` | void* | Writes pointer v at ptr |

---

## Platform

#### `ffi.lastError() -> int`

Current `errno` value.

```hv
ffi.clearError()
# ... call that might set errno ...
err = ffi.lastError()
if err != 0 { print("Error: {err}") }
```

#### `ffi.clearError() -> nil`

Set `errno` to 0.

---

## Complete Example: X11 Window Title

```hv
use ffi

xlib = ffi.open("libX11.so.6")

# Parse X11 definitions
ffi.cdef("
    typedef unsigned long XID;
    typedef XID Window;
    typedef struct _XDisplay Display;
    Display* XOpenDisplay(char*);
    int XCloseDisplay(Display*);
    int XFetchName(Display*, Window, char**);
    int XGetInputFocus(Display*, Window*, int*);
", xlib)

XOpenDisplay = ffi.sym(xlib, "XOpenDisplay")
XCloseDisplay = ffi.sym(xlib, "XCloseDisplay")
XFetchName = ffi.sym(xlib, "XFetchName")
XGetInputFocus = ffi.sym(xlib, "XGetInputFocus")

dpy = ffi.call(XOpenDisplay, "pointer", ["string"], "")
if dpy == 0 {
    throw "Cannot open display"
}

focus_ptr = ffi.allocBytes(8)
revert_ptr = ffi.allocBytes(4)
ffi.call(XGetInputFocus, "int32", ["pointer", "pointer", "pointer"], dpy, focus_ptr, revert_ptr)
focus = ffi.get_u64(focus_ptr)

name_ptr = ffi.allocBytes(8)
status = ffi.call(XFetchName, "int32", ["pointer", "pointer", "pointer"], dpy, focus, name_ptr)
if status != 0 {
    name = ffi.get_ptr(name_ptr)
    if name != 0 {
        print("Window title: {ffi.string(name)}")
    }
}

ffi.call(XCloseDisplay, "int32", ["pointer"], dpy)
ffi.free(focus_ptr)
ffi.free(revert_ptr)
ffi.free(name_ptr)
ffi.close(xlib)
```

---

**Previous:** [DSL & Input Commands](/language/dsl)
**Next:** [Standard Library →](/stdlib/math)