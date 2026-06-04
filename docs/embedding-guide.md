# Embedding Guide

How to embed the Havel language runtime into a C++ application — VM setup, host function registration, module loading, and execution.

---

## Overview

Havel's VM can be embedded in any C++23 application. The core components you need:

1. **VM** — the virtual machine that executes bytecode
2. **Pipeline** — the compiler frontend (lexer → parser → semantic → bytecode)
3. **Host functions** — C++ functions exposed to Havel scripts
4. **Module loader** — resolves and loads Havel modules

---

## Minimal Embedding

### Include Headers

```cpp
#include "havel-lang/compiler/vm/VM.hpp"
#include "havel-lang/compiler/core/Pipeline.hpp"
#include "havel-lang/stdlib/StdLibModules.hpp"
```

### Create and Run

```cpp
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

## VM Initialization

### Configuration

```cpp
havel::compiler::VMConfig config;
config.enable_debug = false;       // Debug output
config.enable_jit = true;          // LLVM JIT (if available)
config.max_stack_size = 1024;      // Max stack depth
config.gc_threshold = 1024 * 1024; // GC trigger threshold (bytes)

havel::compiler::VM vm(config);
```

### Registering Host Functions

Host functions are C++ `std::function<Value(const std::vector<Value>&)>` exposed to Havel:

```cpp
vm.registerHostFunction("myapp.getVersion", [](const std::vector<Value>& args) -> Value {
    return Value::makeString("1.0.0");
});

vm.registerHostFunction("myapp.add", [](const std::vector<Value>& args) -> Value {
    double a = args[0].asNumber();
    double b = args[1].asNumber();
    return Value::makeNumber(a + b);
});
```

In Havel scripts:

```
version = myapp.getVersion()
sum = myapp.add(3, 4)
```

### Registering as Global

To make a function available as a bare global (no namespace):

```cpp
vm.registerHostFunction("myFunc", handler);  // callable as myFunc()
vm.registerGlobal("myFunc", Value::makeHostFunction("myFunc", handler));
```

---

## Host Module System

### Creating a Module

A module is a collection of related host functions registered under a namespace:

```cpp
class MyAppModule : public havel::compiler::HostModule {
public:
    void registerFunctions(VMApi& api) override {
        api.registerHostFunction("myapp.getData", [](const std::vector<Value>& args) {
            return Value::makeString("data");
        });

        api.registerHostFunction("myapp.process", [](const std::vector<Value>& args) {
            // ... processing logic
            return Value::makeNil();
        });
    }
};
```

### Registering the Module

```cpp
vm.registerModule<MyAppModule>();
```

### Module in Havel

```
use myapp               // imports all from myapp module
data = myapp.getData()
```

---

## Value Types

### Creating Values

| C++ Method | Havel Type | Example |
|------------|------------|---------|
| `Value::makeInt(42)` | `int` | Integer |
| `Value::makeNumber(3.14)` | `num` | Float |
| `Value::makeString("hello")` | `str` | String |
| `Value::makeBool(true)` | `bool` | Boolean |
| `Value::makeNil()` | `nil` | Null |
| `Value::makeArray({v1, v2})` | `array` | Array |
| `Value::makeObject()` | `object` | Object |

### Reading Values

| C++ Method | Returns | Notes |
|------------|---------|-------|
| `val.isInt()` | bool | Check type |
| `val.isNumber()` | bool | Check type |
| `val.isString()` | bool | Check type |
| `val.asInt()` | int64_t | Cast to int |
| `val.asNumber()` | double | Cast to double |
| `val.asString()` | std::string | Cast to string |
| `val.asBool()` | bool | Cast to bool |
| `val.toString()` | std::string | Debug representation |

---

## Execution Methods

### Run a String

```cpp
Value result = vm.runString("1 + 2");
// result == Value::makeInt(3)
```

### Run a File

```cpp
Value result = vm.runFile("script.hv");
```

### Run with Persistent Globals (REPL-style)

```cpp
// Execute line-by-line, preserving global state
vm.executePersistent("x = 5");
vm.executePersistent("y = x + 1");
Value y = vm.getGlobal("y");  // y == 6
```

### Load a Script from Another Script

The `load()` host function is automatically available in scripts:

```
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

Override module resolution for custom import paths:

```cpp
class CustomModuleLoader : public havel::compiler::ModuleLoader {
public:
    std::optional<std::string> resolve(const std::string& name) override {
        // Look up module by name
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

### Spawning Goroutines

Goroutines are cooperative and run on the VM thread. From C++, you can queue work:

```cpp
// Schedule a callback on the VM's event loop
vm.deferToVM([]() {
    // This runs on the VM thread, safe to interact with VM state
    vm.setGlobal("backgroundResult", Value::makeString("done"));
});
```

### OS Threads

For true parallelism, use `std::thread` with thread-safe global access:

```cpp
std::thread worker([&vm]() {
    // Do CPU-intensive work...
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
| `ENABLE_HAVEL_LANG` | Auto | Enable Havel language (auto-on if LLVM found) |
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

For server/embedded use without a display:

```cmake
# Build with mode 12 or 15 — no Qt, no GUI
set(HEADLESS_BUILD ON)
```

Headless mode removes all GUI dependencies. X11 input handling is still available for hotkey registration on Linux servers with Xvfb.

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
