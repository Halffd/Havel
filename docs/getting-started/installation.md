---
title: "Installation"
description: "Install Havel from source, build modes, system requirements, and package manager instructions for Debian, Fedora, and Arch."
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

## Quick Install (Pre-built Packages)

### Debian/Ubuntu (`.deb`)

```bash
# Download from GitHub releases or build locally
wget https://github.com/Halffd/Havel/releases/latest/download/havel-1.0.0-Linux-x86_64.deb
sudo apt install ./havel-1.0.0-Linux-x86_64.deb
```

**Dependencies (auto-installed via apt):**
```bash
libx11-6, libxtst6, libxrandr2, libxinerama1, libxcomposite1, libxi6, libxfixes3, libxdamage1,
libwayland-client0, libxkbcommon0, libfmt10, libpcre2-8-0, libpcre2-16-0,
libgl1, libcurl4, libpipewire-0.3-0t64, libasound2t64,
libtesseract5, libleptonica6, libopencv-core410, libopencv-imgproc410, libopencv-imgcodecs410,
libreadline8t64, libncurses6, libffi8
```

### Fedora/RHEL (`.rpm`)

```bash
# Download from GitHub releases or build locally
wget https://github.com/Halffd/Havel/releases/latest/download/havel-1.0.0-Linux-x86_64.rpm
sudo dnf install ./havel-1.0.0-Linux-x86_64.rpm
```

**Dependencies (auto-installed via dnf):**
```bash
libX11, libXtst, libXrandr, libXinerama, libXcomposite, libXi, libXfixes, libXdamage,
wayland, libxkbcommon, fmt, pcre2, mesa-libGL, libcurl, pipewire, alsa-lib,
tesseract, leptonica, opencv-core, opencv-imgproc, opencv-imgcodecs,
readline, ncurses, libffi
```

### Arch Linux (AUR/PKGBUILD)

```bash
# Using the provided PKGBUILD (in repo root)
git clone https://github.com/Halffd/Havel
cd Havel
makepkg -si

# Or install from AUR (when available):
# yay -S havel
```

**Dependencies (from PKGBUILD):**
```bash
qt6-base, qt6-charts, mpv, libx11, libxrandr, libxinerama, libxcomposite,
libxtst, libxi, libxfixes, libxdamage, libpulse, alsa-lib, dbus, pcre2,
opencv, tesseract, leptonica, nlohmann-json, wayland, pipewire
```

## Build from Source

```bash
git clone https://github.com/Halffd/Havel
cd Havel

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

### Portable Builds (for Distribution)

```bash
# Build portable binaries without -march=native
cmake -B build-release -DCMAKE_BUILD_TYPE=Release -DPORTABLE_BUILD=ON
cmake --build build-release

# Create .deb and .rpm packages
./packaging/package.sh build-release
```

## Verify Installation

```bash
# Run the binary
havel --version

# Run a script
havel scripts/test_basic.hv

# Start REPL
havel --repl
```

## Install System-Wide (from Build)

```bash
# From build-release (mode 5 or 9)
sudo cmake --install build-release

# Or from local build directory
sudo cmake --install build-debug

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