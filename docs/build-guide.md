# Build Guide

How to build Havel from source — dependencies, build modes, configuration, and troubleshooting.

---

## Quick Start

```bash
git clone https://github.com/anomalyco/havel.git
cd havel
./build.sh 6 build        # Fast debug build (no LLVM)
./build-debug/havel        # Run
```

---

## Build System

The build uses `build.sh` as the primary interface, wrapping CMake:

```bash
./build.sh [mode] [command]
```

### Build Modes

| Mode | Type | Tests | Havel Lang | LLVM | Headless | Build Dir |
|------|------|-------|------------|------|----------|-----------|
| 0 | Debug | Yes | Yes | Yes | No | build-debug |
| 5 | Release | Yes | Yes | Yes | No | build-release |
| 6 | Debug | Yes | Yes | No | No | build-debug |
| 9 | Release | Yes | Yes | No | No | build-release |
| 12 | Debug | Yes | Yes | No | Yes | build-headless |
| 15 | Release | Yes | Yes | No | Yes | build-headless |

### Commands

| Command | Description |
|---------|-------------|
| `build` | CMake configure + build |
| `test` | Build + run all tests |
| `detect` | Show system dependencies |
| `clean` | Remove build directory |

### Examples

```bash
./build.sh 5 build        # Full release with LLVM
./build.sh 6 build        # Fast debug (no LLVM) — recommended for development
./build.sh 12 build       # Headless debug (no Qt/GUI) — for servers
./build.sh 0 test         # Full debug test suite
./build.sh detect         # Check what's installed
```

---

## Dependencies

### Critical (Build Fails Without These)

| Dependency | Purpose | Install |
|------------|---------|---------|
| Clang | Compiler (forced: `CC=clang CXX=clang++`) | `sudo apt install clang` |
| CMake | Build system | `sudo apt install cmake` |
| X11 + Xtst | Window management, input | `sudo apt install libx11-dev libxtst-dev` |
| Xrandr | Display configuration | `sudo apt install libxrandr-dev` |
| Xinerama | Multi-monitor | `sudo apt install libxinerama-dev` |
| Xcomposite | Window compositing | `sudo apt install libxcomposite-dev` |
| spdlog | Logging | `sudo apt install libspdlog-dev` |
| nlohmann_json | JSON parsing | `sudo apt install nlohmann-json3-dev` |
| Lua 5.4 | Legacy script support | `sudo apt install lua5.4 liblua5.4-dev` |
| Tesseract OCR | Text recognition | `sudo apt install libtesseract-dev` |
| Leptonica | Image processing (Tesseract dep) | `sudo apt install libleptonica-dev` |
| OpenCV | Computer vision | `sudo apt install libopencv-dev` |

### Optional (Graceful Fallback)

| Dependency | Purpose | Install |
|------------|---------|---------|
| LLVM dev | JIT compilation | `sudo apt install llvm-dev` |
| Qt6 | GUI backend | `sudo apt install qt6-base-dev` |
| GTK4 | GUI backend | `sudo apt install libgtk-4-dev` |
| ImGui + GLFW | Immediate-mode GUI | Build from source |
| PipeWire | Audio | `sudo apt install libpipewire-dev` |
| PulseAudio | Audio | `sudo apt install libpulse-dev` |
| ALSA | Audio | `sudo apt install libasound2-dev` |

### Dependency Detection

```bash
./build.sh detect
```

This shows which dependencies are found and which are missing, with install hints.

---

## Build Configuration Details

### Compiler

- **Clang is forced**: `CC=clang CXX=clang++` in `build.sh`
- **C++ Standard**: C++23 (`-std=c++23`)

### Debug Builds (Modes 0, 6, 12)

- AddressSanitizer (ASAN) enabled automatically
- UndefinedBehaviorSanitizer (UBSAN) enabled automatically
- No optimization (`-O0`)
- Debug symbols included
- All assertions active

