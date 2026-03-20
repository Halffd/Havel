# Havel Architecture Roadmap

This document locks the implementation order for Havel and keeps boundaries explicit.

Guiding rule:

`RUNTIME -> HOST -> LANGUAGE -> SYNTAX`

If syntax drives architecture, the project will stall.

## Core Architecture (Non-Negotiable)

Havel is split into 3 layers:

1. Core VM (pure, small, deterministic)
2. Host Bridge / FFI (unsafe, powerful, OS-facing)
3. Havel Runtime (standard library + domain systems)

### 1) Core VM
Only these belong in the VM:

- values: `number`, `string`, `bool`, `null`
- containers: arrays/maps
- functions + closures
- control flow

Keep VM behavior simple and predictable.

### 2) Host Bridge (FFI)
Everything OS/app integration goes here:

- `io.*`
- `window.*`
- `mpv.*`
- `clipboard.*`

Conceptually:

```cpp
Value callHostFunction(std::string name, std::vector<Value> args);
registerHostFunction("window.moveToNextMonitor", fn_ptr);
```

### 3) Havel Runtime
Most "features" live here, not in the VM:

- hotkeys
- modes
- signals
- window queries
- process module
- config system

These are runtime systems built on VM + FFI.

## DSL Rule

Hotkey/mode syntax is sugar that compiles to runtime calls.

Examples:

```havel
^!+c => doSomething()
```

Compiles to:

```havel
hotkey.register("Ctrl+Alt+Shift+C", fn() { doSomething() })
```

```havel
mode gaming { ... }
```

Compiles to:

```havel
mode.define("gaming", fn() { ... })
```

## Operator Overloading Rule

Do not hardcode object magic in the VM.

Use runtime dispatch:

```text
if hasOperator(obj, "add") then call(obj.op_add, rhs)
```

VM stays dumb. Objects stay extensible.

## Development Phases

### Phase 0 — Lock Core Language (Done)

Minimum language:

- values: `number`, `string`, `bool`, `null`
- variables: `let a = 1`
- expressions: `+ - * /`, comparisons
- control: `if`, `while`, `return`
- functions: `fn add(a,b) { return a+b }`
- objects/maps: `{ key = value }`

### Phase 1 — AST Interpreter (Done)

Pipeline:

`source -> tokenizer -> parser -> AST -> interpreter`

Goal:

- validate grammar
- test semantics
- debug language design

### Phase 2 — Stable AST + Semantic Rules (Done)

Lock semantics for:

- scoping
- variable resolution
- closures
- call semantics

Add resolver pass:

`AST -> scope resolution -> annotated AST`

Example:

```havel
let a = 1
fn f() { return a }
```

Compiler must resolve `a` as outer-scope capture.

### Phase 3 — Design Bytecode

Start stack-based bytecode opcodes:

- `PUSH_CONST`
- `LOAD_LOCAL`
- `STORE_LOCAL`
- `ADD`, `SUB`, `MUL`, `DIV`
- `CALL`, `RETURN`
- `JUMP`, `JUMP_IF_FALSE`
- `MAKE_OBJECT`, `GET_FIELD`, `SET_FIELD`

Example:

```havel
let a = 1 + 2
```

```text
CONST 1
CONST 2
ADD
STORE a
```

### Phase 4 — Build VM

Use a classic stack VM:

```cpp
struct VM {
    stack[];
    instruction_pointer;
    call_stack[];
};
```

Execution loop reads opcode and executes stack actions.

### Phase 5 — Bytecode Compiler

Pipeline:

`source -> lexer -> parser -> AST -> resolver -> bytecode compiler -> VM`

Example:

```havel
fn add(a,b){ return a+b }
```

```text
LOAD_LOCAL 0
LOAD_LOCAL 1
ADD
RETURN
```

### Phase 6 — Garbage Collection

Implement mark-sweep when heap objects are real.

Roots:

- VM stack
- globals
- closures/upvalues

### Phase 7 — Standard Library (Done)

Core modules (native C++ builtins):

- strings
- arrays
- maps
- filesystem
- math
- time
- process

### Phase 8 — Write Compiler in Havel

Create `compiler.hv` that:

- parses source
- builds AST
- emits bytecode

Runs on existing C++ VM initially.

### Phase 9 — Self-Compile

`compiler.hv -> compiler.bc`

Then `compiler.bc` compiles user programs.

### Phase 10 — Remove Bootstrap Interpreter

Final structure:

- C++ runtime
  - VM
  - bytecode loader
- `compiler.bc`
- `user_program.bc`

C++ becomes runtime host only.

## Complexity Bombs (Explicit Decisions)

1. Async/channels
- Option A (recommended): event loop + callbacks
- Option B (expensive): coroutines + scheduler + suspension

2. Signals/hotkeys/modes
- must use a central dispatcher/scheduler
- avoid ad-hoc direct wiring

3. Window/process/mpv APIs
- side-effect heavy, often async
- host calls must be non-blocking or carefully bounded blocking

## Scope Control for Existing Stub Features

Classify all pending features as:

- `[CORE]` required for VM/runtime foundation
- `[RUNTIME]` defer safely
- `[DROP]` remove now

Unclassified stubs are schedule risk.

## Realistic Execution Order

### Phase 1 (current execution block)

- VM
- FFI
- basic functions

### Phase 2

- hotkeys (compiled to runtime calls)
- window/process modules

### Phase 3

- modes + signals

### Phase 4

- async/channels

## Commit Discipline

Commit at the end of each stage with a focused message and passing tests.

Recommended pattern:

- `phase-X: <short deliverable>`
- include migration notes when bytecode/runtime behavior changes
