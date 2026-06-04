# VM Internals

Architecture of the Havel virtual machine: value representation, dispatch loop, call frames, garbage collector, and module system.

---

## Value Representation

Havel uses tagged 64-bit values (NaN-boxing style). Each `Value` is a 64-bit word where the upper bits encode the type tag.

### Type Tags

| Tag | Type | Description |
|-----|------|-------------|
| `INT_TAG` | `int` | 64-bit signed integer (inline) |
| `FLOAT_TAG` | `num` | 64-bit IEEE 754 double (inline) |
| `BOOL_TAG` | `bool` | true/false (inline) |
| `NIL_TAG` | `nil` | Null/none singleton |
| `STRING_ID` | `str` | Heap-allocated string (via `StringRef`) |
| `STRING_VAL_ID` | `str` | Value-string variant |
| `ARRAY_ID` | `array` | Heap-allocated dynamic array |
| `OBJECT_ID` | `object` | Heap-allocated key-value map |
| `CLOSURE_ID` | `fn` | Closure (function + captured upvalues) |
| `FUNCTION_OBJ_ID` | `fn` | Bare function object (no captured upvalues) |
| `THREAD_ID` | `thread` | Goroutine reference |
| `CHANNEL_ID` | `channel` | Channel reference |
| `COROUTINE_ID` | `coroutine` | Coroutine/fiber reference |
| `SET_ID` | `set` | Heap-allocated unique-element set |
| `TUPLE_ID` | `tuple` | Heap-allocated fixed-size heterogeneous container |
| `HOST_FN_ID` | `fn` | Host (C++) function reference |

### String Handling

Strings are heap-allocated via `VM::getHeap().allocateString(str)`, returning a `StringRef { id }` used with `Value::makeStringId(id)`. Reverse lookup: `VM::resolveStringKey(value)` retrieves the string from any string-tagged `Value`.

Two string tags exist for internal optimization: `STRING_ID` (heap reference) and `STRING_VAL_ID` (value-string).

---

## Stack-Based VM

The VM is register-based within frames but uses an operand stack for intermediate values.

### Operand Stack

- `std::stack<Value> stack` — main operand stack for expression evaluation
- Push/pop operations for each bytecode instruction

### Call Frames

Each function invocation creates a `CallFrame`:

```
CallFrame:
  - function: reference to the function being executed
  - locals_base: base index into the locals array
  - return_ip: return address in the caller's bytecode
  - owns_globals: whether this frame owns its global scope (for module isolation)
  - chunk: pointer to the BytecodeChunk being executed
```

### Local Variables

- `locals` — flat array of local variable slots, indexed per frame
- `immutable_locals_` — tracks which locals are `val`/`const` (immutable)
- Each frame gets a contiguous range in the locals array starting at `locals_base`

---

## Dispatch Loop

The VM's main execution loop is a `switch` dispatch over opcodes:

```cpp
while (ip < chunk.instructions.size()) {
    auto& instr = chunk.instructions[ip];
    switch (instr.opcode) {
        case OpCode::LOAD_CONST: ...
        case OpCode::STORE_VAR: ...
        case OpCode::CALL: ...
        // ...
    }
    ip++;
}
```

### Key Opcodes

#### Load/Store

| Opcode | Operands | Description |
|--------|----------|-------------|
| `LOAD_CONST` | const_idx | Push constant from pool |
| `LOAD_VAR` | var_idx | Push local variable |
| `STORE_VAR` | var_idx | Pop to local variable |
| `LOAD_GLOBAL` | name_idx | Push global by name |
| `STORE_GLOBAL` | name_idx | Pop to global by name |
| `LOAD_UPVALUE` | upvalue_idx | Push captured upvalue |
| `STORE_UPVALUE` | upvalue_idx | Pop to captured upvalue |

#### Arithmetic

| Opcode | Operands | Description |
|--------|----------|-------------|
| `ADD` | — | Pop two, push sum |
| `SUB` | — | Pop two, push difference |
| `MUL` | — | Pop two, push product |
| `DIV` | — | Pop two, push quotient |
| `MOD` | — | Pop two, push modulo |
| `NEGATE` | — | Pop one, push negation |

#### Comparison

