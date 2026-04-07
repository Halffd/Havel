# CLAUDE.md
This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**Havel** is a window management utility with a built-in scripting language. It provides:
- Global hotkey management and window manipulation for Linux X11/Wayland
- A complete Havel scripting language with compiler, VM, and optional LLVM JIT
- Cross-platform UI support (Qt6, GTK4, ImGui)
- Modular architecture with host modules for different functionality

## Build Commands

### Primary Build System
Use the custom build script for most operations:

```bash
# Build modes: 0=Debug(full), 5=Release(full), 6=Debug(no LLVM), 9=Release(no LLVM)
./build.sh [mode] <command>

# Common commands:
./build.sh 5 build          # Full release build with tests and LLVM
./build.sh 6 build          # Debug build without LLVM (faster)
./build.sh 0 all           # Clean, build, and run in debug mode
./build.sh clean           # Clean build directory
./build.sh test            # Run all tests
```

### Makefile Targets
```bash
make debug           # Debug build (mode 0)
make release         # Release build (mode 5)
make test            # Run tests
make clean           # Clean build directories
make all             # Clean, build, and run
```

### Testing
```bash
# Run a single Havel script
./build-release/hav script.hv
./build-debug/hav script.hv

# Run test suite
./build.sh test

# Run specific test script
./build-debug/hav scripts/test_basic.hv
```

## Architecture

### Directory Structure

**Core Window Management** (`src/`)
- `core/` - Core application logic, automation, I/O
- `window/` - X11/Wayland window management
- `utils/`, `fs/`, `process/`, `media/` - Utility modules
- `host/` - Host module system for extensible functionality
- `gui/` - UI backends (Qt, GTK, ImGui)
- `testing/` - Unit test utilities

**Havel Language Implementation** (`src/havel-lang/`)
- `compiler/` - Bytecode compiler (pipeline, IR generation, optimization)
- `lexer/` - Lexical analysis
- `parser/` - Syntax parsing (expression, statement, pattern parsers)
- `semantic/` - Type checking, symbol table, module resolution
- `runtime/` - VM execution engine
  - `vm/` - Stack-based virtual machine
  - `gc/` - Garbage collector
  - `core/` - Builtin values and types
- `stdlib/` - Standard library modules (Math, String, Array, etc.)
- `errors/` - Error reporting and diagnostics
- `lsp/` - Language Server Protocol support
- `llvm/` - LLVM JIT integration (optional)

### Key Components

**Havel Language**
- Custom bytecode instruction set with ~100 opcodes
- Stack-based VM with garbage collection
- Optional LLVM JIT for performance-critical code
- Module system with import/export
- Host function binding system for C++ integration

**Compiler Pipeline**
1. Lexer: Tokenizes source into tokens
2. Parser: Builds AST with precedence climbing for expressions
3. Semantic Analyzer: Type checking, symbol resolution, module imports
4. Bytecode Compiler: Generates bytecode IR, optimizes, emits final bytecode
5. VM/JIT: Executes bytecode or compiles via LLVM

**Host Module System**
Modules in `src/havel-lang/stdlib/` provide host functions to Havel scripts:
- `HotkeyModule` - Global hotkey registration
- `WindowModule` - Window manipulation
- `MathModule`, `StringModule`, `ArrayModule` - Standard data operations
- `FsModule`, `ProcessModule` - System interaction
- `TypeModule` - Type introspection

## Development Tips

**Build Mode Selection**
- Use modes 0-5 with LLVM for full functionality (requires LLVM dev libraries)
- Use modes 6-9 without LLVM for faster builds on systems without LLVM
- Mode 0: Debug with all features (default for development)
- Mode 5: Release with all features (default for distribution)

**Common Development Tasks**

```bash
# Quick iteration without LLVM
./build.sh 8 build && ./build-debug/hav script.hv

# Full test run
./build.sh 0 test

# Debug a specific issue
./build.sh 0 build
gdb ./build-debug/hav
```

**Debugging the Language**
- Use `scripts/havel_diag_*.hv` files for language feature testing
- Compiler errors are reported with source locations via the error system
- VM execution traces available in debug builds
- Use `--trace` flag for bytecode execution tracing

**Adding New Features**
- **New host function**: Add to appropriate stdlib module, register in module's `register_*_functions()`
- **New syntax**: Modify lexer, update parser for new grammar, add AST node, update semantic analyzer
- **New builtin type**: Add to `src/havel-lang/core/Value.hpp`, update VM operations

**Dependencies**
- CMake 3.16+, C++23 compatible compiler (Clang recommended)
- Qt6 (default UI), X11 dev libraries, Lua 5.4
- D-Bus, Wayland dev libraries
- spdlog, nlohmann-json (pkg-config)
- LLVM (optional, for JIT)

## Testing

The project has extensive test coverage:
- Unit tests in `tests/` directory (C++ gtest-based)
- Havel language tests in `scripts/` (*.hv files)
- Integration tests for compiler pipeline
- Performance benchmarks in `benchmarks/` (if present)

Test commands automatically run both C++ unit tests and Havel script tests.
