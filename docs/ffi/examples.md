# FFI Examples

Working examples for common FFI use cases.

## Example 1: Call libc getpid

```havel
use ffi

let libc = ffi.open("libc.so.6")
let getpid = ffi.sym(libc, "getpid")
let pid = ffi.call(getpid, "int64", [])
print("PID: " + pid)
ffi.close(libc)
```

## Example 2: Typed Memory Read/Write

```havel
use ffi

let ptr = ffi.allocBytes(8)

ffi.set_i32(ptr, 42)
print("i32: " + ffi.get_i32(ptr))

ffi.set_f64(ptr, 3.14159)
print("f64: " + ffi.get_f64(ptr))

ffi.free(ptr)
```

## Example 3: Sizeof and Alignof

```havel
use ffi

print("sizeof(int32) = " + ffi.sizeof("int32"))    # 4
print("sizeof(int64) = " + ffi.sizeof("int64"))    # 8
print("sizeof(pointer) = " + ffi.sizeof("pointer")) # 8
print("alignof(int64) = " + ffi.alignof("int64"))  # 8
```

## Example 4: C String Conversion

```havel
use ffi

# Havel string -> C string
let cstr = ffi.cstring("hello from havel")
# cstr is a pointer to a malloc'd C string

# C string -> Havel string
let havel_str = ffi.string(cstr)
print(havel_str)  # "hello from havel"

ffi.free(cstr)
```

## Example 5: C Constants as Globals

```havel
use ffi

let libc = ffi.open("libc.so.6")

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

## Example 6: X11 Get Focused Window

```havel
use ffi

let xlib = ffi.open("libX11.so.6")
if xlib == 0 {
    print("Failed to load libX11.so.6")
    exit(1)
}

let XOpenDisplay = ffi.sym(xlib, "XOpenDisplay")
let XDefaultRootWindow = ffi.sym(xlib, "XDefaultRootWindow")
let XGetInputFocus = ffi.sym(xlib, "XGetInputFocus")
let XCloseDisplay = ffi.sym(xlib, "XCloseDisplay")

let dpy = ffi.call(XOpenDisplay, "pointer", ["pointer"], 0)
if dpy == 0 {
    print("Cannot open display")
    exit(1)
}
print("Display opened")

let root = ffi.call(XDefaultRootWindow, "uint64", ["pointer"], dpy)
print("Root window: " + root)

let focus_ptr = ffi.allocBytes(8)
let revert_ptr = ffi.allocBytes(4)
ffi.call(XGetInputFocus, "int32",
    ["pointer", "pointer", "pointer"],
    dpy, focus_ptr, revert_ptr)

let focus = ffi.get_u64(focus_ptr)
let revert = ffi.get_i32(revert_ptr)
print("Focused window: " + focus + " (revert: " + revert + ")")

ffi.free(focus_ptr)
ffi.free(revert_ptr)
ffi.call(XCloseDisplay, "int32", ["pointer"], dpy)
ffi.close(xlib)
```

## Example 7: Struct Definition and Field Access

```havel
use ffi

# Define a Point struct
let fields = []
fields.push(["x", "int32"])
fields.push(["y", "int32"])
ffi.newStruct("Point", fields)

# Allocate an instance
let point = ffi.alloc("Point")

# Write fields
ffi.setField(point, "Point", "x", 10)
ffi.setField(point, "Point", "y", 20)

# Read fields
let x = ffi.field(point, "Point", "x")
let y = ffi.field(point, "Point", "y")
print("Point(" + x + ", " + y + ")")

ffi.free(point)
```

## Example 8: Errno Handling

```havel
use ffi

ffi.clearError()
# ... perform an operation that might fail ...
let err = ffi.lastError()
if err != 0 {
    print("Operation failed, errno=" + err)
}
```

## Example 9: Reading a C Array

```havel
use ffi

# Allocate an array of 5 int32 values
let arr_ptr = ffi.allocBytes(5 * 4)

# Write some values
ffi.set_i32(arr_ptr, 10)
ffi.set_i32(ffi.alloc(4), 20)  # offset +4 would need pointer arithmetic

# Read the array back as a Havel array
let values = ffi.array(arr_ptr, "int32", 5)
print("First element: " + values[0])

ffi.free(arr_ptr)
```

## Example 10: Global Variable Access

```havel
use ffi

let libc = ffi.open("libc.so.6")

# Look up errno by name
let errno_ptr = ffi.var(libc, "errno")

# Read its current value
let err = ffi.get(errno_ptr, "int32")
print("errno = " + err)

# Write a new value
ffi.set(errno_ptr, "int32", 0)
print("errno cleared: " + ffi.get(errno_ptr, "int32"))

ffi.close(libc)
```
