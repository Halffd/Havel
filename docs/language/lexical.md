---
title: "Lexical Structure"
description: "Tokens, comments, identifiers, literals, operators, and keywords in Havel."
---

# Lexical Structure

## Comments

```hv
// single-line comment — the only comment syntax
```

Block comments (`/* */`) are **supported in the self-hosted parser** with nested comment support. `#` is **not** a comment character (used for hotkey prefixes and the `#` length operator).

## Block Syntax

Three styles, semantically equivalent:

```hv
// 1. Brace blocks
if x > 0 { print("positive") }

// 2. Colon-indented (dedent ends block)
if x > 0:
    print("positive")
    print("still in block")
print("outside")

// 3. Double-colon for hotkey context
F1 :: { print("F1 pressed") }
```

## Identifiers

- Start with letter or underscore: `name`, `_private`, `MAX_SIZE`
- Followed by letters, digits, underscores
- Case-sensitive
- These keywords are allowed as identifiers in expression positions: `class`, `struct`, `enum`, `mode`, `val`, `const`, `let`

## Number Literals

```hv
42              // decimal
0xFF            // hexadecimal (0x or 0X)
0o77            // octal (0o or 0O)
0b1010          // binary (0b or 0B)
3.14            // floating point
1_000_000       // underscores as digit separators
```

Exponent notation: `1.5e-11`, `1E+3`

## String Literals

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

| Syntax | Context | Example |
|--------|---------|---------|
| `{var}` / `{expr}` | f-strings, multiline f-strings | `f"hello {name}"`, `f"2+2={2+2}"` |
| `${expr}` | f-strings, multiline f-strings, backticks | `f"2+2={2+2}"` |
| `$var` | f-strings, multiline f-strings, regular strings, backticks | `"value: $x"`, `f"value: $x"` |

String concatenation: use interpolation (preferred) or `+` operator.

Regular (non-f) strings only support `$var` short form; use f-strings for full expression interpolation.

## Operators

### Arithmetic

| Operator | Meaning | Example | Result |
|----------|---------|---------|--------|
| `+` | Addition | `5 + 2` | `7` |
| `-` | Subtraction | `5 - 2` | `3` |
| `*` | Multiplication | `5 * 2` | `10` |
| `**` | Power | `5 ** 2` | `25` |
| `/` | Float division | `5 / 2` | `2.5` |
| `//` | Integer division | `5 // 2` | `2` |
| `%%` | Divmod (quotient + remainder) | `5 %% 2` | `(2, 1)` |
| `%` | Modulo | `5 % 2` | `1` |

### Bitwise (inside `(( ))` only)

| Operator | Meaning | Example | Result |
|----------|---------|---------|--------|
| `&` | Bitwise AND | `(( 5 & 3 ))` | `1` |
| `\|` | Bitwise OR | `(( 5 | 3 ))` | `7` |
| `^` | Bitwise XOR | `(( 5 ^ 3 ))` | `6` |
| `~` | Bitwise NOT | `(( ~5 ))` | `-6` |
| `<<` | Left shift | `(( 5 << 1 ))` | `10` |
| `>>` | Right shift | `(( 5 >> 1 ))` | `2` |

### Compound Assignment

| Operator | Meaning | Example |
|----------|---------|---------|
| `=` | Assignment | `x = 5` |
| `+=` | Add and assign | `x += 2` |
| `-=` | Subtract and assign | `x -= 2` |
| `*=` | Multiply and assign | `x *= 2` |
| `/=` | Float divide and assign | `x /= 2` |
| `//=` | Integer divide and assign | `x //= 2` |
| `%=` | Modulo and assign | `x %= 2` |
| `**=` | Power and assign | `x **= 2` |
| `&=` | Bitwise AND assign | `x &= 3` |
| `\|=` | Bitwise OR assign | `x \|= 3` |
| `^=` | Bitwise XOR assign | `x ^= 3` |
| `<<=` | Left shift assign | `x <<= 1` |
| `>>=` | Right shift assign | `x >>= 1` |

### Comparison

| Operator | Meaning | Example |
|----------|---------|---------|
| `==` | Equal | `a == b` |
| `!=` | Not equal | `a != b` |
| `<` | Less than | `a < b` |
| `>` | Greater than | `a > b` |
| `<=` | Less or equal | `a <= b` |
| `>=` | Greater or equal | `a >= b` |
| `~` | Regex match | `str ~ /pattern/` |
| `!~` | Regex not match | `str !~ /pattern/` |
| `is` | Identity (same reference) | `a is b` |
| `is not` | Not identity | `a is not b` |
| `in` | Membership | `x in list` |
| `not in` | Not membership | `x not in list` |

