# REPL Guide

Interactive Read-Eval-Print Loop for the Havel language. The REPL uses the same bytecode VM as script execution — not an AST interpreter — so behavior is identical to running `.hv` files.

---

## Starting the REPL

```bash
./build-debug/havel          # starts REPL when no script is given
./build-debug/havel --repl   # explicit REPL mode
```

With debug flags:

```bash
./build-debug/havel -d -dl -dp -da --debug-bytecode
```

| Flag | Effect |
|------|--------|
| `-d` | Enable all debug output |
| `-dl` | Debug lexer |
| `-dp` | Debug parser |
| `-da` | Debug AST |
| `--debug-bytecode` | Trace bytecode execution |
| `--debug-jit` | Trace JIT compilation |

---

## REPL Commands

Commands start with `:` and are processed before compilation.

| Command | Alias | Description |
|---------|-------|-------------|
| `:help` | `:h` | Show help message |
| `:quit` | `:q` | Exit REPL |
| `:exit` | `:e` | Exit REPL |
| `:reset` | | Reset VM state (clear globals, reinitialize) |
| `:debug` | | Toggle debug mode |
| `:ast` | | Toggle AST display |
| `:bytecode` | `:bc` | Toggle bytecode disassembly display |
| `:classes` | | Show all defined classes, structs, protocols, and impls |
| `:load file.hv` | `:l file.hv` | Load and execute a script file into the current session |
| `:globals` | `:g` | List all global variables |
| `:history` | | Show input history |

---

## Persistence Across Lines

The REPL preserves state across each line you type. Definitions accumulate in the VM's global scope.

### Variables

```hv
havel> x = 42
42
havel> x * 2
84
```

### Functions

```hv
havel> fn greet(name) { print("hello {name}") }
havel> greet("world")
hello world
```

### Classes and Structs

```hv
havel> class Dog {
  @@count = 0
  fn init(name) { @name = name; @@count++ }
}
havel> d = Dog("rex")
havel> d.name
rex
```

Classes, structs, protocols, and impls defined in one line are visible to subsequent lines. The compiler is seeded with known type names before each compilation so it does not re-declare existing types.

### Cross-Chunk Function Calls

Functions defined in one REPL line are callable from later lines. Internally, `FunctionObjId` globals are wrapped into `RuntimeClosure` objects after each `executePersistent()` call, ensuring the closure captures the correct chunk context.

---

## Attaching to a Running VM

The REPL can attach to an already-running VM (e.g., after a script has executed):

```cpp
repl.attach(&vm, &bridge, knownGlobals);
repl.run();
```

This shares all existing globals, class definitions, and hotkey registrations with the REPL session. Use this when you want an interactive shell after running a script.

---

## Loading Scripts

Use `:load` (or `:l`) to execute a script file in the current REPL session:

```hv
havel> :load scripts/helpers.hv
```

Or from Havel code, use the `load()` host function:

```hv
havel> load("scripts/helpers.hv")
```

Both merge all definitions from the loaded file into the current global scope. This is different from `use` / `loadModule`, which sandboxes the loaded module and returns an exports object.

### `:load` vs `load()` vs `use`

| Method | Scope | Returns | Use Case |
|--------|-------|---------|----------|
| `:load file.hv` | Merges into current globals | nil | REPL convenience |
| `load("file.hv")` | Merges into caller's globals | `true` on success | Script-level file inclusion |
| `use module` | Sandboxed module scope | exports object | Library imports |

---

## Multi-Line Input

The REPL detects incomplete input (unclosed braces, parentheses, brackets) and prompts for continuation:

```hv
havel> class Point {
...   fn init(x, y) {
...     @x = x
...     @y = y
...   }
... }
```

The continuation prompt is `... ` by default (configurable via `REPLConfig::continuePrompt`).

---

## Signal Handling

| Signal | Key | Behavior |
|--------|-----|----------|
| SIGINT | Ctrl-C | Stops running goroutines/loops, clears current input |
| EOF | Ctrl-D | Exits REPL when input buffer is empty |
| SIGQUIT | Ctrl-\ | Triggers panic with crash report and core dump |

---

## REPL Configuration

```cpp
REPLConfig config;
config.debugMode = true;
config.showAST = true;
config.debugBytecode = true;
config.prompt = "havel> ";
config.continuePrompt = "... ";
config.historyFile = "";           // default: ~/.havel_history
config.outputLogFile = "";         // default: no logging
config.stopOnError = false;

REPL repl(config);
repl.initialize(hostAPI);
repl.run();
```

---

## Inspecting Definitions

### `:classes` Command

Shows all defined types in the current session:

```hv
havel> :classes
Classes: Dog, Cat
Structs: Point, Rect
Protocols: Hashable, Iterable
Impls: Hashable for Point, Drawable for Circle
```

If a category is empty, it shows "No X defined yet".

### `:globals` Command

Lists all global variable names currently defined in the VM.

---

## How Persistence Works Internally

1. **Globals merge**: After each `executePersistent()`, new globals are merged into `saved_globals` before restoring the previous global scope. This prevents clobbering.

2. **Known names seeding**: Before each compilation, the `ByteCompiler` is seeded with `known_class_names_`, `known_struct_names_`, `known_protocol_names_`, and `known_impl_names_` so it does not re-declare existing types.

3. **FunctionObjId wrapping**: After each execution, any `FunctionObjId` globals are wrapped into `RuntimeClosure` objects so they remain callable across chunk boundaries.

4. **Chunk retention**: All REPL chunks are stored in `repl_chunks_` so the VM can find their bytecode when calling functions from previous lines.

---

## Common Workflows

### Interactive Development

```hv
havel> fn fib(n) { if n <= 1 { n } else { fib(n-1) + fib(n-2) } }
havel> fib(10)
55
```

### Hotkey Testing

```hv
havel> F1 => { print("pressed") }
havel> :globals
// Verify hotkey is registered
```

### Loading a Library

```hv
havel> :load scripts/my_lib.hv
havel> my_lib_function(42)
```

### Debugging Bytecode

```hv
havel> :bytecode
havel> fn add(a, b) { a + b }
// Bytecode disassembly is printed
```
