---
title: "Extern Types Reference"
description: "Extern declarations and bindings to C++/Rust types."
---

# Extern Types Reference

## Overview

The `extern` keyword declares types and functions implemented in native code (C++/Rust) that can be used from Havel.

```hv
extern {
    // Type declarations
    type NativeType
    
    // Function declarations
    fn native_function(arg: int) -> str
    
    // Constants
    val NATIVE_CONSTANT = 42
}
```

---

## Extern Type Declaration

```hv
// Opaque type (no Havel-side layout)
extern type NativeWindow

// With methods (implemented in native code)
extern type NativeWindow {
    fn getTitle() -> str
    fn getGeometry() -> { x: int, y: int, w: int, h: int }
    fn setTitle(title: str) -> nil
    fn close() -> nil
}
```

### In C++ (Host Module)

```cpp
// Register extern type
class NativeWindowModule : public HostModule {
public:
    void registerFunctions(VMApi& api) override {
        // Register the type
        api.registerExternType("NativeWindow", [](const auto& args) -> Value {
            // Constructor
            return Value::makeExtern("NativeWindow", new NativeWindow());
        });
        
        // Register methods
        api.registerExternMethod("NativeWindow", "getTitle", 
            [](Value self, const auto& args) -> Value {
                auto* win = static_cast<NativeWindow*>(self.asExtern());
                return Value::makeString(win->getTitle());
            });
    }
};
```

---

## Extern Function Declaration

```hv
extern {
    // Simple function
    fn native_add(a: int, b: int) -> int
    
    // With pointer arguments
    fn native_process(data: pointer, len: int) -> int
    
    // Returning extern type
    fn native_create_window(title: str) -> NativeWindow
}
```

### In C++

```cpp
vm.registerHostFunction("native_add", [](const auto& args) -> Value {
    int a = args[0].asInt();
    int b = args[1].asInt();
    return Value::makeInt(a + b);
});

vm.registerHostFunction("native_create_window", [](const auto& args) -> Value {
    std::string title = args[0].asString();
    auto* win = new NativeWindow(title);
    return Value::makeExtern("NativeWindow", win);
});
```

---

## Extern Constants

```hv
extern {
    val NATIVE_MAX_SIZE = 1024
    val NATIVE_VERSION = "1.0.0"
    val NATIVE_FLAGS = 0xFF
}
```

These are resolved at module load time from the host module.

---

## Binding C++ Classes

### Class Wrapper Pattern

```cpp
// C++ class
class ImageProcessor {
public:
    ImageProcessor(int width, int height);
    void process(uint8_t* data, size_t len);
    std::string getResult() const;
    ~ImageProcessor();
};

// Host module binding
class ImageProcessorModule : public HostModule {
public:
    void registerFunctions(VMApi& api) override {
        // Constructor
        api.registerHostFunction("ImageProcessor", [](const auto& args) -> Value {
            int w = args[0].asInt();
            int h = args[1].asInt();
            auto* proc = new ImageProcessor(w, h);
            return Value::makeExtern("ImageProcessor", proc, 
                [](void* ptr) { delete static_cast<ImageProcessor*>(ptr); });
        });
        
        // Method: process
        api.registerExternMethod("ImageProcessor", "process", 
            [](Value self, const auto& args) -> Value {
                auto* proc = static_cast<ImageProcessor*>(self.asExtern());
                // Handle buffer from Havel (array/string)
                auto buf = args[0].asArray();  // or asString()
                proc->process(buf.data(), buf.size());
                return Value::makeNil();
            });
        
        // Method: getResult
        api.registerExternMethod("ImageProcessor", "getResult",
            [](Value self, const auto& args) -> Value {
                auto* proc = static_cast<ImageProcessor*>(self.asExtern());
                return Value::makeString(proc->getResult());
            });
        
        // Destructor (optional, GC will call finalizer)
        api.registerExternMethod("ImageProcessor", "close",
            [](Value self, const auto& args) -> Value {
                auto* proc = static_cast<ImageProcessor*>(self.asExtern());
                delete proc;
                self.setExtern(nullptr);
                return Value::makeNil();
            });
    }
};
```

### In Havel

```hv
use image_processor

proc = ImageProcessor(1920, 1080)
proc.process(imageData)
result = proc.getResult()
proc.close()
```

---

## Binding Rust via C ABI

### Rust Side

```rust
// lib.rs
#[no_mangle]
pub extern "C" fn rust_add(a: i64, b: i64) -> i64 {
    a + b
}

#[no_mangle]
pub extern "C" fn rust_create_processor() -> *mut Processor {
    Box::into_raw(Box::new(Processor::new()))
}

#[no_mangle]
pub extern "C" fn rust_process(ptr: *mut Processor, data: *const u8, len: usize) {
    unsafe { (*ptr).process(std::slice::from_raw_parts(data, len)) }
}

#[no_mangle]
pub extern "C" fn rust_destroy(ptr: *mut Processor) {
    unsafe { drop(Box::from_raw(ptr)) }
}
```

### Havel FFI Binding

```hv
use ffi

rustlib = ffi.open("librustlib.so")

rust_add = ffi.sym(rustlib, "rust_add")
rust_create = ffi.sym(rustlib, "rust_create_processor")
rust_process = ffi.sym(rustlib, "rust_process")
rust_destroy = ffi.sym(rustlib, "rust_destroy")

fn add(a, b) => ffi.call(rust_add, "int64", ["int64", "int64"], a, b)

fn createProcessor() {
    ptr = ffi.call(rust_create, "pointer", [])
    { ptr: ptr, _finalizer: fn() { ffi.call(rust_destroy, "void", ["pointer"], ptr) } }
}

fn process(proc, data) {
    ffi.call(rust_process, "void", ["pointer", "pointer", "int64"], 
        proc.ptr, ffi.cstring(data), data.len())
}
```

---

## Memory Management

### Ownership

| Pattern | Responsibility |
|---------|----------------|
| Havel creates, Havel destroys | Use finalizer in extern type |
| Native creates, Havel destroys | Return pointer, register finalizer |
| Havel creates, Native destroys | Pass to native, native frees |
| Shared | Reference counting (Arc/Rc) |

### Finalizers

```hv
extern type NativeResource {
    fn close() -> nil
}

// In host module registration
api.registerExternType("NativeResource", [](const auto& args) -> Value {
    auto* res = new NativeResource();
    return Value::makeExtern("NativeResource", res, 
        [](void* ptr) { delete static_cast<NativeResource*>(ptr); });
});
```

The finalizer is called by GC when the extern object is collected.

---

## Type Mapping

| Havel | C++ | Rust |
|-------|-----|------|
| `int` | `int64_t` | `i64` |
| `num` | `double` | `f64` |
| `bool` | `bool` | `bool` |
| `str` | `std::string` | `String` / `&str` |
| `array` | `std::vector<Value>` | `Vec<Value>` |
| `object` | `std::unordered_map<std::string, Value>` | `HashMap<String, Value>` |
| `pointer` | `void*` | `*mut c_void` |
| `NativeType` | `NativeType*` | `*mut NativeType` |

---

## Best Practices

1. **Always provide finalizers** for extern types that own memory
2. **Use opaque pointers** for complex C++ types
3. **Validate pointers** in host functions before dereferencing
4. **Document ownership** clearly in module docs
5. **Handle panics** in Rust FFI (use `catch_unwind`)
6. **Match ABIs** exactly (calling convention, struct layout)

---

**Previous:** [Error Handling Reference](/reference/error-handling)
**Next:** [Contributing →](/contributing/build-system)