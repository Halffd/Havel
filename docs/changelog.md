---
title: "Changelog"
description: "Version history and breaking changes."
---

# Changelog

All notable changes to Havel are documented here.

---

## 2026-11-08 - Launcher pipe fixes

### Fixed
- A pipeline stage that exits early (`... | head -1`) no longer kills the
  runtime: the write to the closed pipe raised SIGPIPE, whose default
  disposition terminated havel (the smoke suite reported
  `test_process_pipe (exit=-13)`, i.e. killed by signal 13). SIGPIPE is now
  ignored process-wide when nothing else installed a handler, and the
  write is additionally performed with SIGPIPE blocked on the calling
  thread, so the write returns EPIPE and the pipeline returns its result.
- A child that writes more than the pipe buffer (64 KiB on Linux) no longer
  deadlocks the runtime. The launcher used to wait for the child before
  draining its stdout, so the child blocked in write() while the parent
  blocked in waitpid(). Feeding stdin, draining stdout/stderr and reaping
  the child now happen in one poll loop, which also removes the mirror
  deadlock in the shell path (feeding all of stdin before draining stdout
  deadlocked children that write output before consuming their input).

---

## 2026-11-06 - Shell blocks inside scripts

### Added
- `sh { ... }` runs shell-shaped lines from a script: the block is translated
  by the same layer as the REPL's `:shell` mode (process.pipe pipelines,
  `cd`/`export`/`echo`, `>`/`>>`/`<` redirects, `[ -f x ]` tests) and the
  translated statements execute inline. `$name` / `${expr}` resolve to the
  enclosing scope's Havel variables - `notify-send "${w.title}"`,
  `sleep 1.5`, `kill w.pid`.
- `$(cmd)` works as an expression in Havel source (`out = $(echo hi)`); it
  compiles to `runResult(cmd)` and yields the same result object as
  backticks.
- `shell.translate` gained a script mode for the block translation; the REPL
  keeps bash environment semantics for unknown `$names`.

### Changed
- Unsupported bash syntax inside `sh { }` now fails the compile with the
  same suggestion the REPL prints (e.g. "use go { ... } for background
  execution") instead of a generic parse error.

---

## 2026-10-31 - REPL shell mode

### Added
- The REPL speaks shell: `:shell` toggles a bash-shaped input mode
  (`havel[sh]>`); `:havel` or a bare `exit` returns. Pipelines, redirects
  (`>`, `>>`, `<`), `$(...)`, globs, `$VAR`, `cd`, `export`, `echo`, `pwd`,
  `!!`, `!$`. Control flow stays Havel (`for f in *.log { ... }`); bash-only
  syntax (`[[ ]]`, `$@`, `(( ))`, arrays, `declare`, subshells, `&`, `2>&1`)
  is rejected with a suggestion.
- `process.pipe([cmd, ...])` / `process.pipe([[cmd, ...], ...])`: run a
  command or a pipeline with real pipes and direct exec (quoted arguments
  keep their spaces); optional second argument is stdin. Returns the last
  stage's result object.
- `fs.globArgs(pattern)`: bash-style argument globbing - the matches, or the
  literal pattern in a one-element array when nothing matches.
- `shell.translate(line)`: the shell-to-Havel translator as a callable
  (`{ok, source, error}`); this is what the REPL's `:shell` layer runs.
- `:save [file.hv]`: write the session out as a runnable script.
- Launcher: direct-exec runs feed and close the child's stdin (pipeline
  stages, and commands that read stdin no longer hang).

### Changed
- Shell mode prints like a shell: command stdout verbatim, stderr on the
  error stream, `[exit N]` after a failing command; `cd`/`export` are silent.

---

## 2026-10-30 - Bash-shaped CLI and shell pipelines

### Added
- `havel -c 'code'` runs inline code (`-E`/`--eval` remain as aliases)
- `havel -e 'expr'` evaluates one expression and prints its value
- Backtick captures and `$!` return a process **result object**: `.success`,
  `.exitCode`, `.stdout`, `.stderr`; `str()` of it is `.stdout`
- **Shell pipelines**: `` `cmd1` | `cmd2` `` feeds each stage's stdout into
  the next stage's stdin and evaluates to the last stage's result. Function
  stages chain on the result: `` `printf ...` | lines | grep "x" | count ``
- `fs.glob()` accepts absolute patterns (`/var/log/*.log`)
- `string.lines()`, `array.grep(pattern)`
- `on_error(fn(e) { ... })` - script-level uncaught-error handler
- `debug.on()` / `debug.off()` - language trace flags + debug log level
- Shebang scripts: `#!/usr/bin/env havel` + `chmod +x` runs the file directly
  (documented in getting-started/running-programs)

### Changed
- `-e` now means `--eval-print`; `--error` keeps the long form only
- `-c` now means `--code`; `--config` keeps the long form only
- Shell runs close the child's stdin after piping any input; a child that
  reads stdin (e.g. `cat` with no file) no longer hangs

### Notes
- `!(`cmd`)` truthiness is not implemented yet: the JIT truthiness bridge is
  bit-only (heap-less), so object truthiness cannot follow `.success` without
  a runtime-ABI change. Use `!(`cmd`.success)` for the same intent.
- `debug.on()`/`debug.off()` require the module form: bare `on`/`off` remain
  statement keywords, so use the exported `debug` object fields.

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
- `fs.glob` matches full path patterns through POSIX `glob()` (tilde, `{a,b}` braces, character classes), returns sorted results, and yields `[]` for no match or a missing directory; `fs.glob_many`, `fs.glob_dirs`, `fs.glob_files` were added

### Changed
- Build system: modes 0-16 with granular feature flags
- Self-hosted compiler is now default (`--self-hosted`)
- Hotkey syntax: only `=>` arrow form allowed
- Module system: Python-style, no `export` keyword
- String interpolation: only `"{var}"` and `$var` syntax
- Variable declaration: `val` for immutable, no `let`/`const`
- **Brightness setters now apply to all monitors** when no monitor index specified (was: first monitor only)
- Reading a property of `nil` now raises a catchable error (`Cannot read property 'x' of nil`); previously it silently returned `nil`. `?.` is unchanged and remains the null-safe form
- When-block exit branches now observe the post-transition hotkey `enabled`/`grab` state (`enabled` field reads are live, like `grab`)

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
- Range expressions (`..`, inclusive)
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