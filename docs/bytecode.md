# Havel Bytecode Specification

## Overview

This document specifies the Havel bytecode format used by the hybrid execution engine (Compiler → Bytecode → Interpreter/JIT).

## Bytecode Design Goals

- **Compact**: Minimal memory footprint
- **Simple**: Easy to interpret and compile
- **Extensible**: Support for future language features
- **Efficient**: Optimized for common operations

## Instruction Format

### Basic Instruction Structure

```cpp
struct Instruction {
    OpCode opcode;           // 1 byte - Operation code
    uint8_t operand_count;   // 1 byte - Number of operands
    uint16_t operand_data;   // 2 bytes - Packed operand data
    uint32_t next_addr;     // 4 bytes - Jump target (optional)
};
```

**Total size**: 8 bytes per instruction

### Compact Encoding

For instructions with small operands, data is packed into `operand_data`:

- **Immediate values**: Stored directly in operand_data
- **Constant indices**: High 12 bits for index, low 4 bits for type
- **Variable indices**: High 12 bits for slot, low 4 bits for scope

## OpCode Definitions

### Stack Operations (0x00-0x0F)

| OpCode | Hex | Name | Description | Operands |
|---|---|---|---|---|
| LOAD_CONST | 0x01 | Load Constant | const_index |
| LOAD_VAR | 0x02 | Load Variable | var_index |
| STORE_VAR | 0x03 | Store Variable | var_index |
| POP | 0x04 | Pop Stack | none |
| DUP | 0x05 | Duplicate Top | none |
| SWAP | 0x06 | Swap Top Two | none |
| ROT | 0x07 | Rotate Top Three | count |

**Example:**
```havel
let x = 42;        // LOAD_CONST 0
print(x);           // LOAD_VAR 0, POP (print)
```

### Arithmetic Operations (0x10-0x1F)

| OpCode | Hex | Name | Description | Operands |
|---|---|---|---|---|
| ADD | 0x10 | Addition | none |
| SUB | 0x11 | Subtraction | none |
| MUL | 0x12 | Multiplication | none |
| DIV | 0x13 | Division | none |
| MOD | 0x14 | Modulo | none |
| POW | 0x15 | Power | none |
| NEG | 0x16 | Negation | none |
| INC | 0x17 | Increment | none |
| DEC | 0x18 | Decrement | none |

**Stack Effect:** Pops two values, pushes one result

**Example:**
```havel
let result = a + b;  // LOAD_VAR a, LOAD_VAR b, ADD, STORE_VAR result
```

### Comparison Operations (0x20-0x2F)

| OpCode | Hex | Name | Description | Operands |
|---|---|---|---|---|
| EQ | 0x20 | Equal | none |
| NEQ | 0x21 | Not Equal | none |
| LT | 0x22 | Less Than | none |
| LTE | 0x23 | Less Than Equal | none |
| GT | 0x24 | Greater Than | none |
| GTE | 0x25 | Greater Than Equal | none |
| CMP | 0x26 | Compare (-1, 0, 1) | none |

**Stack Effect:** Pops two values, pushes boolean result

### Logical Operations (0x30-0x3F)

| OpCode | Hex | Name | Description | Operands |
|---|---|---|---|---|
| AND | 0x30 | Logical AND | none |
| OR | 0x31 | Logical OR | none |
| XOR | 0x32 | Logical XOR | none |
| NOT | 0x33 | Logical NOT | none |

### Control Flow Operations (0x40-0x4F)

| OpCode | Hex | Name | Description | Operands |
|---|---|---|---|---|
| JUMP | 0x40 | Unconditional Jump | target_addr |
| JUMP_IF_FALSE | 0x41 | Jump if False | target_addr |
| JUMP_IF_TRUE | 0x42 | Jump if True | target_addr |
| JUMP_IF_EQ | 0x43 | Jump if Equal | target_addr |
| JUMP_IF_NEQ | 0x44 | Jump if Not Equal | target_addr |
| CALL | 0x45 | Function Call | func_addr, arg_count |
| RETURN | 0x46 | Return from Function | none |
| YIELD | 0x47 | Cooperative Yield | none |

### Function Operations (0x50-0x5F)

| OpCode | Hex | Name | Description | Operands |
|---|---|---|---|---|
| DEFINE_FUNC | 0x50 | Define Function | func_index, param_count |
| CLOSURE | 0x51 | Create Closure | func_index, capture_count |
| GET_CLOSURE | 0x52 | Get Closure Value | capture_index |
| SET_CLOSURE | 0x53 | Set Closure Value | capture_index |

### Array Operations (0x60-0x6F)

| OpCode | Hex | Name | Description | Operands |
|---|---|---|---|---|
| ARRAY_NEW | 0x60 | New Array | size |
| ARRAY_GET | 0x61 | Get Array Element | none |
| ARRAY_SET | 0x62 | Set Array Element | none |
| ARRAY_PUSH | 0x63 | Push to Array | none |
| ARRAY_POP | 0x64 | Pop from Array | none |
| ARRAY_LENGTH | 0x65 | Get Array Length | none |

### Object Operations (0x70-0x7F)

| OpCode | Hex | Name | Description | Operands |
|---|---|---|---|---|
| OBJECT_NEW | 0x70 | New Object | property_count |
| OBJECT_GET | 0x71 | Get Property | none |
| OBJECT_SET | 0x72 | Set Property | none |
| OBJECT_HAS | 0x73 | Has Property | none |
| OBJECT_DELETE | 0x74 | Delete Property | none |
| OBJECT_KEYS | 0x75 | Get Keys | none |

