---
title: "C++ Embedding API"
description: "Embedding Havel in C++ applications: VM setup, host functions, module loading, and execution."
---

# C++ Embedding API

## Overview

Embed Havel's VM in any C++23 application. Core components:

1. **VM** — Virtual machine executing bytecode
2. **Pipeline** — Compiler frontend (lexer → parser → semantic → bytecode)
3. **Host Functions** — C++ functions exposed to Havel
4. **Module Loader** — Resolves and loads Havel modules

**Source**: `src/havel-lang/compiler/vm/VM.hpp`, `Pipeline.hpp`, `StdLibModules.hpp`

---

## Minimal Example

```cpp
#include "havel-lang/compiler/vm/VM.hpp"
#include "havel-lang/compiler/core/Pipeline.hpp"
#include "havel-lang/stdlib/StdLibModules.hpp"

int main() {
    // 1. Create VM
    havel::compiler::VM vm;
    
    // 2. Register standard library
    havel::compiler::registerStdLibModules(vm);
    
    // 3. Compile and execute
    auto result = vm.runString("print({1 + 2})");
    // result is a Value — the last expression result
    
    return 0;
}
```

---

## VM Configuration

```cpp
havel::compiler::VMConfig config;
config.enable_debug = false;           // Debug output
config.enable_jit = true;              // LLVM JIT (if available)
config.max_stack_size = 1024;          // Max stack depth
config.gc_threshold = 1024 * 1024;     // GC trigger (bytes)

havel::compiler::VM vm(config);
```

---

## Registering Host Functions

Host functions are `std::function<Value(const std::vector<Value>&)>`:

```cpp
// Simple function
vm.registerHostFunction("myapp.getVersion", [](const std::vector<Value>& args) -> Value {
    return Value::makeString("1.0.0");
});

// With arguments
vm.registerHostFunction("myapp.add", [](const std::vector<Value>& args) -> Value {
    double a = args[0].asNumber();
    double b = args[1].asNumber();
    return Value::makeNumber(a + b);
});
```

In Havel:
```hv
version = myapp.getVersion()
sum = myapp.add(3, 4)
```

### Register as Global (No Namespace)

```cpp
vm.registerHostFunction("myFunc", handler);
vm.registerGlobal("myFunc", Value::makeHostFunction("myFunc", handler));
```

Call as `myFunc()` in Havel.

---

## Host Module System

### Creating a Module

```cpp
class MyAppModule : public havel::compiler::HostModule {
public:
    void registerFunctions(VMApi& api) override {
        api.registerHostFunction("myapp.getData", [](const std::vector<Value>& args) {
            return Value::makeString("data");
        });
        
        api.registerHostFunction("myapp.process", [](const std::vector<Value>& args) {
            // processing logic
            return Value::makeNil();
        });
    }
};
```

### Registering the Module

```cpp
vm.registerModule<MyAppModule>();
```

### Using in Havel

```hv
use myapp
data = myapp.getData()
```

---

## Value Types

### Creating Values

| C++ Method | Havel Type |
|------------|------------|
| `Value::makeInt(42)` | `int` |
| `Value::makeNumber(3.14)` | `num` |
| `Value::makeString("hello")` | `str` |
| `Value::makeBool(true)` | `bool` |
| `Value::makeNil()` | `nil` |
| `Value::makeArray({v1, v2})` | `array` |
| `Value::makeObject()` | `object` |
| `Value::makeHostFunction(name, fn)` | `fn` |

### Reading Values

| C++ Method | Returns |
|------------|---------|
| `val.isInt()` | bool |
| `val.isNumber()` | bool |
| `val.isString()` | bool |
| `val.isBool()` | bool |
| `val.asInt()` | int64_t |
| `val.asNumber()` | double |
| `val.asString()` | std::string |
| `val.asBool()` | bool |
| `val.toString()` | std::string (debug) |

---

## Execution Methods

### Run String

```cpp
Value result = vm.runString("1 + 2");
// result == Value::makeInt(3)
```

### Run File

```cpp
Value result = vm.runFile("script.hv");
```

### Persistent Globals (REPL-style)

```cpp
vm.executePersistent("x = 5");
vm.executePersistent("y = x + 1");
Value y = vm.getGlobal("y");  // y == 6
```

