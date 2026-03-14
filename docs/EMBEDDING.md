# Havel Embedding Guide

Clean, embeddable C++ API for Havel language. Follows modern direct API design (QuickJS/Wren style) - no stack manipulation required.

## Quick Start

```cpp
#include <Havel.hpp>

int main() {
    havel::VM vm;
    
    // Load and run code
    vm.load(R"(
        fn add(a, b) { return a + b }
        print(add(5, 3))  // 8
    )");
    
    return 0;
}
```

## Core Concepts

### VM

The `VM` class is your main interface to the Havel interpreter.

```cpp
havel::VM vm;

// Load code
auto result = vm.load("print('Hello')");
if (!result) {
    std::cerr << "Error: " << result.error << std::endl;
}

// Load from file
vm.loadFile("script.hv");

// Call function
auto ret = vm.call("myFunction", {arg1, arg2});
```

### Value

Everything in Havel is a `Value`:

```cpp
Value v;

v = Value();           // nil
v = Value(true);       // bool
v = Value(42);         // number
v = Value(3.14);       // number
v = Value("hello");    // string

// Type checks
if (v.isNumber()) { ... }
if (v.isString()) { ... }
if (v.isFunction()) { ... }

// Conversions
bool b = v.asBool();
double n = v.asNumber();
std::string s = v.asString();
```

### Result

Operations that can fail return `Result<T>`:

```cpp
auto result = vm.load(code);

if (result.ok) {
    // Success
    Value val = *result;
} else {
    // Error
    std::cerr << result.error << std::endl;
}

// Or use boolean conversion
if (!result) {
    std::cerr << result.error << std::endl;
}
```

## Registering Native Functions

### Simple Function

```cpp
vm.registerFn("print", [](VM& vm, const std::vector<Value>& args) {
    for (const auto& arg : args) {
        std::cout << arg.toString() << " ";
    }
    std::cout << std::endl;
    return Value();
});
```

### Function with Error Handling

```cpp
vm.registerFn("divide", [](VM& vm, const std::vector<Value>& args) {
    if (args.size() < 2) {
        return Value();  // Return nil for error
    }
    
    double a = args[0].asNumber();
    double b = args[1].asNumber();
    
    if (b == 0.0) {
        return Value();  // Return nil for division by zero
    }
    
    return Value(a / b);
});
```

### Function with Host Context

```cpp
struct GameContext {
    int score = 0;
};

GameContext ctx;
vm.setHostContext(&ctx);

vm.registerFn("getScore", [&ctx](VM& vm, const std::vector<Value>& args) {
    return Value(static_cast<double>(ctx.score));
});

vm.registerFn("setScore", [&ctx](VM& vm, const std::vector<Value>& args) {
    if (!args.empty()) {
        ctx.score = static_cast<int>(args[0].asNumber());
    }
    return Value();
});
```

## Registering Modules

Group related functions into modules:

```cpp
vm.registerModule("math", {
    {"sqrt", [](VM& vm, auto& args) {
        return Value(std::sqrt(args[0].asNumber()));
    }},
    {"sin", [](VM& vm, auto& args) {
        return Value(std::sin(args[0].asNumber()));
    }},
    {"cos", [](VM& vm, auto& args) {
        return Value(std::cos(args[0].asNumber()));
    }}
});
```

Usage in Havel:

```havel
print(math.sqrt(16))  // 4
print(math.sin(0))    // 0
```

## Working with Arrays

```cpp
// Get array from VM
Array arr(vm.getGlobal("myArray"));

// Read
size_t len = arr.size();
Value first = arr.get(0);

// Write
arr.set(0, Value(42));
arr.push(Value(100));

// Create new array in Havel
vm.load("let arr = [1, 2, 3]");
```

## Working with Objects

```cpp
// Get object from VM
Object obj(vm.getGlobal("player"));

// Read properties
Value name = obj.get("name");
Value hp = obj.get("hp");

// Write properties
obj.set("hp", Value(100));
obj.set("pos", positionVec);

// Check existence
if (obj.has("inventory")) {
    // Has inventory
}

// List all keys
auto keys = obj.keys();
```

## Working with Structs

```cpp
// Get struct from VM
Struct vec(vm.getGlobal("myVec"));

// Get type
std::string type = vec.getType();  // "Vec2"

// Access fields
Value x = vec.getField("x");
Value y = vec.getField("y");

// Call methods
auto result = vec.callMethod("toString");
std::cout << result.asString() << std::endl;
```

## Calling Functions

### By Name

```cpp
auto result = vm.call("add", {Value(5), Value(3)});
if (result) {
    std::cout << result->asNumber() << std::endl;  // 8
}
```

### By Value

```cpp
Value func = vm.getGlobal("myFunction");
auto result = vm.call(func, {arg1, arg2});
```

## Error Handling

### Catching Runtime Errors

```cpp
auto result = vm.load(R"(
    let x = undefinedVariable  // Error!
)");

if (!result) {
    std::cerr << "Runtime error: " << result.error << std::endl;
}
```

### Try-Catch Pattern

```cpp
try {
    auto result = vm.call("riskyFunction", args);
    if (!result) {
        throw std::runtime_error(result.error);
    }
} catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
}
```

## Complete Example: Game Integration

