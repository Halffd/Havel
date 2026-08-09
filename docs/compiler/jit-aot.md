---
title: "JIT/AOT Compilation"
description: "Tiered compilation: interpreter, LLVM JIT, AOT native, and artifact emission."
---

# JIT/AOT Compilation

## Overview

Havel supports three execution tiers:

```
Source → Bytecode → [Interpreter] → [Tier 1 JIT] → [Tier 2 JIT] → [AOT Native]
```

| Tier | Description | Trigger |
|------|-------------|---------|
| **Interpreter** | Bytecode dispatch loop | Default |
| **Tier 1 JIT** | LLVM ORC JIT (fast compile) | Hot function (> threshold) |
| **Tier 2 JIT** | Optimized LLVM JIT | Very hot function |
| **AOT** | Ahead-of-time native binary | `--target aot` |

---

## Requirements

- LLVM development libraries
- Build modes with LLVM: `0` (debug), `5` (release)
- CMake: `ENABLE_LLVM=ON` (auto-enabled if LLVM found)

---

## JIT Compilation

### Enable JIT

```bash
# Run with JIT (requires mode 0 or 5)
havel --target jit script.hv

# With JIT debugging
havel --target jit --debug-jit script.hv
```

### Tier Thresholds

```bash
--tier1-threshold <n>   # Tier 1 trigger (default: 1000)
--tier2-threshold <n>   # Tier 2 trigger (default: 10000)
--tiering               # Enable tiered compilation
```

### JIT Internals

**Source**: `src/havel-lang/compiler/llvm/`, `BytecodeOrcJIT.cpp`

1. Hot function detected via execution counter
2. Bytecode → LLVM IR via `BytecodeOrcJIT::compileFunction()`
3. LLVM optimizes (inline, constant fold, etc.)
4. ORC JIT emits native code
5. VM patches call site to native entry point

---

## AOT Compilation

### Native Binary

```bash
# Compile to native executable
havel --target aot script.hv -o myapp

# Full AOT (all artifacts)
havel --full-aot --target aot script.hv -o myapp
```

### Artifact Emission

```bash
# LLVM IR
havel --emit-llvm script.hv -o script.ll

# Assembly
havel --emit-asm script.hv -o script.s

# Object file
havel --emit-obj script.hv -o script.o
```

### Target Options

```bash
--os native|linux|windows|macos|wasm   # Target OS
--arch TRIPLE                          # e.g., x86_64-linux-gnu
--syntax att|intel                     # Assembly syntax
```

---

## ELF Binary Output

```bash
havel --target elf script.hv -o script.elf
```

Produces a standalone ELF executable with embedded bytecode and runtime.

---

## WebAssembly (Experimental)

```bash
havel --target wasm script.hv -o script.wasm
```

Requires `wasm32` target in LLVM.

---

## Raw Binary

```bash
havel --target bin script.hv -o script.bin
```

Raw machine code, no headers. For embedded/firmware use.

---

## Pipeline Diagram

```
Source (.hv)
    |
    v
+----------+     +----------+     +----------+
|  Lexer   | --> |  Parser  | --> | Semantic |
+----------+     +----------+     +----------+
    |                                   |
    v                                   v
+------------------+            +------------------+
| Bytecode Compiler|            |  Type Annotations|
|   (IR + Opt)     |            |  (for JIT opt)   |
+------------------+            +------------------+
    |
    v
+------------------+
|     VM / JIT     |
|  (Interpreter)   |
|       |          |
|       v          |
|  [Tier 1 JIT]    |  --hot function-->
|       |          |
|       v          |
|  [Tier 2 JIT]    |  --very hot-->
|       |          |
|       v          |
|    [AOT]         |  --pre-compile-->
+------------------+
```

---

## Performance Notes

| Mode | Startup | Peak Throughput | Use Case |
|------|---------|-----------------|----------|
| Interpret | Fast | Baseline | Scripts, REPL |
| JIT | Medium | 2-5x | Long-running, hot loops |
| AOT | Slow (compile) | 3-10x | Distribution, embedded |

---

**Previous:** [Bytecode Format](/compiler/bytecode)
**Next:** [Garbage Collection →](/compiler/gc)