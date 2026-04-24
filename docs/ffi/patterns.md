# FFI Patterns

Common patterns for using the FFI module in Havel scripts.

## Pattern: Output Parameters

Many C APIs use pointer arguments to return values. Allocate a buffer, pass
it as a `"pointer"` argument, then read the result with typed accessors.

```havel
use ffi

let libc = ffi.open("libc.so.6")
let xlib = ffi.open("libX11.so.6")

# XGetInputFocus(Display* dpy, Window* focus_return, int* revert_to_return)
let XGetInputFocus = ffi.sym(xlib, "XGetInputFocus")
let dpy = ffi.call(ffi.sym(xlib, "XOpenDisplay"), "pointer", ["pointer"], 0)

# Allocate output buffers
let focus_ptr = ffi.allocBytes(8)   # Window (unsigned long)
let revert_ptr = ffi.allocBytes(4)  # int

# Call with pointer output parameters
ffi.call(XGetInputFocus, "int32",
    ["pointer", "pointer", "pointer"],
    dpy, focus_ptr, revert_ptr)

# Read results
let focus = ffi.get_u64(focus_ptr)
let revert = ffi.get_i32(revert_ptr)

# Clean up
ffi.free(focus_ptr)
ffi.free(revert_ptr)
```

## Pattern: C Constants via cdef

Use `ffi.cdef` with `#define` to import C constants as Havel globals.

```havel
use ffi

let libc = ffi.open("libc.so.6")

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
let fd = ffi.call(open_fn, "int32", ["pointer", "int32", "int32"],
    path_ptr, O_RDWR, 0)
```

## Pattern: Reading Extern Variables

Use `ffi.cdef` with `extern` declarations and a library handle to read
global C variables directly into Havel.

```havel
use ffi

let libc = ffi.open("libc.so.6")

ffi.cdef("extern int errno;", libc)
# errno is now a Havel global with the current value

# To re-read errno after a failing call:
let errno_ptr = ffi.var(libc, "errno")
let current_errno = ffi.get(errno_ptr, "int32")
```

## Pattern: String Arguments

C functions expecting `char*` arguments need the string type declared and
the Havel string value passed directly.

```havel
use ffi

let libc = ffi.open("libc.so.6")
let puts_fn = ffi.sym(libc, "puts")

# "string" type automatically copies the Havel string to a C buffer
ffi.call(puts_fn, "int32", ["string"], "hello from havel")
```

For functions that modify the string buffer in-place, allocate with
`ffi.cstring` and pass as `"pointer"`:

```havel
let buf = ffi.cstring("initial value")
# pass buf as "pointer" type to a function that writes to it
ffi.call(some_fn, "int32", ["pointer"], buf)
let result = ffi.string(buf)
ffi.free(buf)
```

## Pattern: Struct Definition and Access

Define struct layouts with `ffi.newStruct`, allocate instances with
`ffi.alloc`, and access fields with `ffi.field`/`ffi.setField`.

```havel
use ffi

# Define struct type
let fields = []
fields.push(["tv_sec", "int64"])
fields.push(["tv_nsec", "int64"])
ffi.newStruct("timespec", fields)

# Allocate and populate
let ts = ffi.alloc("timespec")
ffi.setField(ts, "timespec", "tv_sec", 0)
ffi.setField(ts, "timespec", "tv_nsec", 0)

# Call nanosleep
let libc = ffi.open("libc.so.6")
let nanosleep = ffi.sym(libc, "nanosleep")
ffi.call(nanosleep, "int32", ["pointer", "pointer"], ts, 0)

ffi.free(ts)
```

## Pattern: Pointer Chaining

When C APIs return pointers-to-pointers, use `ffi.get_ptr` to dereference.

```havel
# XGetWindowProperty writes a pointer into prop_return_ptr
let prop_return_ptr = ffi.allocBytes(8)

ffi.call(XGetWindowProperty, "int32", xgwp_types,
    dpy, window, atom, 0, 1024, 0, XA_STRING,
    actual_type, actual_fmt, nitems, bytes_after, prop_return_ptr)

# Dereference the pointer-to-pointer
let data_ptr = ffi.get_ptr(prop_return_ptr)
if data_ptr != 0 {
    let value = ffi.string(data_ptr)
    print("Property: " + value)
    ffi.call(XFree, "void", ["pointer"], data_ptr)
}
```

## Pattern: Checking for NULL Returns

C functions that return NULL on failure return `0` as a pointer in Havel.
Compare against `0`:

```havel
let dpy = ffi.call(XOpenDisplay, "pointer", ["pointer"], 0)
if dpy == 0 {
    print("Cannot open display")
    exit(1)
}
```

## Pattern: Void Return Functions

Functions with `void` return type return `null`. Use `"void"` as the return
type name:

```havel
ffi.call(XFree, "void", ["pointer"], ptr)
```

## Pattern: Error Handling with errno

Wrap calls that set `errno` with `ffi.clearError`/`ffi.lastError`:

```havel
ffi.clearError()
let result = ffi.call(open_fn, "int32", ["pointer", "int32", "int32"],
    path, O_RDONLY, 0)
if result == -1 {
    let err = ffi.lastError()
    print("open failed with errno=" + err)
}
```
