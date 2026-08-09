---
title: "Bytecode Format"
description: "Havel bytecode instruction set, chunk structure, and IR representation."
---

# Bytecode Format

## Overview

Havel compiles to a stack-based bytecode format. Each compilation unit produces a `BytecodeChunk` containing:

```
BytecodeChunk:
  - functions: BytecodeFunction[]
  - strings: StringPool (interned strings)
```

**Source**: `src/havel-lang/compiler/core/BytecodeIR.hpp`, `ByteCompiler.cpp`

---

## Instruction Format

Each instruction is a struct:

```cpp
struct Instruction {
    OpCode opcode;
    std::vector<Value> operands;
    std::optional<SourceLocation> location;
};
```

Operands are `Value` objects (constants, indices, etc.) stored inline.

---

## Opcode Reference

### Load/Store

| Opcode | Operands | Description |
|--------|----------|-------------|
| `LOAD_CONST` | const_idx | Push constant from function's constant pool |
| `LOAD_GLOBAL` | name_idx | Push global variable by name index |
| `STORE_GLOBAL` | name_idx | Pop to global variable by name index |
| `STORE_IMMUT_GLOBAL` | name_idx | Pop to global and mark immutable (`val`) |
| `LOAD_VAR` | var_idx | Push local variable by index |
| `STORE_VAR` | var_idx | Pop to local variable by index |
| `STORE_IMMUT_VAR` | var_idx | Pop to local and mark immutable (`val`) |
| `LOAD_UPVALUE` | upvalue_idx | Push captured upvalue |
| `STORE_UPVALUE` | upvalue_idx | Pop to captured upvalue |
| `POP` | — | Pop and discard top of stack |
| `DUP` | — | Duplicate top of stack |
| `SWAP` | — | Swap top two stack values |
| `PUSH_NULL` | — | Push `null` |

### Arithmetic

