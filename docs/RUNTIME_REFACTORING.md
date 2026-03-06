# Havel Runtime Refactoring Plan

## Current State

The Havel interpreter is currently a monolithic 13,000+ line file (`Interpreter.cpp`) that combines:
- Language runtime (AST execution, variable scoping, value system)
- Standard library (math, strings, arrays, etc.)
- Host API modules (window management, audio, hotkeys, etc.)
- Async scheduler and channels
- Trait system
- Configuration system

This architecture works but is becoming difficult to maintain and test.

## Problem: Circular Dependencies

The main obstacle to refactoring is circular dependencies:

```
Interpreter.hpp
    ↓ (uses)
HavelValue
    ↓ (uses)
HavelResult
    ↓ (uses)
BuiltinFunction
    ↓ (uses)
HavelValue  ← circular!
```

Additionally:
- `Environment` needs `HavelValue`
- `HavelValue` needs `HavelResult`
- `HavelResult` needs `BuiltinFunction`
- `BuiltinFunction` is typedef'd in `Interpreter.hpp`

**Attempted Solution (Failed):**

An attempt was made to extract `HavelValue`, `HavelResult`, and `BuiltinFunction` to a new `runtime/Value.hpp`. However, this failed because:

1. `std::variant` requires complete types for all alternatives
2. `HavelValue` contains `BuiltinFuncType` which returns `HavelResult`
3. `HavelResult` contains `HavelValue`
4. This creates a circular dependency that `std::variant` cannot handle

The original code works because everything is defined in the same header file in a specific order that satisfies the compiler. Splitting across headers breaks this delicate ordering.

**Possible Solutions:**

1. **Use pointers in variant**: Change `HavelResult` to use `std::shared_ptr<HavelValue>` instead of `HavelValue` directly. This breaks the circular dependency but adds heap allocation overhead.

2. **Keep monolithic structure**: Accept that the value types must stay together for now.

3. **Full VM rewrite**: Implement a bytecode VM where values are tagged unions (not std::variant). This is a major undertaking.

For now, option 2 is chosen - keeping the monolithic structure but documenting the plan for future refactoring.

## Target Architecture

```
src/havel-lang/
├── types/
│   └── HavelType.hpp       # HavelType, TypeRegistry, TypeChecker
├── runtime/
│   ├── Value.hpp           # HavelValue, HavelValueBase, HavelResult
│   ├── Environment.hpp     # Environment, TraitRegistry
│   ├── ValueHelpers.hpp    # ValueToString, ValueToNumber, etc.
│   ├── Interpreter.hpp     # Interpreter class declaration
│   ├── Interpreter.cpp     # Core execution logic
│   ├── Scheduler.hpp       # AsyncScheduler
│   └── Scheduler.cpp       # Async execution
├── stdlib/
│   ├── MathModule.cpp      # math.* functions
│   ├── StringModule.cpp    # string.* functions
│   ├── ArrayModule.cpp     # array.* functions
│   └── FileModule.cpp      # file.* functions
└── modules/
    ├── WindowModule.cpp    # window.* functions
    ├── AudioModule.cpp     # audio.* functions
    ├── HotkeyModule.cpp    # hotkey.* functions
    └── ...
```

## Refactoring Steps

### Phase 1: Extract Value Types (Highest Priority)

1. **Move HavelValueBase variant to new `runtime/Value.hpp`**
   - Forward-declare HavelFunction, Channel, HavelStructInstance
   - Define HavelArray, HavelObject, HavelSet typedefs
   - Define HavelValueBase variant

2. **Define HavelResult and BuiltinFunction**
   - These must come before HavelValue
   - HavelResult = variant<HavelValue, HavelRuntimeError, ReturnValue, BreakValue, ContinueValue>
   - BuiltinFunction = std::function<HavelResult(const std::vector<HavelValue>&)>

3. **Move HavelValue struct to Value.hpp**
   - Includes all constructors, accessors, helpers
   - Depends on HavelType from types/HavelType.hpp

4. **Update all includes**
   - Interpreter.hpp includes runtime/Value.hpp
   - All .cpp files that use HavelValue include runtime/Value.hpp

### Phase 2: Extract Environment

1. **Create `runtime/Environment.hpp`**
   - Includes runtime/Value.hpp (for HavelValue)
   - Defines Environment class
   - Defines TraitImpl and TraitRegistry

2. **Update Interpreter.hpp**
   - Include runtime/Environment.hpp
   - Remove Environment and TraitRegistry definitions

