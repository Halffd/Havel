# Havel Operators Reference

## Arithmetic

| Operator | Meaning | Example | Result |
|----------|---------|---------|--------|
| `+` | Addition | `5 + 2` | `7` |
| `-` | Subtraction | `5 - 2` | `3` |
| `*` | Multiplication | `5 * 2` | `10` |
| `**` | Power | `5 ** 2` | `25` |
| `/` | Float division | `5 / 2` | `2.5` |
| `\` | Integer division | `5 \ 2` | `2` |
| `\\` | Divmod (quotient + remainder) | `5 \\ 2` | `(2, 1)` |
| `%` | Modulo | `5 % 2` | `1` |
| `%%` | Remainder (sign of dividend) | `-5 %% 2` | `-1` |
| `*@` | Matrix multiplication | `A *@ B` | Matrix |

## Bitwise

| Operator | Meaning | Example | Result |
|----------|---------|---------|--------|
| `&` | Bitwise AND | `5 & 3` | `1` |
| `\|` | Bitwise OR | `5 \| 3` | `7` |
| `^` | Bitwise XOR | `5 ^ 3` | `6` |
| `~` | Bitwise NOT | `~5` | `-6` |
| `<<` | Left shift | `5 << 1` | `10` |
| `>>` | Right shift (logical) | `5 >> 1` | `2` |

## Unary

| Operator | Meaning | Example |
|----------|---------|---------|
| `-` | Negation | `-x` |
| `+` | Unary plus | `+x` |
| `!` | Logical NOT | `!true` |
| `#` | Length operator | `#array` |
| `++` | Increment (prefix/postfix) | `x++`, `++x` |
| `--` | Decrement (prefix/postfix) | `x--`, `--x` |
| `@` | This/self reference | `@field` |
| `@@` | Class/static variable | `@@count` |

## Comparison

| Operator | Meaning | Example |
|----------|---------|---------|
| `==` | Equal | `a == b` |
| `!=` | Not equal | `a != b` |
| `<` | Less than | `a < b` |
| `>` | Greater than | `a > b` |
| `<=` | Less or equal | `a <= b` |
| `>=` | Greater or equal | `a >= b` |
| `~` | Regex match | `str ~ r"pattern"` |
| `in` | Membership | `x in list` |
| `not in` | Not membership | `x not in list` |
| `is` | Identity | `a is b` |
| `is not` | Not identity | `a is not b` |

## Logical

| Operator | Meaning | Example |
|----------|---------|---------|
| `&&` | Logical AND | `x > 0 && x < 10` |
| `\|\|` | Logical OR | `x < 0 \|\| x > 10` |
| `!` | Logical NOT | `!flag` |
| `and` | Logical AND (keyword) | `x > 0 and x < 10` |
| `or` | Logical OR (keyword) | `x < 0 or x > 10` |
| `not` | Logical NOT (keyword) | `not flag` |

## Assignment

| Operator | Meaning | Example |
|----------|---------|---------|
| `=` | Assignment | `x = 5` |
| `+=` | Add and assign | `x += 2` |
| `-=` | Subtract and assign | `x -= 2` |
| `*=` | Multiply and assign | `x *= 2` |
| `/=` | Float divide and assign | `x /= 2` |
| `\=` | Integer divide and assign | `x \= 2` |
| `%=` | Modulo and assign | `x %= 2` |
| `%%=` | Remainder and assign | `x %%= 2` |
| `**=` | Power and assign | `x **= 2` |
| `&=` | Bitwise AND assign | `x &= 3` |
| `\|=` | Bitwise OR assign | `x \|= 3` |
| `^=` | Bitwise XOR assign | `x ^= 3` |
| `<<=` | Left shift assign | `x <<= 1` |
| `>>=` | Right shift assign | `x >>= 1` |

## Pipeline & Flow

| Operator | Meaning | Example |
|----------|---------|---------|
| `\|>` | Pipeline | `data \|> map(f) \|> filter(g)` |
| `;` | Statement separator (inline) | `x = 1; y = 2` |
| `,` | Comma separator | `[1, 2, 3]` |

## Null Handling

| Operator | Meaning | Example |
|----------|---------|---------|
| `??` | Nullish coalescing | `x ?? defaultValue` |
| `?.` | Optional chaining | `obj?.field?.value` |

## Substring/Slice

