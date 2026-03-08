# Havel Language Design Principles

## Core Philosophy

> **Most languages die because the creator tries to design Rust v2 before finishing Lua v0.1.**
> 
> **Start tiny. Grow later.**

## v0.1 Scope (SHIPPABLE)

### Core Features (Must Have)

```
1. Variables      - let x = 5
2. Functions      - fn add(a, b) { a + b }
3. Conditionals   - if/else
4. Loops          - while, for, repeat
5. Calls          - foo(bar)
6. Structs        - struct Point { x, y }
7. Modules        - use io, import x from "path"
```

### Nice to Have (If Time Permits)

```
- as keyword      - casts: x as int
- match           - simple pattern switch
- ? operator      - error propagation (if Result-based)
- => lambdas      - (x) => x + 1
- implicit return - last expression returns
- |> pipeline     - value |> f |> g
```

### v0.2+ (Deferred)

```
- Complex pattern matching
- Destructuring
- Trait system
- Advanced type system
- Macros
- Async/await
- Generics
```

## Syntax Decisions

### Lambdas: AVOID `||`

**Bad** (Rust-style, parser pain):
```havel
|| print("hi")
|x| x + 1
```

**Good** (Simple grammar):
```havel
(x) => x + 1
fn(x) => x + 1
```

### Casts: USE `as`

**Good** (Readable, explicit):
```havel
x as int
y as float
```

**Bad** (C-style):
```havel
(int)x
```

### Match: SIMPLE Version Only

**Good** (Swift/Kotlin style):
```havel
match value {
    1 => print("one")
    2 => print("two")
    _ => print("other")
}
```

**Defer** (Rust-style complexity):
```havel
match value {
    Some(x) if x > 0 => print(x),
    Point { x, y } => print(x + y),
    _ => print("other")
}
```

### Return: IMPLICIT OK

**Good** (Less boilerplate):
```havel
fn add(a, b) {
    a + b  // implicit return
}
```

**Also OK** (Explicit):
```havel
fn add(a, b) {
    return a + b
}
```

## Error Model Decision

### Option A: Result-Based (enables `?`)

```havel
fn read_file(path) -> Result<String, Error> {
    file = open(path)?  // propagates errors
    text = read(file)?
    Ok(text)
}
```

**Pros:**
- Explicit error handling
- No exceptions
- `?` operator works

**Cons:**
- Verbose
- Requires Result type

### Option B: Exceptions (no `?`)

```havel
fn read_file(path) {
    try {
        file = open(path)
        text = read(file)
    } catch err {
        print(err)
    }
}
```

**Pros:**
- Familiar
- Less boilerplate

**Cons:**
- Hidden control flow
- `?` operator useless

### Decision for v0.1

**Use Exceptions** - Simpler, familiar, works without Result type.

**Defer `?` operator** to v0.2 if we add Result type.

## Pipeline Operator

**Strong candidate for v0.1:**

```havel
value
  |> parse
  |> validate
  |> save
```

**Benefits:**
- Extremely readable
- Easy to implement (just function calls)
- Used in Elixir, F#, PowerShell

**Implementation:**
```cpp
// Just rewrites to nested calls
save(validate(parse(value)))
```

## Language Goals

### Target Use Cases

1. **Hotkey scripting** - Primary use case
2. **Automation workflows** - Secondary
3. **System integration** - Via modules

### Non-Goals (v0.1)

1. **Systems programming** - Not C/Rust
2. **Web development** - Not JS/Python
3. **Data science** - Not Python/R
4. **Game engines** - Not C#/Lua

### Success Metrics

v0.1 is successful if:

- [ ] Can write hotkey scripts
- [ ] Can automate workflows
- [ ] Can load modules
- [ ] **Ships and works**

## Implementation Order

### Phase 1: Core (NOW)

1. ✅ Variables
2. ✅ Functions
3. ✅ If/else
4. ✅ Loops
5. ✅ Calls
6. ✅ Structs (basic)
7. ✅ Modules (basic)

### Phase 2: Polish (NEXT)

1. `as` casts
2. Simple `match`
3. Implicit return
4. `=>` lambdas
5. Pipeline `|>`

### Phase 3: Advanced (LATER)

1. `?` operator (if Result added)
2. Complex pattern matching
3. Trait system
4. Advanced types

## Warning Signs

**STOP adding features if:**

1. Parser becomes complex
2. Tests take >1 hour to run
3. Can't explain feature in 1 sentence
4. Feature needs its own documentation
5. More than 2 weeks to implement

**Remember:**

> Lua shipped with 8 keywords.
> 
> Python shipped with minimal syntax.
> 
> Both succeeded by being **finished**, not perfect.

## Current Status

**v0.1 Progress:**

- [x] Variables
- [x] Functions
- [x] If/else
- [x] Loops
- [x] Calls
- [x] Structs (basic)
- [x] Modules (basic)
- [ ] `as` casts
- [ ] Simple `match`
- [ ] Implicit return
- [ ] `=>` lambdas
- [ ] Pipeline `|>`

**Build Status:** ✅ Compiles, runs

**Next:** Ship v0.1 with current features. Add polish in v0.2.
