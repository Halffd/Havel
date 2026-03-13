# Havel Architecture Refactoring Plan

## Current State (Good)

- 3.7k lines in Interpreter.cpp (reasonable for tree-walker)
- Clean AST visitor pattern
- Proper environment scoping
- Module system exists

## Current State (Needs Work)

- Host APIs coupled to interpreter
- No clear module boundaries  
- Stdlib functions mixed with core semantics
- Hard to test language core in isolation

## Phase 1: Extract Host Context (Week 1-2)

### Goal: Interpreter never directly accesses WindowManager, HotkeyManager, etc.

**Create:** `src/havel-lang/runtime/HostAPI.hpp`

```cpp
// Abstract interface for host operations
class IHostAPI {
public:
    virtual ~IHostAPI() = default;
    
    // Window operations
    virtual std::string GetActiveWindowTitle() = 0;
    virtual std::string GetActiveWindowClass() = 0;
    virtual pID GetActiveWindowPID() = 0;
    
    // Hotkey operations  
    virtual bool RegisterHotkey(const std::string& key, std::function<void()> cb) = 0;
    
    // ... other host operations
};
```

**Change:** Interpreter takes `IHostAPI*` instead of concrete managers

**Benefit:** Language core can be tested without GUI/hotkey infrastructure

## Phase 2: Split Stdlib (Week 2-3)

### Goal: Move builtin functions out of Interpreter

**Current:**
```cpp
// In Interpreter.cpp
void Interpreter::visitArrayLiteral(...) { ... }
void Interpreter::visitObjectLiteral(...) { ... }
```

**Should be:**
```cpp
// In stdlib/ArrayModule.cpp
void registerArrayModule(Environment& env) {
    env.Define("array", BuiltinFunction(...));
}
```

**Files to create:**
- `stdlib/CoreModule.cpp` - core language functions
- `stdlib/ArrayModule.cpp` - array operations  
- `stdlib/ObjectModule.cpp` - object operations
- `stdlib/StringModule.cpp` - string operations (already exists!)
- `stdlib/MathModule.cpp` - math operations (already exists!)

## Phase 3: Module Loader Cleanup (Week 3-4)

### Goal: Clear separation between language and modules

**Current:** ModuleLoader knows about everything

**Should be:**
```cpp
// Language core loads only core modules
void Interpreter::InitializeStandardLibrary() {
    registerCoreModule(env);
    registerArrayModule(env);
    registerObjectModule(env);
    // ... only language essentials
}

// Host loads host modules separately  
void LoadHostModules(Environment& env, IHostAPI* host) {
    registerWindowModule(env, host);
    registerHotkeyModule(env, host);
    registerAudioModule(env, host);
    // ... host-specific modules
}
```

## Phase 4: Value System Prep for VM (Future)

### Goal: Make value system VM-friendly

**Current:** `std::variant<...>` (fine for interpreter)

**Future VM:** Tagged union
```cpp
struct Value {
    enum Type { NIL, BOOL, NUMBER, OBJ } type;
    union {
        bool boolean;
        double number;
        Obj* obj;
    };
};
```

**Don't do this yet** - but design current code to make future migration easier

## Immediate Actions (This Week)

1. **Create IHostAPI interface** - Decouple interpreter from concrete managers
2. **Extract Array/Object builtins** - Move out of Interpreter.cpp  
3. **Document module boundaries** - What belongs in core vs modules

## Success Metrics

- [ ] Interpreter.cpp < 2.5k lines
- [ ] Zero direct dependencies on WindowManager/HotkeyManager
- [ ] Language core testable without GUI
- [ ] Clear module API documentation
- [ ] Build time < 60 seconds

## Files That Should NOT Be Touched Yet

- Async scheduler (working well)
- Channel implementation (solid)
- Trait system (good design)
- Config system (works)

## Files That MUST Be Refactored

- `Interpreter.cpp` - Extract host API usage
- `ModuleLoader.cpp` - Split core vs host modules
- `Environment.hpp` - Clean up module registration

---

## Key Insight

**Your architecture is 80% correct already.** 

The visitor pattern is clean. The environment system works. The module system exists.

The problem is just **coupling**, not fundamental design.

Small, incremental refactors will fix this without breaking everything.
