---
title: "Control Flow"
description: "Conditional statements, loops, pattern matching, and exception handling."
---

# Control Flow

## If / Else / Elif

```hv
if condition {
    // body
}

if condition {
    // then
} else {
    // else
}

if cond1 {
    // ...
} elif cond2 {
    // ...
} else {
    // ...
}
```

### If as Expression

```hv
result = if x > 0 { "positive" } else { "non-positive" }
```

Brace-call sugar is disabled during condition parsing: `if f(x) { ... }` always treats `{ ... }` as the body, not a brace-call argument.

## While Loop

```hv
while condition {
    // body
}
```

## Infinite Loop

```hv
loop {
    // body
    if done { break }
}
```

Use `break` to exit.

## For Loop

```hv
// Single iterator
for item in collection {
    // body
}

// Key-value iteration
for (key, value) in object {
    // body
}

// Range
for i in 0..10 {       // 0 to 9
    print(i)
}
for i in 0..=10 {      // 0 to 10
    print(i)
}

// Destructuring in for
for [x, y] in points {
    print("{x}, {y}")
}
```

Keywords (`let`, `val`, `const`, `if`, `for`, `while`, `match`) are allowed as iterator names.

## Repeat

```hv
repeat count {           // count can be variable or expression
    // body
}

repeat 5 print("hi")     // single inline statement
```

Brace-call sugar is disabled during count expression parsing.

## Break / Continue

```hv
break              // exit loop
break value        // break with value (for loop expressions)
continue           // next iteration
```

## When Block (Conditional Context)

```hv
when condition {
    // statements execute when condition becomes true
    // condition is re-evaluated on variable changes (event-driven)
}
```

Not pattern matching — it's a **conditional context block**. The condition inherits into inner statements.

```hv
when window.active.title.contains("Chrome") {
    ^R => send("^F5")      // hotkey only active in Chrome
    ^T => send("^T")
}
```

Compiles to `when.register(condition_fn, body_fn)`.

## Match (Pattern Matching)

```hv
match expr {
    pattern => body,
    pattern => body,
}
```

### Multiple Discriminants

```hv
match expr1, expr2 {
    (pattern1, pattern2) => body,
}
```

### Pattern Types

| Pattern | Example | Description |
|---------|---------|-------------|
| Literal | `1`, `"hello"`, `true` | Exact match |
| Variable | `x` | Binds value to name |
| Wildcard | `_` | Matches anything, discards |
| Type | `type Name` | Type test |
| Array | `[a, b]` | Destructure array |
| Tuple | `(a, b)` | Destructure tuple |
| Enum Variant | `Variant(payload)` | Match enum variant |

```hv
match value {
    0 => "zero",
    1 | 2 => "one or two",
    n if n > 10 => "big: {n}",
    [x, y] => "pair: {x}, {y}",
    type SomeType => "is SomeType",
    _ => "other"
}
```

`=>` separates pattern from body. During match parsing, `=>` is **not** parsed as an arrow function.

## Switch (C-Style)

```hv
switch (expr) {
    case value:
        // body
    case value2:
        // body
    default:
        // body
}
```

Explicit `case` labels, no fall-through.

## Try / Catch / Finally

```hv
try { body }
try { body } catch e { handler }
try { body } catch (e) { handler }
try { body } finally { cleanup }
try { body } catch e { handler } finally { cleanup }
```

Both `catch e { }` and `catch (e) { }` forms supported.

### Throw

```hv
throw expr
```

Any value can be thrown. In catch blocks, `it` refers to the thrown value:

```hv
try {
    risky()
} catch {
    print("caught: {it}")  // 'it' is the thrown value
}
```

---

**Previous:** [Variables & Mutability](/language/variables)
**Next:** [Functions →](/language/functions)