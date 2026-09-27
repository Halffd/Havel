---
title: "Basic Syntax"
description: "Havel syntax fundamentals: comments, literals, variables, control flow, and functions."
---

# Basic Syntax

## Comments

Only single-line comments are supported:

```hv
// This is a comment
x = 5  // inline comment
```

No block comments (`/* */`) and no `#` comments (`#` is used for hotkey modifiers and the length operator).

## Identifiers

- Start with letter or underscore: `name`, `_private`, `MAX_SIZE`
- Followed by letters, digits, underscores
- Case-sensitive
- Keywords allowed as identifiers in expression positions: `class`, `struct`, `enum`, `mode`, `val`, `const`, `let`

## Literals

### Numbers

```hv
42              // decimal
0xFF            // hexadecimal
0o77            // octal
0b1010          // binary
3.14            // floating point
1_000_000       // underscores as digit separators
```

### Strings

```hv
"double-quoted string"       // regular string
'single-quoted string'       // equivalent
f"interpolated {variable}"   // f-string (${expr} or {expr})
`backtick command`            // shell command interpolation
'c'                           // char literal (single character)
/hello/                       // regex literal
"""multi-line string"""       // triple-quoted multi-line
```

### Interpolation

```hv
name = "world"
print("hello ${name}")        // regular string: ${var} interpolates
print("value: $name")         // short form (variable only)
print(f"2 + 2 = {2 + 2}")     // f-string evaluates {expr}
print(`echo {name}`)          // backtick: shell interpolation
```

Bare `{name}` inside a regular (non-f) string prints the braces
literally — use `${name}` or `$name`. `{expr}` evaluation only happens
inside f-strings and backticks. `+` joins strings too ("a" + "b" gives
"ab") but interpolation is the idiomatic style.

### Collections

```hv
[1, 2, 3]              // array
[]                      // empty array
{1, 2, 3}              // set (unique elements)
{ key: value }         // sorted object (keys ordered)
!{ key: value }        // unsorted object (insertion order)
(1, "hello", true)     // tuple literal (type() reports it as "array")
```

## Variables

### Declaration

```hv
x = 5                 // mutable (default)
val x = 5             // immutable — enforced (reassignment throws)
VAL = 5               // uppercase marks intent; still mutable (convention only)
```

`val` enforces immutability at runtime. Uppercase names only
communicate immutability by convention; the runtime allows reassignment.

### Destructuring

```hv
[a, b] = [1, 2]       // array destructuring
```

Tuple/parens destructuring `(a, b) = (1, 2)` and object destructuring
`{ x, y } = { x: 1 }` are not supported.

## Operators

### Arithmetic

```hv
+  -  *  /  %      // basic
**                  // power
%%                  // integer modulo
```

`//` starts a comment; there is no `//` division operator.

### Comparison

```hv
==  !=  <  >  <=  >=
```

Left-associative (not Python-style chaining): `a < b < c` is `(a < b) < c`.

### Logical

```hv
&&   // and
||   // or
!    // not
```

### Nullish

```hv
??   // nullish coalesce: a ?? b  →  a if not nil else b
?.   // null-safe member access
?:   // ternary (condition ? then : else)
```

### Pipe

```hv
|>   // left pipe:  x |> f  →  f(x)
<|   // right pipe: f <| x  →  f(x)
```

### Range

```hv
1..10    // inclusive range (1 to 10); type(1..10) == "range"
```

Ranges are inclusive at both ends, work in `for x in 1..10` loops, and
are not indexable — convert with `array.range(n)` if you need indexing.

### Assignment

```hv
=  +=  -=  *=  /=  %=
```

### Bitwise (only inside `(( ))`)

```hv
(( a | b ))   // OR
(( a & b ))   // AND
(( a ^ b ))   // XOR
(( ~a ))       // NOT
(( a << 4 ))   // left shift
(( a >> 2 ))   // right shift
```

Outside `(( ))`, `|` is pipe-right, `&` is unused, `~` is home/tilde operator.

## Blocks

Two equivalent styles:

```hv
// 1. Brace blocks
if x > 0 { print("positive") }

// 2. Colon-indented (dedent ends block)
if x > 0:
    print("positive")
    print("still in block")
```

(Hotkey bindings use `=>`, e.g. `F1 => { print("F1") }`; there is no
`::` block syntax.)

## Control Flow (Preview)

```hv
// If/else
if x > 0 { "pos" } else if x < 0 { "neg" } else { "zero" }

// Loops
for i in 0..10 { print(i) }
for item in items { print(item) }
while condition { ... }
loop { ... break ... }
repeat 5 { ... }

// Match (pattern matching)
match value {
    1 => "one",
    2 => "two",
    _ => "other"
}
```

## Functions (Preview)

```hv
// Declaration
fn add(a, b) { a + b }
fn add(a, b) -> int { a + b }

// Arrow (single expression)
fn double(x) => x * 2

// Lambda
x => x * 2
(x, y) => x + y
(x) => { let r = x * 2; r }

// Implicit return (last expression)
fn max(a, b) {
    if a > b { a } else { b }
}
```

---

**Previous:** [First Script](/getting-started/first-script)
**Next:** [Running Programs →](/getting-started/running-programs)