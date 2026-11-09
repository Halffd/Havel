---
title: "Havel Documentation"
description: "Havel — a systems scripting language for desktop automation, window management, and hotkey-driven workflows."
---

# Havel

**Havel** is one language for the automation scripts you already write. It runs
your hotkeys, drives your windows on X11 and wlroots Wayland, calls the OS
directly instead of shelling out to six tools, and when it fails you get one
stack trace instead of six. Fast enough for scripts that run all day; not
trying to be fast where speed is not the problem. On GNOME Wayland, the
[support matrix](#platform-support) tells you exactly which parts are missing.

The compiler pipeline is C++ (lexer, parser, bytecode compiler) — fast, boring,
and what ships today. A self-hosted compiler, written in Havel itself, is in
transition ([95% of the repo's own scripts lint through
it](/plans/self-hosted-transition)); it becomes the default once it has run the
full suite for a release cycle without regressing — not before.

## Quick Start

```bash
# Build (debug, no LLVM — fastest iteration)
./build.sh build

# Run a script
./build.sh run script.hv

# Fast test subset (seconds), or the full pre-merge gate
./build.sh test --kind quick
./build.sh test --kind gate

# Which binary is current, across all checkouts on this machine
./build.sh which
./build.sh status
```

```hv
// hello.hv
fn greet(name) {
    "Hello, ${name}!"
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
| **Compiler pipeline** | C++ lexer, parser, and bytecode compiler (what ships); self-hosted compiler in transition at 95% lint coverage, default at v2 once proven |
| **Tiered JIT** | Interpreter-first; Cranelift / LLVM tiers kick in for long-running scripts — daemons, batch, the self-hosted compiler itself. No performance cliff when the script *is* the workload; your hotkey script never notices |
| **Hotkey system** | Global X11 hotkeys with modifiers (`^`, `+`, `!`, `#`), conditional grabbing, policies |
| **Window management** | Focus, move, resize, enumerate, match by title/class/pid |
| **Concurrency** | `go` blocks, channels, prefix `<-` await, OS threads with actor messaging |
| **FFI** | `ffi.open`, `ffi.sym`, `ffi.call`, `ffi.cdef` for C library binding |
| **LSP/DAP** | Language Server Protocol and Debug Adapter Protocol support |
| **Cross-platform** | Linux X11 (primary), headless mode for servers |

## Platform support

Havel on X11 is complete. Havel on Wayland is complete on wlroots
compositors, protocol-limited on GNOME and KDE — and the matrix says exactly
what works where, so the gaps are obvious before you hit them instead of
surprising you mid-script.

| Feature | X11 | Wayland / Sway | Wayland / Hyprland | Wayland / GNOME | Wayland / KDE |
|---|---|---|---|---|---|
| Global hotkeys | ✅ | ✅ | ✅ | ⚠️ portal | ⚠️ KWin script |
| Window move / resize | ✅ | ✅ | ✅ | ❌ | ⚠️ |
| Focus / activate | ✅ | ✅ | ✅ | ⚠️ | ⚠️ |
| Screen capture | ✅ | ✅ | ✅ | ⚠️ portal | ⚠️ portal |
| Input synthesis | ✅ | ✅ | ✅ | ❌ | ❌ |

The "WM-aware with graceful degradation" story is deliberate: where the
compositor exposes the protocol (`wlr-foreign-toplevel-management`,
`wlr-virtual-keyboard`/`-pointer`, portals), the same script works; where it
does not, the call fails with a named error, not silence.

## When it breaks

One stack trace, one file, one place to look. A Havel failure names the
source line, the function, and the hotkey that triggered it:

```
error: cannot read clipboard: no X11 selection owner
  at clipboard.get()  [src/stdlib/ClipboardModule.cpp:142]
  at capture_selection()  [scripts/hotkeys.hv:47]
  at ^+s handler  [scripts/hotkeys.hv:34]
  at hotkey dispatch  [hotkey '^+s', key=KEY_S, mods=Ctrl+Shift]
```

Compare the glue-stack version of the same failure — three tools, none of them
yours, and the actual cause is in the last frame of the third one:

```
Error: Command failed: xclip -o -selection clipboard
xclip: Error: Can't open display: (null)
    at checkExecSyncError (node:child_process:922:11)
    at Object.execFileSync (node:child_process:363:17)
    at ...
    at processTicksAndRejections (node:internal/process/task_queues:95:5)
```

The difference is the pitch: read one trace in one language, or read a trace,
then a shell script, then a manpage, then guess which of the three failed.

## Navigation

| Section | Description |
|---------|-------------|
| [Getting Started](/getting-started/installation) | Installation, first script, CLI reference |
| [Language Reference](/language/lexical) | Complete syntax and semantics |
| [Standard Library](/stdlib/math) | All built-in modules with signatures |
| [Compiler Internals](/compiler/bytecode) | Bytecode format, JIT, GC, embedding |
| [Guides](/guides/first-hotkey) | Practical tutorials and how-tos |
| [API Reference](/reference/host-functions) | Host functions, FFI, error handling |
| [Contributing](/contributing/directory-structure) | Layout, tests, code style, extending |

---

**Next:** [Installation →](/getting-started/installation)