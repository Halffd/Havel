---
title: "Adding Host Functions"
description: "How to add new C++ host functions and modules to Havel."
---

# Adding Host Functions

## Overview

Host functions are C++ functions exposed to Havel scripts. They are registered via the host module system.

---

## Simple Host Function

### 1. Create Function

```cpp
// In a bridge module (e.g., src/host/module/MyBridge.cpp)
Value myCustomFunction(const std::vector<Value>& args, HostContext* ctx) {
    // Validate args
    if (args.size() != 2) {
        throw ScriptThrow{Value::makeString("myCustomFunction expects 2 arguments")};
    }
    
    int a = args[0].asInt();
    int b = args[1].asInt();
    
    // Do work
    int result = a + b;
    
    return Value::makeInt(result);
}
```

### 2. Register in Bridge

```cpp
// In MyBridge::install()
options.host_functions["my.customFunction"] = [ctx = ctx_](const auto& args) {
    return myCustomFunction(args, ctx);
};
```

### 3. Use in Havel

```hv
result = my.customFunction(3, 4)  // 7
```

---

## Host Module (Recommended)

### 1. Create Module Class

```cpp
// src/host/module/MyModule.hpp
#pragma once
#include "host/module/HostModule.hpp"

class MyModule : public havel::host::HostModule {
public:
    void registerFunctions(VMApi& api) override;
};
```

```cpp
// src/host/module/MyModule.cpp
#include "MyModule.hpp"
#include "havel-lang/compiler/vm/VMApi.hpp"

using namespace havel::compiler;

void MyModule::registerFunctions(VMApi& api) {
    // Simple function
    api.registerHostFunction("my.add", [](const std::vector<Value>& args) -> Value {
        double a = args[0].asNumber();
        double b = args[1].asNumber();
        return Value::makeNumber(a + b);
    });
    
    // Function with context
    api.registerHostFunction("my.greet", [](const std::vector<Value>& args) -> Value {
        std::string name = args[0].asString();
        return Value::makeString("Hello, " + name + "!");
    });
    
    // Function returning object
    api.registerHostFunction("my.getInfo", [](const std::vector<Value>& args) -> Value {
        auto obj = Value::makeObject();
        obj.setField("version", Value::makeString("1.0.0"));
        obj.setField("author", Value::makeString("Me"));
        return obj;
    });
}
```

### 2. Register Module

In `src/host/module/ModularHostBridges.cpp` or your bridge's `install()`:

```cpp
void MyBridge::install(PipelineOptions& options) {
    // ... existing registrations ...
    
    // Register module
    options.host_modules.push_back(std::make_unique<MyModule>());
}
```

### 3. Use in Havel

```hv
use my

sum = my.add(1.5, 2.5)      // 4.0
greeting = my.greet("world") // "Hello, world!"
info = my.getInfo()          // { version: "1.0.0", author: "Me" }
```

---

## Prototype Methods (Object-Oriented)

For methods on types (e.g., `Hotkey.enable()`):

```cpp
api.registerPrototypeMethod("MyType", "myMethod", 1, [&vm](const auto& args) -> Value {
    // args[0] is the receiver (self)
    auto* obj = args[0].asExtern<MyType>();
    obj->doSomething(args[1].asInt());
    return Value::makeNil();
});
```

This registers:
1. `vm.registerHostFunction("MyType.myMethod", ...)`
2. `vm.registerPrototypeMethodByName("MyType", "myMethod", "MyType.myMethod")`

---

## Value Conversion

### From Havel to C++

| Havel Type | C++ Extraction |
|------------|----------------|
| `int` | `args[i].asInt()` → `int64_t` |
| `num` | `args[i].asNumber()` → `double` |
| `bool` | `args[i].asBool()` → `bool` |
| `str` | `args[i].asString()` → `std::string` |
| `array` | `args[i].asArray()` → `std::vector<Value>&` |
| `object` | `args[i].asObject()` → `std::unordered_map<std::string, Value>&` |
| `fn` | `args[i].asFunction()` → `FunctionRef` |
| `pointer` | `args[i].asPointer()` → `void*` |
| `extern` | `args[i].asExtern<T>()` → `T*` |

### From C++ to Havel

| C++ Type | Havel Value Creation |
|----------|---------------------|
| `int64_t` | `Value::makeInt(val)` |
| `double` | `Value::makeNumber(val)` |
| `bool` | `Value::makeBool(val)` |
| `std::string` | `Value::makeString(val)` |
| `std::vector<Value>` | `Value::makeArray(vec)` |
| `std::unordered_map<...>` | `Value::makeObject()` + `setField` |
| `void*` | `Value::makePointer(ptr)` |
| `T*` (extern) | `Value::makeExtern("TypeName", ptr, finalizer)` |
| `nil` | `Value::makeNil()` |

---

## Async Host Functions

For long-running operations, defer to VM thread:

```cpp
api.registerHostFunction("my.asyncTask", [&vm](const std::vector<Value>& args) -> Value {
    auto callback = args[0].asFunction();
    
    // Schedule on VM thread
    vm.deferToVM([vm = &vm, callback]() {
        // Do work...
        Value result = Value::makeString("done");
        vm->callFunction(callback, {result});
    });
    
    return Value::makeNil();  // Returns immediately
});
```

---

## Thread-Safe Globals

```cpp
// From any thread
vm.setGlobalThreadSafe("myGlobal", Value::makeInt(42));

// Read from any thread
Value val = vm.getGlobalThreadSafe("myGlobal");
```

Uses `std::shared_mutex` internally.

---

## Error Handling

```cpp
api.registerHostFunction("my.risky", [](const std::vector<Value>& args) -> Value {
    try {
        // risky operation
        return Value::makeString("success");
    } catch (const std::exception& e) {
        // Convert to script exception
        throw ScriptThrow{Value::makeString(e.what())};
    }
});
```

In Havel:
```hv
try {
    result = my.risky()
} catch {
    print("Error: {it}")
}
```

---

## Best Practices

1. **Validate arguments** — Check count and types early
2. **Use meaningful names** — `my.module.function` not `m.f`
3. **Return consistent types** — Don't return `int` sometimes, `str` others
4. **Document in module** — Add comments for `help()` system
5. **Handle nil** — Check `args[i].isNil()` before extraction
6. **Use finalizers** for extern types that own memory
7. **Keep functions pure** when possible (easier testing)

---

**Previous:** [Code Style](/contributing/code-style)
**Next:** [Extending Self-Hosted Compiler →](/contributing/extending-self-hosted)