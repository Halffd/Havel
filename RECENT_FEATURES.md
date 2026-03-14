# Recent Features Summary

## Operator Overloading System

Complete Wren-style operator overloading for structs.

### Binary Operators
- `op +` - Addition
- `op -` - Subtraction
- `op *` - Multiplication
- `op /` - Division
- `op %` - Modulo
- `op ==` - Equality
- `op !=` - Inequality
- `op <` - Less than
- `op >` - Greater than
- `op <=` - Less or equal
- `op >=` - Greater or equal

### Unary Operators
- `op_neg` - Unary minus
- `op_pos` - Unary plus
- `op_not` - Logical not

### Special Operators
- `op_index` - Indexing (`obj[key]`)
- `op_call` - Callable objects (`obj()`)

### Default Methods
- `toString()` - Automatic string representation

### Example
```havel
struct Vec2 {
    x
    y
    
    init(x, y) {
        this.x = x
        this.y = y
    }
    
    op +(other) {
        Vec2(x + other.x, y + other.y)
    }
    
    op_neg() {
        Vec2(-x, -y)
    }
    
    toString() {
        "Vec2(" + x + ", " + y + ")"
    }
}

let v1 = Vec2(1, 2)
let v2 = Vec2(3, 4)
let v3 = v1 + v2      // op +
let v4 = -v1          // op_neg
print(v3.toString())  // "Vec2(4, 6)"
```

## Embeddable C++ API

Clean, modern C++17 API for embedding Havel in applications.

### Features
- Direct API (no stack manipulation)
- Type-safe with proper conversions
- Result-based error handling
- Easy native function registration
- Host context for application state

### Usage
```cpp
#include <Havel.hpp>

int main() {
    havel::VM vm;
    
    // Load code
    vm.load("fn add(a,b) { return a+b }");
    
    // Call function
    auto result = vm.call("add", {5, 3});
    std::cout << result->asNumber() << std::endl;
    
    // Register native function
    vm.registerFn("sqrt", [](havel::VM& vm, auto& args) {
        return havel::Value(std::sqrt(args[0].asNumber()));
    });
    
    return 0;
}
```

### Files
- `include/Havel.hpp` - C++ header
- `include/Havel-cAPI.h` - C API header
- `src/Havel.cpp` - Implementation
- `embed/README.md` - Documentation
- `examples/example_embed.cpp` - Basic example
- `examples/example_game.cpp` - Game example
- `examples/example_wm.cpp` - Window manager example

## Process Module

New module for process management.

### Functions
- `process.list()` - Get all running PIDs
- `process.find(name)` - Find processes by name
- `process.exists(name)` - Check if process is running
- `process.name(pid)` - Get process name by PID

### Example
```havel
// Find compiz processes
let pids = process.find("compiz")
if pids.length > 0 {
    print("Compiz is running")
}

// Check if firefox exists
if process.exists("firefox") {
    print("Firefox is running")
}
```

## Script + REPL Mode

Run script with full features, then enter REPL.

### Usage
```bash
# Script + REPL with all features
./havel script.hv -r
./havel -r script.hv

# Full REPL (no script)
./havel --full-repl
./havel -fr
```

### Features
- Hotkeys work in REPL
- GUI functions available
- All modules loaded
- Same environment for script and REPL

## Files Changed

### Core
- `src/havel-lang/lexer/Lexer.hpp` - Added `Op` token
- `src/havel-lang/lexer/Lexer.cpp` - Added `op` keyword
- `src/havel-lang/ast/AST.h` - Added `isOperator` flag
- `src/havel-lang/parser/Parser.cpp` - Parse `op` keyword
- `src/havel-lang/types/HavelType.hpp` - Added `operators_` map
- `src/havel-lang/runtime/Interpreter.cpp` - Operator dispatch

### Modules
- `src/modules/process/ProcessModule.cpp` - New module
- `src/modules/process/ProcessModule.hpp` - Header

### Embedding
- `include/Havel.hpp` - C++ API
- `include/Havel-cAPI.h` - C API
- `src/Havel.cpp` - Implementation
- `embed/README.md` - Documentation
- `examples/example_embed.cpp` - Example
- `examples/example_game.cpp` - Game example
- `examples/example_wm.cpp` - WM example
- `havel.pc` - pkg-config

### Documentation
- `docs/OPERATORS.md` - Operator guide
- `docs/EMBEDDING.md` - Embedding guide
- `README.md` - Updated

## Build Status

```
[100%] Built target havel ✅
```

## Total Commits

15 commits adding:
- Complete operator overloading system
- Full embeddable C++ API
- C API for other languages
- Process module
- Script+REPL mode
- Comprehensive documentation
- Multiple examples

All features tested and working! 🎉
