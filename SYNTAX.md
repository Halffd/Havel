# Havel Language Syntax Reference

Derived from the lexer, parser, and AST implementation source code.

## Lexical Structure

### Comments

```
// single-line comment — the only comment syntax
```

No block/multi-line comments. `#` is NOT a comment character (used for hotkey prefixes and the `#` length operator).

### Block Syntax

Three block styles, all semantically equivalent:

1. **Brace blocks**: `{ body }`
2. **Colon-indented**: `:` followed by indented lines; dedent (column < base indentation) ends the block
3. **Double-colon**: `::` for hotkey context blocks

### Identifiers

Start with a letter or underscore, followed by letters, digits, or underscores.

These keywords are contextually allowed as identifiers in expression positions: `class`, `struct`, `enum`, `mode`, `val`, `const`, `let`.

### Number Literals

```
42              // decimal
0xFF            // hexadecimal (0x or 0X prefix)
0o77            // octal (0o or 0O prefix)
0b1010          // binary (0b or 0B prefix)
3.14            // floating point
1_000_000       // underscores as digit separators
```

### String Literals

```
"double-quoted string"       // regular string
'single-quoted string'       // regular string (equivalent)
f"interpolated {variable}"   // f-string interpolation (${expr} or {expr})
`backtick command`            // backtick interpolated string — shell command expression
'c'                           // char literal (single character)
/hello/                       // regex literal
"""multi-line string"""       // triple-quoted multi-line string
```

Interpolated strings use `${expression}` or bare `{expression}` inside f-strings and backtick strings.

### Operators

```
Arithmetic:     +  -  *  /  %  **  //  %%
Comparison:     ==  !=  <  >  <=  >=
Logical:        and  or  not
Nullish:        ??  ?.  ?:
Pipe:           |>  <|
Range:          ..  ..=
Assignment:     =  +=  -=  *=  /=  %=
Arrow:          ->  <-  =>
Bitwise:        &  |  ^  ~  <<  >>   (inside (( )) blocks only)
Misc:           ::  @  @@  #  !  $  $!
```

### Reserved Keywords

These cannot be used as variable names: `fn`, `if`, `else`, `while`, `for`, `in`, `loop`, `break`, `continue`, `return`, `throw`, `try`, `catch`, `finally`, `match`, `mode`, `map`, `repeat`, `struct`, `class`, `enum`, `trait`, `prot`, `impl`, `import`, `use`, `from`, `val`, `const`, `let`, `and`, `or`, `not`, `is`, `matches`, `null`, `true`, `false`, `on`, `off`, `when`, `co`, `await`, `channel`, `select`, `where`, `pool`, `config`, `devices`, `modes`, `hotkey`, `dsl`, `shell`, `go`, `init`.

`select`, `where`, and `pool` are lexer keywords with no parser implementation (reserved for future use).

## Declarations

### Variables

```
name = expr                    // mutable variable
val name = expr                // immutable binding (preferred)
const name = expr              // immutable binding (legacy alias for val)
let name = expr                // mutable (emits deprecation warning)
```

Uppercase names conventionally indicate immutability:

```
MAX_RETRIES = 10
```

### Destructuring

```
[a, b] = arr                   // array destructuring
(a, b) = tuple                 // tuple destructuring
```

## Functions

### Declaration

```
fn name(params) { body }
fn name(params) -> ReturnType { body }
```

Parentheses are optional for zero-parameter functions:

```
fn greet { print("hello") }
```

### Lambda / Arrow Functions

```
x => x * 2                     // single-expression lambda
(x, y) => x + y                // multi-parameter lambda
(x) => {                        // block-body lambda
  let result = x * 2
  result
}
```

### Implicit Return

Functions return the last expression's value. No explicit `return` keyword needed for the common case.

### Decorators

```
[async] fn foo() { }           // async function
[const] fn bar() { }           // const function
[pure] fn baz() { }            // pure function
[gpu] fn compute() { }         // GPU function
[inline] fn fast() { }         // inline hint
[noinline] fn slow() { }       // noinline hint
[entry] fn main() { }          // entry point
```

### Error-Throwing Variants

A `?` suffix on a function name declares an error-throwing variant:

```
fn parse?(s) { }               // throws on error instead of returning null
```

### Go Blocks

```
go { expression }              // concurrent block — runs expression in a new thread
go lambda()                    // runs lambda in a new thread
```

## Control Flow

### If / Else

