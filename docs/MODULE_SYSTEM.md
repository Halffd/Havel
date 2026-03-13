# Havel Module System Guide

## Overview

Havel uses a clean module system that separates:
- **Standard Library** - Portable, OS-agnostic functions
- **Host Modules** - OS-specific integrations (window, audio, hotkeys, etc.)

## Module Structure

### Standard Library Module

```cpp
// MyModule.hpp
#pragma once
#include "../havel-lang/runtime/Environment.hpp"

namespace havel::modules {
    void registerMyModule(Environment& env);
}
```

```cpp
// MyModule.cpp
#include "MyModule.hpp"
#include "../havel-lang/runtime/ModuleMacros.hpp"

namespace havel::modules {

STD_MODULE_DESC(my, "My custom module") {
    // Define functions
    env.Define("hello", HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        return HavelValue("Hello from my module!");
    }));
}

} // namespace havel::modules
```

### Host Module

```cpp
// WindowModule.cpp
#include "../havel-lang/runtime/ModuleMacros.hpp"

HOST_MODULE_DESC(window, "Window management") {
    // Check if hostAPI is available
    if (!hostAPI) {
        havel::error("Window module requires host API");
        return;
    }
    
    // Use hostAPI to access window operations
    env.Define("focus", HavelValue(BuiltinFunction([hostAPI](...) {
        // hostAPI->FocusWindow(...);
    }));
}
```

## Registration Macros

### Standard Library

```cpp
// Simple registration
STD_MODULE(array) {
    // Module code
}

// With custom description
STD_MODULE_DESC(math, "Mathematical functions") {
    // Module code
}

// With dependencies
STD_MODULE_DEPS(json, (array, string)) {
    // Can use array and string functions
}
```

### Host Modules

```cpp
// Simple registration
HOST_MODULE(window) {
    // Module code with hostAPI access
}

// With custom description
HOST_MODULE_DESC(audio, "Audio control") {
    // Module code
}

// With dependencies
HOST_MODULE_DEPS(media, (window, config)) {
    // Can use window and config modules
}
```

## Module Loading

### Automatic Loading

Modules registered with `autoLoad = true` (default) are loaded automatically:

```cpp
// In interpreter initialization
LOAD_ALL_STD_MODULES(env, hostAPI);   // Load all stdlib modules
LOAD_ALL_HOST_MODULES(env, hostAPI);  // Load all host modules
```

### Manual Loading

```cpp
// Load specific module
LOAD_MODULE(env, array);
LOAD_HOST_MODULE(env, hostAPI, window);

// Or using API directly
ModuleRegistry::Load(env, "array");
ModuleRegistry::Load(env, "window", hostAPI);
```

### Check Module Status

```cpp
// Check if registered
if (ModuleRegistry::IsRegistered("array")) {
    // Module is available
}

// Check if loaded
if (ModuleRegistry::IsLoaded("array")) {
    // Module is already loaded
}

// Get module info
auto* info = ModuleRegistry::GetModuleInfo("array");
if (info) {
    print(info->name);
    print(info->description);
}
```

## Module Dependencies

Declare dependencies when registering:

```cpp
REGISTER_MODULE("json", "JSON parsing", registerJsonModule, true, 
                {"array", "string"});  // Depends on array and string
```

Dependencies are loaded automatically before the module.

## Best Practices

### 1. Keep Modules Focused

Each module should have a single responsibility:

```cpp
// Good
STD_MODULE(array) { ... }    // Array operations only
STD_MODULE(string) { ... }   // String operations only

// Bad
STD_MODULE(utils) { 
    // Array operations
    // String operations
    // Math operations
    // ... too many responsibilities
}
```

### 2. Use HostAPI Interface

Host modules should **only** access system features through `IHostAPI`:

```cpp
// Good
HOST_MODULE(window) {
    env.Define("focus", [hostAPI](...) {
        hostAPI->FocusWindow(...);  // Uses interface
    });
}

// Bad - direct dependency on concrete class
HOST_MODULE(window) {
    env.Define("focus", [](...) {
        WindowManager::Focus(...);  // Direct coupling!
    });
}
```

### 3. Error Handling

Always validate arguments and provide clear error messages:

```cpp
env.Define("divide", BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() < 2) {
        return HavelRuntimeError("divide() requires two arguments");
    }
    
    double b = args[1].asNumber();
    if (b == 0.0) {
        return HavelRuntimeError("divide() by zero");
    }
    
    return HavelValue(args[0].asNumber() / b);
}));
```

### 4. Document Modules

Add documentation comments:

```cpp
/**
 * Array Module
 * 
 * Provides array manipulation functions:
 * - array.push(arr, value) - Add element to end
 * - array.pop(arr) - Remove last element
 * - array.map(arr, fn) - Transform elements
 */
STD_MODULE_DESC(array, "Array operations") {
    ...
}
```

## Module Naming Conventions

- **Lowercase** - `array`, `string`, `math`
- **Descriptive** - `clipboard`, `brightness`, `screenshot`
- **No prefixes** - `window.focus` not `window.windowFocus`

## Testing Modules

### Test Standard Library Module

```cpp
TEST(ArrayModule, Push) {
    Environment env;
    registerArrayModule(env);
    
    // Test array.push functionality
    auto result = env.Execute("let arr = [1,2]; push(arr, 3); return arr");
    EXPECT_EQ(result, "[1, 2, 3]");
}
```

### Test Host Module

```cpp
TEST(WindowModule, Focus) {
    Environment env;
    MockHostAPI mockHost;
    
    EXPECT_CALL(mockHost, FocusWindow(_)).Times(1);
    
    registerWindowModule(env, &mockHost);
    env.Execute("window.focus('Firefox')");
}
```

## Migration Guide

### Old Style (Before Refactoring)

```cpp
// In Interpreter.cpp
void Interpreter::visitSomeNode(...) {
    WindowManager::Focus(...);  // Direct coupling
}
```

### New Style (After Refactoring)

```cpp
// In modules/window/WindowModule.cpp
HOST_MODULE(window) {
    env.Define("focus", [hostAPI](...) {
        hostAPI->FocusWindow(...);  // Through interface
    });
}
```

## Troubleshooting

### Module Not Loading

1. Check module is registered: `ModuleRegistry::IsRegistered("name")`
2. Check dependencies are satisfied
3. Check hostAPI is provided for host modules

### Circular Dependencies

Modules cannot have circular dependencies. Restructure:

```cpp
// Bad - circular
REGISTER_MODULE("a", ..., ..., {"b"});
REGISTER_MODULE("b", ..., ..., {"a"});

// Good - extract shared functionality
REGISTER_MODULE("common", ..., ..., {});
REGISTER_MODULE("a", ..., ..., {"common"});
REGISTER_MODULE("b", ..., ..., {"common"});
```

### Host API Not Available

```cpp
HOST_MODULE(window) {
    if (!hostAPI) {
        havel::error("Window module requires host API");
        return;
    }
    // ... rest of module
}
```

## Examples

See `src/modules/example/` for complete working examples of both standard library and host modules.
