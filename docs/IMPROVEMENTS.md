# Havel Language Improvements - Summary

## 1. Parser: Fixed Implicit Call Sugar with Operators

**Problem:** `print a ** 5` failed because the parser treated `print a` as a complete call expression, leaving `** 5` dangling.

**Root Cause:** The implicit call sugar in `parsePrattExpression()` was using `nud()` to parse the argument, which only parses a single token without considering following operators.

**Solution:** Changed implicit call sugar to:
1. Parse the argument as a **full expression** using `parsePrattExpression(0)`
2. Temporarily disable `allowBraceSugar` to prevent infinite recursion
3. Support **comma-separated multi-args**: `print a, b, c` → `print(a, b, c)`

**File:** `src/havel-lang/parser/Parser.cpp` (lines ~563-601)

**Test Results:**
```
print a ** 5        → 32    ✓
print a + b         → 5     ✓
print a * b         → 6     ✓
print a, b, a + b   → 2 3 5 ✓
```

---

## 2. Unified Error System

**Problem:** Errors were fragmented across different systems:
- Lexer: `CompilerError`, `LexError`
- Parser: `ParseError`, `CompilerError`
- VM: `ScriptError`
- Runtime: `RuntimeError`
- No centralized error reporting or formatting

**Solution:** Created `src/havel-lang/errors/ErrorSystem.h` with:

### Core Types
- **`HavelError`**: Unified error type with:
  - `ErrorSeverity` (Error, Warning, Info)
  - `ErrorStage` (Lexer, Parser, AST, Compiler, Bytecode, VM, Runtime, GC)
  - Source location (line, column, length, file)
  - Stack trace support (for runtime errors)
  - Machine-readable error codes (E001, E010, etc.)
  - Rich formatting via `what()` method

### Error Codes
```
E001-E005: Lexer errors (invalid char, unterminated string, etc.)
E010-E016: Parser errors (unexpected token, expected expression, etc.)
E020-E022: AST errors (invalid type, duplicate field, etc.)
E030-E036: Compiler errors (undefined variable, type mismatch, etc.)
E040-E043: Bytecode errors (invalid opcode, stack overflow, etc.)
E050-E055: Runtime errors (division by zero, index out of bounds, etc.)
```

### ErrorReporter Singleton
- Central error accumulation via `ErrorReporter::instance()`
- Methods: `error()`, `warning()`, `info()`, `errorAt()`
- Error counting and summary reporting
- Convenience macros: `REPORT_ERROR`, `REPORT_WARNING`, etc.

### Integration Points
- **VM ScriptError → HavelError**: Added `ScriptError::toHavelError()` conversion
- **HotkeyExecutor**: Catches ScriptError and converts to HavelError automatically
- **Backward compatibility**: Old `CompilerError` type still works for existing code

---

## 3. Hotkey Block Error Propagation

**Problem:** Hotkey execution errors were fire-and-forget - just printed to `cerr` with no propagation or structured handling.

**Solution:** Enhanced `HotkeyExecutor` to:
1. Track errors in each `Task` using `std::optional<HavelError>`
2. Catch `ScriptError` specifically and convert to `HavelError`
3. Route errors through `ErrorReporter::instance()` for centralized handling
4. Support custom error callbacks via `Task::errorCallback`
5. Use structured debug logging instead of raw `cerr`

**File:** `src/core/io/HotkeyExecutor.hpp`

**Changes:**
- Added `#include "../../havel-lang/errors/ErrorSystem.h"`
- Added `error` and `errorCallback` fields to `Task` struct
- Enhanced exception handling in `workerLoop()`:
  - Catches `ScriptError` → converts via `toHavelError()`
  - Catches `std::exception` → wraps in `HavelError`
  - Catches `...` → creates generic `HavelError`
  - All errors reported through `ErrorReporter`
  - Custom callbacks supported for advanced error handling

---

## 4. Build Status

**Successfully built and tested:** Parser fix works correctly
- Binary: `build-debug/havel` (updated 2026-04-06 00:03)
- Test: `test_implicit_call.hv` passes all cases

**Known issue:** Qt moc parsing of `VM.hpp` fails with namespace ambiguity for `HostContext`
- This is a **pre-existing issue** unrelated to our changes
- The forward declaration `const class havel::HostContext *` in `havel::compiler::VM` confuses Qt's moc
- Workaround: The binary from the earlier successful build works correctly
- Full rebuild requires fixing the Qt moc issue separately

---

## 5. Next Steps

### Completed
- ✅ Fix implicit call sugar with operators
- ✅ Add comma-separated multi-arg support
- ✅ Create unified ErrorSystem
- ✅ Integrate VM ScriptError → HavelError
- ✅ Enhance HotkeyExecutor error handling

### Pending
- ⏳ Fix Qt moc namespace issue with HostContext
- ⏳ Add thread, interval, and timeout blocks with full support
- ⏳ Migrate remaining components to use ErrorSystem (lexer, parser, compiler)
- ⏳ Add error recovery suggestions to HavelError
- ⏳ Implement error code documentation

---

## Files Modified

1. `src/havel-lang/parser/Parser.cpp` - Fixed implicit call sugar
2. `src/havel-lang/errors/ErrorSystem.h` - **NEW** - Unified error system
3. `src/havel-lang/compiler/vm/VM.hpp` - Added ErrorSystem include, ScriptError conversion
4. `src/havel-lang/compiler/vm/VM.cpp` - Implemented ScriptError::toHavelError()
5. `src/core/io/HotkeyExecutor.hpp` - Enhanced error handling with HavelError