```
if condition { body }
if condition { body } else { body }
if condition { body } else if condition2 { body2 } else { body3 }
```

`if` is also an expression:

```
result = if x > 0 { "positive" } else { "non-positive" }
```

Brace-call sugar is disabled during condition parsing, so `if f(x) { ... }` always parses `{ ... }` as the body, not as a brace-call argument to `f`.

### While

```
while condition { body }
```

### Loop

```
loop { body }                  // infinite loop — use break to exit
```

### For

```
for x in collection { body }
for (key, value) in collection { body }
```

Multiple iterators use parenthesized form. Keywords (`let`, `val`, `const`, `if`, `for`, `while`, `match`) are allowed as iterator names.

### Repeat

```
repeat countExpr { body }
repeat countExpr statement     // single inline statement
```

Brace-call sugar is disabled during count expression parsing.

### Break / Continue

```
break
break value                    // break with value (for loop expressions)
continue
```

### When

```
when condition { statements }
```

The condition inherits into inner statements. This is NOT pattern matching — it's a conditional context block.

### Match

```
match expr {
  pattern => body,
  pattern => body,
}
```

Multiple discriminants (comma-separated):

```
match expr1, expr2 {
  (pattern1, pattern2) => body,
}
```

`=>` separates pattern from body. During match parsing, `=>` is not parsed as an arrow function.

### Switch

```
switch (expr) {
  case value:
    body
  case value2:
    body
}
```

C-style switch with explicit `case` labels.

### Try / Catch / Finally

```
try { body }
try { body } catch e { handler }
try { body } catch (e) { handler }
try { body } finally { cleanup }
try { body } catch e { handler } finally { cleanup }
```

Catch supports both `catch e { }` and `catch (e) { }` forms.

### Throw

```
throw expr
```

## Types

### Struct

```
struct Name {
  field1
  field2: Type
  field3: Type = defaultValue

  fn method(params) { body }
  op +(other) { body }            // operator overload
}
```

Fields: `name [: type] [= default]`. Methods use `fn`/`op` keywords.

### Class

```
class Name {
  @@classField = value            // @@ prefix = class-level (static)
  @instanceField = value          // @ prefix = instance-level

  @@fn classMethod() { }          // static method
  @fn instanceMethod() { }        // instance method
  init(params) { }                // constructor

  op [](index) { }                // operator overload
}
```

Inheritance:

```
class Child : Parent {
  // ...
}
```

Class members: `@@` static, `@` instance; `fn` methods, `op` operator overloads, `init` constructor; fields with `val`/`const`/`let`.

### Enum

```
enum Name {
  Variant1
  Variant2(payload)
  Variant3 = 0
  Variant4(payload) = 1
}
```

Optional parenthesized payload type, optional integer default value. Variants are newline or comma separated.

### Trait

```
trait Name {
  fn method(params) -> ReturnType
  fn methodWithDefault(params) { body }
}
```

Optional return type annotation, optional default body.

### Protocol

```
prot Name {
  fn method(params) -> ReturnType
  op +(other)                     // operator method name
}
```

Supports operator method names: `+`, `-`, `*`, `/`, `<`, `>`, `[]`.

### Impl

```
impl TraitName for TypeName {
  fn method(params) { body }
}
```

Only `fn` function declarations allowed in impl body.

## Operator Overloading

Operator methods map to syntactic operators:

| Operator | Method      | Notes                    |
|----------|-------------|--------------------------|
| `+`      | `op_add`    |                          |
| `-`      | `op_sub`    |                          |
| `*`      | `op_mul`    |                          |
| `/`      | `op_div`    |                          |
| `%`      | `op_mod`    |                          |
| `**`     | `op_pow`    |                          |
| `==`     | `op_eq`     |                          |
| `!=`     | `op_neq`    |                          |
| `<`      | `op_lt`     |                          |
| `>`      | `op_gt`     |                          |
| `<=`     | `op_lte`    |                          |
| `>=`     | `op_gte`    |                          |
| `!`      | `op_not`    |                          |
| `-@`     | `op_negate` | unary minus              |
| `#`      | `op_length` | `#obj` → obj.op_length() |
| `""`     | `op_toString` | string coercion        |
| `()`     | `op_call`   | callable objects         |
| `[]`     | `op_index`  | subscript access         |
| `[]=`    | `op_index_set` | subscript assignment  |
| `repr`   | `op_repr`   | debug representation     |
| `code`   | `op_code`   |                          |
| `@()`    | `init`      | constructor              |
| `-@()`   | `op_destructor` | destructor           |
| `&`      | `op_bitand` | bitwise (( )) only       |
| `\|`     | `op_bitor`  | bitwise (( )) only       |
| `^`      | `op_bitxor` | bitwise (( )) only       |
| `<<`     | `op_shl`    | bitwise (( )) only       |
| `>>`     | `op_shr`    | bitwise (( )) only       |