### Special Operations (0x80-0x8F)

| OpCode | Hex | Name | Description | Operands |
|---|---|---|---|---|
| PRINT | 0x80 | Print Value | none |
| DEBUG | 0x81 | Debug Info | none |
| NOP | 0x82 | No Operation | none |
| HALT | 0x83 | Halt Execution | none |

## Constant Pool

### Constant Types

```cpp
enum class ConstantType : uint8_t {
    NULL_TYPE = 0x00,
    BOOLEAN = 0x01,
    INTEGER = 0x02,
    FLOAT = 0x03,
    STRING = 0x04,
    FUNCTION = 0x05,
    ARRAY = 0x06,
    OBJECT = 0x07
};
```

### Constant Format

```cpp
struct Constant {
    ConstantType type;
    uint32_t size;
    union {
        bool bool_value;
        int64_t int_value;
        double float_value;
        struct {
            uint32_t length;
            char* data;
        } string_value;
        uint32_t function_index;
        uint32_t array_index;
        uint32_t object_index;
    };
};
```

## Function Format

### Function Header

```cpp
struct Function {
    uint32_t name_index;        // Index in constant pool
    uint16_t param_count;        // Number of parameters
    uint16_t local_count;        // Number of local variables
    uint32_t code_start;        // Start instruction index
    uint32_t code_end;          // End instruction index
    uint32_t max_stack_depth;   // Maximum stack depth needed
};
```

### Parameter and Local Variables

- **Parameters**: Stored in locals[0..param_count-1]
- **Locals**: Stored in locals[param_count..param_count+local_count-1]
- **Access**: Direct indexing into local array

## Execution Model

### Stack Machine

```
Stack Growth: ↑
[Top]    value3
          value2
          value1
[Bottom] value0
```

### Calling Convention

1. **Function Call**: Push arguments left-to-right
2. **Stack Frame**: 
   - Return address
   - Old frame pointer
   - Local variables
3. **Return**: Pop frame, jump to return address

### Example Execution

**Havel Code:**
```havel
let add = (a, b) => a + b;
let result = add(10, 20);
```

**Bytecode:**
```
; Function definition
DEFINE_FUNC 0, 2           ; Define function 0 with 2 params
LOAD_VAR 0                  ; Load parameter a
LOAD_VAR 1                  ; Load parameter b
ADD                          ; a + b
RETURN                       ; Return result

; Main code
LOAD_CONST 0                 ; Load function reference
LOAD_CONST 10                ; Load argument 1
LOAD_CONST 20                ; Load argument 2
CALL 0, 2                   ; Call function with 2 args
STORE_VAR result              ; Store result
```

## Optimization Hints

### Inline Cache Markers

Special opcodes for optimized paths:

| OpCode | Hex | Name | Description |
|---|---|---|---|
| ADD_INT | 0x91 | Add Integers (optimized) |
| ADD_STR | 0x92 | Concatenate Strings (optimized) |
| MUL_INT | 0x93 | Multiply Integers (optimized) |

### Type Specialization

Instructions can include type hints:

```cpp
// Type-hinted addition
ADD_INT          ; Both operands are integers
ADD_FLOAT         ; Both operands are floats
ADD_STR           ; Both operands are strings
```

## Debug Information

### Source Mapping

```cpp
struct DebugInfo {
    uint32_t instruction_index;
    uint32_t line_number;
    uint32_t column_number;
    uint32_t source_file_index;
};
```

### Variable Tracking

```cpp
struct VariableInfo {
    uint32_t name_index;
    uint32_t scope_start;
    uint32_t scope_end;
    bool is_parameter;
};
```

## Implementation Notes

### Endianness

- **Little Endian**: Multi-byte values stored LSB first
- **Alignment**: Instructions aligned to 8-byte boundaries

### Limits

- **Max Instructions**: 2^32 (4 billion)
- **Max Constants**: 2^32 (4 billion)
- **Max Functions**: 2^16 (65,536)
- **Max Stack Depth**: 2^16 (65,536)

### Security

- **Bounds Checking**: All array/object access validated
- **Stack Overflow**: Detected and prevented
- **Type Safety**: Runtime type validation

## Version History

| Version | Date | Changes |
|---|---|---|
| 1.0 | 2026-02-11 | Initial specification |
| 1.1 | Future | Add SIMD operations |
| 1.2 | Future | Add async opcodes |

## Tool Support

### Disassembler

```bash
hav --disassemble script.hv
```

Output:
```
0000: DEFINE_FUNC 0, 2
0004: LOAD_VAR 0
0005: LOAD_VAR 1
0006: ADD
0007: RETURN

0008: LOAD_CONST 0
0009: LOAD_CONST 10
0010: LOAD_CONST 20
0011: CALL 0, 2
0012: STORE_VAR result
```

### Debugger Integration

```bash
hav --debug script.hv
```

- **Breakpoints**: Set on instruction addresses
- **Step Execution**: Execute one instruction at a time
- **Stack Inspection**: View current stack state
- **Variable Inspection**: View local variables

---

This specification provides the foundation for implementing Havel bytecode interpreters, JIT compilers, and debugging tools.
