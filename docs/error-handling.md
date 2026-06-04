# Error Handling

How errors are created, reported, and caught in Havel — from compile-time diagnostics to runtime exception handling.

---

## Error Types

Havel uses 12 distinct error types across the compilation pipeline and runtime:

### Unified Error System

**Source**: `src/havel-lang/errors/ErrorSystem.h`

The `HavelError` type is the unified error representation:

```cpp
struct HavelError {
    ErrorSeverity severity;    // Error, Warning, Info
    ErrorStage stage;          // Lexer, Parser, Semantic, ByteCompiler, VM, Module
    SourceLocation location;   // file, line, column
    std::string message;
    std::string code;          // e.g. "E001"
    std::vector<StackFrame> stackTrace;
};
```

`ErrorReporter` is a central singleton that accumulates all errors. It provides a fluent builder API:

```cpp
ErrorReporter::instance()
    .error("E042", "undefined variable '{name}'")
    .at(source, line, col)
    .note("did you mean '{suggestion}'?")
    .report();
```

### Compile-Time Errors

| Type | Source | When |
|------|--------|------|
| `LexError` | Lexer | Invalid token, unterminated string |
| `ParseError` | Parser | Syntax error, unexpected token |
| `CompilerError` (lexer-level) | Lexer | Character encoding issues |
| `CompilerError` (bytecode-level) | ByteCompiler | Type mismatch, undefined symbol |

Compile errors flow: `LexError` → `ParseError` → `CompilerError` → `COMPILER_THROW` macro → `ErrorReporter` + `std::runtime_error`.

The ByteCompiler has a `collect_errors_` mode for linting — errors are accumulated instead of throwing, allowing multiple errors to be reported in one pass.

### Runtime Errors

| Type | Source | When |
|------|--------|------|
| `ScriptThrow` | VM | User-initiated `throw` |
| `ScriptError` | VM | Uncaught exception reaching top level |
| `RuntimeError` | Legacy interpreter | Pre-VM interpreter errors |
| `HavelException` | VM | Internal VM assertion failures |

Runtime errors flow: `ScriptThrow{value}` (C++ exception) → `handleScriptThrow()` → catch handler or `ScriptError` (uncaught).

### Supporting Types

```cpp
struct SourceLocation {
    std::string file;
    uint32_t line;
    uint32_t column;
};

struct StackFrame {
    std::string function;
    SourceLocation location;
};
```

---

## Error Codes

Machine-readable error codes identify specific error categories:

| Range | Stage | Examples |
|-------|-------|---------|
| E001–E010 | Lexer | E001: unterminated string, E002: invalid character |
| E011–E020 | Parser | E011: unexpected token, E012: expected expression |
| E021–E030 | Semantic | E021: type mismatch, E022: undefined variable |
| E031–E040 | ByteCompiler | E031: invalid assignment target |
| E041–E050 | Module | E041: module not found, E042: circular import |
| E051–E061 | Runtime | E051: division by zero, E052: index out of bounds |

---

## Error Reporting

### Rust-Style Diagnostics

**Source**: `src/havel-lang/errors/ErrorPrinter.h`

The `ErrorPrinter` produces caret-style diagnostics with ANSI colors:

```
error[E022]: undefined variable 'x'
  --> script.hv:7:5
   |
 7 |     x + 1
   |     ^ not defined in this scope
   |
   = note: did you mean 'y'?
```

### Pipeline Formatting

**Source**: `src/havel-lang/compiler/core/Pipeline.cpp`

`formatDiagnostic()` and `enrichRuntimeError()` re-format raw error messages with source context. When a runtime error occurs, the pipeline:

1. Captures the raw error message
2. Extracts the source location from the current chunk
3. Reads the source line from the original file
4. Builds a caret-style diagnostic with context

### Error Recovery (Parser)

**Source**: `src/havel-lang/parser/Parser.cpp`

The parser uses error recovery modes to continue after syntax errors:

