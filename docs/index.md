---
title: "Havel Documentation"
description: "Havel — a systems scripting language for desktop automation, window management, and hotkey-driven workflows."
---

# Havel

**Havel** is a systems scripting language for desktop automation, window management, and hotkey-driven workflows. It combines a high-performance bytecode VM with a self-hosted compiler, optional LLVM JIT/AOT compilation, and a rich standard library for X11, window manipulation, process control, and system interaction.

## Quick Start

```bash
# Build (debug, no LLVM — fastest iteration)
./build.sh 6 build

# Run a script
./build-debug/havel script.hv

# Start REPL
./build-debug/havel --repl
```

```hv
// hello.hv
fn greet(name) {
    "Hello, {name}!"
}

print(greet("world"))

// Hotkey: Ctrl+Shift+A prints a message
^+A => { print("hotkey triggered!") }
```

```bash
./build-debug/havel hello.hv
```

## Feature Highlights

| Feature | Description |
|---------|-------------|
| **Bytecode VM** | Stack-based VM with cooperative goroutines, channels, and coroutines |
| **Self-hosted compiler** | Lexer, parser, and bytecode compiler written in Havel itself |
| **LLVM JIT/AOT** | Optional tiered compilation: interpret → JIT → native (modes 0, 5) |
| **Hotkey system** | Global X11 hotkeys with modifiers (`^`, `+`, `!`, `#`), conditional grabbing, policies |
| **Window management** | Focus, move, resize, enumerate, match by title/class/pid |
| **Concurrency** | `go` blocks, `channel`, `await`, `<-`, `select`, OS threads with actor messaging |
| **FFI** | `ffi.open`, `ffi.sym`, `ffi.call`, `ffi.cdef` for C library binding |
| **LSP/DAP** | Language Server Protocol and Debug Adapter Protocol support |
| **Cross-platform** | Linux X11 (primary), headless mode for servers |

## Navigation

| Section | Description |
|---------|-------------|
| [Getting Started](/getting-started/installation) | Installation, first script, CLI reference |
| [Language Reference](/language/lexical) | Complete syntax and semantics |
| [Standard Library](/stdlib/math) | All built-in modules with signatures |
| [Compiler Internals](/compiler/bytecode) | Bytecode format, JIT, GC, embedding |
| [Guides](/guides/first-hotkey) | Practical tutorials and how-tos |
| [API Reference](/reference/host-functions) | Host functions, FFI, error handling |
| [Contributing](/contributing/build-system) | Build, test, code style, extending |

---

**Next:** [Installation →](/getting-started/installation)