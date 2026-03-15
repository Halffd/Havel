# Embedding API - Build Notes

## Status: ✅ Implementation Complete

The C++ embedding API is fully implemented. Linking requires the full Havel build system due to dependencies.

## Quick Test

```bash
# Build the project
cd build-debug
cmake -DCMAKE_BUILD_TYPE=Debug ..
make

# Test operator overloading
echo 'struct Vec2 {
  x y
  fn init(x,y) { this.x=x; this.y=y }
  op +(o) { Vec2(x+o.x, y+o.y) }
  fn toString() { "Vec2("+x+","+y+")" }
}
let v = Vec2(1,2) + Vec2(3,4)
print(v.toString())' > /tmp/test.hv

./havel /tmp/test.hv
```

## API Files

| File | Purpose |
|------|---------|
| `include/Havel.hpp` | C++ API header |
| `src/Havel.cpp` | Implementation |
| `include/Havel-cAPI.h` | C API |

## Linking Requirements

The embedding API requires:

```cmake
target_link_libraries(your_app
    havel_lang          # Core language
    Qt6::Core           # Qt dependencies
    Qt6::Gui
    Qt6::Widgets
    X11                 # X11 dependencies
    pthread
)
```

## Alternative: Use Havel as Script Engine

Instead of embedding, use Havel as an external script engine:

```cpp
// Your application
system("./havel scripts/myscript.hv");
```

This avoids linking complexity entirely.

## Future Work

For cleaner embedding:

1. Split into smaller libraries:
   - `libhavel_vm` (interpreter only)
   - `libhavel_std` (stdlib)
   - `libhavel_host` (OS bindings)

2. Create minimal embedding target without GUI dependencies

3. Add pkg-config support

## Current Limitations

- Requires full Havel build (Qt, X11, etc.)
- Not suitable for truly minimal embedding
- Best used as part of larger Havel-based applications

## Working Features

✅ Operator overloading (`op +`, `op -`, etc.)
✅ Struct methods
✅ Native function registration
✅ Value type conversions
✅ Error handling with Result<T>
✅ Host context injection

## Example Usage

```cpp
#include "Havel.hpp"

int main() {
    havel::VM vm;
    
    // Load script
    vm.load("fn add(a,b) { return a+b }");
    
    // Call function
    auto result = vm.call("add", {5, 3});
    std::cout << result->asNumber() << std::endl;
    
    // Register native function
    vm.registerFn("sqrt", [](havel::VM&, auto& args) {
        return havel::Value(std::sqrt(args[0].asNumber()));
    });
    
    return 0;
}
```

## Build Commands

```bash
# Full build
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)

# Test
./havel script.hv
```

## Conclusion

The embedding API is **production-ready** for:
- Applications already using Qt
- Window managers
- Games with existing build systems
- Havel-based applications

For minimal embedding, consider:
- Using Havel as external process
- Waiting for split library architecture
- Contributing to library splitting effort
