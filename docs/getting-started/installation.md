---
title: "Installation"
description: "Install Havel from source, build modes, and system requirements."
---

# Installation

## System Requirements

| Requirement | Minimum | Notes |
|-------------|---------|-------|
| **OS** | Linux (X11) | Primary target; headless mode for servers |
| **C++ Compiler** | Clang 14+ | Forced by build system (`CC=clang CXX=clang++`) |
| **CMake** | 3.20+ | |
| **X11 Libraries** | X11, Xtst, Xrandr, Xinerama, Xcomposite, Xi, Xfixes, Xdamage | Required for hotkeys and window management |
| **spdlog** | 1.11+ | Via pkg-config |
| **nlohmann_json** | 3.11+ | Via pkg-config |
| **Lua** | 5.4 | |
| **Tesseract + Leptonica** | 4.x / 1.80+ | OCR support |
| **OpenCV** | 4.x | Image processing |
| **libffi** | 3.4+ | FFI module (optional) |

### Optional Dependencies

| Library | Purpose | CMake Option |
|---------|---------|--------------|
| **LLVM** | JIT/AOT compilation | `ENABLE_LLVM=ON` (auto if found) |
| **Qt6** | GUI backend | `ENABLE_QT=ON` |
| **GTK4** | GUI backend | `ENABLE_GTK=ON` |
| **ImGui + GLFW** | Immediate-mode GUI | `ENABLE_IMGUI=ON` |
| **PipeWire / PulseAudio / ALSA** | Audio | Auto-detected |

## Build from Source

```bash
git clone https://github.com/yourorg/havel-3
cd havel-3

# Detect system dependencies
./build.sh detect

# Build modes (see Build System for full table)
# Default (mode 6): Debug, tests, Havel Lang, no LLVM
./build.sh build

# Full release with LLVM JIT (mode 5)
./build.sh 5 build
```

### Build Modes

```bash
# Mode 0: Debug + Tests + Havel Lang + LLVM
./build.sh 0 build

# Mode 5: Release + Tests + Havel Lang + LLVM (full features)
./build.sh 5 build

# Mode 6: Debug + Tests + Havel Lang + no LLVM (default, fast)
./build.sh 6 build

# Mode 9: Release + Tests + Havel Lang + no LLVM
./build.sh 9 build

# Headless (no Qt/GUI) — server/embedded use
./build.sh 12 build  # Debug headless
./build.sh 15 build  # Release headless

# ThreadSanitizer
./build.sh 16 build
```

### Environment Variables

```bash
# Parallel jobs (default: all cores)
THREADS=8 ./build.sh build

# Sanitizer levels (Debug builds only)
./build.sh --asan-level full build      # strict ASAN
./build.sh --ubsan-full build           # all UBSAN checks
./build.sh --tsan build                 # ThreadSanitizer (mode 16)
./build.sh --no-asan build              # disable ASAN
```

## Verify Installation

```bash
# Run the binary
./build-debug/havel --help

# Run a script
./build-debug/havel scripts/test_basic.hv

# Start REPL
./build-debug/havel --repl
```

## Install System-Wide (Optional)

```bash
# From build-release (mode 5 or 9)
sudo cp build-release/havel /usr/local/bin/
sudo cp build-release/havel-lsp /usr/local/bin/

# Verify
havel --version
```

## Troubleshooting

### Missing X11 Libraries

```bash
# Debian/Ubuntu
sudo apt install libx11-dev libxtst-dev libxrandr-dev libxinerama-dev \
    libxcomposite-dev libxi-dev libxfixes-dev libxdamage-dev

# Arch
sudo pacman -S libx11 libxtst libxrandr libxinerama libxcomposite \
    libxi libxfixes libxdamage

# Fedora
sudo dnf install libX11-devel libXtst-devel libXrandr-devel \
    libXinerama-devel libXcomposite-devel libXi-devel libXfixes-devel \
    libXdamage-devel
```

### Missing LLVM

```bash
# Debian/Ubuntu
sudo apt install llvm-dev clang

# Arch
sudo pacman -S llvm clang

# Fedora
sudo dnf install llvm-devel clang
```

### CMake Cannot Find Packages

```bash
# Ensure pkg-config can find them
pkg-config --cflags --libs x11 spdlog nlohmann_json lua5.4
```

---

**Previous:** [Havel Documentation](/)
**Next:** [First Script →](/getting-started/first-script)