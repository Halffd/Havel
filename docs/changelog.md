---
title: "Changelog"
description: "Version history and breaking changes."
---

# Changelog

All notable changes to Havel are documented here.

---

## [Unreleased]

### Added
- Complete documentation system (this site)
- Self-hosted compiler pipeline in `modules/lang/`
- LLVM JIT/AOT compilation support
- Language Server Protocol (LSP) server
- Debug Adapter Protocol (DAP) support
- FFI module with libffi integration
- WebSocket client/server support
- HTTP client module
- SQLite module
- Async utilities module (`modules/app/async.hv`)
- **Brightness module: shadow lift control** (enhance contrast in dark areas without blowing out highlights)
- **Brightness panel UI** (`scripts/app/brightness_panel.hv`) - full GUI with sliders, monitor selector, presets
- **All brightness setters now apply to all monitors** when no monitor index is provided
- **Python-style tracebacks** for runtime errors with full call stack, file/line/column, and function names

### Changed
- Build system: modes 0-16 with granular feature flags
- Self-hosted compiler is now default (`--self-hosted`)
- Hotkey syntax: only `=>` arrow form allowed
- Module system: Python-style, no `export` keyword
- String interpolation: only `"{var}"` and `$var` syntax
- Variable declaration: `val` for immutable, no `let`/`const`
- **Brightness setters now apply to all monitors** when no monitor index specified (was: first monitor only)

### Deprecated
- `let` for immutable bindings (use `val`)
- `const` keyword (use `val`)
- `hotkey "..."` syntax
- `hotkey.register()` function
- Explicit `return` statements
- `this` keyword (use `@`)
- `static` keyword (use `@@`)
- `#` comments (use `//`)

### Removed
- Legacy `host_global_names` set
- Block comments (`/* */`)
- Python-style comparison chaining (`a < b < c`)
- `and`/`or` keywords (use `&&`/`||`/`!`)

### Fixed
- **Segfault when calling non-callable values** (null, int, string, object) - now throws descriptive runtime error with Python-style traceback
- GC finalizer ordering
- Hotkey policy race conditions
- Conditional hotkey re-evaluation thrashing
- JIT tier transition crashes
- FFI `cdef` constant parsing

---

## [0.9.0] - 2024-01-15

### Added
- Self-hosted compiler (lexer, parser, semantic, bytecode)
- Tiered JIT compilation (interpreter → tier1 → tier2)
- AOT compilation targets: `elf`, `asm`, `ir`, `bin`, `wasm`
- Generational garbage collector
- Coroutines with `yield`/`resume`
- WaitGroups for synchronization
- `select` for channel multiplexing
- Pattern matching in `match` expressions
- Traits and protocols with `impl`
- Classes with inheritance (`class Child : Parent`)
- Structs with operator overloading
- Enums with payloads
- Pipeline operator (`|>`, `<|`)
- Nullish coalescing (`??`)
- Optional chaining (`?.`)
- Range expressions (`..`, `..=`)
- Destructuring assignment
- Module system (`use`, `import`, `from`, `as`)

### Changed
- Bytecode format v2 (new instruction set)
- VM: stack-based with register frames
- Scheduler: three priority queues
- Hotkeys: persistent goroutines
- Conditional hotkeys: event-driven (no polling)

### Fixed
- Memory leaks in closure capture
- Channel deadlock on close
- Thread join race condition
- GC finalizer double-run

---

## [0.8.0] - 2023-10-01

### Added
- Hotkey system with modifiers (`^`, `+`, `!`, `#`)
- Window management (focus, move, resize, enumerate)
- Brightness control (DDCCI, gamma, temperature)
- Clipboard watch/history
- Screenshot capture
- Mouse/keyboard injection
- Mode system with priorities
- DSL input commands (`:500`, `"{Enter}"`, `w(x,y)`)

### Changed
- Configuration: Hyprland-style sections
- Config saves debounced (500ms)

---

## [0.7.0] - 2023-06-15

### Added
- Lua replacement: Havel scripting language
- Basic syntax: functions, control flow, data structures
- Standard library: math, string, array, object, fs, process
- REPL with `.help`, `.vars`, `.fns` commands
- Bytecode VM with basic instructions
- Host module system for C++ extensions

---

## [0.6.0] - 2023-03-01

### Added
- X11 hotkey registration
- Basic window operations
- Configuration file support

---

## Breaking Changes Summary

| Version | Change | Migration |
|---------|--------|-----------|
| 0.9.0 | `let`/`const` → `val` | Replace `let x = 1` with `val x = 1` |
| 0.9.0 | `hotkey "..."` → `=>` | Replace `hotkey "F1" {}` with `F1 => {}` |
| 0.9.0 | `this` → `@` | Replace `this.x` with `@x` |
| 0.9.0 | `static` → `@@` | Replace `static x` with `@@x` |
| 0.9.0 | `and`/`or` → `&&`/`||`/`!` | Replace `and` with `&&` |
| 0.9.0 | `export` removed | Remove `export` keywords |
| 0.9.0 | String concat → interpolation | Replace `a + b` with `"{a}{b}"` |
| 0.9.0 | `#` comments → `//` | Replace `# comment` with `// comment` |

---

## Migration Guide

See [Migrating from Python/JavaScript/Lua](/guides/migration) for detailed migration instructions.

---

**Previous:** [Extending Self-Hosted Compiler](/contributing/extending-self-hosted)
**Next:** [Documentation Home](/)