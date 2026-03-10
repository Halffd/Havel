# Semantic Analyzer - Module and Builtin Detection

## Overview

The SemanticAnalyzer now detects missing modules, module functions, and builtin functions **before execution**, providing helpful error messages during the compilation phase.

## Features

### 1. Undefined Module Detection ✅

Detects when code tries to use a module that doesn't exist:

```havel
let x = nonexistentModule.someFunction()
```

**Error:**
```
╭─ Semantic Analysis Errors (2 errors found)
│
│ [ERROR line 4:9] Undefined variable: nonexistentModule
│
│ [ERROR line 4:39] Undefined module: nonexistentModule
│
╰─ Semantic analysis failed
```

### 2. Undefined Module Function Detection ✅

Detects when code tries to call a function that doesn't exist in a module:

```havel
let x = math.nonexistentFunction()
```

**Error:**
```
╭─ Semantic Analysis Errors (1 errors found)
│
│ [ERROR line 4:33] Undefined function 'nonexistentFunction' in module 'math'
│
╰─ Semantic analysis failed
```

### 3. Undefined Builtin Detection ✅

Detects when code tries to call a builtin function that doesn't exist:

```havel
undefinedFunction()
```

**Error:**
```
╭─ Semantic Analysis Errors (1 errors found)
│
│ [ERROR line 1:1] Undefined function: undefinedFunction
│
╰─ Semantic analysis failed
```

## Known Modules

The following modules are recognized by the semantic analyzer:

| Module | Functions |
|--------|-----------|
| **audio** | getVolume, setVolume, increaseVolume, decreaseVolume, toggleMute, setMute, isMuted, getDevices, setDeviceVolume, getDeviceVolume, getApplications, setAppVolume, getAppVolume, increaseAppVolume, decreaseAppVolume |
| **brightnessManager** | getBrightness, setBrightness, increaseBrightness, decreaseBrightness, getTemperature, setTemperature, increaseTemperature, decreaseTemperature, getShadowLift, setShadowLift, setGammaRGB, increaseGamma, decreaseGamma |
| **math** | abs, ceil, floor, round, sin, cos, tan, asin, acos, atan, atan2, sinh, cosh, tanh, exp, log, log10, log2, sqrt, cbrt, pow, min, max, clamp, lerp, random, randint, deg2rad, rad2deg, sign, fract, mod, distance, hypot |
| **string** | upper, lower, trim, replace, split, join, length, substr, find, contains |
| **file** | read, write, exists, size, delete, copy, move, listDir, mkdir, isFile, isDir |
| **process** | run, find, list, kill, getPid, getName |
| **window** | active, list, focus, minimize, maximize, close, move, resize, getMonitors, getMonitorArea, moveToNextMonitor |
| **mode** | get, set, previous, is |
| **io** | send, sendKey, keyDown, keyUp, map, remap, block, unblock, suspend, resume, grab, ungrab, keyTap, mouseMove, mouseMoveTo, mouseClick, mouseDoubleClick, mousePress, mouseRelease, mouseScroll, mouseGetPosition, mouseSetSensitivity, mouseGetSensitivity, getCurrentModifiers |
| **system** | notify, run, beep, sleep |
| **clipboard** | get, set, clear |
| **timer** | setTimeout, setInterval, clear |
| **app** | enableReload, disableReload, toggleReload, getScriptPath |
| **http** | get, post, download |
| **browser** | connect, open, setZoom, getZoom, resetZoom, click, type, eval |
| **help** | list, show |

## Known Builtins

The following builtin functions are recognized:

- `print`, `println`, `error`, `warn`, `info`, `debug`
- `len`, `type`
- `sqrt`, `abs`, `sin`, `cos`, `tan`, `PI`, `E`
- `lower`, `upper`
- `sleep`, `exit`
- `spawn`, `await`, `channel`, `yield`

## Implementation

### Files Modified

1. **`src/havel-lang/semantic/SemanticAnalyzer.hpp`**
   - Added `SemanticErrorKind::UndefinedModule`
   - Added `SemanticErrorKind::UndefinedModuleFunction`
   - Added `SemanticErrorKind::UndefinedBuiltin`
   - Added `knownModules_` map
   - Added `knownBuiltins_` set
   - Added helper methods

2. **`src/havel-lang/semantic/SemanticAnalyzer.cpp`**
   - Implemented `initializeKnownModules()`
   - Implemented `initializeKnownBuiltins()`
   - Implemented `isKnownModuleFunction()`
   - Implemented `isKnownBuiltin()`
   - Updated `visitExpression()` to check module/function calls
   - Updated `buildSymbolTable()` to register modules

### How It Works

```cpp
// In visitExpression() for CallExpression:
if (call.callee->kind == ast::NodeType::MemberExpression) {
    // Check module.function() calls
    if (!isKnownModuleFunction(module, func)) {
        if (knownModules_.count(module) > 0) {
            // Module exists but function doesn't
            reportError(UndefinedModuleFunction, ...);
        } else {
            // Module doesn't exist
            reportError(UndefinedModule, ...);
        }
    }
}
```

## Test Scripts

### test_undefined_module.hv
```havel
// This should FAIL: undefined module
let x = nonexistentModule.someFunction()
```

**Result:** ✅ Correctly detected

### test_undefined_module_func.hv
```havel
// This should FAIL: undefined function in existing module
let x = math.nonexistentFunction()
```

**Result:** ✅ Correctly detected

### test_module_syntax.hv
```havel
// Valid module references - should pass
let audioModule = audio
let mathModule = math
```

**Result:** ✅ Passes semantic analysis

## Benefits

1. **Early Error Detection** - Catches typos and mistakes before execution
2. **Better Error Messages** - Clear indication of what's missing
3. **IDE Support** - Can be used for autocomplete and linting
4. **Documentation** - List of known modules serves as API documentation

## Future Enhancements

1. **Argument Count Checking** - Verify function calls have correct number of arguments
2. **Type Checking** - Verify argument types match function signatures
3. **Return Type Checking** - Verify return values are used correctly
4. **Module Loading** - Track which modules are actually loaded vs just declared

## Build Status

```
[100%] Built target havel
```

All tests pass successfully.
