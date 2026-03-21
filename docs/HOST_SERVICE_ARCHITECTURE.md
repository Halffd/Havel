# Host Service Architecture Guide

This document explains the architecture for host services and when to use each pattern.

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│  Havel Scripts                                               │
└─────────────────────────────────────────────────────────────┘
        ↓
┌─────────────────────────────────────────────────────────────┐
│  Module Layer (src/modules/)                                 │
│  - Thin binding layers                                       │
│  - Convert HavelValue ↔ C++ types                           │
│  - Handle errors, validation                                 │
└─────────────────────────────────────────────────────────────┘
        ↓
┌─────────────────────────────────────────────────────────────┐
│  Service Layer (src/host/)                                   │
│  - Pure C++ business logic                                   │
│  - No VM, no interpreter, no HavelValue                      │
│  - Registered in ServiceRegistry                             │
└─────────────────────────────────────────────────────────────┘
        ↓
┌─────────────────────────────────────────────────────────────┐
│  Core Layer (src/core/)                                      │
│  - Low-level system implementations                          │
│  - Qt dependencies, X11, system APIs                         │
└─────────────────────────────────────────────────────────────┘
```

## Service Patterns

### Pattern 1: Service Layer (RECOMMENDED for new features)

Use this pattern for **new features** or when **extracting existing logic**.

**Characteristics:**
- Pure C++ business logic
- No Qt dependencies (or minimal)
- Testable without GUI
- Registered in ServiceRegistry

**Example: IOService**

```cpp
// src/host/io/IOService.hpp
namespace havel::host {
class IOService {
public:
    explicit IOService(havel::IO* io);
    bool sendKeys(const std::string& keys);
    bool sendKey(const std::string& key);
    // ... more operations
private:
    havel::IO* m_io;
};
}

// src/host/io/IOService.cpp
namespace havel::host {
bool IOService::sendKeys(const std::string& keys) {
    if (!m_io) return false;
    m_io->Send(keys.c_str());
    return true;
}
}

// Registration (at application startup)
auto ioService = std::make_shared<host::IOService>(io.get());
ServiceRegistry::instance().registerService<host::IOService>(ioService);
```

**Module Binding:**

```cpp
// src/modules/io/IOModule.cpp
void registerIOModule(Environment& env, std::shared_ptr<IHostAPI> hostAPI) {
    IO* io = hostAPI->GetIO();
    host::IOService ioService(io);
    
    (*ioObj)["send"] = HavelValue(BuiltinFunction(
        [&ioService](const std::vector<HavelValue>& args) {
            std::string keys = args[0].asString();
            ioService.sendKeys(keys);
            return HavelValue(nullptr);
        }));
}
```

**HostBridge Handler:**

```cpp
// src/havel-lang/compiler/bytecode/HostBridge.cpp
BytecodeValue HostBridgeRegistry::handleSend(...) {
    auto ioService = deps_.services->get<host::IOService>();
    if (ioService) {
        ioService->sendKeys(text);
    } else if (deps_.io) {
        deps_.io->Send(text.c_str());  // Legacy fallback
    }
    return BytecodeValue(nullptr);
}
```

### Pattern 2: Direct Core Dependency (ACCEPTABLE for Qt-heavy features)

Use this pattern when:
- Feature **requires Qt GUI** (windows, dialogs, trays)
- Feature is **tightly coupled to Qt event loop**
- Business logic is **minimal** (mostly Qt API wrapping)

**Characteristics:**
- Heavy Qt dependencies
- Requires GUI application
- Not easily testable
- Module directly uses core manager

**Example: ScreenshotModule**

```cpp
// src/modules/screenshot/ScreenshotModule.cpp
void registerScreenshotModule(Environment& env, 
                              std::shared_ptr<IHostAPI> hostAPI) {
    auto screenshotObj = std::make_shared<...>();
    
    (*screenshotObj)["full"] = HavelValue(BuiltinFunction(
        [hostAPI](const std::vector<HavelValue>& args) {
            auto* sm = hostAPI->GetScreenshotManager();
            if (!sm) return HavelRuntimeError("ScreenshotManager unavailable");
            
            QString path = sm->takeScreenshot();
            return HavelValue(path.toStdString());
        }));
    
    env.Define("screenshot", HavelValue(screenshotObj));
}
```

**Why this is OK:**
- ScreenshotManager is Qt-heavy (QScreen, QImage, etc.)
- No meaningful "business logic" to extract
- Module is already thin (just Qt API wrapping)

### Pattern 3: Hybrid (TRANSITIONAL)

Use this pattern when **migrating from Pattern 2 to Pattern 1**.

**Example: ClipboardService**

```cpp
// src/host/clipboard/ClipboardService.hpp
class ClipboardService {
public:
    static std::string getText();  // Pure function
    static bool setText(const std::string& text);
    
    // Qt-heavy operations stay in core
    static void showManager(void* manager);  // Requires ClipboardManager
};

