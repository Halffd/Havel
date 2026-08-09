---
title: "Running Programs"
description: "CLI flags, execution modes, and environment variables for the Havel runtime."
---

# Running Programs

## Synopsis

```bash
havel [options] <script.hv>
havel [options] --repl
havel [options] -E 'code'
havel [options] --build <script.hv>
havel [options] --test <dir>
havel [options] --lint <file.hv>
```

## Execution Modes

| Mode | Flag | Description |
|------|------|-------------|
| **Run script** | `havel script.hv` | Full features (hotkeys, IO, GUI) |
| **Minimal run** | `havel --run script.hv` | No hotkeys/IO/GUI, fast startup |
| **REPL** | `havel --repl` | Interactive session |
| **Run then REPL** | `havel --repl script.hv` | Execute script, then enter REPL |
| **Test directory** | `havel --test dir/` | Run all `.hv` files as tests |
| **Lint only** | `havel --lint file.hv` | Syntax/check only, no execution |
| **Build bytecode** | `havel --build file.hv -o out.hvc` | Compile to bytecode |

## Compiler Pipeline Selection

| Flag | Description |
|------|-------------|
| `--self-hosted` | Use pure Havel pipeline (default) |
| `--no-self-hosted` | Use C++ parser (legacy) |

The self-hosted pipeline is the default and uses the Havel-written lexer/parser/compiler. The C++ pipeline is retained for bootstrapping and debugging.

## Compilation Targets

| Flag | Target | Description |
|------|--------|-------------|
| `--target interpret` | Interpreter | Bytecode VM (default) |
| `--target jit` | LLVM JIT | Tiered JIT compilation |
| `--target aot` | AOT native | Ahead-of-time native binary |
| `--target asm` | Assembly | Emit `.s` file |
| `--target ir` | LLVM IR | Emit `.ll` file |
| `--target elf` | ELF binary | Native executable |
| `--target wasm` | WebAssembly | WASM output (experimental) |
| `--target bin` | Raw binary | Raw machine code |

### AOT Artifact Emission

```bash
# Emit specific artifacts
havel --emit-llvm script.hv -o script.ll
havel --emit-asm script.hv -o script.s
havel --emit-obj script.hv -o script.o

# Full AOT pipeline (all artifacts)
havel --full-aot --target aot script.hv -o script
```

### Target Options

```bash
--os native|linux|windows|macos|wasm    # Target OS
--arch TRIPLE                          # Target triple (e.g., x86_64-linux-gnu)
--syntax att|intel                     # Assembly syntax (default: att)
```

## Debug Flags

| Flag | Description |
|------|-------------|
| `-d`, `--debug` | Enable all debug output |
| `-dp`, `--debug-parser` | Parser debug (token stream, AST) |
| `-da`, `--debug-ast` | AST dump |
| `-dl`, `--debug-lexer` | Lexer debug (tokens) |
| `-dbc`, `--debug-bytecode` | Bytecode disassembly |
| `-dgc`, `--debug-gc` | GC debug (allocations, collections) |
| `-de`, `--debug-engine` | VM engine debug |
| `-dio`, `--debug-io` | IO subsystem debug |
| `-dhk`, `--debug-hotkeys` | Hotkey system debug |
| `--debug-jit` | Print LLVM IR and assembly for JIT |

## Error Handling

| Flag | Description |
|------|-------------|
| `-e`, `--error` | Stop on first error/warning |
| `--log-level LEVEL` | Log level: `debug`, `info`, `warning`, `error`, `fatal` |
| `--log-file PATH` | Write logs to file |
| `--log-no-color` | Disable ANSI colors |
| `--log-origin-filter FILTER` | Filter by origin: `category[:subsystem][:priority]` |

## Minimal Mode

```bash
havel --minimal script.hv
# or
havel -m script.hv
```

Disables: hotkey system, IO (keyboard/mouse), GUI backends, window management. Useful for pure computation scripts, testing, or headless environments.

## VM Configuration

```bash
--heap-max <bytes>           # Max heap size
--gc-budget <n>              # GC work per tick
--gc-incremental             # Enable incremental GC
--gc-stop-the-world          # Force stop-the-world GC
--gc-full-interval <n>       # Full GC interval
--gc-promotion-age <n>       # Generational promotion age
--max-call-depth <n>         # Max call stack depth
--max-instructions <n>       # Max instructions per execution
--tick-instructions <n>      # Instructions per scheduler tick
--hotkey-tick-instructions <n> # Instructions for hotkey goroutines
--tier1-threshold <n>        # JIT tier 1 threshold (hotness)
--tier2-threshold <n>        # JIT tier 2 threshold
--tiering                    # Enable tiered JIT
--timer-interval <ms>        # Timer resolution
```

## Environment Variables

All CLI flags have environment variable equivalents:

| Variable | CLI Flag |
|----------|----------|
| `HAVEL_LOG_LEVEL` | `--log-level` |
| `HAVEL_LOG_FILE` | `--log-file` |
| `HAVEL_LOG_NO_COLOR` | `--log-no-color` |
| `HAVEL_LOG_ORIGIN_FILTER` | `--log-origin-filter` |

## Examples

```bash
# Run script with JIT
havel --target jit script.hv

# Run with full debugging
havel -d -dl -dp -da --debug-bytecode --debug-jit script.hv

# Compile to native binary (requires LLVM)
havel --target aot --full-aot script.hv -o myapp

# Run as headless server (no GUI)
havel --minimal server.hv

# Lint and exit on first error
havel --lint script.hv -e

# REPL with debug
havel --repl -d

# Benchmark mode (no hotkeys, max performance)
havel --minimal --target jit benchmark.hv
```

## Exit Codes

| Code | Meaning |
|------|---------|
| `0` | Success |
| `1` | Compile error |
| `2` | Runtime error (uncaught throw) |
| `3` | Argument/usage error |
| `4` | System error (missing deps, etc.) |

---

**Previous:** [Basic Syntax](/getting-started/basic-syntax)
**Next:** [Language Reference →](/language/lexical)