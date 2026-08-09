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
print("hello {name}")        // bare brace
print("value: $x")           // short form (variable only)
print(f"2 + 2 = {2 + 2}")    // f-string with expression
print(`echo {name}`)         // backtick: shell interpolation
```

**Do not** use `+`, `,`, `.`, or newlines for concatenation — use `{var}` or `$var` interpolation.

### Collections

```hv
[1, 2, 3]              // array
[]                      // empty array
{1, 2, 3}              // set (unique elements)
:{1, 2, 3}             // explicit set literal
{ key: value }         // sorted object (keys ordered)
!{ key: value }        // unsorted object (insertion order)
(1, "hello", true)     // tuple (heterogeneous, fixed-size)
```

## Variables

### Declaration

```hv
val x = 5              // mutable (default)
x = 5              // mutable (default, python-style)
VAL = 5            // immutable (uppercase convention)
VAL PI = 3.14      // convention: uppercase = immutable
```

Uppercase names conventionally indicate immutability.

### Destructuring

```hv
[a, b] = [1, 2]       // array destructuring
(a, b) = (1, 2)       // tuple destructuring
{ x, y } = { x: 1, y: 2 }  // object destructuring (keys match)
```

## Operators

### Arithmetic

```hv
+  -  *  /  %      // basic
**                  // power
//                  // integer division
%%                  // integer modulo
```

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
1..10    // exclusive (1 to 9)
1..=10   // inclusive (1 to 10)
```

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

Three equivalent styles:

```hv
// 1. Brace blocks
if x > 0 { print("positive") }

// 2. Colon-indented (dedent ends block)
if x > 0:
    print("positive")
    print("still in block")

// 3. Double-colon for hotkey context
F1 :: { print("F1") }
```

## Control Flow (Preview)

```hv
// If/else
if x > 0 { "pos" } elif x < 0 { "neg" } else { "zero" }

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