| Opcode | Operands | Description |
|--------|----------|-------------|
| `ADD` | — | Pop two, push sum |
| `SUB` | — | Pop two, push difference (a - b) |
| `MUL` | — | Pop two, push product |
| `DIV` | — | Pop two, push quotient (float) |
| `INT_DIV` | — | Pop two, push integer quotient (a // b) |
| `DIVMOD` | — | Pop two, push tuple (quotient, remainder) |
| `REMAINDER` | — | Pop two, push remainder (a % b) |
| `MOD` | — | Alias for REMAINDER |
| `POW` | — | Pop two, push a ** b |
| `NEGATE` | — | Pop one, push negation |

### Compound Assignment (Local Variable Optimization)

| Opcode | Operands | Description |
|--------|----------|-------------|
| `ADD_ASSIGN` | var_idx | `local += value` |
| `SUB_ASSIGN` | var_idx | `local -= value` |
| `MUL_ASSIGN` | var_idx | `local *= value` |
| `DIV_ASSIGN` | var_idx | `local /= value` |
| `INT_DIV_ASSIGN` | var_idx | `local //= value` |
| `REMAINDER_ASSIGN` | var_idx | `local %= value` |
| `MOD_ASSIGN` | var_idx | Alias for REMAINDER_ASSIGN |
| `POW_ASSIGN` | var_idx | `local **= value` |
| `BITWISE_AND_ASSIGN` | var_idx | `local &= value` |
| `BITWISE_OR_ASSIGN` | var_idx | `local \|= value` |
| `BITWISE_XOR_ASSIGN` | var_idx | `local ^= value` |
| `SHIFT_LEFT_ASSIGN` | var_idx | `local <<= value` |
| `SHIFT_RIGHT_ASSIGN` | var_idx | `local >>= value` |

### Increment/Decrement (Local Variable Optimization)

| Opcode | Operands | Description |
|--------|----------|-------------|
| `INCLOCAL` | var_idx | `++local` (prefix) |
| `DECLOCAL` | var_idx | `--local` (prefix) |
| `INCLOCAL_POST` | var_idx | `local++` (postfix) |
| `DECLOCAL_POST` | var_idx | `local--` (postfix) |

### Comparison

| Opcode | Operands | Description |
|--------|----------|-------------|
| `EQ` | — | Pop two, push `a == b` |
| `NEQ` | — | Pop two, push `a != b` |
| `IS` | — | Pop two, push identity (same reference) |
| `LT` | — | Pop two, push `a < b` |
| `LTE` | — | Pop two, push `a <= b` |
| `GT` | — | Pop two, push `a > b` |
| `GTE` | — | Pop two, push `a >= b` |

### Logical Operations

| Opcode | Operands | Description |
|--------|----------|-------------|
| `AND` | — | Pop two, push `a && b` (short-circuit) |
| `OR` | — | Pop two, push `a \|\| b` (short-circuit) |
| `NOT` | — | Pop one, push `!a` |
| `IS_NULL` | — | Pop one, push `a == null` |

### Bitwise Operations

| Opcode | Operands | Description |
|--------|----------|-------------|
| `BIT_AND` | — | Pop two, push `a & b` |
| `BIT_OR` | — | Pop two, push `a \| b` |
| `BIT_XOR` | — | Pop two, push `a ^ b` |
| `BIT_LSH` | — | Pop two, push `a << b` |
| `BIT_RSH` | — | Pop two, push `a >> b` |
| `BIT_NOT` | — | Pop one, push `~a` |
| `LENGTH` | — | Pop one, push `#a` (dispatch to `op_length` or `len`) |

### Control Flow

| Opcode | Operands | Description |
|--------|----------|-------------|
| `JUMP` | target_ip | Unconditional jump |
| `JUMP_IF_FALSE` | target_ip | Jump if top of stack falsy |
| `JUMP_IF_TRUE` | target_ip | Jump if top of stack truthy |
| `JUMP_IF_NULL` | target_ip | Jump if top of stack is null/undefined |
| `CALL` | arg_count | Call function on stack |
| `CALL_DYN` | — | Dynamic arg count from stack (for spread) |
| `CALL_SPREAD` | — | Spread call: combine literal + spread array + call |
| `TAIL_CALL` | arg_count | Tail call (reuses frame) |
| `CALL_METHOD` | name_idx | Dispatch method call by type |
| `CALL_METHOD_SPREAD` | — | Method call with spread |
| `RETURN` | — | Return from function |
| `TRY_ENTER` | catch_ip, finally_ip | Push exception handler |
| `TRY_EXIT` | — | Pop exception handler |
| `LOAD_EXCEPTION` | — | Push current exception object |
| `THROW` | — | Throw top of stack as exception |

### Function Operations

| Opcode | Operands | Description |
|--------|----------|-------------|
| `DEFINE_FUNC` | func_idx | Create function object |
| `CLOSURE` | func_idx | Create closure with upvalues |

### Array Operations

| Opcode | Operands | Description |
|--------|----------|-------------|
| `ARRAY_NEW` | count | Create array from `count` stack items |
| `ARRAY_GET` | — | Pop index + array, push element |
| `ARRAY_SET` | — | Pop value + index + array, set element |
| `ARRAY_DEL` | — | Delete array element at index |
| `ARRAY_PUSH` | — | Push element to array |
| `ARRAY_LEN` | — | Push array length |
| `ARRAY_FREEZE` | — | Mark top-of-stack array as frozen (tuples) |
| `ARRAY_POP` | — | Pop and return last element |
| `ARRAY_HAS` | — | Check if array contains value |
| `ARRAY_FIND` | — | Find index of value |
| `ARRAY_MAP` | — | Map function over array |
| `ARRAY_FILTER` | — | Filter array by predicate |
| `ARRAY_REDUCE` | — | Reduce array to single value |
| `ARRAY_FOREACH` | — | Execute function for each element |

### Set Operations

| Opcode | Operands | Description |
|--------|----------|-------------|
| `SET_NEW` | — | Create set |
| `SET_SET` | — | Set[key] = value (stack: value, key, set) |
| `SET_DEL` | — | Delete key from set (stack: key, set) |

### Range Operations

| Opcode | Operands | Description |
|--------|----------|-------------|
| `RANGE_NEW` | — | Create range: start..end or start..step..end |
| `RANGE_STEP_NEW` | — | Create range with step |

### Enum Operations

| Opcode | Operands | Description |
|--------|----------|-------------|
| `ENUM_NEW` | tag, payload_count | Create enum variant |
| `ENUM_TAG` | — | Get enum tag |
| `ENUM_PAYLOAD` | — | Get payload by index |
| `ENUM_MATCH` | — | Pattern match on enum |

### Object Operations (VM Intrinsics)

| Opcode | Operands | Description |
|--------|----------|-------------|
| `OBJECT_KEYS` | — | Push array of keys |
| `OBJECT_VALUES` | — | Push array of values |
| `OBJECT_ENTRIES` | — | Push array of [key, value] pairs |
| `OBJECT_HAS` | — | Check if key exists |
| `OBJECT_DELETE` | — | Delete key from object |
| `OBJECT_GET_RAW` | — | Get property without method binding (super calls) |

### Object Operations (Bytecode)

| Opcode | Operands | Description |
|--------|----------|-------------|
| `OBJECT_NEW` | field_count | Create sorted object |
| `OBJECT_NEW_UNSORTED` | field_count | Create unsorted object (`!{}` syntax) |
| `OBJECT_GET` | name_idx | Get field with method binding |
| `OBJECT_SET` | name_idx | Set field |

### String Operations (VM Intrinsics)

| Opcode | Operands | Description |
|--------|----------|-------------|
| `STRING_LEN` | — | Push string length |
| `STRING_UPPER` | — | Convert to uppercase |
| `STRING_LOWER` | — | Convert to lowercase |
| `STRING_TRIM` | — | Trim whitespace |
| `STRING_SUB` | — | Substring |
| `STRING_FIND` | — | Find substring |
| `STRING_HAS` | — | Check if contains substring |
| `STRING_STARTS` | — | Check if starts with |
| `STRING_ENDS` | — | Check if ends with |
| `STRING_SPLIT` | — | Split by delimiter |
| `STRING_REPLACE` | — | Replace substring |
| `STRING_PROMOTE` | — | Convert StringValId → StringId |
| `STRING_REVERSE` | — | Reverse string |
| `STRING_REPEAT` | — | Repeat string |
| `STRING_TRIM_START` | — | Trim leading whitespace |
| `STRING_TRIM_END` | — | Trim trailing whitespace |
| `STRING_INCLUDES` | — | Check if contains substring |
| `STRING_PAD_START` | — | Left pad |
| `STRING_PAD_END` | — | Right pad |

### Spread Operator

| Opcode | Operands | Description |
|--------|----------|-------------|
| `SPREAD` | — | Spread array/object into another |
| `SPREAD_CALL` | — | Spread call |

### Type Conversion

| Opcode | Operands | Description |
|--------|----------|-------------|
| `AS_TYPE` | — | Type assertion |
| `TO_INT` | — | Convert to int |
| `TO_FLOAT` | — | Convert to float |
| `TO_STRING` | — | Convert to string |
| `TO_BOOL` | — | Convert to bool |
| `TYPE_OF` | — | Get type name string |

### Special Operations

| Opcode | Operands | Description |
|--------|----------|-------------|
| `PRINT` | — | Print top of stack |
| `DEBUG` | — | Debug breakpoint |
| `IMPORT` | path_idx | Runtime module import |
| `IMPORT_WILDCARD` | — | Import all exports as globals |

### Class Operations

| Opcode | Operands | Description |
|--------|----------|-------------|
| `CLASS_NEW` | typeId, parentId, fieldCount | Create class with parent |
| `CLASS_GET_FIELD` | name_idx | Get field with prototype chain lookup |
| `CLASS_SET_FIELD` | name_idx | Set field with prototype chain lookup |
| `LOAD_CLASS_PROTO` | — | Load parent class reference |
| `CALL_SUPER` | — | Call method from parent class |

### Struct Intrinsics

| Opcode | Operands | Description |
|--------|----------|-------------|
| `STRUCT_NEW` | typeNameId, arg_count | Create struct instance |
| `STRUCT_GET` | name_idx | Get struct field by name |
| `STRUCT_SET` | name_idx | Set struct field by name |

### Protocol Intrinsics

| Opcode | Operands | Description |
|--------|----------|-------------|
| `PROT_CHECK` | name_id | Runtime protocol check (value → bool) |
| `PROT_CAST` | name_id | Runtime checked cast (value → value/null) |

### Concurrency Primitives

| Opcode | Operands | Description |
|--------|----------|-------------|
| `THREAD_SPAWN` | — | Spawn OS thread: `thread { ... }` |
| `THREAD_JOIN` | — | Join thread |
| `THREAD_SEND` | — | Send message to thread |
| `THREAD_RECEIVE` | — | Receive message from thread |
| `INTERVAL_START` | — | Start interval timer |
| `INTERVAL_STOP` | — | Stop interval timer |
| `TIMEOUT_START` | — | Start one-shot timeout |
| `TIMEOUT_CANCEL` | — | Cancel pending timeout |

### Coroutines

| Opcode | Operands | Description |
|--------|----------|-------------|
| `YIELD` | — | Yield from coroutine (optional value/delay) |
| `YIELD_RESUME` | — | Resume yielded coroutine |
| `GO_ASYNC` | — | Spawn async function call: `go func()` |
| `FIBER_AWAIT` | — | `<- expr`: await/blocking expression |
| `FIBER_SLEEP` | — | `<- sleep(ms)`: non-blocking sleep |

### Channels

| Opcode | Operands | Description |
|--------|----------|-------------|
| `CHANNEL_NEW` | — | Create channel: `channel()` |
| `CHANNEL_SEND` | — | Send value to channel |
| `CHANNEL_RECEIVE` | — | Receive from channel (blocking) |
| `CHANNEL_CLOSE` | — | Close channel |

### Defer

| Opcode | Operands | Description |
|--------|----------|-------------|
| `DEFER_PUSH` | — | Push closure onto defer stack |

### WaitGroup

| Opcode | Operands | Description |
|--------|----------|-------------|
| `WAITGROUP_NEW` | — | Create waitgroup |
| `WAITGROUP_ADD` | — | Increment counter |
| `WAITGROUP_DONE` | — | Decrement counter |
| `WAITGROUP_WAIT` | — | Block until counter == 0 |

### Module Context

| Opcode | Operands | Description |
|--------|----------|-------------|
| `BEGIN_MODULE` | — | Begin module scope, collect exports |
| `END_MODULE` | — | End module scope, return exports |

### Math Intrinsics (libm wrappers)

| Opcode | Operands | Description |
|--------|----------|-------------|
| `MATH_SIN` | — | `sin(x)` |
| `MATH_COS` | — | `cos(x)` |
| `MATH_TAN` | — | `tan(x)` |
| `MATH_ASIN` | — | `asin(x)` |
| `MATH_ACOS` | — | `acos(x)` |
| `MATH_ATAN` | — | `atan(x)` |
| `MATH_ATAN2` | — | `atan2(y, x)` |
| `MATH_SINH` | — | `sinh(x)` |
| `MATH_COSH` | — | `cosh(x)` |
| `MATH_TANH` | — | `tanh(x)` |
| `MATH_SQRT` | — | `sqrt(x)` |
| `MATH_LOG` | — | `log(x)` (natural) |
| `MATH_LOG2` | — | `log2(x)` |
| `MATH_LOG10` | — | `log10(x)` |
| `MATH_EXP` | — | `exp(x)` |
| `MATH_CEIL` | — | `ceil(x)` |
| `MATH_FLOOR` | — | `floor(x)` |
| `MATH_ROUND` | — | `round(x)` |
| `MATH_ABS` | — | `abs(x)` |

### Object Intrinsics

| Opcode | Operands | Description |
|--------|----------|-------------|
| `OBJECT_FREEZE` | — | Freeze object |
| `OBJECT_SEAL` | — | Seal object |
| `OBJECT_IS_FROZEN` | — | Check if frozen |
| `OBJECT_IS_SEALED` | — | Check if sealed |
| `OBJECT_SIZE` | — | Get object size (key count) |
| `OBJECT_ASSIGN` | — | Merge objects |

### Bit Intrinsics

| Opcode | Operands | Description |
|--------|----------|-------------|
| `BIT_POPCOUNT` | — | Population count |
| `BIT_CTZ` | — | Count trailing zeros |
| `BIT_CLZ` | — | Count leading zeros |
| `BIT_BSWAP` | — | Byte swap |
| `BIT_ROTL` | — | Rotate left |
| `BIT_ROTR` | — | Rotate right |

### Time Primitive

| Opcode | Operands | Description |
|--------|----------|-------------|
| `TIME_NOW` | — | Current timestamp (ms) |

### Format Intrinsics

| Opcode | Operands | Description |
|--------|----------|-------------|
| `FORMAT_HEX` | — | Encode to hex |
| `FORMAT_UNHEX` | — | Decode from hex |
| `FORMAT_BASE64_ENCODE` | — | Base64 encode |
| `FORMAT_BASE64_DECODE` | — | Base64 decode |

### Iteration Protocol

| Opcode | Operands | Description |
|--------|----------|-------------|
| `ITER_NEW` | — | Create iterator from iterable |
| `ITER_NEXT` | — | Get next `{value, done}` from iterator |

### No-Op

| Opcode | Operands | Description |
|--------|----------|-------------|
| `NOP` | — | No operation |

---

## Constant Pool

Each `BytecodeFunction` has its own constant pool (`std::vector<Value> constants`). Constants are referenced by index in operands.

```
Constants:
  [0] = 42 (int)
  [1] = 3.14 (num)
  [2] = "hello" (string)
  [3] = true (bool)
  [4] = nil
  ...
```

---

## Function Entries

Each function (including nested) gets a `BytecodeFunction` entry:

```
BytecodeFunction:
  - name: string
  - instructions: Instruction[]
  - instruction_locations: SourceLocation[]
  - constants: Value[]
  - upvalues: UpvalueDescriptor[]
  - param_count: uint32_t
  - local_count: uint32_t
  - default_values: optional<Value>[]
  - variadic_param_index: uint32_t
  - param_names: string[]
  - source_file: string
  - source_line: uint32_t
  - is_generator: bool
  - is_timer_closure: bool
  - type_feedback: TypeFeedback[]
  - execution_count: uint32_t
  - jit_compiled: bool
```

---

## Example: Compilation Output

```hv
fn add(a, b) { a + b }
val x = add(1, 2)
```

Produces (simplified):

```
Function: add
0: LOAD_VAR    0      ; a
1: LOAD_VAR    1      ; b
2: ADD
3: RETURN

Function: <main>
0: LOAD_CONST  0      ; 1
1: LOAD_CONST  1      ; 2
2: LOAD_GLOBAL 0      ; "add"
3: CALL        2
4: STORE_IMMUT_VAR 0  ; x
5: RETURN
```

---

## Disassembly

```bash
havel --debug-bytecode script.hv
```

Output:

```
Bytecode for script.hv:
Function: <main>
0000 LOAD_CONST     0      ; 1
0001 LOAD_CONST     1      ; 2
0002 LOAD_GLOBAL    0      ; "add"
0003 CALL           2
0004 STORE_IMMUT_VAR 0      ; x
0005 RETURN

Constants:
  [0] int: 1
  [1] int: 2

Globals:
  [0] "add"
  [1] "x"
```

---

**Previous:** [Hotkey Module](/stdlib/hotkey)
**Next:** [JIT/AOT Compilation →](/compiler/jit-aot)