# HostContext Architecture Migration

## Overview

Migrating from **ServiceRegistry (pull-based)** to **HostContext (push-based)** architecture.

## Problem

**Before (ServiceRegistry):**
```cpp
// HostBridge pulls services from global registry
auto ioService = registry.get<IOService>();
auto windowService = registry.get<WindowService>();
```

**Issues:**
- Hidden dependencies
- Global state
- Hard to embed
- Hard to test with mocks

## Solution

**After (HostContext):**
```cpp
// Dependencies are injected
HostContext ctx;
ctx.io = std::make_shared<IO>();
ctx.windowManager = windowManager;

auto bridge = createHostBridge(vm, ctx);
```

**Benefits:**
- Explicit dependencies
- No global state
- Easy embedding
- Easy mocking for tests

## Key Components

### 1. HostContext (New)

```cpp
struct HostContext {
    // Core services
    std::shared_ptr<IO> io;
    std::shared_ptr<WindowManager> windowManager;
    std::shared_ptr<HotkeyManager> hotkeyManager;
    // ... other services
    
    // Capability-based extensions
    std::unordered_map<std::string, std::shared_ptr<Capability>> caps;
};
```

### 2. Capability System (New)

```cpp
struct Capability {
    virtual ~Capability() = default;
    virtual std::string typeName() const = 0;
};

// Embedder provides custom implementation
struct MyFileSystem : Capability {
    std::string typeName() const override { return "MyFileSystem"; }
    // ... custom methods
};

ctx.caps["fs"] = std::make_shared<MyFileSystem>();
```

### 3. HostModule (New)

```cpp
struct HostModule {
    std::string name;
    std::unordered_map<std::string, HostFn> functions;
};

// Embedder registers custom module
HostModule mod;
mod.name = "engine";
mod.functions["spawn_window"] = [](args) { /* ... */ };
bridge->registerModule(mod);
```

### 4. HostBridge (Refactored)

**Before:**
```cpp
HostBridgeRegistry(VM& vm, HostBridgeDependencies deps);
// deps.services points to global ServiceRegistry
```

**After:**
```cpp
HostBridge(VM& vm, HostContext ctx);
// ctx has injected services
```

## Migration Status

### Phase 1: Foundation ✅
- [x] HostContext.hpp created
- [x] HostBridge refactored to use HostContext
- [x] Capability system defined
- [x] HostModule for pluggable extensions

### Phase 2: Migration (TODO)
- [ ] Update REPL.cpp to use HostContext
- [ ] Update HavelApp.cpp to use HostContext
- [ ] Update ModuleLoader.cpp to use HostContext
- [ ] Update stdlib modules to use HostBridge& instead of HostBridgeRegistry&

### Phase 3: Cleanup (TODO)
- [ ] Remove ServiceRegistry dependency from HostBridge
- [ ] Remove HostBridgeDependencies struct
- [ ] Update all call sites

## Usage Examples

### Basic Embedding

```cpp
#include "havel-lang/runtime/HostContext.hpp"
#include "havel-lang/compiler/bytecode/HostBridge.hpp"

// Create context with dependencies
havel::HostContext ctx;
ctx.io = std::make_shared<IO>();
ctx.windowManager = windowManager;

// Create VM
havel::compiler::VM vm;

// Create HostBridge with injected context
auto bridge = havel::compiler::createHostBridge(vm, ctx);
bridge->install();

// Execute scripts
vm.execute(...);
```

### Custom Capabilities

```cpp
// Define custom capability
struct CustomEngineCap : havel::Capability {
    std::string typeName() const override { return "CustomEngine"; }
    
    void spawnWindow(const std::string& title) {
        // Engine-specific implementation
    }
};

// Register capability
ctx.caps["engine"] = std::make_shared<CustomEngineCap>();

// Access in host functions
auto engineCap = ctx.getCap<CustomEngineCap>("engine");
if (engineCap) {
    engineCap->spawnWindow("My Window");
}
```

### Custom Modules

```cpp
havel::HostModule mod;
mod.name = "game";

mod.registerFn("spawn_entity", [](const auto& args) {
    // Game-specific logic
    return nullptr;
});

mod.registerFn("load_level", [](const auto& args) {
    // Level loading
    return nullptr;
});

bridge->registerModule(mod);
```

## Benefits for Embedders

1. **No Global State**: Each runtime instance has its own context
2. **Custom Implementations**: Replace any service with custom implementation
3. **Sandboxing**: Control what capabilities are available
4. **Testing**: Inject mock services for deterministic tests
5. **Versioning**: HostContext can include version field for compatibility

## Next Steps

1. Complete migration of all call sites
2. Add version field to HostContext
3. Add C ABI layer for plugin support
4. Add declarative binding helpers
5. Add mock service implementations for testing

## References

- HostContext.hpp: `src/havel-lang/runtime/HostContext.hpp`
- HostBridge.hpp: `src/havel-lang/compiler/bytecode/HostBridge.hpp`
- HostBridge.cpp: `src/havel-lang/compiler/bytecode/HostBridge.cpp`
