---
title: "Using FFI to Call C Libraries"
description: "Foreign Function Interface: binding C libraries, memory management, callbacks, and struct operations."
---

# Using FFI to Call C Libraries

## Overview

The `ffi` module wraps **libffi** for portable, ABI-correct C function calls from Havel.

```hv
use ffi
```

Requires `libffi` development headers at build time.

---

## Basic Workflow

```hv
// 1. Open library
libc = ffi.open("libc.so.6")

// 2. Look up symbol
getpid = ffi.sym(libc, "getpid")

// 3. Call function
pid = ffi.call(getpid, "int64", [])

// 4. Clean up
ffi.close(libc)
```

---

## Type Mapping

| Havel Type | C Type | Size |
|------------|--------|------|
| `"int8"` / `"char"` | `int8_t` | 1 |
| `"int16"` / `"short"` | `int16_t` | 2 |
| `"int32"` / `"int"` | `int32_t` | 4 |
| `"int64"` / `"long"` | `int64_t` | 8 |
| `"uint8"` | `uint8_t` | 1 |
| `"uint32"` | `uint32_t` | 4 |
| `"uint64"` | `uint64_t` | 8 |
| `"float"` / `"f32"` | `float` | 4 |
| `"double"` / `"f64"` | `double` | 8 |
| `"pointer"` / `"void*"` | `void*` | 8 |
| `"string"` / `"char*"` | `char*` | 8 |

---

## Calling Functions

### No Arguments

```hv
getpid = ffi.sym(libc, "getpid")
pid = ffi.call(getpid, "int64", [])
```

### With Arguments

```hv
printf = ffi.sym(libc, "printf")
ffi.call(printf, "int32", ["string", "int32"], "Value: %d\n", 42)
```

### Output Parameters

```hv
XGetInputFocus = ffi.sym(xlib, "XGetInputFocus")

focus_ptr = ffi.allocBytes(8)
revert_ptr = ffi.allocBytes(4)

ffi.call(XGetInputFocus, "int32",
    ["pointer", "pointer", "pointer"],
    dpy, focus_ptr, revert_ptr)

focus = ffi.get_u64(focus_ptr)
revert = ffi.get_i32(revert_ptr)

ffi.free(focus_ptr)
ffi.free(revert_ptr)
```

---

## C Definition Parser (`ffi.cdef`)

Parse C headers and auto-install constants:

```hv
libc = ffi.open("libc.so.6")

ffi.cdef("
    #define EINTR 4
    #define EINVAL 22
    #define O_RDONLY 0
    #define O_CREAT 64
    int open(const char*, int, ...);
    int close(int);
    extern int errno;
", libc)

# Constants are now globals
print("EINTR = {EINTR}")        # 4
print("O_RDONLY = {O_RDONLY}")  # 0

# Functions have .address set
open_fn = ffi.sym(libc, "open")  # or use decls.find(...).address
```

Returns array of declaration objects:
```hv
{
    kind: "function",     // or "constant", "variable", "struct", "typedef", "union"
    name: "open",
    address: 0x7f...      // resolved symbol pointer
}
```

---

## Memory Management

### Allocation

```hv
// By type (sizeof + zero-init)
# point = ffi.alloc("int64")  // 8 bytes

// By byte count
# buf = ffi.alloc(256)        // 256 bytes
# buf = ffi.allocBytes(1024)  // explicit

// C string (auto-free with ffi.free)
# cstr = ffi.cstring("hello world")
```

### Freeing

```hv
ffi.free(buf)
ffi.free(cstr)
```

### Struct Operations

```hv
// Define struct type
# fields = []
fields.push(["x", "int32"])
fields.push(["y", "int32"])
# Point = ffi.newStruct("Point", fields)

// Allocate struct instance
# point_ptr = ffi.alloc("Point")  // or ffi.allocBytes(ffi.sizeof("Point"))

// Access fields
ffi.setField(point_ptr, "Point", "x", 10)
ffi.setField(point_ptr, "Point", "y", 20)

# x = ffi.field(point_ptr, "Point", "x")  // 10
```

---

## Typed Accessors (Direct Memory)

For output parameters, avoid type registry overhead:

```hv
// Integer readers
# i32 = ffi.get_i32(ptr)
# u64 = ffi.get_u64(ptr)

// Integer writers
ffi.set_i32(ptr, 42)
ffi.set_u64(ptr, 100)

// Float
# f = ffi.get_f64(ptr)
ffi.set_f32(ptr, 3.14)

// Pointer
# p = ffi.get_ptr(ptr)
ffi.set_ptr(ptr, other_ptr)
```

---

## Callbacks: C Calling Havel

```hv
// Create C-callable function pointer
# callback = ffi.callback(fn(a, b) { a + b }, "int32", ["int32", "int32"])

// Pass to C library
# register_cb = ffi.sym(libc, "register_callback")
ffi.call(register_cb, "int32", ["pointer"], callback)
```

The Havel closure is invoked when C calls the function pointer.

---

## Complete Example: X11 Window Title

```hv
use ffi

# xlib = ffi.open("libX11.so.6")

// Parse X11 definitions
ffi.cdef("
    typedef unsigned long XID;
    typedef XID Window;
    typedef struct _XDisplay Display;
    Display* XOpenDisplay(char*);
    int XCloseDisplay(Display*);
    int XFetchName(Display*, Window, char**);
    int XGetInputFocus(Display*, Window*, int*);
", xlib)

# XOpenDisplay = ffi.sym(xlib, "XOpenDisplay")
# XCloseDisplay = ffi.sym(xlib, "XCloseDisplay")
# XFetchName = ffi.sym(xlib, "XFetchName")
# XGetInputFocus = ffi.sym(xlib, "XGetInputFocus")

// Open display
# dpy = ffi.call(XOpenDisplay, "pointer", ["string"], "")
if dpy == 0 { throw "Cannot open display" }

// Get focused window
# focus_ptr = ffi.allocBytes(8)
# revert_ptr = ffi.allocBytes(4)
ffi.call(XGetInputFocus, "int32", ["pointer", "pointer", "pointer"], dpy, focus_ptr, revert_ptr)
# focus = ffi.get_u64(focus_ptr)

// Get window title
# name_ptr = ffi.allocBytes(8)
# status = ffi.call(XFetchName, "int32", ["pointer", "pointer", "pointer"], dpy, focus, name_ptr)
if status != 0 {
    name = ffi.get_ptr(name_ptr)
    if name != 0 {
        print("Window title: {ffi.string(name)}")
    }
}

// Cleanup
ffi.call(XCloseDisplay, "int32", ["pointer"], dpy)
ffi.free(focus_ptr)
ffi.free(revert_ptr)
ffi.free(name_ptr)
ffi.close(xlib)
```

---

## Error Handling

```hv
ffi.clearError()
// ... ffi.call that might set errno ...
# err = ffi.lastError()
if err != 0 {
    print("FFI error: {err}")
}
```

---

## Platform Notes

| Platform | Library Naming |
|----------|----------------|
| Linux | `libc.so.6`, `libX11.so.6`, `libm.so.6` |
| macOS | `libc.dylib`, `libSystem.dylib` |
| Windows | `msvcrt.dll`, `kernel32.dll` (limited support) |

Use `dlopen` semantics: `ffi.open("libname")` searches library paths.

---

## Performance Tips

1. **Cache symbols**: `ffi.sym` once, reuse pointer
2. **Use typed accessors** for output params: `ffi.get_i32` vs `ffi.get`
3. **Batch allocations**: Reuse buffers instead of alloc/free per call
4. **Avoid `ffi.cdef` in hot paths**: Parse once at startup

---

## Troubleshooting

| Issue | Solution |
|-------|----------|
| `ffi.call` segfault | Check argument types match C signature exactly |
| Wrong return value | Verify return type string (`"int64"` vs `"int32"`) |
| String corruption | Use `"string"` type for `char*` args (auto-manages C string) |
| Struct layout wrong | Match C struct packing; use `ffi.alignof` |
| Library not found | Check `LD_LIBRARY_PATH`; use full path |

---

**Previous:** [Web Server](/guides/web-server)
**Next:** [Profiling and Debugging →](/guides/profiling)