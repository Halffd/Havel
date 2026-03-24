# Havel Architecture - Complete Implementation

## Overview

This document describes the complete architecture of the Havel window manager scripting system, as implemented in March 2026.

## Core Principles

1. **Separation of Concerns** - Business logic separated from language bindings
2. **Single Ownership** - VM owns all closures, systems use handles
3. **Service Discovery** - Services registered in ServiceRegistry, not passed directly
4. **Pure C++ Services** - No VM types in service layer

## Architecture Layers

```
┌─────────────────────────────────────────────────────────────┐
│  Havel Scripts (.hv files)                                  │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│  Frontend (Lexer → Parser → AST)                            │
│  OR                                                          │
│  Bytecode Compiler (AST → Bytecode)                         │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│  VM (Stack-based bytecode interpreter)                      │
│  - Owns ALL closures via GC                                 │
│  - Provides CallbackId system                               │
│  - Executes bytecode                                        │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│  HostBridge (VM ↔ Service adapter)                          │
│  - Registers host functions                                 │
│  - Manages callback lifecycle                               │
│  - Invokes callbacks through VM                             │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│  ServiceRegistry (Singleton)                                │
│  ├── IOService                                              │
│  ├── HotkeyService                                          │
│  ├── WindowService                                          │
│  ├── ModeService                                            │
│  ├── ProcessService                                         │
│  ├── ClipboardService                                       │
│  ├── AudioService                                           │
│  └── BrightnessService                                      │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│  Core Systems (Low-level implementation)                    │
│  ├── IO (X11 input handling)                                │
│  ├── HotkeyManager                                          │
│  ├── WindowManager                                          │
│  ├── ModeManager                                            │
│  └── ...                                                    │
└─────────────────────────────────────────────────────────────┘
```

## Key Components

### 1. VM (src/havel-lang/compiler/bytecode/VM.hpp)

**Responsibility:** Execute bytecode, manage GC, own closures

**Key Features:**
- Stack-based bytecode execution
- Mark-sweep garbage collection
- Callback system with opaque IDs
- External root pinning for long-lived closures

```cpp
// Callback system - VM owns closures, systems use opaque IDs
using CallbackId = uint32_t;

class VM {
    CallbackId registerCallback(const BytecodeValue& closure);
    BytecodeValue invokeCallback(CallbackId id, std::span<BytecodeValue> args);
    void releaseCallback(CallbackId id);
};
```

### 2. ServiceRegistry (src/host/ServiceRegistry.hpp)

**Responsibility:** Central service discovery

**Key Features:**
- Singleton pattern
- Type-safe service registration/retrieval
- Thread-safe access

```cpp
class ServiceRegistry {
public:
    static ServiceRegistry& instance();
    
    template<typename T>
    void registerService(std::shared_ptr<T> service);
    
    template<typename T>
    std::shared_ptr<T> get();
};
```

### 3. Services (src/host/*/Service.hpp)

**Responsibility:** Pure C++ business logic

**Characteristics:**
- No VM types (no BytecodeValue, no HavelValue)
- No interpreter dependencies
- Standard C++ types only (bool, int, string, vector)
- Testable without VM

**Services Implemented:**
| Service | File | Operations |
|---------|------|------------|
| IOService | `src/host/io/` | 30+ keyboard/mouse/IO ops |
| HotkeyService | `src/host/hotkey/` | Hotkey registration, KeyTap |
| WindowService | `src/host/window/` | Window queries, control |
| ModeService | `src/host/mode/` | Mode queries, control |
| ProcessService | `src/host/process/` | Process queries, control |
| ClipboardService | `src/host/clipboard/` | Clipboard text ops |
| AudioService | `src/host/audio/` | Volume, mute control |
| BrightnessService | `src/host/brightness/` | Brightness, temperature |

### 4. HostBridge (src/havel-lang/compiler/bytecode/HostBridge.hpp)

**Responsibility:** Adapt VM to services

**Key Features:**
- Registers host functions with VM
- Manages callback lifecycle through VM
- Invokes callbacks through VM (not directly)

```cpp
struct HostBridgeDependencies {
    // THE ONLY dependency for services
    ServiceRegistry* services;
    
    // TEMPORARY - ModeManager for complex mode operations
    ModeManager* mode_manager;
};
```

### 5. REPL (src/havel-lang/tools/REPL.hpp)

**Responsibility:** Interactive bytecode VM execution

**Key Features:**
- Bytecode VM execution (not AST interpreter)
- Full host API access through ServiceRegistry
- Multi-line input support
- Built-in commands (exit, help, clear)

```cpp
class REPL {
    void initialize(std::shared_ptr<IHostAPI> hostAPI);
    int run();  // Interactive loop
    bool execute(const std::string& code);
};
```

## CallbackId Pattern

### The Problem

Before this architecture, systems like ModeManager held VM closures directly:

```cpp
// WRONG - ModeManager holding VM closures ❌
struct ModeDefinition {
    std::function<void()> onEnter;  // Holds closure
    std::function<void()> onExit;   // Holds closure
};

// Problems:
// - GC can't collect what ModeManager holds
// - Circular dependency (ModeManager needs VM)
// - Unclear lifetime ownership
```

### The Solution

VM owns ALL closures, systems use opaque IDs:

```cpp
// CORRECT - CallbackId indirection ✅
using CallbackId = uint32_t;

struct ModeBinding {
    std::optional<CallbackId> condition_id;
    std::optional<CallbackId> enter_id;
    std::optional<CallbackId> exit_id;
};

// Usage:
CallbackId id = vm.registerCallback(closure);  // VM pins closure
modeBinding.enter_id = id;                      // System stores ID
vm.invokeCallback(id);                          // VM invokes closure
vm.releaseCallback(id);                         // VM unpins, GC can collect
```