## Expressions

### Operator Precedence (low to high)

| Precedence | Operators | Associativity |
|-----------|-----------|---------------|
| 0 | Prefix unary | — |
| 10 | `=` `+=` `-=` `*=` `/=` `%=` | right |
| 15 | `? :` ternary | right |
| 20 | `??` nullish coalesce | right |
| 25 | `or` | left |
| 30 | `and` | left |
| 35 | `\|>` `<|` pipe | left |
| 50 | `is` `matches` | left |
| 52 | `not in` | left |
| 55 | `in` | left |
| 60 | `==` `!=` `<` `>` `<=` `>=` | left |
| 65 | `..` `..=` range | right |
| 70 | `+` `-` | left |
| 80 | `*` `/` `%` `//` `%%` | left |
| 90 | `**` power | right |
| 100 | Postfix `?` `++` `--` | — |
| 110 | `.` `.?` `?.` member access | left |

Comparison operators are left-associative (NOT Python-style chaining).

Right-associative operators: all assignments, `**`, `??`, `?`, `->`, `<-`, `..`/`..=`.

### Member Access

```
obj.field                       // dot access
obj?.field                      // null-safe access
obj.method()                    // method call
obj?.method?()                  // null-safe + error-throwing variant
```

### Scope Resolution

```
::name                          // global scope lookup
```

`::` forces lookup in the global scope.

### Function Calls

```
f(args)                         // parenthesized call
f arg                           // paren-free call (single argument)
f { body }                      // brace-call sugar (block as first argument)
```

### Slice Expression

```
arr[start:end:step]             // desugars to arr.slice(start, end, step)
```

`::` inside brackets is treated as two colons for slice syntax.

### Range

```
1..10                           // exclusive range (1 to 9)
1..=10                          // inclusive range (1 to 10)
```

### Nullish Coalescing

```
value ?? default                // value if not null, else default
```

### Ternary

```
condition ? then_expr : else_expr
```

### Await / Fiber Receive

```
await expr                      // unary prefix — wait for fiber/channel value
<- expr                         // left arrow — equivalent to await (blocking expression)
```

Both produce an `AwaitExpression` node.

### Channel

```
channel()                       // create a new channel
```

### Shell Commands

```
$ (expr)                        // run command without capturing output
$! (expr)                       // run command and capture output
$! variable                     // capture into variable
$! [array]                      // capture into array
```

`$` runs without capture, `$!` captures output.

### Object Literals

```
{ key: value, key2: value2 }            // sorted object
!{ key: value, key2: value2 }           // unsorted object (insertion-order)
```

`!{ }` is the unsorted/insertion-order object literal. Parsed as `BangOpenBrace` single token or `Not` + `OpenBrace`.

### Array Literals

```
[1, 2, 3]                      // array
[]                              // empty array
```

### Set Literals

```
:{1, 2, 3}                     // set literal
```

### Pipelines

```
expr |> function               // pipe left into function: function(expr)
expr <| function               // pipe right from function: function(expr)
```

### LINQ-style (desugared)

```
from x in numbers where x > 2 select x * 2
// desugars to: numbers |> filter(x => x > 2) |> map(x => x * 2)
```

## Bitwise Operations

Bitwise operators are only available inside `(( ))` double-parenthesis blocks:

```
(( a | b ))                     // bitwise OR
(( a & b ))                     // bitwise AND
(( a ^ b ))                     // bitwise XOR
(( ~a ))                        // bitwise NOT
(( a << 4 ))                    // left shift
(( a >> 2 ))                    // right shift
```

Outside `(( ))`, `|` is the pipe-right operator, `&` is unused, and `~` is the tilde/home operator.

## Hotkey Syntax

### Hotkey Registration

```
F1 => { body }                  // simple hotkey
^+F1 => { body }                // Ctrl+Shift+F1
^C => { body }                  // Ctrl+C
```

