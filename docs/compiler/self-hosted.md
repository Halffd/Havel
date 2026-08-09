---
title: "Self-Hosted vs Bootstrap"
description: "Self-hosted compiler pipeline vs C++ bootstrap pipeline."
---

# Self-Hosted vs Bootstrap

## Overview

Havel has two compiler pipelines:

| Pipeline | Language | Location | Purpose |
|----------|----------|----------|---------|
| **Bootstrap (C++)** | C++ | `src/havel-lang/lexer/`, `parser/`, `compiler/core/` | Bootstrapping, debugging, fallback |
| **Self-Hosted** | Havel | `modules/lang/` | Production, development, dogfooding |

The self-hosted pipeline is the **default** (`--self-hosted` flag). The C++ pipeline is legacy (`--no-self-hosted`).

---

## Self-Hosted Pipeline

### Structure

```
modules/lang/
  lexer.hv        # Lexical analyzer
  parser.hv       # Parser (Pratt/precedence climbing)
  ast.hv          # AST node definitions
  semantic.hv     # Type checking, symbol resolution
  bytecode.hv     # Bytecode IR generation
  optimizer.hv    # Optimization passes
  emitter.hv      # Bytecode emission
  pipeline.hv     # Orchestration
```

### Build Process

```bash
# Build self-hosted modules (runs after main build)
./emit_pipeline.sh ./build-debug/havel
```

This compiles `modules/lang/*.hv` to `out/modules/lang/*.hvc` (bytecode).

### Loading

At runtime, the VM loads self-hosted modules from `out/modules/lang/`:

```cpp
// In Pipeline::compileAndRun()
if (use_self_hosted) {
    loadSelfHostedModules(vm);  // Loads .hvc files
}
```

### Advantages

1. **Dogfooding**: Compiler written in the language it compiles
2. **Faster iteration**: Modify `.hv` files, rebuild with `emit_pipeline.sh`
3. **Full language features**: Can use all Havel features in compiler
4. **Single source of truth**: Language spec = compiler implementation

---

## Bootstrap Pipeline (C++)

### Structure

```
src/havel-lang/
  lexer/Lexer.cpp         # ~2,145 lines
  parser/Parser.cpp       # ~11,022 lines
  compiler/core/
    ByteCompiler.cpp      # ~7,867 lines
    ByteCompiler.hpp
    BytecodeIR.hpp
    Pipeline.cpp
    CompilerUtils.cpp
  semantic/               # Type checker, symbol resolution
```

### When Used

- `--no-self-hosted` flag
- Bootstrapping new platforms
- Debugging compiler internals
- When self-hosted pipeline has bugs

### Key Differences

| Aspect | Self-Hosted | Bootstrap (C++) |
|--------|-------------|-----------------|
| Language | Havel | C++ |
| Rebuild time | ~2s (bytecode) | ~30s (C++ compile) |
| Features | Full Havel | C++17 |
| Debugging | Havel-level | GDB/LLDB |
| Parser algorithm | Same (Pratt) | Same (Pratt) |

---

## Pipeline Orchestration

Both pipelines share the same `Pipeline` interface:

```cpp
// src/havel-lang/compiler/core/Pipeline.cpp
PipelineOptions opts;
opts.host_functions = bridge.collectFunctions();
opts.host_globals = bridge.collectGlobals();
opts.use_self_hosted = true;  // or false

auto result = Pipeline::compileAndRun(source, "script.hv", opts);
```

### Self-Hosted Path

```cpp
if (opts.use_self_hosted) {
    // 1. Load self-hosted modules
    vm.loadModule("lang/lexer");
    vm.loadModule("lang/parser");
    vm.loadModule("lang/semantic");
    vm.loadModule("lang/bytecode");
    vm.loadModule("lang/optimizer");
    vm.loadModule("lang/emitter");
    vm.loadModule("lang/pipeline");
    
    // 2. Call pipeline.run(source)
    return vm.callGlobal("pipeline.run", { source, filename, opts });
}
```

### Bootstrap Path

```cpp
else {
    // Direct C++ pipeline
    Lexer lexer(source);
    Parser parser(lexer.tokens());
    AST ast = parser.parse();
    SemanticAnalyzer analyzer;
    TypedAST tast = analyzer.analyze(ast);
    BytecodeCompiler compiler;
    BytecodeChunk chunk = compiler.compile(tast);
    return vm.execute(chunk);
}
```

---

## Switching Pipelines

```bash
# Self-hosted (default)
havel script.hv
havel --self-hosted script.hv

# Bootstrap (C++)
havel --no-self-hosted script.hv
```

---

## Development Workflow

### Modifying Self-Hosted Compiler

```bash
# 1. Edit Havel source
vim modules/lang/parser.hv

# 2. Rebuild pipeline
./emit_pipeline.sh ./build-debug/havel

# 3. Test
./build-debug/havel script.hv
```

### Modifying Bootstrap Compiler

```bash
# 1. Edit C++ source
vim src/havel-lang/parser/Parser.cpp

# 2. Rebuild
./build.sh 6 build

# 3. Test with bootstrap
./build-debug/havel --no-self-hosted script.hv
```

---

## Debugging

```bash
# Debug self-hosted pipeline
havel -d -dl -dp -da --debug-bytecode script.hv

# Debug bootstrap pipeline
havel --no-self-hosted -d -dl -dp -da script.hv

# Compare outputs
havel --self-hosted script.hv > out1.txt
havel --no-self-hosted script.hv > out2.txt
diff out1.txt out2.txt
```

---

## Future: Full Self-Hosting

Goal: Move all compiler stages to self-hosted, keep only VM and host modules in C++.

| Stage | Current | Target |
|-------|---------|--------|
| Lexer | Havel | Havel ✓ |
| Parser | Havel | Havel ✓ |
| Semantic | Havel | Havel ✓ |
| Bytecode IR | Havel | Havel ✓ |
| Optimizer | Havel | Havel ✓ |
| Emitter | Havel | Havel ✓ |
| Pipeline | Havel | Havel ✓ |
| VM | C++ | C++ (keep) |
| Host modules | C++ | C++ (keep) |
| GC | C++ | C++ (keep) |

---

**Previous:** [Garbage Collection](/compiler/gc)
**Next:** [C++ Embedding API →](/compiler/cpp-api)