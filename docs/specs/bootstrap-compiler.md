# Spec: bootstrap-compiler

Scope: repo

# Bootstrap Minimum — C++ Deletion Criteria

## Permanent C++ (never delete)
- VM core (stack, dispatch, GC roots)
- GC (mark/sweep/compact)
- evdev / X11 / window management
- FFI / LLVM JIT
- Fiber scheduler
- Host module system + stdlib modules (HotkeyModule, WindowModule, etc.)

## Bootstrap-only C++ (READ-ONLY, no more edits, no splits, no refactors)
These exist only to compile the first self-hosted Havel script.
Once the self-hosted compiler passes its tests, delete them.

| C++ file | Delete condition | Self-hosted replacement |
|----------|-----------------|------------------------|
| `Parser.cpp` + split files | `pratt.hv` can parse itself (produces valid AST for all of `modules/lang/`) | `modules/lang/pratt.hv` |
| `Lexer.cpp` | `lexer.hv` can tokenize itself (produces identical token stream as C++ lexer for all of `modules/lang/`) | `modules/lang/lexer.hv` |
| `ByteCompiler.cpp` + split files | `emitter.hv` can emit correct bytecode IR for all of `modules/lang/` | `modules/lang/emitter.hv` |
| `SemanticAnalyzer.cpp` | `types.hv` passes type-checking on all of `modules/lang/` | `modules/lang/types.hv` |

## Self-hosted compiler (`modules/lang/`)
| File | Lines | Role |
|------|-------|------|
| `lexer.hv` | 1519 | Tokenizer |
| `pratt.hv` | 3134 | Pratt parser |
| `ast.hv` | 780 | AST node definitions |
| `emitter.hv` | 1970 | AST → bytecode emitter |
| `optimizer.hv` | 652 | Bytecode optimizer |
| `scope.hv` | 134 | Scope/variable resolution |
| `types.hv` | 398 | Type system |
| `error.hv` | 385 | Error reporting |
| `debug.hv` | 539 | Debug/tracing |
| `loader.hv` | 415 | Module loader |
| `launcher.hv` | 501 | Compiler driver |

## Deletion sequence
1. **Lexer self-test**: `lexer.hv` tokenizes `lexer.hv` → delete `Lexer.cpp`
2. **Parser self-test**: `pratt.hv` parses `pratt.hv` → delete `Parser.cpp` (and all split files)
3. **Emitter self-test**: `emitter.hv` emits bytecode for `emitter.hv` → delete `ByteCompiler.cpp` (and all split files)
4. **Full self-compilation**: `modules/lang/` compiles itself end-to-end → delete remaining bootstrap C++ (semantic analyzer, etc.)

## Rules
- NO more C++ refactoring, splitting, or editing of bootstrap files
- NO new C++ files in parser/compiler/lexer directories
- ALL new compiler work happens in `modules/lang/*.hv`
- VM, GC, FFI, host modules stay in C++ permanently