| Syntax | Meaning | Example |
|--------|---------|---------|
| `[start:end]` | Slice (start inclusive, end exclusive) | `"hello"[1:4]` → `"ell"` |
| `[:end]` | From start to end | `"hello"[:3]` → `"hel"` |
| `[start:]` | From start to end | `"hello"[2:]` → `"llo"` |
| `[start:end:step]` | Slice with step | `"hello"[::2]` → `"hlo"` |

## Hotkey Modifiers

| Modifier | Meaning | Example |
|----------|---------|---------|
| `^` | Ctrl | `^C` |
| `+` | Shift | `+A` |
| `!` | Alt | `!F4` |
| `#` | Meta/Windows | `#R` |
| `~` | Don't grab (pass through) | `~F1` |
| `*` | Any modifiers allowed | `*F1` |
| `\|` | No repeat | `\|F1` |
| `:` | State or repeat interval | `F1:down`, `F1:100` |
| `&` | Combo (order specific) | `A&B` |
| `kc:` | Keycode | `kc:38` |
| `sc:` | Scancode | `sc:0x38` |

## Hotkey Conditional

| Keyword | Meaning | Example |
|---------|---------|---------|
| `if` | Conditional execution | `F1 if mode == "gaming" => { }` |
| `when` | Reactive condition | `F1 when window.active == "Firefox" => { }` |
| `pool` | Hotkey namespace | `pool "media" { F1 => play() }` |

## Shell Commands

| Syntax | Capture |
|--------|---------|
| `` `cmd` `` | stdout (string) |
| `` e`cmd` `` | stderr |
| `` s`cmd` `` | stdout + stderr |
| `` p`cmd` `` | PID (int) |
| `` ?`cmd` `` | Exit code (int) |
| `` a`cmd` `` | Full object (stdout, stderr, exitCode, pid, success) |
| `` &`cmd` `` | Run detached (no capture) |

## DSL Block Operators (Input Automation)

| Operator | Meaning | Example |
|----------|---------|---------|
| `>` | Send keys | `> "hello"` |
| `:` | Sleep (ms) | `: 500` |
| `<` | Get input/state | `< mouse` |
| `*` | Repeat N times | `* 3` |
| `*?` | While loop | `*? x < 10` |
| `*:` | For loop | `*: i in 1..10` |
| `?` | If block | `? x > 5` |
| `?;` | When block (reactive) | `?; x > 5` |
| `!!` | Repeat previous line | `!!` |
| `&` | Run in thread | `&` |
| `\|` | Pipeline (within DSL) | `data \| process` |
| `$` | Shell command | `$ "ls"` |
| `->` | Print | `-> result` |
| `<-` | Return/break | `<- x` |
| `>>` | Write to config/file | `>> config.key` |
| `<<` | Read from config/file | `<< config.key` |

## Precedence (highest to lowest)

| Level | Operators | Associativity |
|-------|-----------|---------------|
| 1 | `()` `[]` `.` `?.` | Left |
| 2 | `++` `--` `-` `+` `!` `~` `#` `@` `@@` | Right |
| 3 | `**` | Right |
| 4 | `*` `/` `\` `%` `%%` `*@` | Left |
| 5 | `<<` `>>` | Left |
| 6 | `&` (bitwise) | Left |
| 7 | `^` (bitwise) | Left |
| 8 | `\|` (bitwise) | Left |
| 9 | `+` `-` | Left |
| 10 | `..` `..=` | Left |
| 11 | `<` `>` `<=` `>=` `in` `not in` `is` `is not` `~` `~=` | Left |
| 12 | `==` `!=` | Left |
| 13 | `&` (logical, `&&`) | Left |
| 14 | `^` (logical) | Left |
| 15 | `\|` (logical, `\|\|`) | Left |
| 16 | `??` | Left |
| 17 | `?:` (ternary) | Right |
| 18 | `=` `+=` `-=` `*=` `/=` `\=` `%=` `%%=` `**=` `&=` `\|=` `^=` `<<=` `>>=` | Right |
| 19 | `|>` | Left |
| 20 | `,` | Left |

## Notes

- `#` length operator: `#array`, `#string`, `#object` (number of keys)
- `in` and `not in` work with arrays, strings, objects, sets
- Ternary operator: `condition ? true_expr : false_expr`
- `->` is also used for function return type annotation: `fn add(a, b) -> int`
- Hotkey modifiers work inside hotkey definitions only
- DSL operators work inside `dsl { }` blocks only
- // and /* */ serve as comments