### Benefits

✅ **One GC owner** - VM pins all closures  
✅ **No circular dependencies** - Systems are VM-agnostic  
✅ **Clear lifetime** - `releaseCallback()` frees the closure  
✅ **Scales** - Any number of systems can use callbacks  
✅ **Industry standard** - How Lua, game engines, Unreal/Unity do it

## Execution Flows

### Script Execution (--run script.hv)

```
HavelLauncher::runScriptOnly()
    ↓
runBytecodePipeline(code)
    ↓
LexicalResolver (scopes, upvalues)
    ↓
ByteCompiler (AST → Bytecode)
    ↓
VM executes bytecode
    ↓
Result
```

### Daemon Mode (GUI)

```
HavelApp initialization
    ↓
ServiceRegistry initialized with all services
    ↓
VM + HostBridgeRegistry created
    ↓
VM can execute scripts with full host API access
```

### REPL Mode (--repl)

```
HavelLauncher::runRepl()
    ↓
REPL::initialize(hostAPI)
    ↓
ServiceRegistry initialized
    ↓
VM + HostBridgeRegistry created
    ↓
REPL::run() - Interactive loop
    ↓
User input → Bytecode compilation → VM execution → Result
```

## File Structure

```
src/
├── havel-lang/                    # Pure language runtime
│   ├── compiler/bytecode/         # VM, compiler, HostBridge
│   │   ├── VM.{hpp,cpp}           # Bytecode VM with CallbackId
│   │   ├── HostBridge.{hpp,cpp}   # VM ↔ Service adapter
│   │   ├── ByteCompiler.{hpp,cpp} # AST → Bytecode
│   │   └── GC.{hpp,cpp}           # Garbage collector
│   ├── tools/
│   │   └── REPL.{hpp,cpp}         # Interactive REPL
│   └── stdlib/                    # Pure Havel standard library
│
├── host/                          # SERVICE LAYER
│   ├── ServiceRegistry.{hpp}      # Service discovery
│   ├── io/IOService.{hpp,cpp}
│   ├── hotkey/HotkeyService.{hpp,cpp}
│   ├── window/WindowService.{hpp,cpp}
│   ├── mode/ModeService.{hpp,cpp}
│   ├── process/ProcessService.{hpp,cpp}
│   ├── clipboard/ClipboardService.{hpp,cpp}
│   ├── audio/AudioService.{hpp,cpp}
│   └── brightness/BrightnessService.{hpp,cpp}
│
├── modules/                       # Thin binding layers
│   ├── io/IOModule.cpp            # Uses IOService
│   ├── hotkey/HotkeyModule.cpp    # Uses HotkeyService
│   └── ...
│
└── core/                          # Low-level systems
    ├── IO.{hpp,cpp}
    ├── HotkeyManager.{hpp,cpp}
    ├── WindowManager.{hpp,cpp}
    └── ...
```

## Usage Examples

### Command Line

```bash
# Run script with bytecode VM
./havel --run script.hv

# Start REPL
./havel --repl

# Run script then enter REPL
./havel script.hv --repl

# Full daemon mode with GUI
./havel

# Daemon with startup script
./havel --startup script.hv
```

### REPL Session

```
havel> 1 + 2
=> 3

havel> fn add(a, b) { return a + b }

havel> add(3, 4)
=> 7

havel> io.send("Hello from REPL!")

havel> window.active()
=> { id: 12345, title: "Firefox", class: "firefox", ... }

havel> exit
Goodbye!
```

### Havel Script

```havel
// Define hotkey
Hotkey("Ctrl+Alt+K", fn() {
    io.send("Hello!")
})

// Window operations
win = window.getActive()
window.moveToMonitor(win.id, 1)

// Mode system
mode.define("gaming", 
    fn() { window.any(exe == "steam.exe") },
    fn() { brightness.setBrightness(0.5) },
    fn() { brightness.setBrightness(1.0) }
)

// Process operations
pids = process.find("firefox")
if (pids.len() > 0) {
    print("Firefox is running")
}
```

## Testing

### Build

```bash
cd /path/to/havel
cmake -G Ninja -S . -B build
ninja -C build havel
```

### Run Tests

```bash
# Bytecode VM smoke tests
./build/havel-bytecode-smoke

# Unit tests
./build/ManagerTest
./build/utils_test
```

## Migration Status

| Component | Status | Notes |
|-----------|--------|-------|
| Service Layer | ✅ Complete | 8 services implemented |
| ServiceRegistry | ✅ Complete | Singleton pattern |
| CallbackId System | ✅ Complete | VM owns all closures |
| HostBridge Migration | ✅ Complete | All handlers use services |
| REPL | ✅ Complete | Bytecode VM-based |
| VM Integration | ✅ Complete | Connected to HavelApp |

## Future Work

1. **Deprecate AST Interpreter** - Migrate all execution to bytecode VM
2. **Remove ModeManager dependency** - Complete CallbackId migration
3. **Add more services** - Network, Filesystem, Timer services
4. **Performance optimization** - Profile and optimize hot paths
5. **Documentation** - API reference, tutorials, examples

## References

- `docs/Havel.md` - Original architecture roadmap
- `docs/VM_STATUS.md` - VM implementation status
- `SERVICE_LAYER_MIGRATION.md` - Service layer migration guide
- `docs/HOST_SERVICE_ARCHITECTURE.md` - Host service patterns

---

**Last Updated:** March 21, 2026  
**Status:** Architecture Complete ✅
