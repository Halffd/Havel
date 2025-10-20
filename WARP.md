# WARP.md

This file provides guidance to WARP (warp.dev) when working with code in this repository.

## Project Overview

Havel (also known as HvC - Hotkey vs Control) is a comprehensive hotkey management and window automation system for Linux X11 environments. The project consists of two main components:

1. **HvC System**: A powerful C++23 application for managing hotkeys, window control, and media playback
2. **Havel Language**: A declarative automation scripting language designed for hotkey management and workflow automation

## Build System & Common Commands

The project uses CMake with a sophisticated build script that supports multiple configuration modes.

### Build Commands

```bash
# Standard build (Debug + Tests + Havel Lang + LLVM)
./build.sh build

# Quick build modes
./build.sh 0 build    # Mode 0: Full debug with all features
./build.sh 6 build    # Mode 6: Debug without LLVM (faster builds)
./build.sh 9 build    # Mode 9: Full release without LLVM
./build.sh 1 build    # Mode 1: Minimal release build

# Build and run
./build.sh 0 all      # Clean, build, and run
./build.sh run        # Just run the built executable

# Clean build
./build.sh clean

# Run tests (if enabled in build mode)
./build.sh test
```

### Build Modes

The build system supports 9 different modes with various feature combinations:

- **Modes 0-5**: Include LLVM JIT compilation
- **Modes 6-9**: LLVM-free builds (faster compilation, interpreter-only)
- **Debug modes**: 0, 2, 3, 6, 8 (include debug symbols)
- **Release modes**: 1, 5, 7, 9 (optimized builds)
- **Test modes**: 0, 3, 5, 6, 9 (include test executables)

Use modes 6-9 for faster builds or systems without LLVM development libraries.

### Direct CMake Build

```bash
# Manual build
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Debug -DUSE_CLANG=ON -DENABLE_LLVM=ON -DENABLE_TESTS=ON -DENABLE_HAVEL_LANG=ON ..
make -j$(nproc)

# Run from build directory
./havel
```

### Testing

```bash
# Run all available tests
cd build && make test
# or
./build.sh test

# Individual test executables (when tests are enabled)
./build/test_havel      # Havel language tests
./build/test_gui        # GUI component tests
./build/files_test      # File system tests
./build/main_test       # Core functionality tests
./build/utils_test      # Utility tests
```

## Architecture Overview

### Core System Design

The project follows a modular architecture with clear separation of concerns:

```
src/
├── core/                   # Core system components
│   ├── init/              # Application initialization
│   ├── automation/        # Automation systems (AutoClicker, etc.)
│   ├── io/               # Input/output handling
│   ├── util/             # Core utilities
│   └── *.{cpp,hpp}       # Core managers (Hotkey, Config, etc.)
├── window/                # Window management system
├── media/                 # Media control (MPV, Audio)
├── gui/                   # Qt6-based GUI components
├── utils/                 # Utility classes (Logger, etc.)
├── fs/                    # File system operations
└── havel-lang/           # Havel language implementation
    ├── runtime/          # Language runtime and interpreter
    ├── compiler/         # Language compiler
    └── ast/             # Abstract syntax tree
```

### Key Components

#### 1. HotkeyManager (`src/core/HotkeyManager.{hpp,cpp}`)
- Central hotkey registration and management
- Supports global and contextual hotkeys
- Mode-based hotkey profiles (default, gaming, etc.)
- Condition evaluation system for context-aware hotkeys

#### 2. WindowManager (`src/window/WindowManager.{hpp,cpp}`)
- X11 window detection and manipulation
- Window positioning, resizing, and state management
- Multi-monitor support
- Window grouping and classification

#### 3. IO System (`src/core/IO.{hpp,cpp}`)
- Low-level keyboard and mouse event handling
- X11 event processing and key grabbing
- Input simulation and injection

#### 4. Configuration System (`src/core/ConfigManager.{hpp,cpp}`)
- JSON-based configuration management
- Hot-reloading configuration changes
- Type-safe configuration access with defaults

#### 5. Havel Language Runtime (`src/havel-lang/`)
- Custom scripting language for automation
- LLVM JIT compilation (optional)
- Interpreter-based execution
- Pipeline-based syntax for data transformation

### Design Patterns

1. **Singleton Pattern**: Used for global managers (WindowManager, ConfigManager)
2. **Factory Pattern**: Component creation and platform abstraction
3. **Observer Pattern**: Event handling and configuration change notifications
4. **Command Pattern**: Hotkey actions and automation tasks

## Platform Support

- **Primary**: Linux with X11 window system
- **Wayland**: Partial support (protocols generated at build time)
- **Dependencies**: Qt6, X11 libraries, Lua 5.4, optional LLVM

## Development Workflow

### Debugging

Enable debug logging:
```bash
./build.sh 0 build  # Debug build
./havel --debug     # Run with debug logging
```

Debug flags are configurable in the `HotkeyManager`:
- `verboseKeyLogging`: Log all key events
- `verboseWindowLogging`: Log window state changes  
- `verboseConditionLogging`: Log condition evaluations

### Configuration

Configuration is stored in JSON format. Key configuration sections:

- `Window.MoveSpeed`: Window movement speed
- `Network.Port`: Socket server port for remote control
- `UI.Theme`: GUI theme selection
- `Hotkeys.*`: Hotkey-specific settings

### Havel Language Development

The project includes a custom scripting language with:

- **File extension**: `.hv`
- **Syntax**: Pipeline-based with `|` operator
- **Hotkey mapping**: Arrow function style with `=>`
- **Example**:
  ```hv
  F1 => send "Hello World!"
  
  clipboard.get 
      | text.upper 
      | text.replace " " "_"
      | send
  ```

## Important Notes

1. **Compiler Requirements**: Requires C++23 compatible compiler (Clang recommended)
2. **LLVM Dependency**: LLVM support requires Havel Language to be enabled
3. **X11 Permissions**: Application needs appropriate permissions to grab global keys
4. **Build Logs**: All build logs are saved to `logs/build-mode[X]-[type].log`
5. **System Tray**: GUI application runs in system tray by default

## Common Issues

- **Build failures**: Try LLVM-free modes (6-9) if LLVM libraries are missing
- **Hotkey conflicts**: Use `xev` to check for key event conflicts with other applications
- **Window management**: Some window managers may not support all operations
- **Permissions**: Global hotkey registration may require elevated privileges

## File Structure Highlights

- `build.sh`: Comprehensive build script with multiple modes
- `CMakeLists.txt`: CMake configuration with optional components
- `config/`: Configuration files and examples
- `src/main.cpp`: Application entry point with Qt integration
- `DOCUMENTATION.md`: Extensive technical documentation
- `Havel.md`: Havel language specification

This codebase combines low-level system programming with high-level automation scripting, making it a powerful tool for desktop workflow automation on Linux systems.