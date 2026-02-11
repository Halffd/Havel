# Hybrid Execution Engine Implementation

## Overview

This document describes the hybrid execution engine for Havel, implementing a Java-style approach with bytecode compilation, interpretation, and JIT optimization.

## Architecture

### Phase 1: Compilation (AST → Bytecode)

The compiler translates Havel source code into an intermediate bytecode representation:

```
Havel Source → AST → Bytecode
```

**Bytecode Features:**
- Stack-based virtual machine
- Compact instruction format
- Platform independent
- Optimized for interpretation

### Phase 2: Interpretation (Bytecode → Execution)

The interpreter executes bytecode using a virtual machine:

```
Bytecode → Stack VM → Native Execution
```

**Interpreter Features:**
- Fast startup time
- Dynamic typing support
- Memory efficient
- Debug capabilities

### Phase 3: JIT Optimization (Hot Paths)

The JIT compiler optimizes frequently executed code:

```
Hot Bytecode → Native Code → Maximum Performance
```

**JIT Features:**
- Hot path detection
- Dynamic compilation
- Native code generation
- Automatic optimization

## Implementation Components

### 1. Bytecode Format

#### Instructions
```cpp
enum class OpCode : uint8_t {
    LOAD_CONST,    // Load constant onto stack
    LOAD_VAR,      // Load variable onto stack
    STORE_VAR,     // Store from stack to variable
    POP,           // Pop value from stack
    DUP,           // Duplicate top of stack
    
    ADD, SUB, MUL, DIV, MOD, POW,  // Arithmetic
    EQ, NEQ, LT, LTE, GT, GTE,    // Comparisons
    AND, OR, NOT,                   // Logical
    
    JUMP, JUMP_IF_FALSE, JUMP_IF_TRUE,  // Control flow
    CALL, RETURN,                         // Functions
    
    ARRAY_NEW, ARRAY_GET, ARRAY_SET, ARRAY_PUSH,  // Arrays
    OBJECT_NEW, OBJECT_GET, OBJECT_SET,            // Objects
    
    PRINT, DEBUG, NOP  // Debug and utilities
};
```

#### Values
```cpp
using BytecodeValue = std::variant<
    std::nullptr_t,  // null
    bool,             // boolean
    int64_t,          // integer
    double,           // float
    std::string,      // string
    uint32_t          // constant pool index
>;
```

### 2. Compiler Implementation

The `HavelBytecodeCompiler` class:

- **Visits AST nodes** to generate bytecode
- **Manages symbol tables** for variables
- **Optimizes** during compilation (constant folding, dead code elimination)
- **Generates compact instruction sequences**

#### Example Compilation
```havel
let x = 2 + 3 * 4;
```

Compiles to:
```
LOAD_CONST 3
LOAD_CONST 4
MUL
LOAD_CONST 2
ADD
STORE_VAR x
```

### 3. Interpreter Implementation

The `HavelBytecodeInterpreter` class:

- **Stack-based VM** with operand stack
- **Instruction dispatch loop** for execution
- **Type system** with dynamic checking
- **Memory management** for values

#### Example Execution
```
Stack: []           IP: 0  LOAD_CONST 3
Stack: [3]          IP: 1  LOAD_CONST 4  
Stack: [3, 4]       IP: 2  MUL
Stack: [12]          IP: 3  LOAD_CONST 2
Stack: [12, 2]       IP: 4  ADD
Stack: [14]          IP: 5  STORE_VAR x
Stack: []           IP: 6  (next instruction)
```

### 4. JIT Implementation

The `HavelJITCompiler` class:

- **Tracks execution counts** for functions
- **Compiles hot functions** to native code
- **Maintains compiled function cache**
- **Falls back to interpreter** for cold code

#### JIT Threshold
- Default: 100 executions
- Configurable per application
- Automatic hot path detection

### 5. Hybrid Engine

The `HybridEngine` class coordinates all phases:

```cpp
class HybridEngine {
    std::unique_ptr<BytecodeCompiler> compiler;
    std::unique_ptr<BytecodeInterpreter> interpreter;
    std::unique_ptr<JITCompiler> jit;
    std::unordered_map<std::string, uint32_t> execution_counts;
};
```

#### Execution Flow
1. **Compile** AST to bytecode once
2. **Interpret** bytecode for execution
3. **Track** execution frequency
4. **JIT compile** hot functions
5. **Execute** native code for optimized functions

## Performance Characteristics

### Startup Performance
- **Fast**: No full native compilation
- **Portable**: Bytecode runs anywhere
- **Memory efficient**: Compact representation

### Runtime Performance
- **Progressive optimization**: Gets faster over time
- **Hot path optimization**: Frequently used code runs at native speed
- **Cold code**: Still runs efficiently via interpreter

### Memory Usage
- **Bytecode**: Compact intermediate format
- **JIT cache**: Only for hot functions
- **Garbage collection**: Automatic memory management

## Comparison with Other Approaches

### Pure Interpretation
- ✅ Fast startup
- ❌ Slow execution
- ❌ No optimization

### Pure Compilation
- ✅ Fast execution
- ❌ Slow startup
- ❌ Platform specific

### Hybrid Approach (Havel)
- ✅ Fast startup
- ✅ Fast execution (hot paths)
- ✅ Platform independent
- ✅ Progressive optimization

## Usage Examples

### Basic Usage
```cpp
auto engine = createHybridEngine();
engine->compile(program);
auto result = engine->execute("main", args);
```

### Performance Tuning
```cpp
engine->setJITEnabled(true);
engine->setJITThreshold(50);  // Compile after 50 executions
```

### Debug Mode
```cpp
interpreter->setDebugMode(true);
// Shows instruction execution and stack state
```

## Future Enhancements

### Advanced Optimizations
- **Inline caching** for property access
- **Escape analysis** for stack allocation
- **Loop unrolling** for hot loops
- **Type specialization** for dynamic types

### Advanced JIT Features
- **Profile-guided optimization**
- **Adaptive compilation thresholds**
- **Multi-tier compilation** (baseline + optimized)
- **AOT compilation** for startup critical code

### Integration Features
- **Debug information** preservation
- **Source mapping** for debugging
- **Hot reloading** for development
- **Performance profiling** integration

## Benefits

### For Developers
- **Fast development cycle** (no compilation wait)
- **Progressive performance** (gets faster during use)
- **Platform independence** (write once, run anywhere)
- **Debugging support** (source-level debugging)

### For Users
- **Fast startup** (no long compilation)
- **Excellent performance** (native speed for hot code)
- **Memory efficiency** (compact bytecode)
- **Stable performance** (predictable execution)

### For the Language
- **Flexibility** (runtime features possible)
- **Optimization** (automatic performance tuning)
- **Portability** (cross-platform bytecode)
- **Extensibility** (easy to add optimizations)

## Conclusion

The hybrid execution engine provides the best of both worlds:
- **Compilation benefits**: Optimization, type checking, validation
- **Interpretation benefits**: Fast startup, flexibility, debugging
- **JIT benefits**: Native performance, adaptive optimization

This approach is proven successful by languages like Java, C#, JavaScript (V8), and Python (PyPy), making it an excellent choice for Havel's execution model.
