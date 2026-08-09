---
title: "Code Style"
description: "Coding standards from AGENTS.md applied to this codebase."
---

# Code Style

Based on `AGENTS.md` rules. These are enforced in code review.

---

## General Rules

1. **No stubs** — Every function has a real implementation
2. **No "would"** — Write real code or say you don't know
3. **No parallel implementations** — One file per feature
4. **No scope creep** — Do exactly what was asked
5. **No documentation of broken things** — Fix it instead
6. **Do not reimplement existing things**
7. **Verify before marking done** — Show output or test result
8. **Fix one thing, confirm it works, then move to next**
9. **No abstraction layers over broken abstraction layers**
10. **No hardcoded placeholder values** — Real values from real APIs

---

## C++ Style

### Language Standard

- C++23 required
- Clang forced (`CC=clang CXX=clang++`)

### Memory Safety

| ❌ Avoid | ✅ Use |
|----------|--------|
| Raw `new`/`delete` | `std::make_unique`/`std::make_shared` |
| Raw pointers | `std::unique_ptr`, `std::shared_ptr`, references |
| C-style casts | `static_cast`, `reinterpret_cast` |
| `malloc`/`free` | `std::vector`, `std::string`, RAII |

### Concurrency

| ❌ Avoid | ✅ Use |
|----------|--------|
| Manual `lock()`/`unlock()` | `std::lock_guard`, `std::scoped_lock` |
| Double-checked locking | `std::call_once` / `std::once_flag` |
| `std::mutex` for read-heavy | `std::shared_mutex` |
| Raw atomics without order | `std::memory_order_acquire/release` |

### Performance

| ❌ Avoid | ✅ Use |
|----------|--------|
| Unnecessary copies | `std::string_view`, `const&` |
| Small string no reserve | `s.reserve(n)` |
| Lock contention | Atomic operations where possible |
| False sharing | `alignas(64)` for hot data |

### Error Handling

- Prefer explicit failures over silent ones
- Produce useful diagnostics
- Silent failures are bugs

### Undefined Behavior

| ❌ Avoid | ✅ Use |
|----------|--------|
| Signed integer overflow | Unsigned or checked arithmetic |
| Null dereference | Check before dereference |
| Uninitialized reads | `int x{};` (value initialization) |
| Invalidated iterators/references | Copy values, not references |

---

## Havel Language Style

### Syntax (Enforced by Compiler)

| ❌ Rejected | ✅ Use |
|-------------|--------|
| Semicolons at line ends | Newline separation only |
| `let` for immutable | `val` or uppercase |
| `const` keyword | `val` |
| `export` keyword | Not needed (all exported) |
| `hotkey "..."` | `^+F1 => { }` |
| `hotkey.register()` | `F1 => { }` |
| Explicit `return` | Implicit (last expression) |
| `this` | `@` (ruby-style) |
| `static` | `@@` |
| `#` comments | `//` comments |
| `+`/`,`/`.` for strings | `"{var}"` or `$var` interpolation |
| `and`/`or` | `&&`/`||`/`!` |
| `impl X for Y` | `impl X for Y` (with colons) |

### Naming Conventions

| Type | Convention |
|------|------------|
| Variables/functions | `snake_case` |
| Constants/immutable | `UPPER_SNAKE_CASE` |
| Types (struct/class/enum) | `PascalCase` |
| Modules/files | `snake_case.hv` |
| Private (convention) | `_leading_underscore` |

### Module System

- Python-style: every top-level function, variable, class is exported
- No `export` keyword
- Prefix with `_` for "private by convention"
- Consumer: `use mymodule` or `use { fn } from "mymodule"`

---

## Commit Rules

1. No capslock in commit messages
2. No emoji in commits
3. Commit messages are for humans, not marketing
4. No hype words (synergy, paradigm, revolutionary, ecosystem, zero-cost abstraction)
5. No emoji in code comments unless the bug is genuinely funny
6. Small commits, descriptive messages
7. Commit after every completed logical task

---

## Git Rules

- Never `git checkout`, `git reset`, `git clean` unless explicitly instructed
- Never discard work because build broke — fix the build
- Commit every completed task before starting unrelated work
- Resolve merge conflicts instead of discarding work

---

## Debugging

- Prefer understanding over guessing
- Use: debugger, stack traces, sanitizers, targeted logging
- Do not flood codebase with debug prints
- Debug output should answer a specific question
- Remove temporary debugging before committing

---

## Failure Recovery

If change breaks build:
1. Read the error
2. Fix your change
3. Rebuild
4. Repeat until build succeeds

Do not abandon implementation by reverting.

---

## Blocking Bugs

If a pre-existing bug prevents testing, it is **in scope**. Fix it first.

Never classify blocking issues as:
- pre-existing
- unrelated
- out of scope

---

**Previous:** [Testing](/contributing/testing)
**Next:** [Adding Host Functions →](/contributing/adding-host-functions)