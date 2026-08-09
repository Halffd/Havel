---
title: "Error Handling Reference"
description: "Error types, codes, try/catch/finally, throw, and error propagation."
---

# Error Handling Reference

## Error Types

### Compile-Time Errors

| Type | Stage | When |
|------|-------|------|
| `LexError` | Lexer | Invalid token, unterminated string |
| `ParseError` | Parser | Syntax error, unexpected token |
| `CompilerError` | ByteCompiler | Type mismatch, undefined symbol |

### Runtime Errors

| Type | Source | When |
|------|--------|------|
| `ScriptThrow` | VM | User-initiated `throw` |
| `ScriptError` | VM | Uncaught exception at top level |
| `RuntimeError` | Legacy | Pre-VM interpreter errors |
| `HavelException` | VM | Internal VM assertion failures |

---

## Error Codes

| Range | Stage | Examples |
|-------|-------|----------|
| E001–E010 | Lexer | E001: unterminated string, E002: invalid character |
| E011–E020 | Parser | E011: unexpected token, E012: expected expression |
| E021–E030 | Semantic | E021: type mismatch, E022: undefined variable |
| E031–E040 | ByteCompiler | E031: invalid assignment target |
| E041–E050 | Module | E041: module not found, E042: circular import |
| E051–E061 | Runtime | E051: division by zero, E052: index out of bounds |

---

## Try / Catch / Finally

### Syntax

```hv
try { body }
try { body } catch e { handler }
try { body } catch (e) { handler }
try { body } finally { cleanup }
try { body } catch e { handler } finally { cleanup }
```

Both `catch e { }` and `catch (e) { }` forms supported.

### Throw

```hv
throw expr
```

Any value can be thrown. In catch blocks, `it` refers to the thrown value:

```hv
try {
    risky()
} catch {
    print("caught: {it}")  // 'it' is the thrown value
}
```

### Catch Variable

```hv
try {
    mightFail()
} catch err {
    // 'err' or 'it' both work
    print("Error: {err}")
}
```

---

## Error Propagation

### Across Modules

When an error occurs inside a loaded module:

1. If the module has try/catch, it handles locally
2. If uncaught, propagates up through `loadScript()` / `loadModule()`
3. `loadScript()` saves/restores exception state
4. `loadModule()` wraps errors as module load failures

### From Host Functions

Host functions can throw by returning a `ScriptThrow` or using the error API:

```cpp
vm.registerHostFunction("my.func", [](const auto& args) -> Value {
    if (error_condition) {
        throw havel::ScriptThrow{Value::makeString("error message")};
    }
    return Value::makeNil();
});
```

---

## Common Patterns

### Guard Clauses

```hv
try {
    val result = mightFail()
    processResult(result)
} catch {
    print("operation failed: {it}")
    fallback()
}
```

### Resource Cleanup

```hv
try {
    val f = fs.open("data.txt")
    processData(f)
} finally {
    f.close()
}
```

### Nested Error Handling

```hv
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

### Result Type Pattern

```hv
fn readFile(path) {
    try {
        content = fs.read(path)
        { ok: true, data: content }
    } catch {
        { ok: false, error: it }
    }
}

result = readFile("config.json")
if result.ok {
    print(result.data)
} else {
    print("Failed: {result.error}")
}
```

---

## Debugging Errors

### Rust-Style Diagnostics

```
error[E022]: undefined variable 'x'
  --> script.hv:7:5
   |
 7 |     x + 1
   |     ^ not defined in this scope
   |
   = note: did you mean 'y'?
```

### CLI Flags

```bash
# Stop on first error
havel -e script.hv

# Debug output
havel -d script.hv

# Lint only
havel --lint script.hv
```

---

## Error API (Host Functions)

### From C++

```cpp
// Throw from host function
throw havel::ScriptThrow{Value::makeString("error")};

// Catch in C++
try {
    vm.runString("throw \"error\"");
} catch (const havel::ScriptError& e) {
    e.message;       // Error message
    e.location;      // SourceLocation { file, line, column }
    e.stackTrace;    // Stack trace string
}
```

---

**Previous:** [FFI Reference](/reference/ffi)
**Next:** [Extern Types Reference →](/reference/extern-types)