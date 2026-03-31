# Havel Compiler/VM Refactoring - New Classes Summary

## Overview
This document describes the new classes created during the refactoring of the Havel compiler and VM.

## New Classes

### 1. Closure Management (Closure.hpp/cpp)

#### Closure
- Represents a function with captured environment
- Manages function index and upvalue list
- Factory methods for creating closures from descriptors
- Conversion to BytecodeValue

#### Upvalue
- Represents a captured variable from outer scope
- Types: Local (captures local) or Upvalue (captures upvalue)
- Integrates with GC via UpvalueCell
- Close operation for when outer function returns

#### UpvalueManager
- Manages open upvalues during execution
- Opens new upvalues or returns existing ones
- Closes upvalues when function returns
- Tracks open upvalue count

#### ClosureFactory
- Helper for creating closures during compilation
- Creates closures from function indices with upvalue descriptors

### 2. Compiler Utilities (CompilerUtils.hpp/cpp)

#### CompilerUtils
- **String utilities**: name mangling/demangling, identifier validation, symbol sanitization
- **Slot management**: allocate/free/count used slots
- **Binding utilities**: check binding kinds (global, local, upvalue, function)
- **Debug utilities**: binding kind to string, opcode to string, print bindings
- **Validation**: validate slots, function indices, constant indices

#### ScopeTracker
- Manages nested scopes during compilation
- Tracks local variables per scope
- Handles const declaration tracking
- Slot reservation and release

#### FunctionContext
- Manages function compilation context
- Tracks local variables with slot, const-ness, capture status
- Upvalue management with add/find operations
- Parameter tracking

### 3. Binding Resolution (BindingResolver.hpp/cpp)

#### BindingResolver
- Centralized identifier resolution
- Manages function stack with FunctionContext
- Scope management (begin/end scope)
- Variable declaration (locals, globals, top-level functions)
- Identifier resolution with proper upvalue capture
- Binding recording for AST nodes
- Error handling

### 4. Code Emission (CodeEmitter.hpp/cpp)

#### CodeEmitter
- Bytecode instruction emission
- Function management (begin/end)
- Jump handling (emit, patch)
- Constant management (add, get)
- Local slot reservation
- Tail call optimization tracking
- Source location tracking

#### InstructionBuilder
- Fluent interface for building instructions
- Chain methods: op(), operand(), operands(), atLocation()
- emit() and emitWithIndex() for finalization

### 5. Expression Compilation (ExpressionCompiler.hpp/cpp)

#### ExpressionCompiler
- Compiles all AST expression types to bytecode:
  - Identifier, Binary, Unary, Call
  - Assignment, Member, Index
  - Lambda, Array, Object literals
  - Ternary, Update, Await
  - Spread, Range, Pipeline
  - Interpolated strings
- Helper methods for load/store operations
- Binary operator to bytecode conversion

#### StatementCompiler
- Compiles all AST statement types:
  - Expression, Let, If/While/DoWhile/For/Loop
  - Return, Block, Try/Throw
  - When, Mode blocks
  - Hotkey bindings
- Loop context management for break/continue

#### PatternCompiler
- Compiles destructuring patterns
- Collects identifiers from patterns
- Array and object pattern matching

#### FunctionCompiler
- Compiles function declarations and lambdas
- Parameter compilation with pattern support
- Prologue/epilogue generation

### 6. VM Execution (VMExecutionContext.hpp/cpp)

#### CallFrame
- Single function call frame
- Instruction pointer management
- Local base tracking
- Try handler tracking

#### CallFrameManager
- Call stack management
- Push/pop frames
- Frame access by index
- Stack overflow detection
- Stack trace generation

#### VMStack
- Operand stack for VM
- Push/pop/peek operations
- Bulk operations
- GC root collection

#### VMLocals
- Local variable slots
- Get/set operations
- Capacity management
- Slot reservation/release

#### VMExecutionContext
- Isolated execution context
- Executes functions and closures
- Manages stack, locals, frames, upvalues
- Instruction execution loop
- GC root collection

#### VMGlobals
- Thread-safe global variable storage
- Read/write operations
- Snapshot/restore for persistence

#### VMHostBridge
- Interface for calling host functions
- Function registration
- Callable check

### 7. Garbage Collection (GCManager.hpp/cpp)

#### GCObject
- Base class for GC-managed objects
- Type enumeration (String, Array, Object, Closure, etc.)
- Mark flag and object ID
- markChildren() virtual method
- Size tracking

#### GCRootSet
- Manages root references for GC
- Internal and external root management
- Handle-based external root API

#### GCManager
- High-level GC management
- Configuration (heap sizes, growth factor)
- Collection control (maybeCollect, forceFullCollect)
- Object registration/unregistration
- Root management
- Stats tracking (pause times, allocation counts)
- Mark-sweep implementation

#### GCPtr<T>
- Smart pointer template for GC objects
- Automatic GC integration
- Type-safe access

#### GCString, GCArray, GCObjectMap
- Specific GC object types
- Proper markChildren() implementations
- Size calculation

### 8. Module System (ModuleResolver.hpp/cpp)

#### ModuleResolver
- Module path resolution
- Module loading with caching
- Circular dependency detection
- Path management
- Error tracking

#### ErrorReporter
- Singleton error reporter
- Severity levels: Warning, Error, Fatal
- Source location tracking (file, line, column)
- Error formatting
- Handler registration for callbacks

## File Organization

```
src/havel-lang/compiler/bytecode/
├── Closure.hpp/cpp              # Closure, Upvalue, UpvalueManager
├── CompilerUtils.hpp/cpp        # Shared utilities, ScopeTracker, FunctionContext
├── BindingResolver.hpp/cpp      # Identifier resolution
├── CodeEmitter.hpp/cpp          # Bytecode emission, InstructionBuilder
├── ExpressionCompiler.hpp/cpp   # Expression, Statement, Pattern, Function compilers
├── VMExecutionContext.hpp/cpp   # VM execution, CallFrame, Stack, Locals
├── GCManager.hpp/cpp            # Garbage collection management
└── ModuleResolver.hpp/cpp       # Module loading, Error reporting
```

## Integration Notes

### Using New Classes in Existing Code

The new classes are designed to be used together but can also be used independently:

```cpp
// Example: Using new compiler pipeline
BytecodeChunk chunk;
CodeEmitter emitter(chunk);
BindingResolver resolver(result);
ExpressionCompiler exprCompiler(emitter, resolver, result);
StatementCompiler stmtCompiler(emitter, exprCompiler, resolver);

// Compile a function
FunctionCompiler funcCompiler(emitter, stmtCompiler, exprCompiler,
                               patternCompiler, resolver);
uint32_t funcIndex = funcCompiler.compileFunction(functionDecl);
```

### Migration Path

1. **Phase 1**: New classes exist alongside existing code ✓
2. **Phase 2**: Gradually replace parts of LexicalResolver with BindingResolver
3. **Phase 3**: Gradually replace parts of ByteCompiler with specialized compilers
4. **Phase 4**: Refactor VM to use new Closure and execution classes

## Build Status

All new files compile successfully with the existing CMakeLists.txt (uses GLOB_RECURSE).

Test results:
- Build: ✓ PASS
- quick_test.havel: ✓ PASS (3/3 tests)

## Future Enhancements

- Integrate new classes into main compiler pipeline
- Add unit tests for each new class
- Performance benchmarking vs existing code
- Documentation for public APIs
- Memory leak testing with new GC classes
