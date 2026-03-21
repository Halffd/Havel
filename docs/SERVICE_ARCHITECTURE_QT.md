# Service Architecture - Qt and Platform Dependencies

## The Reality Check

**FALSE DOGMA:** "Services must be pure C++ with no Qt dependencies"

**TRUTH:** Services CAN and SHOULD use Qt, OS APIs, platform libs — whatever they need.

**The actual rule:**
> Types are allowed to be ugly at the edges (services),
> but must be clean at the core (VM).

---

## Correct Architecture

### Service Layer (Can Use Qt, OS APIs, Whatever)

```cpp
// ✅ CORRECT - Service uses Qt internally
class ScreenshotService {
public:
    // Can use Qt types internally - no shame
    QScreen* screen;
    
    QImage full();
    QImage monitor(int index);
    QImage region(int x, int y, int w, int h);
};

// ✅ CORRECT - WindowService uses X11
class WindowService {
    Display* display;  // X11
    std::string getActiveWindowTitle();
};

// ✅ CORRECT - AudioService uses PulseAudio/PipeWire
class AudioService {
    pa_context* context;  // PulseAudio
    double getVolume();
};
```

**No pretending. No fake purity. Just reality.**

---

## The Boundary

### What CANNOT Leak Past HostBridge

| Layer | Can Use | Cannot Expose |
|-------|---------|---------------|
| **Services** | Qt, X11, PulseAudio, PipeWire, etc. | ❌ Qt types to VM |
| **HostBridge** | Any platform types internally | ❌ Platform types to VM |
| **VM** | Only VM-safe types | ✅ Clean, predictable types |

---

## HostBridge Translation Layer

### QImage → VM Image

**WRONG (leaking types):**
```cpp
// ❌ Don't do this
options.host_functions["screenshot.full"] = [](args) {
    QImage img = screenshotService->full();
    return BytecodeValue(img);  // QImage leaks to VM!
};
```

**CORRECT (translation):**
```cpp
// ✅ Do this
options.host_functions["screenshot.full"] = [self](args) {
    QImage img = self->screenshotService->full();
    return self->imageToVMImage(img);  // Translate to VM-safe type
};

// HostBridge translation
VMImage HostBridgeRegistry::imageToVMImage(const QImage& img) {
    VMImage vmImg;
    vmImg.width = img.width();
    vmImg.height = img.height();
    vmImg.stride = img.bytesPerLine();
    vmImg.format = PixelFormat::RGBA8;  // Normalize format
    // Wrap data without copying if possible
    vmImg.data = std::shared_ptr<uint8_t[]>(
        new uint8_t[img.byteCount()],
        [](uint8_t* p) { delete[] p; }
    );
    memcpy(vmImg.data.get(), img.bits(), img.byteCount());
    return vmImg;
}
```

---

## VM Image Type - Proper Representation

### NOT This (Too Weak)
```cpp
// ❌ Don't do this
using ImageData = std::vector<uint8_t>;
// or
using ImageData = std::string;  // pixel soup hell
```

### THIS (Proper Struct)
```cpp
// ✅ Do this
enum class PixelFormat {
    RGBA8,
    RGB8,
    GRAY8,
    // ... defined formats
};

struct VMImage {
    int32_t width = 0;
    int32_t height = 0;
    int32_t stride = 0;  // bytes per row
    PixelFormat format = PixelFormat::RGBA8;
    std::shared_ptr<uint8_t[]> data;  // GC-managed buffer
    
    // Helper: total size in bytes
    size_t size() const {
        return stride * height;
    }
    
    // Helper: pixel at (x, y)
    uint8_t* pixel(int x, int y) {
        return data.get() + y * stride + x * 4;  // Assuming RGBA
    }
};
```

**Benefits:**
- ✅ Format is guaranteed (no guessing)
- ✅ Stride defined (no calculation errors)
- ✅ Ownership clear (shared_ptr for GC)
- ✅ No copies if wrapping existing buffer

---

## Performance: The Real Trap

### The Problem

4K screenshot = 3840 × 2160 × 4 bytes = **33 MB**

**Bad path (multiple copies):**
```
QImage (33MB) 
    ↓ copy
VM buffer (33MB)
    ↓ copy
Script usage (another 33MB)
    
Total: 99MB + CPU time + cache misses
```

