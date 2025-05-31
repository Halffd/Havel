# HvC - Hotkey and Window Controller

HvC is a powerful utility for managing windows and hotkeys across multiple platforms, with a focus on Linux X11 environments. It allows for complex window management, hotkey configurations, and automated tasks via Lua scripting.

## Features

- Global hotkey registration and management
- Window tracking and manipulation
- Lua scripting for complex automation
- Configurable via text files
- Cross-platform support (primarily Linux X11)

## Directory Structure

- `/src` - Source code
- `/include` - Header files
- `/config` - Configuration files
- `/log` - Log files
- `/build` - Build directory

## Building HvC

### Prerequisites

- C++17 compatible compiler (GCC 9+ or Clang 10+)
- CMake 3.10+
- X11 development libraries (on Linux)
- Lua 5.3+ development libraries

### Dependencies

- X11 libraries: Xlib, Xutil, XTest, XKB, XRandR
- Sol2 Lua binding library
- pthread

### Quick Build

We provide a helper script to resolve common build issues:

```bash
# Fix common build issues
./fix_build.sh

# Build the project
mkdir -p build && cd build && cmake .. && cmake --build .
```

Alternatively, you can use the Makefile:

```bash
# Fix build issues and build
make rebuild
```

### Manual Build

```bash
mkdir -p build
cd build
cmake ..
cmake --build .
```

## Configuration

HvC uses several configuration files located in the `config` directory:

- `main.cfg` - Main configuration file
- `hotkeys/*.lua` - Lua scripts for hotkey configurations

See the [configuration guide](config/README.md) for more details.

## Troubleshooting

If you encounter build issues, please refer to the [BUILD_ISSUES.md](BUILD_ISSUES.md) file for common problems and solutions.

## License

This project is licensed under the MIT License - see the LICENSE file for details.

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

# HvC Project Build Fixes

This document outlines the issues fixed to make the HvC project build successfully.

## Fixed Issues

1. **DisplayManager Type References**
   - Fixed `Display*` and `Window` type reference issues in `DisplayManager.cpp` and `DisplayManager.hpp`
   - Added proper X11 includes and fixed static member variable definitions

2. **MPVController Method Signatures**
   - Fixed method signatures in `MPVController.cpp` and `MPVController.hpp` to match
   - Updated `Initialize()` to return a bool instead of void
   - Changed `SendCommand` to accept a vector of strings instead of a single string

3. **Window Class X11 Integration**
   - Fixed `Window` class to properly work with X11 `Window` type
   - Added proper type casting between `havel::Window`, `havel::wID`, and X11's `Window`
   - Defined the `display` variable in `Window.cpp`

4. **IO Class Implementation**
   - Created the missing `IO.cpp` file with implementation of all required methods
   - Added proper destructor for the `IO` class to fix linker errors
   - Added stub implementations for all interface methods

## Remaining Issues

The application can now build successfully, but there are still runtime issues:

1. The application crashes when handling Ctrl+C signals
2. Some hotkey configurations show "Invalid key name" warnings
3. Missing configuration file: "Could not open config file: config/config.json"

## Project Structure Notes

- All X11 display management is now handled through the `DisplayManager` class
- The project is designed to work on Linux with X11 (Wayland support is placeholder)
- The application uses a hotkey system for controlling media playback and window management

## Building the Project

To build the project:

```bash
mkdir build && cd build
cmake ..
cmake --build .
``` 