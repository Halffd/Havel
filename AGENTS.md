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

## Architecture Notes

- **Havel Language** (`src/havel-lang/`): Compiler pipeline (Lexer → Parser → Semantic → Bytecode) + VM + optional LLVM JIT
- **Host Modules** (`src/havel-lang/stdlib/`): Provide built-in functions (Math, String, Array, Hotkey, Window, etc.)
- **UI**: Qt6 loaded as dynamic extension, not linked to main binary
- **Wayland**: Protocols auto-generated at build time to `build/generated/wayland/`

## Testing

- **C++ unit tests**: `tests/` directory, gtest-based, built when ENABLE_TESTS=ON
- **Havel script tests**: `scripts/*.hv` files
- **Bytecode smoke test**: `havel-bytecode-smoke` (Debug builds only - Release LTO causes relocation overflow)

CI runs: CMake configure → build → bytecode-smoke → ctest

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
