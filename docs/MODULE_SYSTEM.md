# Havel Module System

## Simple, Explicit Module Loading

No singletons. No static registration. No macro magic.

## Basic Usage

### Register Modules

```cpp
ModuleLoader loader;

// Register standard library modules
loader.add("array", registerArrayModule);
loader.add("string", registerStringModule);
loader.add("math", registerMathModule);

// Register host modules
loader.addHost("window", registerWindowModule);
loader.addHost("audio", registerAudioModule);
```

### Load Modules

```cpp
// Load specific module
loader.load(env, "array");
loader.load(env, "window", hostAPI);  // Host module needs hostAPI

// Load all registered modules
loader.loadAll(env, hostAPI);
```

## Module Structure

### Standard Library Module

```cpp
// ArrayModule.hpp
#pragma once
#include "../runtime/Environment.hpp"

namespace havel::stdlib {
    void registerArrayModule(Environment& env);
}
```

```cpp
// ArrayModule.cpp
#include "ArrayModule.hpp"

namespace havel::stdlib {

void registerArrayModule(Environment& env) {
    env.Define("push", HavelValue(BuiltinFunction(...)));
    env.Define("pop", HavelValue(BuiltinFunction(...)));
}

} // namespace havel::stdlib
```

### Host Module

```cpp
// WindowModule.cpp
#include "../runtime/ModuleLoader.hpp"

namespace havel::modules {

void registerWindowModule(Environment& env, IHostAPI* hostAPI) {
    if (!hostAPI) return;
    
    env.Define("focus", HavelValue(BuiltinFunction([hostAPI](...) {
        hostAPI->FocusWindow(...);
    })));
}

} // namespace havel::modules
```

## Registration Pattern

### In Interpreter Initialization

```cpp
Interpreter::Interpreter() {
    env = std::make_shared<Environment>();
    
    // Create module loader
    ModuleLoader loader;
    
    // Register modules
    registerStdLibModules(loader);
    registerHostModules(loader);
    
    // Load modules
    loadStdLibModules(*env, loader);
    loadHostModules(*env, loader, hostAPI);
}
```

### Separate Registration Function

```cpp
// StdLibModules.cpp
void registerStdLibModules(ModuleLoader& loader) {
    loader.add("array", registerArrayModule);
    loader.add("string", registerStringModule);
    loader.add("math", registerMathModule);
}
```

## Best Practices

### 1. One Function Per Module

```cpp
// Good
void registerArrayModule(Environment& env);

// Bad - multiple registration functions
void registerArrayFunctions(Environment& env);
void registerArrayMethods(Environment& env);
```

### 2. Explicit Dependencies

```cpp
// Good - explicit
void registerJsonModule(Environment& env) {
    registerArrayModule(env);  // Explicit dependency
    registerStringModule(env);
    // ... json registration
}

// Bad - implicit via macro
REGISTER_MODULE_WITH_DEPS(json, (array, string))
```

### 3. Host Modules Check hostAPI

```cpp
void registerWindowModule(Environment& env, IHostAPI* hostAPI) {
    if (!hostAPI) {
        havel::error("Window module requires host API");
        return;
    }
    // ... registration
}
```

### 4. Group Related Modules

```cpp
void registerStdLibModules(ModuleLoader& loader) {
    // Core types
    loader.add("array", registerArrayModule);
    loader.add("string", registerStringModule);
    loader.add("object", registerObjectModule);
    
    // Utilities
    loader.add("math", registerMathModule);
    loader.add("file", registerFileModule);
    loader.add("regex", registerRegexModule);
}
```

## Testing

```cpp
TEST(ArrayModule, Push) {
    Environment env;
    ModuleLoader loader;
    loader.add("array", registerArrayModule);
    loader.load(env, "array");
    
    // Test array functions
    auto result = env.Execute("push([1,2], 3)");
    EXPECT_EQ(result, "[1, 2, 3]");
}

TEST(WindowModule, Focus) {
    Environment env;
    MockHostAPI mockHost;
    ModuleLoader loader;
    loader.addHost("window", registerWindowModule);
    
    EXPECT_CALL(mockHost, FocusWindow(_)).Times(1);
    loader.load(env, "window", &mockHost);
}
```

## Migration from Old System

### Before

```cpp
// In Interpreter.cpp
void Interpreter::InitializeStandardLibrary() {
    registerArrayModule(environment.get());
    registerMathModule(environment.get());
    // ...
}
```

### After

```cpp
// In Interpreter.cpp
void Interpreter::InitializeStandardLibrary() {
    ModuleLoader loader;
    registerStdLibModules(loader);
    loadStdLibModules(*environment, loader);
}
```

## File Organization

```
src/
  havel-lang/
    runtime/
      ModuleLoader.hpp       # Core module loader
      StdLibModules.hpp      # Stdlib registration
      StdLibModules.cpp
    stdlib/
      ArrayModule.hpp
      ArrayModule.cpp
      StringModule.hpp
      StringModule.cpp
      ...
  modules/
    window/
      WindowModule.cpp
    audio/
      AudioModule.cpp
    ...
```

## Key Points

1. **No singletons** - Each interpreter has its own ModuleLoader
2. **No static registration** - Explicit registration in code
3. **No macros** - Simple function calls
4. **Clear separation** - Stdlib vs host modules
5. **Test-friendly** - Easy to test modules in isolation
