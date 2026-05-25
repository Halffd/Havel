# Havel Scripting Language Reference

Complete reference for the Havel scripting language syntax, types, and semantics.

---

## Syntax Rules

- **No semicolons** at end of lines (use only for inline separation: `x = 1; y = 2`)
- **No `let`** — use Python-style declaration: `x = 5`
- **No `const`** — use `VAL` (uppercase) or Kotlin-style `val`
- **No `export`** — all top-level declarations are exported by default
- **No `end`, `do`, `then`** — use block brackets `{ }` or indent/colon syntax
- **No `and`/`or`** — use `&&`, `||`, `!`
- **No `this`** — use `@` (Ruby-style)
- **No `static`** — use `@@`
- **No `#` comments** — use `//`
- **No `parseInt`/`parseFloat`** — use `int()` or `num()`
- **No `impl X for`** — use vcolons, `class :`
- **No string concatenation with `+` or commas** — use `{var}` or `$var` interpolation
- **No explicit `return`** — use implicit return (last expression)

---

## Types

| Type | Keyword | Description |
|------|---------|-------------|
| Integer | `int` | 64-bit signed integer |
| Float | `num` | 64-bit double |
| Boolean | `bool` | true / false |
| String | `str` | Heap-allocated UTF-8 |
| Array | `array` | Dynamic list |
| Object | `object` | Key-value map |
| Set | `set` | Unique elements |
| Tuple | `tuple` | Fixed-size heterogeneous |
| Function | `fn` | Closure or function |
| Nil | `nil` | Null/none |

---

## Variables

```hv
x = 5          // mutable
VAL PI = 3.14  // immutable convention (uppercase)
y: int = 10    // with type annotation
```

---

## Functions

```hv
fn add(a, b) { a + b }

// Arrow syntax for single expression
fn double(x) => x * 2

// Closures
counter = 0
fn inc() => { counter += 1; counter }
```

### Implicit Return

Functions return the value of their last expression:

```hv
fn max(a, b) {
    if a > b { a } else { b }
}
```

---

## Control Flow

### If/Else

```hv
if x > 0 {
    print("positive")
} elif x < 0 {
    print("negative")
} else {
    print("zero")
}
```

### Loops

```hv
for i in 0..10 {
    print(i)
}

for item in array {
    print(item)
}

while condition {
    // ...
}

repeat 5 {
    print("hello")
}

n = 3
repeat n {
    print(n)
}
```

---

## String Interpolation

```hv
name = "world"
print("hello {name}")    // interpolation
print("value: $x")       // short form

// Expressions inside braces
print("2 + 2 = {2 + 2}")
```

---

## Data Structures

### Arrays

```hv
arr = [1, 2, 3]
arr[0]         // 1
arr.push(4)    // [1, 2, 3, 4]
arr.len()      // 4
```

### Objects

```hv
obj = { name: "test", value: 42 }
obj.name       // "test"
obj["value"]   // 42
```

### Sets

```hv
s = {1, 2, 2, 3}    // {1, 2, 3} (unique)
s.has(2)             // true
```

### Tuples

```hv
t = (1, "hello", true)
t[0]             // 1
```

---

## Structs

```hv
struct Point {
    x: int = 0,
    y: int = 0
}

p = Point(10, 20)
p.x    // 10
```

Structs are lightweight data containers with no methods. See [CLASSES_AND_STRUCTS.md](CLASSES_AND_STRUCTS.md) for full details.

---

## Classes

```hv
class Animal :
    @@count = 0

    fn init(name):
        @name = name
        @@count += 1

    fn speak():
        print("{@name} says hello")

    fn count() => @@count

dog = Animal("rex")
dog.speak()       // "rex says hello"
Animal.count()    // 1
```

- `@` = instance reference (like `self`/`this`)
- `@@` = class variable (like `static`)
- `:` after class name = body follows (indent or braces)

---

## Hotkeys

### Basic

```hv
F1 => send("hello")
^+A => { print("ctrl+shift+a") }
```

### With Alias

```hv
F1 => send("hello") alias: "greet"
```

### With Policy

```hv
^A => { sleep(1000); send("a") } policy: "replace"
```

### Conditional

```hv
^!S if window.active.exe == "chrome" => send("^F5")
^V when mode == "gaming" => click()
```

### When Blocks

```hv
when mode == "gaming" {
    ^!A => click()
    ^!B => click("right")
    F1 if health < 50 => send("e")
}
```

See [hotkey-system.md](hotkey-system.md) for the full API.

---

## Modes

```hv
mode.register("gaming", 10, fn => { window.active.exe == "steam.exe" })

if mode.current() == "gaming" {
    print("game on")
}

mode.set("gaming")
```

See [mode-system.md](mode-system.md) for full details.

---

## Concurrency

### Goroutines

```hv
go {
    sleep(1000)
    print("async")
}
```

### Channels

```hv
ch = channel()

go {
    ch.send(42)
}

val = ch.recv()    // 42
```

### Await

```hv
result = await thread_id
result = await timer_id
result = await coroutine_id
```

---

## Shell Commands

```hv
$ firefox                    // Fire-and-forget
out = `echo hello`           // Capture output
print(out.stdout)            // "hello\n"
print(out.exitCode)          // 0
```

---

## Type Conversions

```hv
int(3.9)          // 3
num("3.14")       // 3.14
str(123)          // "123"
bool(1)           // true
list(1, 2, 3)     // [1, 2, 3]
set_(1, 2, 2)     // {1, 2}
```

---

## Module System

Python-style modules — every top-level function, variable, and class is exported.

```hv
// mymodule.hv
fn greet(name) => print("hello {name}")
_内部的 = 42    // private by convention (underscore prefix)
```

```hv
// main.hv
use mymodule
mymodule.greet("world")

// Or specific imports
use { greet } from "mymodule"
greet("world")
```

---

## Pipeline Operator

```hv
data | transform1 | transform2

"hello" | str.upper() | str.trim()
```

---

## When Blocks (Non-Hotkey)

```hv
when condition {
    // code runs when condition becomes true
}
```

Compiles to: `when.register(condition_fn, body_fn)`

---

## Built-in Functions

| Function | Description |
|----------|-------------|
| `print(value)` | Print to stdout |
| `help()` | Interactive help |
| `help(topic)` | Help on specific topic |
| `type(value)` | Get type name |
| `len(value)` | Length of collection |
| `approx(a, b)` | Fuzzy float comparison |
| `sleep(ms)` | Sleep current goroutine |
| `wait(ms)` | Alias for sleep |

---

## See Also

- [hotkey-system.md](hotkey-system.md) — Full hotkey API reference
- [mode-system.md](mode-system.md) — Mode system details
- [host-functions.md](host-functions.md) — All host function reference
- [runtime-architecture.md](runtime-architecture.md) — Runtime internals
- [OPERATORS.md](OPERATORS.md) — Operator reference
- [GRAMMAR.md](GRAMMAR.md) — Formal grammar
- [CLASSES_AND_STRUCTS.md](CLASSES_AND_STRUCTS.md) — Structs and classes
