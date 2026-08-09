---
title: "Profiling and Debugging"
description: "LSP, DAP, GDB integration, bytecode debugging, and performance profiling."
---

# Profiling and Debugging

## Language Server Protocol (LSP)

Havel includes an LSP server for editor integration.

### Start LSP Server

```bash
./build-debug/havel-lsp
```

### Client Configuration (VS Code)

```json
// .vscode/settings.json
{
    "havel.lsp.enable": true,
    "havel.lsp.path": "./build-debug/havel-lsp"
}
```

### Features

| Feature | Support |
|---------|---------|
| Diagnostics (errors/warnings) | ✓ |
| Hover documentation | ✓ |
| Go to definition | ✓ |
| Completion | ✓ |
| Document symbols | ✓ |
| Workspace symbols | ✓ |
| Code actions | Partial |
| Rename | Partial |

### LSP Flags

```bash
havel-lsp --stdio           # Standard I/O transport
havel-lsp --port 8080       # TCP transport
havel-lsp --log-file lsp.log
```

---

## Debug Adapter Protocol (DAP)

Havel supports DAP for debugger integration.

### Start DAP Server

```bash
./build-debug/havel --dap
```

### VS Code Launch Config

```json
// .vscode/launch.json
{
    "version": "0.2.0",
    "configurations": [
        {
            "type": "havel",
            "request": "launch",
            "name": "Debug Havel Script",
            "program": "${file}",
            "cwd": "${workspaceFolder}",
            "runtimeExecutable": "./build-debug/havel",
            "runtimeArgs": ["--dap"]
        }
    ]
}
```

### DAP Features

- Breakpoints (line, conditional, logpoint)
- Step in/over/out
- Variable inspection
- Watch expressions
- Call stack
- Evaluate in REPL

---

## GDB Integration

### Debug the VM

```bash
gdb --args ./build-debug/havel -d script.hv
```

### Useful GDB Commands

```gdb
# Break at VM dispatch loop
break VM.cpp:executeFrame

# Break at specific opcode
break 'VM::executeInstruction(Instruction)'

# Print Havel Value
print value.toString()

# Print call stack
bt

# Inspect VM state
print vm.current_chunk_
print vm.ip_
print vm.stack_
```

### Pretty Printers

Add to `.gdbinit`:

```python
# Havel Value printer
class ValuePrinter:
    def __init__(self, val):
        self.val = val
    
    def to_string(self):
        return self.val.toString()

gdb.printing.register_pretty_printer(
    gdb.current_objfile(),
    lambda val: ValuePrinter(val) if val.type.name == 'havel::Value' else None
)
```

---

## Bytecode Debugging

### Disassembly

```bash
# Show bytecode for script
./build-debug/havel --debug-bytecode script.hv

# Compile to bytecode file
./build-debug/havel --build script.hv -o script.hvc

# Disassemble bytecode file
./build-debug/havel --debug-bytecode script.hvc
```

### Output Format

```
Bytecode for script.hv:
0000 LOAD_CONST     0      ; 1
0001 LOAD_CONST     1      ; 2
0002 LOAD_GLOBAL    0      ; "add"
0003 CALL           2
0004 STORE_VAR      0      ; x
0005 RETURN

Constants:
  [0] int: 1
  [1] int: 2

Globals:
  [0] "add"
  [1] "x"

Functions:
  [0] add (arity=2, locals=2, start=10, end=20)
```

### Trace Execution

```bash
# Instruction-level trace
./build-debug/havel -dbc -de script.hv

# GC trace
./build-debug/havel -dgc script.hv

# Hotkey trace
./build-debug/havel -dhk script.hv
```

---

## REPL Debugging

```bash
./build-debug/havel --repl
```

### REPL Commands

| Command | Description |
|---------|-------------|
| `.help` | Show help |
| `.exit` / `.quit` | Exit |
| `.clear` | Clear screen |
| `.load file.hv` | Load and execute file |
| `.vars` | List globals |
| `.fns` | List functions |
| `.hotkeys` | List registered hotkeys |
| `.bytecode fn` | Show function bytecode |
| `.ast expr` | Show AST for expression |

### Debug in REPL

