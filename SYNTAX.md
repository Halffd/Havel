# Havel Language Syntax Reference

This document describes the complete syntax of the Havel scripting language, derived from the lexer, parser, and AST implementation.

## Lexical Structure

### Block Syntax

Havel supports two block styles:

1. **Brace blocks** (most common): `{ body }`
2. **Colon-indented blocks**: `:` followed by indented lines (Python-style), dedent ends the block
3. **Double-colon blocks**: `::` for hotkey context blocks

Most examples in this document use brace syntax. Both styles are semantically equivalent.

### Comments

```
// single-line comment
```

No block/multi-line comments exist. `#` is NOT a comment character (it's used for hotkey prefixes and the `#` length operator).

### Identifiers

Identifiers start with a letter or underscore, followed by letters, digits, or underscores. Some keywords are contextually allowed as identifiers in expression positions: `class`, `struct`, `enum`, `mode`, `val`, `const`, `let`.

### Number Literals

```
42          // decimal
0xFF        // hexadecimal (0x or 0X prefix)
0o77        // octal (0o or 0O prefix)
0b1010      // binary (0b or 0B prefix)
3.14        // floating point
1_000_000   // underscores for readability (digit separator)
```

### String Literals

```
"double-quoted string"
'single-quoted string'
f"interpolated {variable} string"
`backtick command`       // shell command expression
```

String interpolation uses `{expr}` inside `f"..."` strings. The `$var` shorthand also interpolates.

### Booleans and Null

```
true
false
null
```

## Operators

### Arithmetic

| Operator | Meaning |
|----------|---------|
| `+` | Addition / string concatenation |
| `-` | Subtraction |
| `*` | Multiplication |
| `/` | Division |
| `%` | Modulo |
| `**` | Exponentiation (right-associative) |
| `%%` | Integer division |
| `\` | Floor division |

### Comparison

| Operator | Meaning |
|----------|---------|
| `==` | Equal |
| `!=` | Not equal |
| `<` | Less than |
| `>` | Greater than |
| `<=` | Less than or equal |
| `>=` | Greater than or equal |

### Logical

| Operator | Meaning |
|----------|---------|
| `and` | Logical AND |
| `or` | Logical OR |
| `not` | Logical NOT (prefix) |

### Bitwise

Bitwise operations use `(( ))` block syntax in DSL context, or standard operators:

| Operator | Meaning |
|----------|---------|
| `&` | Bitwise AND |
| `\|` | Bitwise OR |
| `^` | Bitwise XOR |
| `~` | Bitwise NOT |
| `<<` | Left shift |
| `>>` | Right shift |

### Assignment

All arithmetic/bitwise operators have compound assignment forms:

```
=   +=  -=  *=  /=  %=  **=  \=  %%=
&=  |=  ^=  <<=  >>=
```

Assignment is right-associative.

### Special Operators

| Operator | Meaning | Associativity |
|----------|---------|---------------|
| `??` | Nullish coalescing | Right |
| `?.` | Optional chaining | Left |
| `?` | Ternary conditional | Right |
| `->` | Arrow (type annotation / lambda body) | Right |
| `<-` | Blocking expression (fiber wait) | Right |
| `..` | Range (exclusive) | Left |
| `..=` | Range (inclusive) | Left |
| `#` | Length operator (prefix) | - |
| `@` | Self reference (replaces `this`) | - |
| `@@` | Static reference (replaces `static`) | - |
| `::` | Global scope resolution | - |

### Unary

| Operator | Meaning |
|----------|---------|
| `-` | Negation |
| `not` | Logical NOT |
| `~` | Bitwise NOT |
| `#` | Length / count |
| `++` | Pre-increment |
| `--` | Pre-decrement |

### Update (Postfix)

| Operator | Meaning |
|----------|---------|
| `++` | Post-increment |
| `--` | Post-decrement |

## Declarations

### Variable Declaration

```
name = expr                // mutable variable (python-style)
val name = expr            // immutable binding (preferred)
const name = expr          // immutable binding (legacy alias for val)
let name = expr            // block-scoped mutable variable
```

Upper-case names conventionally indicate constants:

```
PI = 3.14159
MAX_RETRIES = 3
```

### Destructuring

```
[a, b, c] = [1, 2, 3]                    // array destructuring
{key: k, value: v} = {key: "a", value: 1}  // object destructuring
[a, ...rest] = [1, 2, 3, 4]              // rest element
```

### Multiple Assignment

```
a, b = b, a          // swap
x, y = 1, 2          // parallel assignment
```

## Functions

### Declaration

```
fn name(params) { body }
fn name(params) -> expr           // expression body
fn name(params) -> Type { body }  // with return type annotation
```

### Parameters

```
fn f(x, y)              // positional
fn f(x: Type)           // with type annotation
fn f(x = default)       // with default value
fn f(...args)           // variadic (rest parameter)
```

### Error-Throwing Variant

A `?` suffix on the function name creates an error-throwing variant:

```
fn parse?(input) { ... }   // throws on error instead of returning null
```

### Decorators

```
[async] fn f() { }        // async function
[const] fn f() { }        // pure/constant function
[pure] fn f() { }         // pure function
[inline] fn f() { }       // inline hint
[noinline] fn f() { }     // no-inline hint
[gpu] fn f() { }          // GPU kernel
[entry] fn f() { }        // entry point
```

Multiple decorators:

```
[async]
[pure]
fn f() { }
```

### Lambda

```
fn(params) { body }          // block lambda
fn(params) -> expr           // expression lambda
fn { body }                   // no-param block lambda
```

### Implicit Return

Functions return the last expression value. No `return` keyword needed for the final expression. Use `return` or `ret` for early exits:

```
fn abs(x) {
    if x < 0 { return -x }
    x
}
```

### Self Reference

`@` refers to the current instance (replaces `this`):

```
fn init(self, x) {
    @x = x
}
```

`@@` refers to static members (replaces `static`):

```
@@counter = 0
```

## Control Flow

### If / Else

```
if condition { body }
if condition { body } else { body }
if condition { body } else if condition2 { body } else { body }
```

Conditions do NOT use parentheses. Braces are required for block bodies.

### While

```
while condition { body }
do { body } while condition
```

### For

```
for item in collection { body }
for (key, value) in dict { body }
for i in 0..10 { body }            // range (exclusive end)
for i in 0..=10 { body }           // range (inclusive end)
```

### Loop

```
loop { body }          // infinite loop (break to exit)
```

### Repeat

```
repeat 5 { body }      // execute body 5 times
repeat count_expr statement   // inline form
```

### Break and Continue

```
break
break value            // break with value (in loop expressions)
continue
```

### Match

```
match expr {
    pattern1 => body1
    pattern2, pattern3 => body2
    _ => default_body
}

match expr1, expr2 {         // multiple discriminants
    (a, b) => body
}
```

Match supports patterns:
- Literal values: `1`, `"hello"`
- Variable bindings: `x`
- Wildcard: `_`
- Range: `0..10`
- Destructuring: `{key: k}`, `[a, b, ...rest]`
- Or patterns: `pattern1 | pattern2`
- Enum variants: `Some(x)`

### Switch (C-style)

```
switch expr {
    case value1: body1
    case value2: body2
    default: default_body
}
```

## Types

### Type Annotations

```
x: Int
name: String
items: Array
data: Map
fn f(x: Int) -> String { }
```

### Union Types

```
Int | String
Int | null
```

### Record Types

```
{ name: String, age: Int }
```

### Function Types

```
(Int, String) -> Bool
```

### Type Declaration

```
type AliasName = OriginalType
```

## Struct

```
struct Point {
    x: Int
    y: Int

    fn magnitude(self) {
        (@x ** 2 + @y ** 2) ** 0.5
    }
}
```

Structs have value semantics. Fields and methods coexist in the body.

## Class

```
class Animal {
    name: String

    fn init(self, name) {
        @name = name
    }

    fn speak(self) {
        print("{@name} speaks")
    }
}

class Dog : Animal {
    fn speak(self) {
        print("{@name} barks")
    }
}
```

Inheritance uses `:` syntax. `self` is explicit in method signatures. Instance fields use `@` prefix.

## Enum

```
enum Color {
    Red
    Green
    Blue
}

enum Option {
    Some(value)
    None
}

enum Shape {
    Circle { radius: Float }
    Rectangle { width: Float, height: Float }
}
```

Variants can be simple, tuple-like with a payload type, or struct-like with named fields.

## Protocols and Traits

### Protocol Declaration

```
prot Drawable {
    fn draw()
    fn resize(w, h)
}
```

### Trait Declaration

```
trait Serializable {
    fn serialize() -> String
}
```

### Implementation

```
impl Drawable for Shape {
    fn draw(self) {
        // ...
    }
}
```

## Concurrency

### Fibers (Coroutines)

```
co fn producer(ch) {
    for i in 0..10 {
        ch <- i
    }
}

co fn consumer(ch) {
    loop {
        val = <-ch
        print(val)
    }
}
```

- `co fn` declares a coroutine/fiber function
- `<-` is the blocking expression operator (send into / receive from channel)

### Threads

```
thread {
    // concurrent block
}
```

### Go (Fiber Launch)

```
go expr          // launch expression as fiber
```

### Yield

```
yield expr       // yield value from fiber/generator
```

### Channels

```
ch = channel()   // create channel
```

### Interval and Timeout

```
interval 500 {
    // execute every 500ms
}

timeout 3000 {
    // execute after 3000ms
}
```

Interval and timeout objects support `.stop()` and `.cancel()` respectively.

## Collections

### Arrays

```
[1, 2, 3]                      // literal
[1, 2, ...rest]                 // spread
[]                              // empty
arr[0]                          // index access
arr[1:3]                        // slice
arr[-1]                         // negative index
#arr                            // length
```

### Sets

```
{1, 2, 3}                       // set literal (when not in object context)
```

### Objects

```
{ key: value }                  // object literal
{ shorthand }                   // shorthand property
{ [computed]: value }           // computed key
{ ...other }                    // spread
obj.field                       // member access
obj["key"]                      // index access
```

### Tuples

```
(1, "hello", true)              // tuple literal
```

## Hotkeys

### Basic Syntax

```
F1 => { action }
Ctrl+V => print "pasted"
^+C => { copy() }               // ^ is Ctrl alias
```

### With Conditions

```
F2 when mode gaming => { ... }
Ctrl+A when title "Firefox" && class "Navigator" => { ... }
```

Conditions: `mode`, `title`, `class`, `process`, joined by `&&`.

### Multi-Hotkey

```
F1 & F2 => { ... }             // both keys simultaneously
```

### Dynamic Hotkey Assignment

```
dhk = F3 => { print @key }     // assigns hotkey to variable
```

### Disabling

```
!d => { ... }                  // disabled by default
@disable()                      // disable from inside handler
```

### Special Keys

```
#Esc => { ... }                // Win+Esc
RShift => { ... }              // right shift
```

## Import and Use

### Import

```
import "module"                  // import module
import { fn1, fn2 } from "mod"  // named imports
```

### Use

```
use module                       // use module (python-style)
use { fn1, fn2 } from "module"  // named use
```

## Error Handling

### Try / Catch

```
try {
    risky()
} catch e {
    print("caught: {e}")
} finally {
    cleanup()
}
```

### Throw

```
throw "error message"
throw ErrorType("details")
```

## Special Blocks

### Config Block

```
config {
    IO.Executor = Scheduler
    General {
        Terminal = konsole
        GamingApps = steam_app_default,retroarch
    }
    Debug.ForceMinimal = 0
}
```

### Mode Block

```
mode gaming {
    condition = true
    priority 5
    on enter { print("entered gaming") }
    on exit { print("left gaming") }
    on reload { print("reloaded") }
}
```

### Devices Block

```
devices {
    // device definitions
}
```

### Modes Block

```
modes {
    // mode definitions
}
```

### UI Block

```
ui {
    window("Title") {
        button("Click me") {
            onClick => { print("clicked") }
        }
    }
}
```

UI blocks desugar to method calls: `ui.window("Title")`, `ui.button("Click me")`, with event handlers attached.

### With Block

```
with resource {
    // resource is available in scope
}
```

### Signal Definition

```
signal name = condition_expr
signal name: condition_expr
```

Signals define named conditions that can be evaluated at runtime.

### Group Definition

```
group name {
    modes: [mode1, mode2, mode3]
}
```

Groups organize modes into logical collections.

### Modes Block (Legacy)

```
modes {
    mode_name {
        // mode definition
    }
}
```

## When Block

```
when condition {
    // executes when condition becomes true
}
```

`when` is distinct from `if` — it represents a reactive/watched condition (used in hotkey and mode contexts).

## Event Handlers

```
on enter { ... }         // mode enter
on exit { ... }          // mode exit
on reload { ... }        // configuration reload
on start { ... }         // application start
on message { ... }       // message received
on keydown { ... }       // key press
on keyup { ... }         // key release
on tap { ... }           // tap event
on combo { ... }         // key combo
```

Mode activation from top level:

```
on mode_name { ... }     // activate mode_name
off mode_name { ... }    // deactivate mode_name
```

## Shell Commands

### Backtick Expressions

```
`ls -la`               // execute shell command, returns output
```

### Shell Command Statement

```
shell "ls -la"
```

## Pipeline Expression

```
data | filter | map(fn(x) { x * 2 })
data |> filter |> map(fn(x) { x * 2 })
```

Both `|` and `|>` pipe operators are supported.

## Config Append

```
config >> value          // append value to config
```

## Operator Overloading

Inside `class` or `struct` bodies, use `op` instead of `fn` to define operator methods:

```
class Vec2 {
    x: Float
    y: Float

    fn init(self, x, y) {
        @x = x
        @y = y
    }

    op + (other) {
        Vec2(@x + other.x, @y + other.y)
    }
    op == (other) {
        @x == other.x and @y == other.y
    }
    op [] (index) {
        if index == 0 { @x } else { @y }
    }
}
```

### Operator Method Name Mapping

| Operator | Method Name |
|----------|-------------|
| `+` | `op_add` |
| `-` | `op_sub` |
| `*` | `op_mul` |
| `/` | `op_div` |
| `%` | `op_mod` |
| `**` | `op_pow` |
| `==` | `op_eq` |
| `!=` | `op_ne` |
| `<` | `op_lt` |
| `>` | `op_gt` |
| `<=` | `op_le` |
| `>=` | `op_ge` |
| `[]` | `op_index` |
| `[]=` | `op_index_set` |
| `()` | `op_call` |
| `\|>` | `op_pipe_right` |
| `\|` | `op_bit_or` |
| `&` | `op_bit_and` |
| `^` | `op_bit_xor` |
| `~` | `op_bit_not` |
| `>>` | `op_shift_right` |
| `<<` | `op_shift_left` |
| `=` | `op_copy` |
| `+=` | `op_iadd` |
| `-=` | `op_isub` |
| `*=` | `op_imul` |

## Cast Expression

```
expr as Type
```

## Sleep

```
sleep(1000)            // sleep milliseconds
```

## Delete

```
del obj.field          // delete object property
```

## Operator Precedence (High to Low)

1. Postfix: `++`, `--`, `.`, `?.`, `[]`, `()`
2. Unary: `-`, `not`, `~`, `#`, `++`, `--`
3. Exponentiation: `**` (right-associative)
4. Multiplicative: `*`, `/`, `%`, `\`, `%%`
5. Additive: `+`, `-`
6. Shift: `<<`, `>>`
7. Bitwise AND: `&`
8. Bitwise XOR: `^`
9. Bitwise OR: `|`
10. Comparison: `<`, `>`, `<=`, `>=`
11. Equality: `==`, `!=`
12. Logical AND: `and`
13. Logical OR: `or`
14. Nullish coalescing: `??` (right-associative)
15. Ternary: `?` (right-associative)
16. Arrow: `->`, `<-` (right-associative)
17. Range: `..`, `..=`
18. Assignment: `=`, `+=`, `-=`, etc. (right-associative)

## Reserved Keywords

```
let val const if else while do switch for in
loop break continue match case default fn op
return ret try catch finally throw thread interval
timeout yield go sync async channel co del
config devices modes signal group struct class
enum trait prot impl this on off when mode
repeat pool select where
```

`match`, `mode`, `map` are reserved and cannot be used as variable names.

## DSL Mode

DSL (Domain-Specific Language) mode is activated inside `dsl { }` blocks. In DSL context:
- Custom operator syntax becomes available
- `(( ))` blocks denote bitwise operations
- Input context flag is set (`inInputContext = true`)

## Built-in Functions and Modules

### Global Functions

- `print(expr, ...)` / `print(expr)` — output to stdout
- `len(collection)` / `#collection` — length
- `type(value)` — type name string
- `exit(code)` — exit process
- `rand()` — random number
- `sleep(ms)` — sleep

### Standard Library Modules

| Module | Key Functions |
|--------|--------------|
| `math` | `abs`, `ceil`, `floor`, `round`, `sqrt`, `sin`, `cos`, `tan`, `min`, `max`, `PI`, `E` |
| `string` | `len`, `upper`, `lower`, `trim`, `split`, `replace`, `contains`, `starts_with`, `ends_with`, `slice` |
| `array` | `push`, `pop`, `shift`, `unshift`, `map`, `filter`, `reduce`, `sort`, `reverse`, `join`, `slice`, `find`, `includes` |
| `time` | `now`, `epoch`, `date`, `time`, `year`, `month`, `day`, `hour`, `minute`, `second` |
| `sys` | `platform`, `arch`, `cwd`, `pid`, `env`, `detect`, `hardware`, `gc` |
| `shell` | `run`, `cwd`, `getenv`, `escape`, `exec` |
| `fs` | `read`, `write`, `exists`, `mkdir`, `rm`, `ls`, `cp`, `mv`, `stat` |
| `io` | `send`, `read`, `write` |
| `log` | `log`, `info`, `warn`, `error`, `debug` |
| `timer` | `setTimeout`, `setInterval`, `clear`, `activeCount`, `clearAll` |
| `cfg` | configuration access |
| `parser` | `csv`, parsing utilities |
| `object` | `keys`, `values`, `entries`, `merge`, `flatten`, `unflatten`, `delete` |
| `type` | type introspection |
| `process` | process management |
| `filemanager` | file management operations |

### Host Modules (when available)

| Module | Key Functions |
|--------|--------------|
| `hotkey` | global hotkey registration |
| `window` | window manipulation, focus, move, resize |
| `audio` | volume control, playback |
| `brightness` | screen brightness |
| `browser` | browser control |
| `clipboard` | clipboard get/set |
| `io` | input/output, mouse events |
| `ocr` | text recognition |
| `media` | media playback control |
| `screenshot` | screen capture |
| `alttab` | alt-tab window switching |
| `app` | application management |
| `mapmanager` | keyboard mapping |
| `mode` | mode management |
| `http` | HTTP requests |
| `pixel` | pixel automation |

## Module System

Havel uses Python-style modules:
- Every top-level function, variable, and class is exported
- No `export` keyword
- Prefix with `_` to indicate private by convention (not enforced)
- `use mymodule` or `use { fn } from "mymodule"` to consume

## Syntax Pitfalls

These patterns will cause compiler errors:

| Wrong | Right | Reason |
|-------|-------|--------|
| `if (cond) { }` | `if cond { }` | No parens around conditions |
| `for (i in x) { }` | `for i in x { }` | No parens around for |
| `let x = 5` (top-level) | `x = 5` or `val x = 5` | `let` is block-scoped; use `val` for immutable |
| `const X = 5` | `X = 5` or `val X = 5` | `const` is legacy alias |
| `this.field` | `@field` | `this` is not used; use `@` |
| `static method` | `@@method` | `static` is not used; use `@@` |
| `# comment` | `// comment` | `#` is length operator, not comment |
| `export fn f()` | `fn f()` | No `export` keyword |
| `hotkey "Ctrl+F1" {}` | `Ctrl+F1 => { }` | Use arrow hotkey syntax |
| `return x` (last line) | `x` | Use implicit return for final expression |
| `"a" + "b"` | `"a{b}"` or `f"a{b}"` | String concatenation uses interpolation |

## File Extension

Havel source files use the `.hv` extension.