### Logical

| Operator | Meaning | Example |
|----------|---------|---------|
| `&&` | Logical AND | `x > 0 && x < 10` |
| `\|\|` | Logical OR | `x < 0 \|\| x > 10` |
| `!` | Logical NOT | `!flag` |
| `and` | Logical AND (alias) | `x > 0 and x < 10` |
| `or` | Logical OR (alias) | `x < 0 or x > 10` |
| `not` | Logical NOT (alias) | `not flag` |

### Null Handling

| Operator | Meaning | Example |
|----------|---------|---------|
| `??` | Nullish coalescing | `x ?? defaultValue` |
| `?.` | Optional chaining | `obj?.field?.value` |

### Range

| Syntax | Meaning | Example |
|--------|---------|---------|
| `..` | Exclusive range | `1..10` → 1 to 9 |
| `..=` | Inclusive range | `1..=10` → 1 to 10 |

### Pipeline & Flow

| Operator | Meaning | Example |
|----------|---------|---------|
| `\|>` | Pipeline | `data \|> map(f) \|> filter(g)` |
| `<|` | Reverse pipeline | `filter(g) <| data` |
| `;` | Statement separator (inline) | `x = 1; y = 2` |

### Member Access

| Operator | Meaning | Example |
|----------|---------|---------|
| `.` | Property access | `obj.field` |
| `.?` | Safe navigation (deprecated, use `?.`) | `obj.?field` |
| `?.` | Optional chaining | `obj?.field` |

### Special/Misc

| Operator | Meaning | Example |
|----------|---------|---------|
| `#` | Length operator | `#array` |
| `@` | This/self reference | `@field` |
| `@@` | Class/static variable | `@@count` |
| `++` / `--` | Increment/decrement | `x++`, `--x` |
| `->` | Arrow (return type) | `fn() -> int` |
| `<-` | Left arrow | `x <- channel` |
| `=>` | Hotkey arrow | `F1 => { }` |
| `::` | Double colon | `F1 :: { }` |
| `&` | Combo hotkey | `A&B` |
| `\|` | No repeat hotkey | `\|F1` |
| `:` | State/repeat interval | `F1:down`, `F1:100` |
| `~` | Don't grab hotkey | `~F1` |
| `*` | Any modifiers | `*F1` |
| `$` | Shell capture | `` `$cmd` `` |
| `kc:` / `sc:` | Keycode/scancode | `kc:38` |

### Precedence (low to high)

| Level | Operators | Associativity |
|-------|-----------|---------------|
| 0 | Prefix unary | — |
| 10 | `=` `+=` `-=` `*=` `/=` `%=` `**=` `//=` `%%=` `&=` `\|=` `^=` `<<=` `>>=` | Right |
| 15 | `? :` ternary | Right |
| 20 | `??` nullish coalesce | Right |
| 25 | `\|\|` / `or` | Left |
| 30 | `&&` / `and` | Left |
| 35 | `\|>` `<|` pipe | Left |
| 50 | `is` `is not` `matches` `~` `!~` `as` | Left |
| 55 | `in` `not in` | Left |
| 60 | `==` `!=` `<` `>` `<=` `>=` | Left |
| 65 | `..` `..=` range | Right |
| 70 | `+` `-` | Left |
| 80 | `*` `/` `%` `//` `%%` | Left |
| 90 | `**` power | Right |
| 100 | Postfix `++` `--` | — |
| 110 | `.` `.?` `?.` member access | Left |

Right-associative: all assignments, `**`, `??`, `? :`, `..`/`..=`.

Comparison operators are left-associative (NOT Python-style chaining).

## Reserved Keywords

Cannot be used as variable names:

```
fn if else while for in loop break continue return throw try catch finally
match mode map repeat struct class enum trait prot impl import use from
val const let && || ! is matches null true false on off when co await
channel select where pool config devices modes hotkey dsl shell go init
```

`select`, `where`, `pool` are lexer keywords with no parser implementation (reserved for future).

---

**Previous:** [Running Programs](/getting-started/running-programs)
**Next:** [Variables & Mutability →](/language/variables)