### Load Script from Another Script

The `load()` host function is automatically available:

```hv
load("utils.hv")       // merges definitions into current scope
```

---

## Interacting with Havel Objects

### Setting Globals from C++

```cpp
vm.setGlobal("config", Value::makeString("production"));
vm.setGlobal("maxRetries", Value::makeInt(3));
```

### Reading Globals from C++

```cpp
Value config = vm.getGlobal("config");
std::string mode = config.asString();
```

### Thread-Safe Global Access

```cpp
vm.setGlobalThreadSafe("sharedCounter", Value::makeInt(0));
Value counter = vm.getGlobalThreadSafe("sharedCounter");
```

---

## Custom Module Loader

```cpp
class CustomModuleLoader : public havel::compiler::ModuleLoader {
public:
    std::optional<std::string> resolve(const std::string& name) override {
        std::string path = "/opt/myapp/modules/" + name + ".hv";
        if (std::filesystem::exists(path)) {
            return path;
        }
        return std::nullopt;
    }
};

vm.setModuleLoader(std::make_unique<CustomModuleLoader>());
```

---

## Error Handling

### Catching Script Errors

```cpp
try {
    vm.runString("throw \"something went wrong\"");
} catch (const havel::compiler::ScriptError& e) {
    std::cerr << "Script error: " << e.message << std::endl;
    std::cerr << "At: " << e.location.file << ":" << e.location.line << std::endl;
    std::cerr << "Stack trace:\n" << e.stackTrace << std::endl;
}
```

### Compile-Time Errors

```cpp
try {
    vm.runString("fn (");   // incomplete syntax
} catch (const std::runtime_error& e) {
    std::cerr << "Compile error: " << e.what() << std::endl;
}
```

---

## Concurrency from C++

### Scheduling on VM Thread

```cpp
// Queue callback on VM's event loop
vm.deferToVM([]() {
    // Runs on VM thread, safe to interact with VM state
    vm.setGlobal("backgroundResult", Value::makeString("done"));
});
```

### OS Threads with Thread-Safe Access

```cpp
std::thread worker([&vm]() {
    // CPU-intensive work...
    vm.setGlobalThreadSafe("result", Value::makeInt(42));
});
worker.detach();
```

---

## CMake Integration

### As Subdirectory

```cmake
add_subdirectory(havel-lang)
target_link_libraries(myapp PRIVATE havel-lang::havel-lang)
```

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `ENABLE_HAVEL_LANG` | Auto | Enable Havel language |
| `ENABLE_LLVM` | Auto | LLVM JIT compilation |
| `ENABLE_TESTS` | On | Build test suite |
| `ENABLE_ASAN` | On (Debug) | AddressSanitizer |
| `ENABLE_UBSAN` | On (Debug) | UndefinedBehaviorSanitizer |

### Required C++ Standard

```cmake
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
```

---

## Headless Mode

For server/embedded use without display:

```cmake
# Build with mode 12 or 15 — no Qt, no GUI
set(HEADLESS_BUILD ON)
```

Headless removes GUI dependencies. X11 input handling still available for hotkeys on Linux servers with Xvfb.

---

## Complete Example

```cpp
#include "havel-lang/compiler/vm/VM.hpp"
#include "havel-lang/compiler/core/Pipeline.hpp"
#include "havel-lang/stdlib/StdLibModules.hpp"

int main() {
    havel::compiler::VM vm;
    havel::compiler::registerStdLibModules(vm);

    // Register custom host functions
    vm.registerHostFunction("app.greet", [](const std::vector<Value>& args) {
        std::string name = args[0].asString();
        return Value::makeString("Hello, " + name + "!");
    });

    // Set initial globals
    vm.setGlobal("appName", Value::makeString("MyApp"));

    // Run a script
    try {
        Value result = vm.runString(R"(
            greeting = app.greet(appName)
            print(greeting)
            greeting
        )");
        // result == "Hello, MyApp!"
    } catch (const havel::compiler::ScriptError& e) {
        std::cerr << "Error: " << e.message << "\n";
        return 1;
    }

    return 0;
}
```

---

**Previous:** [Self-Hosted vs Bootstrap](/compiler/self-hosted)
**Next:** [Guides →](/guides/first-hotkey)