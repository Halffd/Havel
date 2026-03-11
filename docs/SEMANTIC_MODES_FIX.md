# Semantic Analysis Modes - Fixing the Hardcoded List Problem

## The Problem 💀

The semantic analyzer had a **fundamental design flaw**:

```cpp
// HARDCODED LIST - ALWAYS OUTDATED
void SemanticAnalyzer::initializeKnownModules() {
    knownModules_["math"] = {"sqrt", "abs", "sin"};
    knownModules_["window"] = {"title", "class", "pid"};
    knownModules_["mouse"] = {"move", "click"};
    // ... 500+ lines of hardcoded functions
}
```

**Why this is wrong:**

1. **Modules are registered dynamically at runtime** - from C++ modules, Havel scripts, plugins
2. **Semantic analyzer runs at compile-time** - has NO IDEA what modules will be available
3. **Hardcoded list is always outdated** - new modules break semantic analysis
4. **Perpetual maintenance burden** - every new module requires updating the hardcoded list

## The Solution ✅

**Three-tier semantic analysis modes:**

```cpp
enum class SemanticMode {
    None,    // Skip semantic analysis (AHK-style, runtime errors only)
    Basic,   // Check variables, functions, duplicates (NO module checking) ← DEFAULT
    Strict   // Full checking including modules (for development)
};
```

### Mode Comparison

| Feature | None | Basic (Default) | Strict |
|---------|------|-----------------|--------|
| Undefined variables | ❌ | ✅ | ✅ |
| Duplicate definitions | ❌ | ✅ | ✅ |
| Return outside function | ❌ | ✅ | ✅ |
| Break/continue outside loop | ❌ | ✅ | ✅ |
| **Module checking** | ❌ | ❌ | ✅ |
| **Builtin checking** | ❌ | ❌ | ✅ |
| **False positives** | None | None | Possible |

### Usage

```cpp
// In Interpreter.cpp - uses Basic mode by default
semantic::SemanticAnalyzer semanticAnalyzer;
semanticAnalyzer.setMode(semantic::SemanticMode::Basic);  // ← DEFAULT
bool semanticOk = semanticAnalyzer.analyze(program);
```

```havel
// Your scripts now work without module errors!
detectSystem()          // ✅ No error in Basic mode
run("notepad.exe")      // ✅ No error in Basic mode
send("Hello")           // ✅ No error in Basic mode
window.title            // ✅ No error in Basic mode
```

## Why Basic Mode is the Default

**AHK Philosophy:** Scripts should run, not fail at compile-time because of missing modules.

```havel
// AutoHotkey doesn't check if modules exist at compile-time
; Run "notepad.exe"  ; ← Doesn't check if Run exists
; Send "Hello"       ; ← Doesn't check if Send exists
; WinTitle := WinGetTitle("ahk_class Notepad")  ; ← Doesn't check module

; Havel now works the same way in Basic mode:
run("notepad.exe")    // ✅ Runs fine, fails at runtime if run() doesn't exist
send("Hello")         // ✅ Runs fine, fails at runtime if send() doesn't exist
```

**Benefits:**

1. **No false positives** - Scripts don't fail because of outdated module lists
2. **Dynamic modules work** - User-created modules, plugins, runtime-loaded modules all work
3. **AHK-compatible** - Same philosophy: let it run, fail at runtime if needed
4. **Maintainable** - No more 500-line hardcoded lists to maintain

## When to Use Each Mode

### None Mode
- **Use case:** Maximum performance, no checks
- **Example:** Production deployment where you trust the code
- **Risk:** All errors are runtime errors

### Basic Mode (Default) ⭐
- **Use case:** Normal development and production
- **Catches:** Undefined variables, duplicates, control flow errors
- **Ignores:** Module availability (handled at runtime)
- **Best for:** 99% of use cases

### Strict Mode
- **Use case:** Developing core modules, CI/CD validation
- **Catches:** Everything including module availability
- **Risk:** False positives for dynamic modules
- **Best for:** Core Havel development, not user scripts

## Implementation

### Header Changes

```cpp
// SemanticAnalyzer.hpp
enum class SemanticMode {
    None,
    Basic,
    Strict
};

class SemanticAnalyzer {
public:
    void setMode(SemanticMode mode);
    SemanticMode getMode() const;
    
private:
    SemanticMode mode_ = SemanticMode::Basic;  // ← DEFAULT
};
```

### Runtime Changes

```cpp
// Interpreter.cpp
semantic::SemanticAnalyzer semanticAnalyzer;
semanticAnalyzer.setMode(semantic::SemanticMode::Basic);  // ← Set default
bool semanticOk = semanticAnalyzer.analyze(*programPtr);
```

### Checking Logic

```cpp
// SemanticAnalyzer.cpp - Only check modules in Strict mode
if (mode_ == SemanticMode::Strict && call.callee->kind == ast::NodeType::MemberExpression) {
    // Check module.function() calls
    if (!isKnownModuleFunction(module, func)) {
        reportError(UndefinedModuleFunction, ...);
    }
}

// In Basic/None mode, skip module checking entirely
```

## Migration Guide

### If You Were Using the Old System

**Before:**
```
Your script → Semantic error: "Undefined module: audio"
You → Add "audio" to hardcoded list
Your script → Semantic error: "Undefined function: setAppVolume"
You → Add "setAppVolume" to hardcoded list
... (repeat 47 times)
```

**After:**
```
Your script → ✅ Passes semantic analysis (Basic mode)
Runtime → Executes with actual modules
If module missing → Runtime error (where it belongs)
```

### If You Want Strict Checking

For core Havel development:

```cpp
// In your custom runner
semantic::SemanticAnalyzer analyzer;
analyzer.setMode(semantic::SemanticMode::Strict);  // Enable full checking
analyzer.analyze(program);
```

## Examples

### Script That Works in Basic Mode

```havel
// Uses modules that may or may not be loaded
detectSystem()
let sys = detectSystem()

if sys.windowManager == "hyprland" {
    run("hyprland-config-tool")
}

// Audio module (may not be available)
audio.setAppVolume("Spotify", 0.5)

// OCR module (may not be available)
let text = ocr.read(screen.region(0, 0, 100, 100))
```

**Result:**
- **None mode:** ✅ No checks, runs immediately
- **Basic mode:** ✅ Passes (checks variables, not modules)
- **Strict mode:** ❌ Fails if modules not in hardcoded list

### Script With Actual Errors (Caught in Basic Mode)

```havel
let x = 10
let x = 20  // ❌ Duplicate definition (caught!)

fn add(a, b) {
    return a + b
}

print(undefinedVar)  // ❌ Undefined variable (caught!)

return 42  // ❌ Return outside function (caught!)
```

**Result:**
- **None mode:** ❌ No errors caught
- **Basic mode:** ✅ All 3 errors caught
- **Strict mode:** ✅ All 3 errors caught + module checks

## Conclusion

The hardcoded module list was a **fundamental design flaw** that:
- ❌ Always lagged behind actual module availability
- ❌ Required constant maintenance
- ❌ Caused false positives for valid scripts
- ❌ Went against AHK's "let it run" philosophy

The three-tier mode system **fixes this** by:
- ✅ Basic mode (default) - checks code, not modules
- ✅ Strict mode - for core development when needed
- ✅ None mode - for maximum performance
- ✅ No maintenance burden - modules checked at runtime where they belong

**Default to Basic mode. Use Strict only for core Havel development.**