Modifier prefixes: `^` = Ctrl, `+` = Shift, `!` = Alt, `#` = Super/Win.

NOT allowed: `hotkey "Ctrl+Shift+F1" { }` or `hotkey.register()` syntax — use the `=>` arrow form only.

### On/Off Key Events

```
on keydown keylist { body }
on keyup keylist { body }
off keydown keylist
off keyup keylist
```

## DSL Input Commands

Inside `dsl { }` blocks (where `inInputContext = true`):

```
:500                            // sleep 500ms
"text"                          // send text string
{Enter}                         // send keystroke
lmb                             // left mouse button click
rmb                             // right mouse button click
mmb                             // middle mouse button click
w(10, 20)                       // move mouse to (10, 20)
ws(10, 20)                      // scroll mouse at (10, 20)
```

## Modules

### Import / Use

```
use module                      // import module by name
use { fn1, fn2 } from "path"   // import specific bindings from path
import a, b from "path"        // import a and b from path
import { a, b as alias } from "path"  // import with aliases
import * from "path"           // wildcard import
import Name as alias           // import with alias
import Name from "path" as alias      // import from path with alias
```

### Module System

Python-style modules: every top-level function, variable, and class is exported. No `export` keyword. Prefix with `_` to indicate private by convention (not enforced).

Consumer: `use mymodule` or `use { fn } from "mymodule"`.

## Coroutines / Fibers

### Coroutine Function

```
co fn name(params) { body }    // declares a coroutine/fiber function
```

### Channels

```
ch = channel()                  // create channel
ch.send(value)                  // send to channel
<- ch                           // blocking receive from channel
```

### Await

```
result = await fiberValue       // wait for fiber to complete
result = <- channelValue        // blocking wait for value
```

`<-` is the "blocking expression operator for fibers" — unified wait-for-value.

## Built-in Operators and Methods

### Length Operator

```
#collection                     // calls op_length — returns length of string, array, etc.
```

### String Interpolation

```
"Hello {name}!"                 // bare brace interpolation in strings
f"Value: ${x}"                  // f-string interpolation
`cmd {arg}`                     // backtick interpolation (shell command)
```

Do NOT use `+`, commas, dot, or newlines for string concatenation. Use `{var}` or `$var` interpolation only.

### Self / Instance Reference

```
@                               // refers to self/current instance (ruby-style @)
@@                              // class-level reference
```

Do NOT use `this` — use `@`.

### Static Methods

```
@@fn method() { }               // class-level (static) method
```

Do NOT use `static` keyword — use `@@` prefix.

## Pattern Matching (in match expressions)

Patterns in `match` blocks support:

- Literal patterns: `1`, `"hello"`, `true`
- Variable patterns: `x` (binds the value)
- Wildcard: `_`
- Type patterns: `type Name`
- Destructuring patterns: `[a, b]`, `(a, b)`
- Enum variant patterns: `Variant(payload)`

## Type Annotations

Type annotations are optional throughout:

```
fn name(x: Int, y: String) -> Bool { }
val name: Type = value
struct S { field: Type }
```

Type annotations are parsed but not enforced at runtime in the current implementation.

## Special Syntax

### Config

```
config {
  key: value
}
```

### Devices

```
devices {
  // device declarations
}
```

### Modes

```
modes {
  // mode declarations
}
```

## Syntax That Will Be Rejected

The compiler will error on these patterns — do NOT use them:

| Pattern | Error | Use Instead |
|---------|-------|-------------|
| Semicolons in conditions/loops | syntax error | Newline or no separator |
| `let` for immutable binding | deprecation warning | `val` |
| `const` keyword | works but legacy | `val` |
| `export` keyword | syntax error | Not needed — all top-level is exported |
| `hotkey "Ctrl+Shift+F1" {}` | syntax error | `^+F1 => { }` |
| `hotkey.register()` | syntax error | `F1 => { }` (only in loops/objects) |
| Explicit `return` | works but unidiomatic | Implicit return (last expression) |
| `this` keyword | syntax error | `@` (ruby-style) |
| `static` keyword | syntax error | `@@` prefix |
| `#` comments | syntax error | `//` comments |
| String concatenation with `+`/`,`/`.`/newline | syntax error | `{var}` or `$var` interpolation |

## Parser Safety Limits

- Maximum tokens per parse: 5,000,000
- Maximum parse depth: 512
- `ProgressGuard` RAII checks token count
- `DepthGuard` RAII checks recursion depth
