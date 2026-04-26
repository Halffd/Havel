```markdown
# AGENTS.md

## Build System

Primary build: `./build.sh [mode] [command]`

| Mode | Type | Tests | Havel Lang | LLVM | Build Dir |
|------|------|-------|------------|------|-----------|
| 0 | Debug | ✓ | ✓ | ✓ | build-debug |
| 5 | Release | ✓ | ✓ | ✓ | build-release |
| 6 | Debug | ✓ | ✓ | ✗ | build-debug (default) |
| 9 | Release | ✓ | ✓ | ✗ | build-release |

Common commands:
- `./build.sh 5 build` - Full release with LLVM
- `./build.sh 6 build` - Fast debug (no LLVM)
- `./build.sh test` - Run all tests
- `./build.sh detect` - Show system dependencies

## Executables

| Binary | Purpose |
|--------|---------|
| `build-debug/havel` | Main application |
| `build-debug/havel-lsp` | Language Server Protocol |
| `build-debug/havel-bytecode-smoke` | Bytecode smoke test (Debug only) |

Run Havel scripts: `./build-debug/havel script.hv`

## Architecture

### Directory Structure

**Core Window Management** (`src/`)
- `core/` - Core application logic, automation, I/O
- `window/` - X11/Wayland window management
- `utils/`, `fs/`, `process/`, `media/` - Utility modules
- `host/` - Host module system for extensible functionality
- `tests/` - Unit test utilities

**Havel Language Implementation** (`src/havel-lang/`)
- `compiler/` - Bytecode compiler (pipeline, IR generation, optimization)
  - `vm/` - Stack-based virtual machine
  - `gc/` - Garbage collector
  - `core/` - Builtin values and types
- `lexer/` - Lexical analysis
- `parser/` - Syntax parsing (expression, statement, pattern parsers)
- `semantic/` - Type checking, symbol table, module resolution
- `runtime/` - VM execution engine
- `stdlib/` - Standard library modules (Math, String, Array, etc.)
- `errors/` - Error reporting and diagnostics
- `lsp/` - Language Server Protocol support
- `llvm/` - LLVM JIT integration (optional)

### Compiler Pipeline

1. **Lexer**: Tokenizes source into tokens
2. **Parser**: Builds AST with precedence climbing for expressions
3. **Semantic Analyzer**: Type checking, symbol resolution, module imports
4. **Bytecode Compiler**: Generates bytecode IR, optimizes, emits final bytecode
5. **VM/JIT**: Executes bytecode or compiles via LLVM

### Host Module System

Modules in `src/havel-lang/stdlib/` provide host functions to Havel scripts:
- `HotkeyModule` - Global hotkey registration
- `WindowModule` - Window manipulation
- `MathModule`, `StringModule`, `ArrayModule` - Standard data operations
- `FsModule`, `ProcessModule` - System interaction
- `TypeModule` - Type introspection

## Testing

- **C++ unit tests**: `tests/` directory, gtest-based, built when ENABLE_TESTS=ON
- **Havel script tests**: `scripts/*.hv` files
- **Bytecode smoke test**: `havel-bytecode-smoke` (Debug builds only - Release LTO causes relocation overflow)

CI runs: CMake configure → build → bytecode-smoke → ctest

```bash
# Run a single Havel script
./build-release/havel script.hv

# Run test suite
./build.sh test

# Run specific test script
./build-debug/havel run scripts/test_basic.hv
```

## Dependencies

Critical (fatal if missing):
- X11, Xtst, Xrandr, Xinerama, Xcomposite
- spdlog, nlohmann_json (pkg-config)
- Lua 5.4
- Tesseract OCR, Leptonica, OpenCV

Optional (graceful fallback):
- LLVM (JIT compilation)
- Qt6, GTK4, ImGui+GLFW (UI backends)
- PipeWire, PulseAudio, ALSA (audio)

## Build Quirks

- **Compiler**: Clang is forced (`CC=clang CXX=clang++`)
- **C++ Standard**: C++23
- **Debug builds**: ASAN + UBSAN enabled automatically
- **Release builds**: Thin LTO + native march + visibility hidden
- **LLVM ↔ Havel Lang**: LLVM JIT requires ENABLE_HAVEL_LANG (auto-enabled if missing)
- **Qt6/MOC conflicts**: `#define True=1`, `#define False=0`, etc. to fix keyword conflicts

## Development

### Build Mode Selection
- Use modes 0-5 with LLVM for full functionality (requires LLVM dev libraries)
- Use modes 6-9 without LLVM for faster builds on systems without LLVM
- Mode 0: Debug with all features (default for development)
- Mode 5: Release with all features (default for distribution)

### Common Development Tasks

```bash
# Quick iteration without LLVM
./build.sh 8 build && ./build-debug/havel script.hv

# Full test run
./build.sh 0 test

# Debug a specific issue
./build.sh 0 build
gdb ./build-debug/havel
```

### Debugging the Language
- Use `scripts/tests/*.hv` files for language feature testing
- Compiler errors are reported with source locations via the error system
- VM execution traces available in debug builds
- Use `--debug-bytecode` flag for bytecode execution tracing

### Adding New Features
- **New host function**: Add to appropriate stdlib module, register in module's `register_*_functions()`
- **New syntax**: Modify lexer, update parser for new grammar, add AST node, update semantic analyzer
- **New builtin type**: Add to `src/havel-lang/core/Value.hpp`, update VM operations


## Code Rules

1. No stubs. Every function has a real implementation.
2. No "would". Write real code or say you don't know.
3. No parallel implementations. One file per feature.
4. No scope creep. Do exactly what was asked.
5. No documentation of broken things. Fix it instead.
6. Do not reimplement existing things.
7. Verify before marking done. Show output or test result.
8. Fix one thing, confirm it works, then move to next.
9. Do not add abstraction layers over broken abstraction layers.
10. No hardcoded placeholder values. Real values from real APIs.

## Commit Rules

11. No capslock in commit messages.
12. No emoji in commits.
13. Commit messages are for humans, not marketing.
14. No hype words (synergy, paradigm, revolutionary, ecosystem, zero-cost abstraction).
15. No emoji in code comments unless the bug is genuinely funny.

# ⚠️ HAVEL SYNTAX RULES (READ BEFORE CODING) ⚠️

IF YOU GENERATE HAVEL CODE THAT USES:
- Parentheses around if/for conditions → REJECTED
- Semicolons inside conditions/loops → REJECTED  
- Variable declaration with let → REJECTED  (use python-style declaration)
- const keyword → REJECTED  (use uppercase)
- `export` keyword → REJECTED
- `hotkey "Ctrl+Shift+F1" {}` → REJECTED (use `^+F1 => { }` syntax)
- `hotkey.register()` → REJECTED (only in loops/objects, use `F1 => { }` syntax)
- Explicit returns → REJECTED (use implicit)
- this keyword → REJECTED (use @ ruby-style)
- static keyword → REJECTED (use @@)
- # comments → REJECTED (use //)
- String concatenation with +, commas, dot or newlines → REJECTED (use f-strings python-style)

THE COMPILER WILL ERROR ON THESE PATTERNS. DO NOT USE THEM.
Function calls without parenthesis - ALLOWED
DO NOT THINK ABOUT WHAT HAVEL SYNTAX SHOULD BE

## Module System

Havel uses **Python-style modules**:
- Every top-level function, variable, and class is exported.
- No `export` keyword.
- Prefix with `_` to indicate "private by convention" (not enforced).
- Module consumer: `use mymodule` or `use { fn } from "mymodule"`.