### The Solution: Zero-Copy When Possible

```cpp
// ✅ Wrap QImage memory directly
VMImage HostBridgeRegistry::imageToVMImage(QImage& img) {
    VMImage vmImg;
    vmImg.width = img.width();
    vmImg.height = img.height();
    vmImg.stride = img.bytesPerLine();
    vmImg.format = PixelFormat::RGBA8;
    
    // Share ownership of the buffer
    // QImage keeps data alive, VMImage shares ownership
    vmImg.data = std::shared_ptr<uint8_t[]>(
        img.bits(),  // Wrap existing memory
        [img = std::move(img)](uint8_t*) mutable {
            // QImage destructor frees memory when last ref dies
        }
    );
    
    return vmImg;  // No copy!
}
```

**Rule:**
> Never copy image data unless absolutely necessary

---

## Callback Architecture (This Part is Correct)

```
┌─────────────────────────────────────────────────────────────┐
│  VM (owns ALL closures)                                      │
│  - pinExternalRoot(closure) → CallbackId                    │
│  - invokeCallback(CallbackId) → executes closure            │
│  - releaseCallback(CallbackId) → unpins, GC can collect     │
└─────────────────────────────────────────────────────────────┘
         ↑
         │ CallbackId (opaque handle)
         │
┌─────────────────────────────────────────────────────────────┐
│  HostBridge (internal callback management)                   │
│  - registerCallback() → calls VM::pinExternalRoot()         │
│  - invokeCallback() → calls VM::invokeCallback()            │
│  - releaseCallback() → calls VM::releaseCallback()          │
│  - Handlers use these internally (NOT exposed to scripts)   │
└─────────────────────────────────────────────────────────────┘
         ↑
         │ CallbackId
         │
┌─────────────────────────────────────────────────────────────┐
│  ModeService (with VM injection)                             │
│  - defineMode(name, conditionId, enterId, exitId)           │
│  - Creates lambdas that call vm->invokeCallback(id)         │
│  - One-way dependency: ModeService → VM (no cycle!)         │
└─────────────────────────────────────────────────────────────┘
```

**Key Principle:**
| Layer | Responsibility | Exposed to Scripts? |
|-------|---------------|---------------------|
| VM | Owns closures, GC | ❌ No |
| HostBridge | Internal callback management | ❌ No (internal) |
| Services | Business logic with VM injection | ❌ No |
| Host Functions | Safe, high-level APIs | ✅ Yes |

**Scripts never see CallbackId.**  
**Scripts never manage VM memory.**  
**All callback management is internal.**

---

## Avoiding HostBridge Bloat

### ⚠️ The Trap

HostBridge becoming a god-object:
```cpp
// ❌ Don't do this - HostBridge storing state
class HostBridgeRegistry {
    std::vector<Screenshot> screenshots;  // NO!
    std::unordered_map<std::string, Window> windows;  // NO!
    // ... accumulating state
};
```

### ✅ Correct Mental Model

```
VM         → owns memory + execution
Services   → own logic + platform resources
HostBridge → ONLY translates (stateless)
```

**HostBridge should NOT:**
- Store state beyond callback IDs
- Manage lifetimes beyond translation
- Do business logic

**HostBridge SHOULD:**
- Translate VM types ↔ Service types
- Register host functions
- Manage callback lifecycle (through VM)

---

## Summary: The Rules

1. **Services can use Qt/platform libs** - No fake purity
2. **Don't leak platform types to VM** - HostBridge translates
3. **VM types must be clean and normalized** - Proper structs, not `vector<uint8_t>`
4. **Zero-copy when possible** - Especially for large data (images, audio)
5. **HostBridge is stateless translator** - Not a god-object
6. **Callback management is internal** - Scripts never see CallbackId

---

## Implementation Checklist

For each Qt-heavy service:

- [ ] Service can use Qt internally
- [ ] HostBridge translates Qt types → VM types
- [ ] VM types are proper structs (not raw buffers)
- [ ] Zero-copy wrapping when possible
- [ ] Format/stride/ownership clearly defined
- [ ] No platform types leak to VM

---

**Last Updated:** March 21, 2026  
**Status:** Architecture Correct ✅
