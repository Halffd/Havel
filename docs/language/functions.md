---
title: "Functions"
description: "Function declarations, parameters, implicit returns, decorators, error-throwing variants, lambdas, and closures."
---

# Functions

## Declaration

```hv
fn name(params) { body }
fn name(params) -> ReturnType { body }
```

Parentheses optional for zero-parameter functions:

```hv
fn greet { print("hello") }
```

## Parameters

```hv
fn add(a, b) { a + b }              // positional
fn add(a: int, b: int) -> int { a + b }  // with types
fn greet(name = "world") { "hello {name}" }  // default values
fn variadic(first, ...rest) { rest }  // rest parameter
```

## Implicit Return

Functions return the value of their last expression. No explicit `return` needed:

```hv
fn max(a, b) {
    if a > b { a } else { b }
}

fn factorial(n) {
    if n <= 1 { 1 } else { n * factorial(n - 1) }
}
```

Explicit `return` works but is unidiomatic.

## Arrow Functions (Single Expression)

```hv
fn double(x) => x * 2
fn add(a, b) => a + b
```

## Lambda / Anonymous Functions

```hv
x => x * 2                    // single expression
(x, y) => x + y               // multi-parameter
(x) => {                      // block body
    let result = x * 2
    result
}
```

## Closures

```hv
counter = 0
fn inc() => { counter += 1; counter }

inc()  // 1
inc()  // 2
```

Captured variables are by-reference (shared with enclosing scope).

## Decorators

```hv
[async] fn foo() { }          // async function (returns fiber)
[const] fn bar() { }          // const function (pure, no side effects)
[pure] fn baz() { }           // pure function (same input = same output)
[gpu] fn compute() { }        // GPU function (experimental)
[inline] fn fast() { }        // inline hint
[noinline] fn slow() { }      // noinline hint
[entry] fn main() { }         // entry point
```

Multiple decorators: `[async] [inline] fn foo() { }`

## Error-Throwing Variants

A `?` suffix declares a function that throws on error instead of returning `null`:

```hv
fn parse?(s) {                // throws on parse failure
    // ...
}

fn read_file?(path) {
    // throws if file not found
}
```

Call like a normal function; errors propagate via `throw`.

## Method Syntax (in Structs/Classes)

```hv
struct Point {
    x: int, y: int
    fn distance(other) {
        ((@x - other.x)**2 + (@y - other.y)**2)**0.5
    }
}

class Counter {
    @count = 0
    fn increment() { @count += 1 }
    fn get() => @count
}
```

## Operator Overloading

```hv
struct Vec {
    x: num, y: num
    op +(other) => Vec(@x + other.x, @y + other.y)
    op -(other) => Vec(@x - other.x, @y - other.y)
    op *(scalar) => Vec(@x * scalar, @y * scalar)
    op ==(other) => @x == other.x && @y == other.y
    op #() => (@x**2 + @y**2)**0.5  // length operator
    op ""() => "Vec({@x}, {@y})"    // string coercion
}
```

| Operator | Method | Notes |
|----------|--------|-------|
| `+` | `op_add` | |
| `-` | `op_sub` | |
| `*` | `op_mul` | |
| `/` | `op_div` | |
| `%` | `op_mod` | |
| `**` | `op_pow` | |
| `==` | `op_eq` | |
| `!=` | `op_neq` | |
| `<` | `op_lt` | |
| `>` | `op_gt` | |
| `<=` | `op_lte` | |
| `>=` | `op_gte` | |
| `!` | `op_not` | |
| `-@` | `op_negate` | unary minus |
| `#` | `op_length` | `#obj` → `obj.op_length()` |
| `""` | `op_toString` | string coercion |
| `()` | `op_call` | callable objects |
| `[]` | `op_index` | subscript access |
| `[]=` | `op_index_set` | subscript assignment |
| `repr` | `op_repr` | debug representation |
| `@()` | `init` | constructor |
| `-@()` | `op_destructor` | destructor |

## Higher-Order Functions

```hv
fn map(arr, fn) {
    result = []
    for x in arr { result.push(fn(x)) }
    result
}

fn filter(arr, fn) {
    result = []
    for x in arr { if fn(x) { result.push(x) } }
    result
}

// Usage
doubled = map([1,2,3], x => x * 2)
evens = filter([1,2,3,4], x => x % 2 == 0)
```

## Function as Value

```hv
fn add(a, b) => a + b
f = add              // function reference
f(1, 2)              // 3

// Pass as argument
apply = (f, x, y) => f(x, y)
apply(add, 3, 4)     // 7
```

---

**Previous:** [Control Flow](/language/control-flow)
**Next:** [Types →](/language/types)