| Opcode | Operands | Description |
|--------|----------|-------------|
| `EQUAL` | — | Pop two, push bool |
| `NOT_EQUAL` | — | Pop two, push bool |
| `LESS` | — | Pop two, push bool |
| `GREATER` | — | Pop two, push bool |
| `LESS_EQUAL` | — | Pop two, push bool |
| `GREATER_EQUAL` | — | Pop two, push bool |

#### Control Flow

| Opcode | Operands | Description |
|--------|----------|-------------|
| `JUMP` | target_ip | Unconditional jump |
| `JUMP_IF_FALSE` | target_ip | Jump if top of stack is falsy |
| `JUMP_IF_TRUE` | target_ip | Jump if top of stack is truthy |
| `LOOP` | target_ip | Jump backwards (for loops) |
| `CALL` | arg_count | Call function on stack |
| `TAIL_CALL` | arg_count | Tail call (reuses frame) |
| `RETURN` | — | Return from function |

#### Objects and Arrays

| Opcode | Operands | Description |
|--------|----------|-------------|
| `CREATE_OBJECT` | — | Create empty object |
| `OBJECT_GET` | key_idx | Get field from object |
| `OBJECT_SET` | key_idx | Set field on object |
| `OBJECT_GET_RAW` | key_idx | Get without binding (for super calls) |
| `CREATE_ARRAY` | count | Create array from stack items |
| `ARRAY_GET` | — | Index into array |
| `ARRAY_SET` | — | Set array element |

#### Functions and Closures

| Opcode | Operands | Description |
|--------|----------|-------------|
| `DEFINE_FUNC` | func_idx | Create function object |
| `CLOSURE` | func_idx | Create closure with upvalues |
| `CALL_METHOD` | name_idx | Call method by name (prototype dispatch) |

#### Iterators

| Opcode | Operands | Description |
|--------|----------|-------------|
| `ITER_START` | — | Begin iteration |
| `ITER_NEXT` | target_ip | Advance iterator, jump if done |

#### Exception Handling

| Opcode | Operands | Description |
|--------|----------|-------------|
| `TRY_ENTER` | catch_ip, finally_ip | Push exception handler |
| `TRY_EXIT` | — | Pop exception handler |
| `THROW` | — | Throw top of stack |
| `LOAD_EXCEPTION` | — | Push caught exception |

#### Concurrency

| Opcode | Operands | Description |
|--------|----------|-------------|
| `SPAWN` | — | Spawn goroutine |
| `AWAIT` | — | Wait for fiber/channel |

#### Host Functions

| Opcode | Operands | Description |
|--------|----------|-------------|
| `HOST_CALL` | name_idx | Call host (C++) function by name |

---

## Function Calls

### Regular Call

```
1. Evaluate callee expression → push function Value
2. Evaluate arguments → push onto stack
3. CALL arg_count
4. VM creates new CallFrame
5. Callee's locals start at caller's stack top
6. Execute callee's bytecode
7. RETURN → restore caller's frame
```

### Method Call

```
1. Evaluate receiver → push object Value
2. CALL_METHOD "methodName"
3. VM looks up method in prototype chain
4. Creates bound method {fn, self}
5. Calls the method with self at slot 0
```

### Tail Call

When the last expression in a function body is a call, the compiler emits `TAIL_CALL` instead of `CALL`. The VM reuses the current frame instead of creating a new one, preventing stack growth for recursive functions.

### Host Function Call

```
1. LOAD_GLOBAL "hotkey.register" (or any host function)
2. PUSH arguments
3. CALL
4. VM detects HOST_FN_ID
5. Invokes C++ std::function<Value(const std::vector<Value>&)>
6. Pushes result
```

---

## Global Scope Management

### Globals Map

The VM maintains `globals` as a `robin_hood::unordered_flat_map<std::string, Value>`. Global access is by name string, not by index.

### Module Isolation

When a module is loaded via `use`, the VM:

1. Pushes the current `globals` onto `globals_stack_`
2. Creates a fresh `globals` map for the module
3. Executes the module's chunk
4. Collects the module's exports
5. Pops the saved globals from `globals_stack_`
6. Returns the exports object to the caller

### `executePersistent()`

Used by the REPL to execute one line while preserving state:

