---
title: "Directory Structure"
description: "Source code organization and module layout."
---

# Directory Structure

## Root

```
havel-3/
├── build.sh              # Main build script
├── emit_pipeline.sh      # Self-hosted pipeline builder
├── SYNTAX.md             # Language syntax reference
├── README.md             # Project overview
├── AGENTS.md             # Development guidelines
├── src/                  # C++ source code
├── modules/              # Havel modules (stdlib, apps, etc.)
├── scripts/              # Example/test scripts
├── docs/                 # Documentation (this directory)
├── include/              # C++ headers
├── tests/                # C++ unit tests
└── out/                  # Build output (self-hosted bytecode)
```

---

## Core Application (`src/`)

```
src/
├── core/                 # Core application logic
│   ├── config/           # Configuration system
│   ├── hotkey/           # Hotkey manager (X11)
│   ├── mode/             # Mode system
│   ├── display/          # Display/monitor management
│   ├── io/               # Input/output (keyboard, mouse)
│   ├── window/           # Window management
│   ├── media/            # Media control (MPV)
│   ├── ocr/              # OCR (Tesseract)
│   ├── process/          # Process launcher
│   └── brightness/       # Brightness control
├── host/                 # Host module system
│   ├── module/           # Bridge modules (InputBridge, WindowBridge, etc.)
│   └── HostContext.hpp   # Context for host modules
├── extensions/           # Native extensions
│   ├── gui/              # GUI backends
│   ├── image/            # Image processing
│   ├── ocr/              # OCR extension
│   ├── qt/               # Qt6 backend
│   ├── gtk/              # GTK4 backend
│   └── imgui/            # ImGui backend
├── utils/                # Cross-cutting utilities
│   ├── Logger.hpp
│   ├── File.hpp
│   ├── CrashHandler.hpp
│   ├── JSON.hpp
│   └── ...
└── havel-lang/           # Havel language implementation
```

---

## Havel Language (`src/havel-lang/`)

```
src/havel-lang/
├── compiler/
│   ├── core/             # Bytecode compiler
│   │   ├── ByteCompiler.cpp/hpp
│   │   ├── BytecodeIR.hpp
│   │   ├── Pipeline.cpp
│   │   └── CompilerUtils.cpp
│   ├── vm/               # Virtual machine
│   │   ├── VM.cpp/hpp
│   │   ├── VMHostFunctions.cpp
│   │   ├── Scheduler.hpp/cpp
│   │   ├── Fiber.hpp
│   │   ├── Thread.hpp
│   │   ├── Channel.hpp
│   │   └── ExecutionEngine.hpp
│   ├── gc/               # Garbage collector
│   │   ├── GC.cpp/hpp
│   │   └── ObjectEntry.hpp
│   └── llvm/             # LLVM JIT (optional)
│       ├── BytecodeOrcJIT.cpp
│       └── ...
├── lexer/                # Lexical analyzer
│   ├── Lexer.cpp/hpp
│   └── Token.hpp
├── parser/               # Parser (Pratt/precedence climbing)
│   ├── Parser.cpp/h
│   ├── AST.hpp
│   ├── ExprParser.cpp
│   ├── StmtParser.cpp
│   └── PatternParser.cpp
├── semantic/             # Semantic analysis
│   ├── SemanticAnalyzer.cpp
│   ├── SymbolTable.hpp
│   ├── TypeChecker.cpp
│   └── ModuleResolver.cpp
├── runtime/              # Runtime support
│   ├── Value.hpp
│   ├── Closure.hpp
│   └── ...
├── stdlib/               # Standard library modules (C++)
│   ├── MathModule.cpp
│   ├── StringModule.cpp
│   ├── ArrayModule.cpp
│   ├── ObjectModule.cpp
│   ├── HotkeyModule.cpp
│   ├── WindowModule.cpp
│   ├── FsModule.cpp
│   ├── ProcessModule.cpp
│   ├── SysModule.cpp
│   ├── HttpModule.cpp
│   ├── FfiModule.cpp
│   └── StdLibModules.cpp  # Registration
├── errors/               # Error reporting
│   ├── ErrorSystem.h
│   ├── ErrorPrinter.h
│   └── ErrorReporter.cpp
├── lsp/                  # Language Server Protocol
│   ├── LSPServer.cpp
│   └── ...
└── tests/                # Language test utilities
```

---

## Havel Modules (`modules/`)

```
modules/
├── lang/                 # Self-hosted compiler pipeline
│   ├── lexer.hv
│   ├── parser.hv
│   ├── ast.hv
│   ├── semantic.hv
│   ├── bytecode.hv
│   ├── optimizer.hv
│   ├── emitter.hv
│   └── pipeline.hv
├── std/                  # Standard library (Havel)
│   ├── array.hv
│   ├── string.hv
│   ├── object.hv
│   ├── math.hv
│   ├── time.hv
│   ├── fs.hv
│   ├── process.hv
│   ├── async.hv
│   ├── json.hv
│   ├── yaml.hv
│   ├── toml.hv
│   ├── sqlite.hv
│   ├── crypto.hv
│   ├── random.hv
│   ├── log.hv
│   ├── format.hv
│   ├── regex.hv
│   ├── bit.hv
│   ├── shell.hv
│   ├── path.hv
│   ├── env.hv
│   ├── config.hv
│   ├── terminal.hv
│   ├── audio.hv
│   ├── coroutine.hv
│   ├── future.hv
│   ├── semaphore.hv
│   ├── mutex.hv
│   ├── map.hv
│   ├── list.hv
│   ├── number.hv
│   ├── parser.hv
│   └── ...
├── app/                  # Application modules
│   ├── window.hv
│   ├── x11.hv
│   ├── monitor.hv
│   ├── brightness.hv
│   ├── hotkey.hv
│   ├── mouse.hv
│   ├── screenshot.hv
│   ├── socket.hv
│   ├── pixel.hv
│   ├── opencv.hv
│   ├── mpv.hv
│   ├── media.hv
│   ├── automation.hv
│   ├── modes.hv
│   ├── winmatch.hv
│   ├── winwatch.hv
│   ├── uinput.hv
│   ├── protocols.hv
│   ├── screen.hv
│   ├── zoom.hv
│   └── mode.hv
├── ui/                   # UI framework
│   ├── ui.hv
│   ├── css.hv
│   ├── scss.hv
│   ├── markup.hv
│   ├── style.hv
│   └── events.hv
├── fmt/                  # Formatting
│   └── fmt.hv
├── type/                 # Type introspection
│   └── type.hv
├── test_builtins/        # Builtin tests
│   └── mod.hv
└── textchunker/          # Text chunking
    └── textchunker.hv
```

---

## Scripts (`scripts/`)

```
scripts/
├── tests/                # Language test suite
├── smoke/                # Bytecode smoke tests
├── integration/          # Integration tests
├── stress/               # Stress tests
├── apps/                 # Example applications
├── demo/                 # Demos
├── automation/           # Automation examples
├── js/                   # JavaScript interop
├── build-aot-jit.sh      # AOT/JIT build script
├── install-udev-rules.sh # Udev rules for input
└── *.hv                  # Various test scripts
```

---

## Build Output

```
build-debug/              # Debug build (mode 6)
├── havel                 # Main executable
├── havel-lsp             # LSP server
├── havel-bytecode-smoke  # Bytecode smoke test
└── *.hvc                 # Compiled bytecode modules

build-release/            # Release build (mode 5/9)
├── havel
├── havel-lsp
└── ...
```

---

**Previous:** [Build System](/contributing/build-system)
**Next:** [Testing →](/contributing/testing)