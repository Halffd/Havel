# Havel Embedding API

Clean, modern C++ API for embedding Havel language in your applications.

## Quick Start

```cpp
#include <Havel.hpp>

int main() {
    havel::VM vm;
    
    // Load and run code
    vm.load(R"(
        fn add(a, b) { return a + b }
        print(add(5, 3))
    )");
    
    return 0;
}
```

## Features

- **Direct API** - No stack manipulation (unlike Lua)
- **Type-safe** - C++17 with proper conversions
- **Error handling** - Result-based error handling
- **Easy embedding** - Perfect for games, window managers, apps
- **Native functions** - Register C++ functions easily
- **Host context** - Pass application state to scripts

## Documentation

- [Full Embedding Guide](docs/EMBEDDING.md)
- [Operator Overloading Guide](docs/OPERATORS.md)
- [C API Reference](include/Havel-cAPI.h)

## Examples

### Basic Usage

```cpp
havel::VM vm;

// Load code
vm.load("fn greet(name) { print(\"Hello, \" + name) }");

// Call function
vm.call("greet", {havel::Value("World")});

// Register native function
vm.registerFn("sqrt", [](havel::VM& vm, auto& args) {
    return havel::Value(std::sqrt(args[0].asNumber()));
});
```

### Game Integration

```cpp
struct Game {
    int score = 0;
    havel::VM vm;
    
    Game() {
        vm.setHostContext(this);
        
        vm.registerFn("getScore", [this](...) {
            return havel::Value(score);
        });
        
        vm.loadFile("scripts/game.hv");
    }
    
    void update() {
        vm.call("gameUpdate");
    }
};
```

### Window Manager Integration

```cpp
struct WM {
    havel::VM vm;
    
    WM() {
        vm.registerFn("focusWindow", [this](...) { ... });
        vm.registerFn("closeWindow", [this](...) { ... });
        vm.loadFile("config/wm.hv");
    }
    
    void handleKey(const std::string& combo) {
        vm.call("on" + combo);
    }
};
```

## Building

### Using CMake

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Debug ..
make
```

### Linking

```bash
g++ -std=c++17 myapp.cpp -lhavel -o myapp
```

## API Reference

### VM Class

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
| `setHostContext(ctx)` | Set host context |

### Value Class

| Method | Description |
|--------|-------------|
| `isNil()` | Check if nil |
| `isBool()` | Check if bool |
| `isNumber()` | Check if number |
| `isString()` | Check if string |
| `isArray()` | Check if array |
| `isObject()` | Check if object |
| `isFunction()` | Check if function |
| `asBool()` | Convert to bool |
| `asNumber()` | Convert to number |
| `asString()` | Convert to string |

## Examples Directory

- `example_embed.cpp` - Basic embedding example
- `example_game.cpp` - Game integration example  
- `example_wm.cpp` - Window manager example

## License

Same as Havel language.