1. Push current `globals` as `saved_globals`
2. Execute the chunk
3. **Merge** new globals from the executed chunk into `saved_globals`
4. Restore `saved_globals` as the active globals
5. Wrap any new `FunctionObjId` globals into `RuntimeClosure` objects

This prevents clobbering: if a line defines `x = 5` and a later line defines `y = 10`, both remain accessible.

### `loadScript()`

Script-level file loading (the `load()` host function):

1. Resolve file path via `ModuleLoader::resolve()` or direct filesystem check
2. Parse and compile the file
3. Register protocol/impl info from AST with the VM
4. **Execute in the caller's global scope** (not sandboxed)
5. Save/restore full VM state (stack, locals, frames, current_chunk, exception state)
6. Wrap new `FunctionObjId` globals into closures
7. Merge new definitions into caller's globals
8. Store chunk in `module_chunks_` to keep it alive
9. Circular dependency detection via `modules_loading_` set

---

## Garbage Collector

**Source**: `src/havel-lang/compiler/gc/GC.hpp`, `GC.cpp`

### Architecture

- Generational GC with young and old generations
- Object allocation via `ObjectEntry` in a hash map indexed by `ObjectId`
- `ObjectEntry` supports move semantics (atomic `shape_version` field)
- Copy constructor/assignment deleted (atomic is non-copyable)

### Heap Allocation

| Method | Returns | Description |
|--------|---------|-------------|
| `allocateObject()` | `ObjectRef` | Allocate empty object |
| `allocateArray()` | `ArrayRef` | Allocate dynamic array |
| `allocateString(str)` | `StringRef` | Allocate/intern string |
| `allocateSet()` | `SetRef` | Allocate set |
| `allocateTuple()` | `TupleRef` | Allocate tuple |

### Thread Safety

All heap accessors use mutex locking:

- `GC::mutex_` is `mutable` to allow const methods to lock
- `subHeapBytes` uses CAS (compare-and-swap) for atomic updates
- `next_enum_id_` uses CAS for concurrent ID generation

### GC Roots

The VM registers roots before collection:

- Operand stack values
- Local variables in all active call frames
- Global variables
- Upvalues in all closures
- Pinned hotkey callback closures (pinned in `handleHotkeyRegister`, not in Scheduler — the Scheduler does not have VM access)

---

## Exception Handling

### Try/Catch/Finally

The `TRY_ENTER` opcode pushes an exception handler frame with two addresses:

- `catch_ip` — where to jump on exception
- `finally_ip` — where to jump on normal exit (after try block succeeds)

`TRY_EXIT` pops the handler on normal completion.

### Exception Propagation

When `THROW` is executed:

1. Walk up the call stack looking for a `TRY_ENTER` handler
2. If found, set the exception value and jump to `catch_ip`
3. If not found, unwind the call stack and report as unhandled error

### Script Error Structure

```cpp
struct ScriptError {
    Value value;
    std::string message;
    std::string stackTrace;
    uint32_t line;
    uint32_t column;
};
```

---

## LLVM JIT (Optional)

**Source**: `src/havel-lang/compiler/llvm/`, `src/havel-lang/compiler/BytecodeOrcJIT.cpp`

When LLVM is available (`ENABLE_LLVM` cmake option), hot bytecode paths can be compiled to native code via ORC JIT.

### Requirements

- LLVM development libraries
- `ENABLE_HAVEL_LANG` cmake option (auto-enabled when LLVM is found)

### Build Modes with JIT

| Mode | LLVM | Build Dir |
|------|------|-----------|
| 0 | Yes | build-debug |
| 5 | Yes | build-release |
| 6 | No | build-debug |
| 9 | No | build-release |

---

## Key Files

| File | Role |
|------|------|
| `src/havel-lang/compiler/vm/VM.hpp` | VM class declaration (~1,050 lines) |
| `src/havel-lang/compiler/vm/VM.cpp` | VM implementation (~3,090 lines) |
| `src/havel-lang/compiler/vm/VMHostFunctions.cpp` | Host function registration |
| `src/havel-lang/compiler/gc/GC.hpp` | Garbage collector |
| `src/havel-lang/compiler/core/BytecodeIR.hpp` | Bytecode IR structures |
| `src/havel-lang/compiler/core/BytecodeDisassembler.hpp` | Bytecode disassembly |