### Release Builds (Modes 5, 9, 15)

- Thin LTO (`-flto=thin`)
- Native architecture (`-march=native`)
- Visibility hidden (`-fvisibility=hidden`)
- Optimization (`-O2` or `-O3`)
- No sanitizers
- Strip symbols

### Headless Builds (Modes 12, 15)

- No Qt/GUI dependencies
- No display server required
- Suitable for server environments
- X11 headers still required for input handling

### LLVM Integration

- LLVM JIT requires `ENABLE_HAVEL_LANG` cmake option (auto-enabled when LLVM dev is found)
- Modes 0 and 5 include LLVM
- Modes 6, 9, 12, 15 exclude LLVM
- If LLVM is not installed, use modes 6+ to skip it

---

## Build Artifacts

### Executables

| Binary | Location | Purpose |
|--------|----------|---------|
| `havel` | `build-debug/havel` or `build-release/havel` | Main application |
| `havel-lsp` | `build-debug/havel-lsp` | Language Server Protocol server |
| `havel-bytecode-smoke` | `build-debug/havel-bytecode-smoke` | Bytecode smoke test (Debug only) |

### Running Havel Scripts

```bash
./build-debug/havel script.hv           # Run a script
./build-debug/havel run script.hv       # Same, explicit
./build-debug/havel                     # Start REPL
```

### Debug Flags

```bash
./build-debug/havel run script.hv -d -dl -dp -da --debug-bytecode --debug-jit
```

| Flag | Description |
|------|-------------|
| `-d` | General debug |
| `-dl` | Debug lexer |
| `-dp` | Debug parser |
| `-da` | Debug AST |
| `--debug-bytecode` | Trace bytecode execution |
| `--debug-jit` | Trace JIT compilation |

---

## Troubleshooting

### Clang Not Found

```
error: clang not found
```

Install Clang and ensure it's in PATH:
```bash
sudo apt install clang
which clang clang++
```

### Missing X11 Headers

```
fatal error: X11/Xlib.h: No such file or directory
```

Install X11 development packages:
```bash
sudo apt install libx11-dev libxtst-dev libxrandr-dev libxinerama-dev libxcomposite-dev
```

### spdlog/nlohmann_json Not Found

```
Could not find spdlog via pkg-config
```

These are looked up via pkg-config. Install both the library and its development files:
```bash
sudo apt install libspdlog-dev nlohmann-json3-dev pkg-config
```

### Qt6 MOC Conflicts

Havel defines `True=1` and `False=0` as macros to avoid keyword conflicts with Qt's MOC system. If you see errors related to `True`/`False` keywords, ensure the build defines these macros before including Qt headers.

### LLVM Linker Errors

If building with LLVM (modes 0, 5) and you get linker errors, either:
1. Install LLVM development libraries: `sudo apt install llvm-dev`
2. Switch to non-LLVM modes: `./build.sh 6 build`

### Release LTO Relocation Overflow

The `havel-bytecode-smoke` binary is Debug-only because Release LTO causes relocation overflow. This is a known limitation — use Debug builds for the smoke test.

### Lua 5.4 Not Found

```
Could not find Lua 5.4
```

Ensure both the runtime and development files are installed:
```bash
sudo apt install lua5.4 liblua5.4-dev
```

### Tesseract/OpenCV Issues

```
Could not find Tesseract
```

These are critical dependencies. Install the full set:
```bash
sudo apt install libtesseract-dev libleptonica-dev libopencv-dev
```

---

## Platform Support

| Platform | Status | Notes |
|----------|--------|-------|
| Linux (x86_64) | Primary | Full feature support |
| Linux (ARM64) | Should work | Not regularly tested |
| macOS | Partial | No X11; requires Qt or ImGui backend |
| Windows | Not supported | No X11; would require significant porting |

Havel is primarily designed for Linux desktop automation. Other platforms require adapting the X11-dependent core modules.
