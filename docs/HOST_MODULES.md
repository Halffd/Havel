# Host Module Extraction Plan

## Distinction: Stdlib vs Host Modules

### Stdlib Modules (✅ Completed)
Pure functions that work in any environment:
- **Math** - mathematical operations
- **Type** - type conversions
- **String** - string manipulation
- **Array** - array operations
- **File** - file I/O (uses std::filesystem)
- **Regex** - regular expressions (uses std::regex)

**Characteristics:**
- No dependency on host state
- Use only C++ standard library
- Can work in any embedding context
- Testable without Qt/window manager

### Host Modules (Remaining)
System integration that requires host environment:
- **IO** - key mapping (depends on `IO* io`)
- **Clipboard** - clipboard operations (depends on Qt, `ClipboardManager*`)
- **Window** - window management (depends on `WindowManager*`)
- **Brightness** - screen brightness (depends on `BrightnessManager*`)
- **Audio** - volume control (depends on `AudioManager*`)
- **Media** - media playback (depends on Qt multimedia)
- **Screenshot** - screen capture (depends on `ScreenshotManager*`)
- **Pixel** - pixel detection (depends on `PixelAutomation*`)
- **Automation** - UI automation (depends on host APIs)
- **GUI** - dialog boxes (depends on Qt)
- **Launcher** - process launching (partially extractable)
- **FileManager** - file operations (partially extractable)
- **Timer** - timers (depends on interpreter state)
- **Help** - help system (depends on interpreter state)

**Characteristics:**
- Depend on host manager instances
- Require Qt or OS-specific APIs
- Need access to Interpreter's `this` pointer
- Cannot work standalone

## Recommended Approach

### Option A: Keep Host Modules in Interpreter.cpp
**Pros:**
- Simple, no refactoring needed
- Direct access to all managers
- No additional complexity

**Cons:**
- Interpreter.cpp remains large (~10k lines)
- Harder to test host modules in isolation

### Option B: Extract Host Modules with Host Context
Create `host/` directory with modules that receive a context struct:

```cpp
// host/HostContext.hpp
struct HostContext {
    IO* io;
    WindowManager* windowManager;
    HotkeyManager* hotkeyManager;
    BrightnessManager* brightnessManager;
    AudioManager* audioManager;
    GUIManager* guiManager;
    ScreenshotManager* screenshotManager;
    ClipboardManager* clipboardManager;
    PixelAutomation* pixelAutomation;
    // ... etc
};

// host/ClipboardModule.cpp
void registerClipboardModule(Environment* env, HostContext* ctx) {
    env->Define("clipboard.get", BuiltinFunction([ctx](...) {
        QClipboard* clipboard = QGuiApplication::clipboard();
        return HavelValue(clipboard->text().toStdString());
    }));
}
```

**Pros:**
- Better organization
- Clear dependency declaration
- Easier to see what each module needs

**Cons:**
- Still requires Qt and managers
- Doesn't significantly reduce coupling
- Additional indirection

### Option C: Hybrid Approach (Recommended)

1. **Keep host modules in Interpreter.cpp** for now
2. **Document the module boundaries** with comments
3. **Extract only when there's a clear benefit** (e.g., testing, reuse)

**Rationale:**
- Host modules are inherently coupled to the host environment
- Extracting them doesn't provide the same benefits as stdlib extraction
- The 10k line Interpreter.cpp is acceptable for a system integration layer
- Focus on keeping stdlib pure and well-organized

## Current State

| Category | Status | Lines | Files |
|----------|--------|-------|-------|
| **Stdlib** | ✅ Complete | ~1,300 | 12 files |
| **Environment** | ✅ Complete | ~180 | 2 files |
| **Host Modules** | 📍 In Interpreter.cpp | ~9,000 | 1 file |
| **Core Types** | 🚧 Blocked | ~300 | N/A |

**Total reduction achieved: ~2,600 lines extracted (20% of original)**

## Recommendation

**Stop host module extraction here.**

The remaining ~9,000 lines in Interpreter.cpp are:
- Host integration (Qt, managers, OS APIs)
- Inherently coupled to the runtime environment
- Not suitable for pure module extraction

The architecture is now clean:
- **Pure stdlib** in `stdlib/` (testable, reusable)
- **Environment** in `runtime/` (variable scoping)
- **Host integration** in `Interpreter.cpp` (system-specific)

This is a reasonable and maintainable structure.