// src/host/clipboard/ClipboardService.cpp
std::string ClipboardService::getText() {
    if (!QGuiApplication::instance()) return "";
    return QGuiApplication::clipboard()->text().toStdString();
}
```

## ServiceRegistry Usage

### Registering Services

```cpp
// In application initialization (e.g., main.cpp or HavelLauncher.cpp)
#include "host/ServiceRegistry.hpp"
#include "host/io/IOService.hpp"
#include "host/hotkey/HotkeyService.hpp"

void initializeServices(std::shared_ptr<IO> io, 
                        std::shared_ptr<HotkeyManager> hotkeyManager) {
    // Create services
    auto ioService = std::make_shared<host::IOService>(io.get());
    auto hotkeyService = std::make_shared<host::HotkeyService>(hotkeyManager);
    
    // Register in ServiceRegistry
    ServiceRegistry::instance().registerService<host::IOService>(ioService);
    ServiceRegistry::instance().registerService<host::HotkeyService>(hotkeyService);
}
```

### Using Services in HostBridge

```cpp
// HostBridgeDependencies has ONE service dependency
struct HostBridgeDependencies {
    std::shared_ptr<host::ServiceRegistry> services;  // ← Single dependency
    // Legacy (deprecated)...
};

// Handler uses registry lookup
BytecodeValue handleSomeOperation(...) {
    auto service = deps_.services->get<host::SomeService>();
    if (service) {
        return service->doSomething(args);
    }
    // Fallback to legacy...
}
```

## Decision Tree

```
New Feature / Refactoring
         │
         ↓
  ┌──────────────┐
  │ Requires Qt  │
  │ GUI heavily? │
  └──────────────┘
         │
    ┌────┴────┐
    │         │
   YES       NO
    │         │
    ↓         ↓
┌────────┐  ┌─────────────┐
│Pattern │  │  Pattern 1  │
│   2    │  │  (Service)  │
└────────┘  └─────────────┘
```

## Service Checklist

When creating a new service, verify:

- [ ] No `HavelValue` in service interface
- [ ] No `Environment` or `Interpreter` dependencies
- [ ] No `BuiltinFunction` in service
- [ ] Uses standard C++ types only
- [ ] Registered in `ServiceRegistry`
- [ ] Has fallback to legacy core (for migration)
- [ ] Unit testable without Qt GUI

## Module Checklist

When creating a new module:

- [ ] Module handles HavelValue ↔ C++ conversion
- [ ] Module handles error messages
- [ ] Module validates arguments
- [ ] Service/core handles actual logic
- [ ] Module is thin (< 200 lines ideal)

## Anti-Patterns to Avoid

### ❌ God Object (HostBridge)

```cpp
// WRONG - HostBridge knows about every service
struct HostBridgeDependencies {
    std::shared_ptr<IOService> io;
    std::shared_ptr<HotkeyService> hotkey;
    std::shared_ptr<WindowService> window;
    std::shared_ptr<AudioService> audio;
    std::shared_ptr<BrightnessService> brightness;
    // ... growing forever
};
```

### ✅ Service Registry

```cpp
// CORRECT - HostBridge knows about ONE thing
struct HostBridgeDependencies {
    std::shared_ptr<ServiceRegistry> services;
};
```

### ❌ Business Logic in Modules

```cpp
// WRONG - Module does actual work
(*ioObj)["send"] = HavelValue(BuiltinFunction(
    [io](const std::vector<HavelValue>& args) {
        // Business logic in module!
        std::string keys = args[0].asString();
        if (keys.empty()) return HavelValue(false);
        io->Send(keys.c_str());
        std::this_thread::sleep_for(50ms);  // Logic in module!
        return HavelValue(true);
    }));
```

### ✅ Service Does Logic

```cpp
// CORRECT - Service does work, module just binds
// IOService.cpp
bool IOService::sendKeys(const std::string& keys) {
    if (keys.empty() || !m_io) return false;
    m_io->Send(keys.c_str());
    return true;
}

// IOModule.cpp
(*ioObj)["send"] = HavelValue(BuiltinFunction(
    [&ioService](const std::vector<HavelValue>& args) {
        std::string keys = args[0].asString();
        return HavelValue(ioService.sendKeys(keys));  // Delegate
    }));
```

## Migration Strategy

For existing modules using Pattern 2:

1. **Identify business logic** in module
2. **Extract to service** (Pattern 1)
3. **Register in ServiceRegistry**
4. **Update module** to use service
5. **Update HostBridge** handlers to use registry
6. **Keep legacy fallback** until confident

### Priority Order

| Priority | Module | Reason |
|----------|--------|--------|
| High | io, hotkey | ✅ Migrated |
| High | window | ✅ Migrated |
| High | mode, process | ✅ Migrated |
| High | clipboard | ✅ Migrated |
| Medium | audio, brightness | Qt-heavy, but extractable |
| Medium | screenshot | Qt-heavy, minimal logic |
| Low | timer, launcher | Simple, working fine |
| Low | filesystem, network | Already clean |

## Summary

**Golden Rule:** HostBridge depends on ServiceRegistry, not individual services.

**Service Layer:** Pure C++ business logic, registered in ServiceRegistry.

**Module Layer:** Thin bindings, HavelValue conversion, error handling.

**Core Layer:** Low-level implementations, Qt dependencies, system APIs.

**When in doubt:** Extract to service if it has meaningful business logic.