### Phase 3: Extract Value Helpers

1. **Create `runtime/ValueHelpers.hpp`**
   - ValueToString(), ValueToNumber(), ValueToBool()
   - IsType() helper

2. **Update Interpreter.cpp**
   - Include ValueHelpers.hpp
   - Remove inline implementations

### Phase 4: Extract Standard Library

1. **Create `stdlib/` directory**
2. **Move each builtin group to separate file:**
   - MathModule.cpp: math.abs, math.sqrt, etc.
   - StringModule.cpp: string.upper, string.lower, etc.
   - ArrayModule.cpp: array.map, array.filter, etc.
   - FileModule.cpp: file.read, file.write, etc.

3. **Create module registration:**
   ```cpp
   void registerMathModule(Environment* env);
   void registerStringModule(Environment* env);
   // etc.
   ```

4. **Update Interpreter.cpp:**
   - Call registration functions in InitializeStandardLibrary()

### Phase 5: Extract Host Modules

1. **Create `modules/` directory**
2. **Move each host integration:**
   - WindowModule.cpp: window.focus, window.move, etc.
   - AudioModule.cpp: audio.setVolume, etc.
   - HotkeyModule.cpp: hotkey.register, etc.

3. **Each module registers its functions:**
   ```cpp
   void registerWindowModule(Environment* env, WindowManager* wm);
   ```

### Phase 6: Future - Bytecode VM

Once the above is complete, add bytecode compilation:

```
src/havel-lang/
├── compiler/
│   ├── BytecodeCompiler.hpp  # AST → bytecode
│   ├── BytecodeCompiler.cpp
│   ├── VM.hpp                # Bytecode interpreter
│   └── VM.cpp
└── runtime/
    ├── Interpreter.cpp       # Keep for debugging/REPL
    └── ...
```

## Benefits

After refactoring:

1. **Testability**: Language core can be tested without OS dependencies
2. **Embeddability**: Can embed language without pulling in window manager
3. **Maintainability**: Each module is isolated and focused
4. **Self-hosting**: Compiler only needs core + stdlib, not host APIs
5. **Performance**: Future VM can optimize bytecode execution

## Progress

### Phase 1: Extract Environment (COMPLETED ✅)

Successfully extracted `Environment` and `TraitRegistry` classes to:
- `runtime/Environment.hpp` - Class declarations
- `runtime/Environment.cpp` - TraitRegistry implementation

**Changes:**
- Removed 90 lines from `Interpreter.hpp`
- Removed 45 lines from `Interpreter.cpp`
- Created proper separation of concerns

**Dependency structure:**
```
Environment.hpp
    ↓ (includes)
Interpreter.hpp  (for HavelValue)
```

This is acceptable for now. Future refactoring could move `HavelValue` to a separate `Value.hpp` that both files include.

### Phase 2: Extract Standard Library Modules (IN PROGRESS 🔄)

Successfully extracted math module to `stdlib/MathModule.cpp`:
- `stdlib/MathModule.hpp` - Module declaration
- `stdlib/MathModule.cpp` - Math function implementations (~170 lines)

**Changes:**
- Removed ~430 lines from `Interpreter.cpp`
- Established module registration pattern: `registerMathModule(env)`
- Created reusable pattern for future stdlib modules

**Pattern established:**
```cpp
// stdlib/MathModule.hpp
namespace havel::stdlib {
  void registerMathModule(Environment* env);
}

// Interpreter.cpp
#include "stdlib/MathModule.hpp"
void InitializeMathBuiltins() {
  havel::stdlib::registerMathModule(environment.get());
}
```

**Remaining modules to extract:**
- String functions
- Array functions  
- File functions
- Regex functions
- etc.

### Phase 3: Extract Value Types (BLOCKED)

As documented above, extracting `HavelValue`, `HavelResult`, and `BuiltinFunction` to a separate `Value.hpp` failed due to circular dependencies that `std::variant` cannot handle.

**Options:**
1. Use `std::shared_ptr<HavelValue>` in variant (breaks cycle, adds overhead)
2. Keep monolithic structure (current choice)
3. Full VM rewrite with tagged unions (major undertaking)

### Phase 3-6: Pending

Remaining phases depend on resolving the Value types circular dependency.

## Notes

- Each phase should be committed separately
- Build must work after each phase
- Test suite should pass after each phase
- Don't refactor everything at once - do it incrementally