| Mode | Recovery Strategy |
|------|-------------------|
| `None` | No recovery (first error stops) |
| `PanicSemicolon` | Skip tokens until semicolon |
| `PanicBrace` | Skip tokens until matching `}` |
| `PanicStatement` | Skip tokens until statement boundary |

Recovery allows multiple errors to be reported in a single compilation pass.

---

## Try/Catch/Finally

### Opcodes

| Opcode | Operands | Description |
|--------|----------|-------------|
| `TRY_ENTER` | catch_ip, finally_ip | Push exception handler |
| `TRY_EXIT` | — | Pop exception handler (normal exit) |
| `LOAD_EXCEPTION` | — | Push caught exception value |
| `THROW` | — | Pop and throw value |

### Exception Handler Frame

```cpp
struct TryHandler {
    uint32_t catch_ip;      // Jump target on exception
    uint32_t finally_ip;    // Jump target on normal exit
    size_t stack_depth;     // Stack depth at TRY_ENTER time
};
```

Handlers are stored per-frame in a `try_stack` vector.

### Throw Mechanics

When `THROW` executes:

1. Pop value from stack
2. Throw `ScriptThrow{value}` as a C++ exception
3. The dispatch loop catches `ScriptThrow` via `try/catch` around the entire dispatch
4. `handleScriptThrow()` walks the frame stack looking for a `TryHandler`:
   - If found: unwind expression stack to `stack_depth`, jump to `catch_ip`
   - If not found: unwind call stack, report as unhandled `ScriptError`

`std::runtime_error` from `COMPILER_THROW` is also caught and **converted to a script exception** — so try/catch can catch internal errors:

```cpp
catch (const std::runtime_error& e) {
    // Convert to ScriptThrow so user catch blocks can handle it
    handleScriptThrow(ScriptThrow{Value::makeString(e.what())});
}
```

### Catch Block

```
TRY_ENTER catch_ip, finally_ip
    // ... try body ...
TRY_EXIT
JUMP past_catch

catch_ip:
    LOAD_EXCEPTION      // push caught value
    // ... catch body ...
    // falls through to finally

past_catch:
    // ... finally body (or after finally) ...
```

### Finally Block

The `finally_ip` is reached from two paths:

1. **Normal exit**: After `TRY_EXIT`, the compiler emits a `JUMP` to `finally_ip`
2. **Exception exit**: If no catch handler matches, `handleScriptThrow` jumps to `finally_ip`

Finally blocks always execute, even during exception propagation.

---

## User-Level Syntax

### throw

```
throw "something went wrong"
throw { message: "bad input", code: 400 }
```

Any value can be thrown. It becomes the caught value in catch blocks.

### try/catch

```
try {
    riskyOperation()
} catch {
    // 'it' refers to the thrown value
    print("caught: {it}")
}
```

### try/catch/finally

```
try {
    openResource()
    doWork()
} catch {
    handleError()
} finally {
    closeResource()
}
```

### try/finally (no catch)

```
try {
    doWork()
} finally {
    cleanup()
}
```

---

## Error Propagation Across Modules

When an error occurs inside a loaded module:

1. If the module has a try/catch, it handles the error locally
2. If uncaught, the error propagates up through `loadScript()` / `loadModule()`
3. `loadScript()` saves/restores exception state — an error in the loaded script does not corrupt the caller's exception handlers
4. `loadModule()` wraps errors as module load failures

---

## Common Patterns

### Guard Clauses

```
try {
    val result = mightFail()
    processResult(result)
} catch {
    print("operation failed: {it}")
    fallback()
}
```

### Resource Cleanup

```
try {
    val f = fs.open("data.txt")
    processData(f)
} finally {
    f.close()
}
```

### Nested Error Handling

```
try {
    try {
        innerOperation()
    } catch {
        throw "inner failed: {it}"   // re-throw with context
    }
} catch {
    print("outer caught: {it}")
}
```