```cpp
#include <Havel.hpp>

struct Game {
    int score = 0;
    std::string level = "1";
    double playerX = 0.0;
    double playerY = 0.0;
    
    havel::VM vm;
    
    Game() {
        // Register game functions
        vm.setHostContext(this);
        
        vm.registerFn("getScore", [this](VM& vm, auto& args) {
            return Value(static_cast<double>(score));
        });
        
        vm.registerFn("setScore", [this](VM& vm, auto& args) {
            if (!args.empty()) score = static_cast<int>(args[0].asNumber());
            return Value();
        });
        
        vm.registerFn("getPlayerPos", [this](VM& vm, auto& args) {
            // Return object with x, y
            vm.load("let _pos = {x = " + std::to_string(playerX) + 
                    ", y = " + std::to_string(playerY) + "}");
            return vm.getGlobal("_pos");
        });
        
        vm.registerFn("setPlayerPos", [this](VM& vm, auto& args) {
            if (args.size() >= 2) {
                playerX = args[0].asNumber();
                playerY = args[1].asNumber();
            }
            return Value();
        });
        
        // Load game scripts
        vm.loadFile("scripts/game.hv");
    }
    
    void update() {
        // Call game update function
        vm.call("gameUpdate");
    }
};

int main() {
    Game game;
    
    // Game loop
    while (running) {
        game.update();
    }
    
    return 0;
}
```

## Havel Script Example

```havel
// scripts/game.hv

struct Vec2 {
    x
    y
    
    init(x, y) {
        this.x = x
        this.y = y
    }
    
    op +(other) {
        return Vec2(x + other.x, y + other.y)
    }
    
    toString() {
        return "(" + x + ", " + y + ")"
    }
}

let playerPos = Vec2(0, 0)

fn gameUpdate() {
    // Get current position from C++
    let pos = getPlayerPos()
    playerPos = Vec2(pos.x, pos.y)
    
    // Update game logic
    playerPos = playerPos + Vec2(1, 0)
    
    // Set new position
    setPlayerPos(playerPos.x, playerPos.y)
    
    // Update score
    setScore(getScore() + 1)
    
    print("Player at: " + playerPos.toString())
    print("Score: " + getScore())
}
```

## Best Practices

### 1. Keep Native Functions Simple

```cpp
// Good
vm.registerFn("add", [](VM& vm, auto& args) {
    return Value(args[0].asNumber() + args[1].asNumber());
});

// Bad - too complex
vm.registerFn("complex", [](VM& vm, auto& args) {
    // 100 lines of C++ logic
    // ...
});
```

### 2. Validate Arguments

```cpp
vm.registerFn("divide", [](VM& vm, auto& args) {
    if (args.size() < 2) return Value();
    if (!args[0].isNumber()) return Value();
    if (!args[1].isNumber()) return Value();
    if (args[1].asNumber() == 0.0) return Value();
    
    return Value(args[0].asNumber() / args[1].asNumber());
});
```

### 3. Use Host Context for State

```cpp
struct Context { /* ... */ };
Context ctx;
vm.setHostContext(&ctx);

// Access via lambda capture
vm.registerFn("getState", [&ctx](VM& vm, auto& args) {
    // Use ctx
});
```

### 4. Handle Errors Gracefully

```cpp
auto result = vm.call("riskyFunction");
if (!result) {
    // Log error, don't crash
    logError(result.error);
    return Value();  // Return nil
}
```

## API Reference

### VM Methods

| Method | Description |
|--------|-------------|
| `load(code, name)` | Load and execute code |
| `loadFile(path)` | Load and execute file |
| `call(func, args)` | Call function |
| `getGlobal(name)` | Get global variable |
| `setGlobal(name, val)` | Set global variable |
| `registerFn(name, fn)` | Register native function |
| `registerModule(name, fns)` | Register module |
| `getError()` | Get last error |
| `clearError()` | Clear error |
| `setHostContext(ctx)` | Set host context |
| `getHostContext()` | Get host context |

### Value Methods

| Method | Description |
|--------|-------------|
| `isNil()` | Check if nil |
| `isBool()` | Check if bool |
| `isNumber()` | Check if number |
| `isString()` | Check if string |
| `isArray()` | Check if array |
| `isObject()` | Check if object |
| `isFunction()` | Check if function |
| `isStruct()` | Check if struct |
| `asBool()` | Convert to bool |
| `asNumber()` | Convert to number |
| `asString()` | Convert to string |
| `toString()` | Convert to string |

### Array Methods

| Method | Description |
|--------|-------------|
| `size()` | Get array size |
| `get(i)` | Get element at index |
| `set(i, val)` | Set element at index |
| `push(val)` | Append element |
| `pop()` | Remove and return last |

### Object Methods

| Method | Description |
|--------|-------------|
| `get(key)` | Get property |
| `set(key, val)` | Set property |
| `has(key)` | Check if property exists |
| `keys()` | Get all keys |

### Struct Methods

| Method | Description |
|--------|-------------|
| `getType()` | Get struct type name |
| `getField(name)` | Get field value |
| `setField(name, val)` | Set field value |
| `callMethod(name, args)` | Call method |

## Performance Tips

1. **Reuse VM instances** - Don't create/destroy VM frequently
2. **Cache function references** - Get function once, call multiple times
3. **Minimize conversions** - Keep values in Havel when possible
4. **Batch operations** - Call native functions with arrays, not individual values

## Memory Management

The VM uses shared pointers internally. When the VM is destroyed, all associated values are cleaned up automatically.

```cpp
{
    havel::VM vm;
    vm.load("let x = 42");
    // VM and all values cleaned up here
}
```
