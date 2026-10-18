# Spec: operator-syntax

Scope: feature

# Special Method Syntax Specification

## Overview
Havel classes and structs support special methods using operator overloading syntax. These methods use familiar syntax that maps to `op_*` method names internally.

## Supported Special Methods

### Constructor and Destructor
```havel
class MyClass
    fn @(x, y)     // Constructor - maps to "init"
        @x = x
        @y = y

    fn @-()          // Destructor - maps to "op_destructor"
        print("MyClass destroyed")
```

### Index Operations
```havel
class MyList
    fn [](i)        // Index get - maps to "op_index"
        ...

    fn []=(i, v)    // Index set - maps to "op_index_set"
        ...
```

### Operators
```havel
class Vec2
    fn +(other)      // Maps to "op_add"
    fn -(other)      // Maps to "op_sub"
    fn *(other)      // Maps to "op_mul"
    fn /(other)      // Maps to "op_div"
    fn %(other)      // Maps to "op_mod"
    fn **(other)     // Power - maps to "op_pow"
    fn ==(other)     // Maps to "op_eq"
    fn !=(other)     // Maps to "op_ne"
    fn >=(other)     // Maps to "op_ge"
    fn <=(other)     // Maps to "op_le"
    fn <(other)      // Maps to "op_lt"
    fn >(other)      // Maps to "op_gt"
    fn |>(other)     // Pipe right - maps to "op_pipe_right"
    fn |(other)      // Bitwise OR - maps to "op_bit_or"
    fn &(other)      // Bitwise AND - maps to "op_bit_and"
    fn ^(other)      // Bitwise XOR - maps to "op_bit_xor"
    fn ~()           // Bitwise NOT - maps to "op_bit_not"
    fn >>(other)     // Shift right - maps to "op_shift_right"
    fn <<(other)     // Shift left - maps to "op_shift_left"
    fn =(other)      // Copy - maps to "op_copy"
```

### Callable
```havel
class Adder
    fn ()(args...)  // Callable - maps to "op_call"
        ...
```

### Length and String Representation
```havel
class MyObj
    fn #()          // Length - maps to "op_length"
        ...

    fn ""()         // toString - maps to "op_toString"
        "MyObj(...)"

    fn repr()        // Debug repr - maps to "op_repr"
        ...

    fn code()        // Source code - maps to "op_code"
        ...
```

## Method Name Mapping

| Syntax | Internal Name | Notes |
|--------|--------------|-------|
| `fn @()` | `init` | Constructor |
| `fn @-()` | `op_destructor` | Destructor |
| `fn [](i)` | `op_index` | Index get |
| `fn []=(i, v)` | `op_index_set` | Index set |
| `fn +(other)` | `op_add` | Addition |
| `fn -(other)` | `op_sub` | Subtraction |
| `fn *(other)` | `op_mul` | Multiplication |
| `fn /(other)` | `op_div` | Division |
| `fn %(other)` | `op_mod` | Modulo |
| `fn **(other)` | `op_pow` | Power |
| `fn ==(other)` | `op_eq` | Equality |
| `fn !=(other)` | `op_ne` | Not equal |
| `fn >=(other)` | `op_ge` | Greater or equal |
| `fn <=(other)` | `op_le` | Less or equal |
| `fn <(other)` | `op_lt` | Less than |
| `fn >(other)` | `op_gt` | Greater than |
| `fn \|>(other)` | `op_pipe_right` | Pipe right |
| `fn \|(other)` | `op_bit_or` | Bitwise OR |
| `fn &(other)` | `op_bit_and` | Bitwise AND |
| `fn ^(other)` | `op_bit_xor` | Bitwise XOR |
| `fn ~()` | `op_bit_not` | Bitwise NOT |
| `fn >>(other)` | `op_shift_right` | Shift right |
| `fn <<(other)` | `op_shift_left` | Shift left |
| `fn =(other)` | `op_copy` | Copy |
| `fn ()(args...)` | `op_call` | Callable |
| `fn #()` | `op_length` | Length |
| `fn ""()` | `op_toString` | String representation |
| `fn repr()` | `op_repr` | Debug representation |
| `fn code()` | `op_code` | Source code |

## Usage Example

```havel
class Vec2
    x, y

    fn @(x, y)
        @x = x
        @y = y

    fn @-()
        print("Vec2 destroyed")

    fn +(other)
        Vec2(@x + other.x, @y + other.y)

    fn ""()
        "Vec2($@x, $@y)"

v = Vec2(1, 2)
print(v)        // Vec2(1, 2)
```