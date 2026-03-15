# Havel Interpreter - Architectural TODOs

## Completed Fixes ✅

1. **PixelAutomation Null Reference** - Changed from reference to pointer with null checks
2. **Shell Command Type Checking** - Only accepts string or array, clear error messages
3. **Defensive Checks** - Runtime validation in shell commands and arrays
4. **ShellExecutor Integration** - Using existing ShellExecutor with `splitPipes()`

## Pending Architectural Improvements

### 1. Unix Pipe Implementation (HIGH PRIORITY)

**Current**: Temp file-based pipes
```cpp
cmd1 -> write /tmp/havel_pipe_pid_stage
cat file | cmd2
```

**Problems**:
- Disk I/O for every pipe
- Race conditions
- Slow for large outputs
- Temp file cleanup risks

**Solution**: Use `pipe()`/`fork()`/`dup2()`/`exec()`
```cpp
int pipefd[2];
pipe(pipefd);
if (fork() == 0) {
    dup2(pipefd[1], STDOUT_FILENO);
    execvp(cmd1, args1);
}
// ... setup cmd2 to read from pipefd[0]
```

**Expected**: 10-100× speedup for pipelines

---

### 2. Finish RuntimeContext Refactor (MEDIUM PRIORITY)

**Current**: Evaluators depend on Interpreter
```cpp
class ExprEvaluator {
    Interpreter* interpreter;  // Tight coupling
};
```

**Goal**: Evaluators use RuntimeContext
```cpp
class ExprEvaluator {
    RuntimeContext& ctx;  // Loose coupling
};
```

**Benefits**:
- Easier testing (mock RuntimeContext)
- No circular dependencies
- Pluggable runtime (sandbox, embedded mode)
- Thinner Interpreter

**Status**: RuntimeContext exists but not used by evaluators

---

### 3. Consistent Error Handling Model (LOW PRIORITY)

**Current**: Hybrid Result + exceptions
```cpp
auto result = Evaluate(...);  // Result type
if (isError(result)) return;
HavelValue value = unwrap(result);  // Throws on error
```

**Rationale**: 
- Result for normal flow
- Exception as safety net (catches programmer errors)

**Alternative**: Pure Result model
```cpp
Result<HavelValue, Error> Evaluate(...);
// No exceptions anywhere
```

**Decision**: Document current model clearly, consider migration later

---

### 4. Bytecode Compilation (FUTURE)

**Current**: AST walk + variant dispatch
```
source -> AST -> interpret
```

**Goal**: Compile to bytecode
```
source -> AST -> bytecode -> VM execute
```

**Benefits**:
- 10-50× speedup
- Easier optimization
- Simpler evaluator

**Scope**: Major refactor, plan carefully

---

### 5. Shell Command Grammar (DONE ✅)

**Issue**: Parser should accept any expression after `$`

**Grammar**:
```
shell_command: '$' expression
```

**Status**: Already correct in parser. Defensive checks prevent misuse.

---

## Error Handling Model (Documented)

```cpp
// Result-style for normal flow
auto result = Evaluate(...);
if (isError(result)) { 
    lastResult = result;
    return; 
}

// Exception as safety net (should never happen in production)
HavelValue value = unwrap(result);

// Rationale: unwrap() exception catches programmer errors,
// not runtime errors. All production code checks isError() first.
```

**Rule**: If `unwrap()` throws, it's a bug in the interpreter, not the script.

---

## Performance Notes

### Current Bottlenecks

1. **String conversions** - Every value access may allocate
2. **Variant dispatch** - `std::visit` has overhead
3. **Map lookups** - `unordered_map` for objects/scopes
4. **Temp file pipes** - Disk I/O for every pipe

### Quick Wins

1. ✅ Type checking prevents unnecessary conversions
2. ✅ ShellExecutor centralizes logic
3. ⏳ Unix pipes (next priority)
4. ⏳ String interning for identifiers

### Long-term

1. Bytecode compilation
2. JIT for hot paths
3. Object pooling for HavelValue

---

## Testing Strategy

### Unit Tests Needed

1. ShellExecutor with various inputs
2. Type checking for shell commands
3. PixelAutomation null handling
4. Pipe chain execution

### Integration Tests

1. Full script execution
2. Mode detection with window changes
3. Conditional hotkey evaluation

### Performance Tests

1. Pipeline throughput (before/after Unix pipes)
2. Large array operations
3. Nested object access

---

## Migration Path

### Phase 1: Stabilize (Current)
- ✅ Fix critical bugs
- ✅ Add defensive checks
- ⏳ Unix pipes

### Phase 2: Decouple (Next)
- Finish RuntimeContext refactor
- Add comprehensive tests
- Document error model

### Phase 3: Optimize (Future)
- Bytecode compilation
- Performance profiling
- Targeted optimizations

---

Last updated: 2026-03-15
