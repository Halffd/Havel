---
title: "Variables and Mutability"
description: "Variable declarations, mutability, destructuring, and naming conventions."
---

# Variables and Mutability

## Declaration Forms

```hv
x = 5              // mutable (default, python-style)
VAL = 5            // immutable (uppercase convention)
```

### Mutability

| Form | Mutability | Use Case |
|------|------------|----------|
| `x = 5` | Mutable | Variables that change |
| `let x = 5` | Mutable | Explicit mutable (deprecated, prefer `x = 5`) |
| `val x = 5` | Immutable | Immutable binding (preferred) |
| `const x = 5` | Immutable | Alias for `val` |
| `VAL = 5` | Immutable | Constants (uppercase convention) |
| `ref x = 5` | Mutable, reactive | Boxed value; assignment re-fires `when` watchers |

## Reactive Variables (`ref`)

`ref` declares a reactive box: every read of the name returns the current
value, and every assignment writes through the box, so `when` watchers
observe the assignment:

```hv
ref threshold = 0

when threshold >= 5 {
    print("reached at ${threshold}")
}

threshold = 3      // watcher does not fire
threshold = 5      // watcher fires
```

It is sugar for `threshold = ref(0)` (the box is the `{_val: value}` object
`ref()` has always created) plus transparent `_val` access on the name. The
identifier `ref` stays usable as a plain function name — `r = ref(0)`
followed by `r._val = 10` is the same machinery spelled out, and still
works.

Rules:

- declare before first use; shadowing a `ref` name is not supported;
- a declared `ref` may be mutated from inside a function, and `when`
  watchers in any scope observe it;
- `ref` names hold the value, not the box — interpolations and arithmetic
  see the number, so never write `counter._val` for a declared `ref`.

## Naming Conventions

Uppercase names conventionally indicate immutability:

```hv
MAX_RETRIES = 10
DEFAULT_TIMEOUT = 5000
PI = 3.14159
```

This is a convention only — the compiler does not enforce it.

## Type Annotations

Optional type annotations on declarations:

```hv
x: int = 10
val PI: num = 3.14
name: str = "hello"
items: array = [1, 2, 3]
```

Type annotations are parsed but **not enforced at runtime** in the current implementation. They serve as documentation and for future static analysis.

## Destructuring

### Array Destructuring

```hv
[a, b] = [1, 2]           // a=1, b=2
[a, b, c...] = [1, 2, 3, 4]  // rest: a=1, b=2, c=[3, 4]
[_, b] = [1, 2]          // ignore first: b=2
```

### Tuple Destructuring

```hv
(a, b) = (1, 2)          // a=1, b=2
(a, b, c) = fn_returning_tuple()
```

### Object Destructuring

```hv
{ x, y } = { x: 10, y: 20 }  // x=10, y=20
{ x: a, y: b } = { x: 1, y: 2 }  // rename: a=1, b=2
{ x, ...rest } = { x: 1, y: 2, z: 3 }  // rest = { y: 2, z: 3 }
```

## Scope

Variables are function-scoped (not block-scoped):

```hv
fn example() {
    if true {
        x = 10  // function-scoped
    }
    print(x)   // 10 — accessible
}
```

No `let`/`const` block scoping like JavaScript. Use functions for isolation.

## Reassignment

```hv
x = 5
x = 10          // OK: mutable

let y = 5
y = 10          // OK: let is mutable (deprecated)

val z = 5
z = 10          // Compile error: immutable

const w = 5
w = 10          // Compile error: immutable (alias for val)

VAL = 5
VAL = 10        // Convention says immutable, but not enforced
```

## Global vs Local

- Top-level declarations in a script/module are **module globals**
- Inside functions: **locals** (function-scoped)
- Module globals are isolated per module (use `use`/`import` to access)

```hv
// module.hv
val CONFIG = { debug: true }

// main.hv
use module
print(module.CONFIG.debug)
```

---

**Previous:** [Lexical Structure](/language/lexical)
**Next:** [Control Flow →](/language/control-flow)