```hv
havel> .bytecode add
// Shows bytecode for 'add' function

havel> x = 5
havel> .vars
// x = 5

havel> F1 => { print("test") }
havel> .hotkeys
// F1 -> (alias: none, policy: drop, count: 0)
```

---

## Profiling

### Built-in Profiler

```bash
# Enable profiling
./build-debug/havel --profile script.hv

# Output: profile.json (Chrome trace format)
```

### Profile Output

```json
{
    "traceEvents": [
        { "name": "fn:add", "cat": "function", "ph": "B", "ts": 1000, "pid": 1, "tid": 1 },
        { "name": "fn:add", "cat": "function", "ph": "E", "ts": 1050, "pid": 1, "tid": 1 },
        { "name": "gc", "cat": "gc", "ph": "B", "ts": 2000, "pid": 1, "tid": 1 },
        { "name": "gc", "cat": "gc", "ph": "E", "ts": 2010, "pid": 1, "tid": 1 }
    ]
}
```

Load in Chrome: `chrome://tracing` → Load `profile.json`

### Manual Timing

```hv
fn profile(name, fn) {
    start = sys.clock()
    result = fn()
    elapsed = sys.clock() - start
    print("{name}: {elapsed * 1000}ms")
    result
}

profile("computation", fn => {
    // heavy work
})
```

---

## Memory Profiling

### GC Debugging

```bash
./build-debug/havel -dgc script.hv
```

Output:
```
[GC] Heap: 2.5MB used, 512KB free
[GC] Mark: 15000 objects, 2.3ms
[GC] Sweep: 1200 freed, 0.8ms
[GC] Promotion: 300 to old gen
```

### Heap Snapshot

```hv
// In script or REPL
snapshot = gc.snapshot()
// { total_objects: 15000, by_type: { string: 5000, array: 3000, ... }, memory: 2.5MB }
```

---

## Performance Analysis

### JIT Debugging

```bash
# Show JIT compilation
./build-debug/havel --target jit --debug-jit script.hv
```

Output:
```
[JIT] Compiling fn:add (tier 1)
[JIT] LLVM IR:
define i64 @add(i64 %a, i64 %b) {
  %sum = add i64 %a, %b
  ret i64 %sum
}
[JIT] Native code emitted at 0x7f...
```

### Instruction Counting

```bash
# Limit instructions for testing
./build-debug/havel --max-instructions 10000 script.hv
```

---

## Test Suite

```bash
# Run all tests
./build.sh test

# Specific test types
./build-debug/hvtest --smoke       # Bytecode smoke tests
./build-debug/hvtest --scripts     # Script tests
./build-debug/hvtest --scheduler   # Scheduler rig tests (124 tests)

# Run single script test
./build-debug/havel scripts/tests/test_basic.hv
```

---

## Logging

```bash
# Log levels: debug, info, warning, error, fatal
./build-debug/havel --log-level debug script.hv

# Log to file
./build-debug/havel --log-file havel.log script.hv

# Filter by origin
./build-debug/havel --log-origin-filter "hotkey:*:debug" script.hv
```

### Log Format

```
2024-01-15 14:30:45.123 [INFO] havel::main: Starting script
2024-01-15 14:30:45.124 [DEBUG] havel::hotkey: Registered F1 (policy=drop)
2024-01-15 14:30:45.125 [ERROR] havel::vm: Division by zero at script.hv:10
```

---

## Common Debugging Scenarios

### Hotkey Not Firing

```bash
./build-debug/havel -dhk script.hv
# Check: grab status, condition evaluation, policy
```

### Script Hangs

```bash
# Check goroutine state
./build-debug/havel -de script.hv
# Look for: deadlock, channel wait, scheduler starvation
```

### Memory Leak

```bash
./build-debug/havel -dgc script.hv
# Watch: heap growth, objects not collected, finalizers not running
```

### JIT Miscompilation

```bash
# Compare with interpreter
./build-debug/havel script.hv > out_interp.txt
./build-debug/havel --target jit script.hv > out_jit.txt
diff out_interp.txt out_jit.txt
```

---

**Previous:** [Using FFI](/guides/ffi)
**Next:** [Migrating from Python/JS/Lua →](/guides/migration)